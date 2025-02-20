#include "pieces.hpp"

#include <chrono>
#include <filesystem>
#include <stdexcept>

namespace torrent {

std::size_t Pieces::BLOCK_LENGTH = 1 << 14;

void Pieces::init(
    std::string file_name,
    std::size_t file_length,
    std::size_t p_length,
    std::string pieces
) {
    std::scoped_lock<std::mutex> lock {mutex};

    // Every piece holds 20 bytes in info.pieces
    // We can get the total piece count just by dividing with 20.
    piece_count = pieces.size() / 20;
    bitfield = std::make_unique<Bitfield>(piece_count);

    hashes = std::move(pieces);
    piece_length = p_length;

    bool file_exists = std::filesystem::exists(file_name);

    // Create the file if its already not created.
    file.open(
        file_name,
        asio::file_base::flags::create | asio::file_base::flags::read_write
    );
    if (!file.is_open()) {
        throw std::runtime_error(
            "Error while opening/creating the file " + file_name + "."
        );
    }

    auto file_size = file.size();
    // file_length is the variable we got from the .torrent file.
    // They could potentially be different. So resize it.
    file.resize(file_length);
    if (file_size < file_length) {
        // File was shorter and we extended.
        // So we need to write zeros at the end of the file.
        std::vector<char> buffer;
        buffer.resize(file_length - file_size, 0);
        file.write_some_at(file_size, asio::buffer(buffer));
    }

    auto file_megabytes = file_length / (1024 * 1024);
    BOOST_LOG_TRIVIAL(info)
        << "Opened the file " << file_name << " (" << file_megabytes << " Mb).";

    if (file_exists) {
        // Run SHA1 checksum if file already exists.
        run_sha1_checksum_multithread();
    }
}

void Pieces::wait() {
    while (running) {
        bitfield->wait_piece(); // Wait until a piece is downloaded.
        if (bitfield->get_completed_piece_count() == piece_count) {
            // File is ready.
            return;
        }
    }
}

void Pieces::stop() {
    running = false;
    bitfield->stop_wait();
}

void Pieces::check_pieces_sha1(std::size_t start_piece, std::size_t end_piece) {
    std::string piece_buffer(piece_length, '\0');
    auto asio_buffer =
        asio::mutable_buffer(piece_buffer.data(), piece_buffer.size());
    for (std::size_t i = start_piece; i < end_piece; i += 1) {
        file.read_some_at(i * piece_length, asio_buffer);

        if (check_sha1_piece(i, piece_buffer)) {
            // SHA1 check passed. Add this piece to bitfield.
            bitfield->set_piece(i);
        } /* else { // TODO: Decide if we actually have to zero the piece.
                // SHA1 check failed. Zero this piece.
                std::memset(piece_buffer.data(), 0, piece_length);
                file.write_some_at(i * piece_length, asio_buffer);
            } */
    }
}

void Pieces::run_sha1_checksum_multithread() {
    // Run a SHA1 checksum over the pieces after opening the file.
    // We need multithreading because this will take some time in a single thread.
    // We can do parallelism easily because we don't need to access same parts
    //      of the memory at the same time

    std::vector<std::thread> thread_pool;
    const auto thread_count = std::thread::hardware_concurrency();
    thread_pool.reserve(thread_count);

    BOOST_LOG_TRIVIAL(info)
        << "Starting the SHA1 checksum with " << thread_count << " threads.";

    // Start the timer
    auto start = std::chrono::steady_clock::now();
    auto piece_per_thread = piece_count / thread_count;

    for (std::size_t i = 0; i < thread_count; ++i) {
        thread_pool.emplace_back(std::thread {[=, this]() {
            std::size_t start_index = i * piece_per_thread;
            std::size_t end_index;
            if (i == thread_count - 1) {
                end_index = piece_count;
            } else {
                end_index = start_index + piece_per_thread;
            }
            check_pieces_sha1(start_index, end_index);
        }});
    }

    // Join the threads
    for (auto& thread : thread_pool) {
        thread.join();
    }

    if (bitfield->get_completed_piece_count() == piece_count) {
        running = false; // File is already ready.
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(end - start);

    BOOST_LOG_TRIVIAL(info)
        << "Finished SHA1 checksum in " << elapsed.count() << " seconds. Found "
        << bitfield->get_completed_piece_count() << " valid pieces out of "
        << piece_count << ".";
}

} // namespace torrent
