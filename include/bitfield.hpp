#ifndef TORRENT_BITFIELD_HPP
#define TORRENT_BITFIELD_HPP

#include <boost/log/trivial.hpp>
#include <condition_variable>
#include <cstdint>
#include <functional>
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

    /*
     * Constructs the Bitfield with the given size parameter.
     * The bitfield object will hold size * 8 bits.
     * */
    Bitfield(std::size_t size) : vec(size, 0) {}

    Bitfield(std::vector<std::uint8_t> bitfield_vec) :
        vec(std::move(bitfield_vec)) {}

    /*
     * Returns the size of the inner buffer.
     * Bitfield can hold size() * 8 bits.
     * */
    std::size_t size() {
        std::scoped_lock<std::mutex> lock {mutex};
        return vec.size();
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
     * */
    bool has_piece(std::size_t piece_index) {
        std::scoped_lock<std::mutex> lock {mutex};
        if (piece_index / 8 >= vec.size()) {
            BOOST_LOG_TRIVIAL(error)
                << "Bitfield::has_piece called with invalid parameters.";
            return false;
        }
        auto result = has_piece_internal(piece_index);
        return result;
    }

    /*
     * Set the piece at the given index.
     * Increases bitfield.get_completed_piece_count() if it not already set.
     * */
    void set_piece(std::size_t piece_index) {
        std::unique_lock<std::mutex> lock {mutex};
        if (piece_index / 8 >= vec.size()) {
            BOOST_LOG_TRIVIAL(error)
                << "Bitfield::set_piece called with invalid parameters.";
            return;
        }
        // Check if this piece is already set.
        // This is done to prevent any erros in counting the pieces;
        if (has_piece_internal(piece_index)) {
            return;
        }

        if (on_piece_complete.has_value()) {
            lock.unlock();
            on_piece_complete.value()(piece_index);
        }
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
                        vec[i] |= static_cast<std::uint8_t>(1 << (7 - j));
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
        std::unique_lock<std::mutex> lock {mutex};
        if (piece_index.has_value()) {
            if (on_piece_complete.has_value()) {
                lock.unlock();
                on_piece_complete.value()(piece_index.value());
            }
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
     * Set a handler to be called when a new piece gets added.
     * @param func Function that will be called when a new piece gets downloaded.
     *      Takes a parameter std::size_t piece_index.
     * */
    void set_on_piece_complete(std::function<void(std::size_t)> func) {
        on_piece_complete = {std::move(func)};
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
        vec[piece_index / 8] |=
            static_cast<std::uint8_t>((value) << (7 - (piece_index % 8)));
    }

  private:
    // It's better to use std::vector<std::uint8_t> instead of std::vector<bool> or boost::dynamic_bitset
    // Because we can move incoming bitfields this way instead of copying them.
    std::vector<std::uint8_t> vec;

    std::mutex mutex;

    std::optional<std::function<void(std::size_t)>> on_piece_complete;
};
} // namespace torrent
#endif
