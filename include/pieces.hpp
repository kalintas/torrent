#ifndef TORRENT_PIECES_HPP
#define TORRENT_PIECES_HPP

#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

#include "bitfield.hpp"

namespace torrent {

namespace asio = boost::asio;

/*
 * A thread safe class that holds and manages pieces. 
 * Each object will associated with a file.
 * */
class Pieces {
  public:
    Pieces(asio::io_context& io_context) : io_context(io_context) {}

    /*
     * @param file_name "name" value from the info directory.
     * @param file_length "length" value from the info directory.
     * @param piece_length "piece length" value from the info directory.
     * @param pieces "pieces" byte string containing the hashes of the pieces.
     * */
    void init(
        std::string file_name,
        int file_length,
        int piece_length,
        std::string pieces
    ) {
        // Every piece holds 20 bytes in info.pieces
        // We can get the total piece count just by dividing with 20.
        auto piece_count = pieces.size() / 20;
        bitfield = std::make_unique<Bitfield>(piece_count);

        hashes = std::move(pieces);
    }

    void write_block(std::size_t begin, std::size_t length) {}

    /*
     * Waits until the file is downloaded.
     * */
    void wait() {
        while (!finished) {
            std::unique_lock<std::mutex> lock {cv_mutex};
            cv.wait(lock);
        }
    }

  public:
    std::unique_ptr<Bitfield> bitfield;

  private:
    asio::io_context& io_context;

    bool finished = false;
    std::mutex cv_mutex;
    std::condition_variable cv;

    std::string hashes;
};
} // namespace torrent

#endif
