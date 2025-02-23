#ifndef TORRENT_BITFIELD_HPP
#define TORRENT_BITFIELD_HPP

#include <boost/log/trivial.hpp>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

#include "message.hpp"

namespace torrent {

using PieceIndex = std::optional<std::size_t>;

/*
 * A thread safe class that holds torrent bitfields.
 * */
class Bitfield {
  public:
    Bitfield() {}

    Bitfield(std::size_t bit_count) :
        vec((bit_count / 8) + (bit_count % 8 != 0), 0),
        bit_count(bit_count) {}

    Bitfield(std::vector<std::uint8_t> vec) : vec(std::move(vec)) {}

    std::size_t get_bit_count() {
        std::scoped_lock<std::mutex> lock {mutex};
        return bit_count;
    }

    std::size_t size() {
        std::scoped_lock<std::mutex> lock {mutex};
        return vec.size();
    }

    std::size_t get_completed_piece_count() {
        std::scoped_lock<std::mutex> lock {mutex};
        return completed_piece_count;
    }

    /*
     * Consumes the Bitfield and returns it as bytes.
     * */
    std::vector<std::uint8_t> into_bytes() {
        std::scoped_lock<std::mutex> lock {mutex};
        return std::move(vec);
    }

    Message as_message() {
        std::scoped_lock<std::mutex> lock {mutex};
        return Message {Message::Id::Bitfield, vec.begin(), vec.size()};
    }

    /*
     * Retrieve the bit 
     * Decreases bitfield.get_completed_piece_count() if it is actually set.
     * */
    bool has_piece(std::size_t piece_index) {
        std::scoped_lock<std::mutex> lock {mutex};
        if (piece_index >= bit_count) {
            BOOST_LOG_TRIVIAL(error)
                << "Bitfield::has_piece called with invalid parameters.";
            return false;
        }
        auto result = has_piece_internal(piece_index);
        if (result) {
            completed_piece_count -= 1; // Decrease the count of pieces we have.
        }
        return result;
    }

    /*
     * Set the piece at the given index.
     * Increases bitfield.get_completed_piece_count() if it not already set.
     * */
    void set_piece(std::size_t piece_index) {
        std::scoped_lock<std::mutex> lock {mutex};
        if (piece_index >= bit_count) {
            BOOST_LOG_TRIVIAL(error)
                << "Bitfield::set_piece called with invalid parameters.";
            return;
        }
        // Check if this piece is already set.
        // This is done to prevent any erros in counting the pieces;
        if (has_piece_internal(piece_index)) {
            return;
        }

        completed_piece_count += 1; // Increase the count of pieces we have.
        stop_wait();
        set_piece_internal(piece_index, 1);
    }

    /*
     * Assigns a random available piece regarding the peer_bitfield.
     * Other peers may not assign themselfs this piece until it gets unassigned.
     * @param peer_bitfield Bitfield of the peer.
     * @return A piece index. Return -1 if it can't find any valid piece.
     * */
    PieceIndex assign_piece(Bitfield& peer_bitfield) {
        std::scoped_lock<std::mutex> lock1 {mutex};
        std::scoped_lock<std::mutex> lock2 {peer_bitfield.mutex};

        if (vec.size() != peer_bitfield.vec.size()) {
            // Internal logic error. Should never happen
            throw std::runtime_error(
                "Bitfield::assign_piece called with non matching bitfields"
            );
        }
        // TODO: We are getting the first matching piece.
        //      Probably should select rare first or select it more randomly.
        for (std::size_t i = 0; i < vec.size(); ++i) {
            std::uint8_t value =
                static_cast<std::uint8_t>(~vec[i]) & peer_bitfield.vec[i];
            if (value != 0) {
                // We have a match from these 8 pieces.
                // Now find the first piece and send the index.
                for (std::size_t j = 0; j < 8; ++j) {
                    std::uint8_t bit = (value >> (7 - j)) & 1;
                    if (bit != 0) {
                        // Set the piece bit so other peers can't assign the same piece.
                        vec[i] |= 1 << (7 - j);
                        // Return the piece index.
                        return {i * 8 + j};
                    }
                }
            }
        }
        return {};
    }

    /*
     * This function must be called if piece has been successfully downloaded.
     * Function will increase bitfield.get_completed_piece_count() effectively.
     * And this piece will not be assignable anymore.
     * */
    void piece_success(PieceIndex piece_index) {
        std::scoped_lock<std::mutex> lock {mutex};
        if (piece_index.has_value()) {
            completed_piece_count += 1;
            stop_wait();
        }
    }

    /*
     * This function must be called if there was an error while downloading the piece.
     * It will unassign the piece effectively.
     * */
    void piece_failed(PieceIndex piece_index) {
        std::scoped_lock<std::mutex> lock {mutex};
        if (piece_index.has_value()) {
            // Unset the piece. This way other peers may assign it to themselfs.
            set_piece_internal(piece_index.value(), 0);
        }
    }

    /*
     * Waits until a piece is successfully downloaded.
     * */
    void wait_piece() {
        std::unique_lock<std::mutex> lock {piece_cv_mutex};
        cv_should_wake = false;
        piece_cv.wait(lock, [this] { return cv_should_wake; });
    }

    /*
     * Notifies any ongoing wait and wakes them up.
     * */
    void stop_wait() {
        std::scoped_lock<std::mutex> lock {piece_cv_mutex};
        cv_should_wake = true;
        piece_cv.notify_all();
    }

  private:
    /*
     * Internal has_piece function. Does not use any locks.
     * */
    bool has_piece_internal(std::size_t piece_index) {
        return (vec[piece_index / 8] >> (7 - (piece_index % 8))) & 1;
    }

    /*
     * Internal set_piece function. Does not use any locks.
     * @param value Should be either 0 or 1.
     * */
    void set_piece_internal(std::size_t piece_index, std::uint8_t value) {
        vec[piece_index / 8] |= (value) << (7 - (piece_index % 8));
    }

  private:
    // It's better to use std::vector<std::uint8_t> instead of std::vector<bool> or boost::dynamic_bitset
    // Because we can move incoming bitfields this way instead of copying them.
    std::vector<std::uint8_t> vec;
    std::size_t bit_count;

    std::mutex mutex;

    std::mutex piece_cv_mutex;
    std::condition_variable piece_cv;
    bool cv_should_wake = false;

    std::size_t completed_piece_count = 0;
};
} // namespace torrent
#endif
