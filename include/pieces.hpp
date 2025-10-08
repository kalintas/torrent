#ifndef TORRENT_PIECES_HPP
#define TORRENT_PIECES_HPP

#include <openssl/sha.h>

#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/file_base.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/log/trivial.hpp>
#include <boost/uuid/detail/sha1.hpp>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>

#include "async_file.hpp"
#include "bitfield.hpp"
#include "metadata.hpp"

namespace torrent {

namespace asio = boost::asio;

/*
 * A thread safe class that does IO of pieces. 
 * */
class Pieces: public std::enable_shared_from_this<Pieces> {
  private:
    struct Private {
        explicit Private() = default;
    };

  public:
    Pieces(
        Private,
        asio::io_context& io_context_ref,
        std::shared_ptr<Metadata> metadata_ptr
    ) :
        file(io_context_ref),
        metadata(std::move(metadata_ptr)) {}

    /*
     * Creates a new Pieces object with given metadata. 
     * */
    static std::shared_ptr<Pieces>
    create(asio::io_context& io_context, std::shared_ptr<Metadata> metadata) {
        return std::make_shared<Pieces>(
            Private {},
            io_context,
            std::move(metadata)
        );
    }

    std::shared_ptr<Pieces> get_ptr() {
        return shared_from_this();
    }

    std::weak_ptr<Pieces> get_weak() {
        return shared_from_this();
    }

    /*
     * Fetches the file information from the Metadata.
     * And constructs the Bitfield with that information.
     * Opens the output file if it exists and runs a SHA1 checksum over it.
     * Creates it if it does not.
     * Metadata should be ready before this function gets called.
     * */
    void init_file();

    /*
     * Writes given block to the file async.
     * @param on_finish A function that will be called when
     *      the operation finishes. Signature should be on_finish(const asio::error_code& error_code, bool piece_complete).
     * */
    void write_block_async(
        std::uint32_t piece_index,
        std::uint32_t begin,
        std::vector<std::uint8_t> payload,
        const auto on_finish
    ) {
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
                    assert(bytes_transferred == block_size);
                    // The last pieces can be a little bit shorter than usual pieces.
                    // Check either this is the last block or the last block of the file.
                    // We can do this because our client will always
                    //   request blocks from start to end.
                    if (begin + block_size >= piece_length
                        || (piece_index * piece_length) + begin + block_size
                            >= file.size()) {
                        // Run an SHA1 check for this piece.
                        check_sha1_piece_async(piece_index, on_finish);
                    } else {
                        on_finish(error_code, false);
                    }
                }
            }
        );
    }

    /*
     * Reads given block from the file async.
     * @param on_finish A function that will be called when
     *      the operation finishes. Signature should be on_finish(Message). 
     *      Second parameter is a Piece Message ready to send.
     * */
    void read_block_async(
        std::uint32_t piece_index,
        std::uint32_t begin,
        std::uint32_t length,
        const auto on_finish
    ) {
        if (piece_index >= piece_count || begin > piece_length) {
            // Invalid parameters, ignore.
            return;
        }
        auto buffer_ptr =
            std::make_shared<std::vector<std::uint8_t>>(length + 8);

        file.async_read_some_at(
            piece_index * piece_length + begin,
            asio::buffer(buffer_ptr->data() + 8, length),
            [=](const auto& error_code, std::size_t bytes_transferred) {
                if (error_code) {
                    BOOST_LOG_TRIVIAL(error)
                        << "Error while reading from the file: "
                        << error_code.message();
                } else {
                    assert(bytes_transferred == buffer_ptr->size());
                    Message message {
                        Message::Id::Piece,
                        std::move(*buffer_ptr)
                    };
                    message.write_int(0, piece_index);
                    message.write_int(1, begin);
                    on_finish(std::move(message));
                }
            }
        );
    }

    /*
     * Read from the file sync.
     * */
    std::vector<std::uint8_t>
    read_some_at(std::size_t offset, std::size_t length) {
        std::vector<std::uint8_t> buffer(length, 0);
        file.read_some_at(offset, asio::buffer(buffer));
        return buffer;
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
  private:
    /* Private helper functions. */

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
            [=, this](const auto& error_code, std::size_t) {
                if (error_code) {
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
    check_sha1_piece(std::size_t piece_index, const std::string_view piece);

    /*
     * Checks sha1 of pieces starting in range of [start_piece, end_piece).
     * Sets the bitfield value accordingly when a piece passes sha1.
     * @param start_piece Starts checking sha1 from this index.
     * @param end_piece Ends when index=end_piece.
     * */
    void check_pieces_sha1(std::size_t start_piece, std::size_t end_piece);

    void run_sha1_checksum();

    /*
     * Run a sha1 checksum over the pieces.
     */
    void run_sha1_checksum_multithread();

    /*
     * Creates a new file at the given path.
     * @param offset Offset in bytes which the function will begin to read.
     * @param length Length of the desired file.
     * */
    void extract_file(
        std::size_t offset,
        std::size_t length,
        const std::string& path
    );
    void extract_torrent();

  public:
    std::unique_ptr<Bitfield> bitfield;

  private:
    AsyncFile file;

    std::size_t piece_count;
    std::size_t piece_length;

    bool running = true;
    std::mutex running_cv_mutex;
    std::condition_variable running_cv;

    std::shared_ptr<Metadata> metadata;
};
} // namespace torrent

#endif
