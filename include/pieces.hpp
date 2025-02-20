#ifndef TORRENT_PIECES_HPP
#define TORRENT_PIECES_HPP

#include <openssl/sha.h>

#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/file_base.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/random_access_file.hpp>
#include <boost/log/trivial.hpp>
#include <boost/uuid/detail/sha1.hpp>
#include <condition_variable>
#include <cstdint>
#include <cstring>
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
    Pieces(asio::io_context& io_context) : file(io_context) {}

    /*
     * @param file_name "name" value from the info directory.
     * @param file_length "length" value from the info directory.
     * @param p_length "piece length" value from the info directory.
     * @param pieces "pieces" byte string containing the hashes of the pieces.
     * */
    void init(
        std::string file_name,
        std::size_t file_length,
        std::size_t p_length,
        std::string pieces
    );

    /*
     * Writes given block to the file async.
     * @param on_finish A function that will be called when
     *      the operation finishes. Signature should be on_finish(const asio::error_code& error_code, bool piece_complete).
     * */
    void write_block_async(
        int piece_index,
        int begin,
        std::vector<std::uint8_t> payload,
        const auto on_finish
    ) {
        std::scoped_lock<std::mutex> lock {mutex};
        if (piece_index >= piece_count || begin > piece_length) {
            // Invalid parameter, ignore.
            return;
        }
        auto payload_ptr =
            std::make_shared<std::vector<std::uint8_t>>(std::move(payload));

        const std::size_t block_size = payload_ptr->size() - 8;

        file.async_write_some_at(
            piece_index * piece_length + begin,
            asio::buffer(payload_ptr->data() + 8, block_size),
            [=, this](const auto& error_code, std::size_t bytes_transferred) {
                if (error_code) {
                    BOOST_LOG_TRIVIAL(error)
                        << "Error while writing to the file: "
                        << error_code.message();
                    on_finish(error_code, false);
                } else {
                    // Check if this is the last block.
                    // We can do this because our client will always
                    //   request blocks from start to end.
                    if (begin + block_size >= piece_length) {
                        // Run an SHA1 check for this piece.
                        check_sha1_piece_async(piece_index, on_finish);
                    } else {
                        on_finish(error_code, false);
                    }
                }
            }
        );
    }

    std::size_t get_piece_length() {
        std::scoped_lock<std::mutex> lock {mutex};
        return piece_length;
    }

    std::size_t get_piece_count() {
        std::scoped_lock<std::mutex> lock {mutex};
        return piece_count;
    }

    std::size_t get_block_count() {
        std::scoped_lock<std::mutex> lock {mutex};
        return piece_length / BLOCK_LENGTH;
    }

    /*
     * Waits until the file is downloaded.
     * */
    void wait();

    /*
     * Notifies any wait procedure and wakes them up.
     * */
    void stop();

  public:
    static std::size_t BLOCK_LENGTH;

  private:
    /*
     * Runs an SHA1 check over the given piece.
     * @param piece_index Index of the parameter piece.
     * @param on_finish A function that will be called when the check is finished. 
     *      signature should be "on_finish(const asio::error_code& error_code, bool sha1_passed)"
     * */
    void check_sha1_piece_async(std::size_t piece_index, const auto on_finish) {
        auto buffer_ptr = std::make_shared<std::string>(piece_length, '\0');

        file.async_read_some_at(
            piece_index * piece_length,
            asio::buffer(*buffer_ptr),
            [=, this](const auto& error_code, std::size_t bytes_transferred) {
                if (error_code || bytes_transferred != piece_length) {
                    BOOST_LOG_TRIVIAL(error)
                        << "Error while reading from the file: "
                        << error_code.message();
                    on_finish(error_code, false);
                    return;
                }
                on_finish(
                    error_code,
                    check_sha1_piece(piece_index, *buffer_ptr)
                );
                return;
            }
        );
    }

    /*
     * Checks SHA1 for the given piece.
     * @return Returns true if piece passed SHA1 check, false if not.
     * */
    bool
    check_sha1_piece(std::size_t piece_index, const std::string_view piece) {
        unsigned char hash[20];
        SHA1(
            reinterpret_cast<const unsigned char*>(piece.data()),
            piece.size(),
            hash
        );
        int sha1_check = std::memcmp(
            static_cast<const void*>(&hashes[piece_index * 20]),
            static_cast<const void*>(hash),
            20
        );
        return sha1_check == 0;
    }

    /*
     * Run a sha1 checksum over the pieces after opening the file.
     */
    void run_sha1_checksum_multithread();

    /*
     * Checks sha1 of pieces starting in range of [start_piece, end_piece).
     * Sets the bitfield value accordingly when a piece passes sha1.
     * @param start_piece Starts checking sha1 from this index.
     * @param end_piece Ends when index=end_piece.
     * */
    void check_pieces_sha1(std::size_t start_piece, std::size_t end_piece);

  public:
    std::unique_ptr<Bitfield> bitfield;

  private:
    asio::random_access_file file;

    std::size_t piece_count;
    std::size_t piece_length;

    bool running = true;
    std::mutex cv_mutex;
    std::condition_variable cv;

    std::mutex mutex;

    std::string hashes;
};
} // namespace torrent

#endif
