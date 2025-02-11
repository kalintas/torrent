#ifndef TORRENT_BITFIELD_HPP
#define TORRENT_BITFIELD_HPP

#include <boost/log/trivial.hpp>
#include <cstdint>
#include <mutex>
#include <vector>

#include "message.hpp"

namespace torrent {

/*
 * A thread safe class that holds torrent bitfields.
 * */
class Bitfield {
  public:
    Bitfield() {}

    Bitfield(std::size_t bit_count) : vec(bit_count / 8, 0) {}

    Bitfield(std::vector<std::uint8_t> vec) : vec(std::move(vec)) {}

    std::size_t bit_count() {
        std::scoped_lock<std::mutex> lock {mutex};
        return vec.size() * 8;
    }

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
        auto vec_index = piece_index / 8;
        if (vec_index >= vec.size()) {
            BOOST_LOG_TRIVIAL(error)
                << "Bitfield::has_piece called with invalid parameters.";
            return false;
        }

        return (vec[vec_index] >> (7 - (piece_index % 8))) & 1;
    }

    void set_piece(std::size_t piece_index) {
        std::scoped_lock<std::mutex> lock {mutex};
        auto vec_index = piece_index / 8;
        if (vec_index >= vec.size()) {
            BOOST_LOG_TRIVIAL(error)
                << "Bitfield::set_piece called with invalid parameters.";
            return;
        }
        vec[vec_index] |= 1 << (7 - (piece_index % 8));
    }

  private:
    // It's better to use std::vector<std::uint8_t> instead of std::vector<bool> or boost::dynamic_bitset
    // Because we can move incoming bitfields this way instead of copying them.
    std::vector<std::uint8_t> vec;
    std::mutex mutex;
};
} // namespace torrent

#endif
