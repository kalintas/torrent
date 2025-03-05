#include "pieces.hpp"

#include <boost/log/trivial.hpp>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace torrent {

void Pieces::init_file() {
    // Metadata should be ready before calling this function.
    assert(metadata->is_ready());

    // These values are constant through the download.
    // And they are frequently used so store them in the object.
    piece_count = metadata->get_piece_count();
    piece_length = metadata->get_piece_length();

    bitfield =
        std::make_unique<Bitfield>((piece_count / 8) + (piece_count % 8 != 0));
    const auto& file_name = metadata->get_file_name();
    const std::size_t file_length = metadata->get_total_length();

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

    // file_length is the variable we got from the .torrent file.
    // They could potentially be different. So resize it.
    file.resize(file_length);

    auto file_megabytes = file_length / (1024 * 1024);
    BOOST_LOG_TRIVIAL(info)
        << "Opened the file " << file_name << " (" << file_megabytes << " Mb).";

    if (file_exists) {
        // Create a temporary on piece callback.
        // The reason we are doing this because we don't want to possibly
        //      extract the torrent before finishing the sha1 checksum.
        bitfield->set_on_piece_complete([self_weak = get_weak(
                                         )](std::size_t piece_index) mutable {
            if (auto self = self_weak.lock()) {
                self->metadata->on_piece_complete(piece_index);
            }
        });

        run_sha1_checksum_multithread();
        // The file is already complete. Just extract the torrent.
        if (metadata->is_file_complete()) {
            extract_torrent();
            stop();
            return;
        }
    }

    // Set the on piece callback.
    bitfield->set_on_piece_complete([self_weak = get_weak(
                                     )](std::size_t piece_index) mutable {
        // Create a weak pointer to avoid cyclic reference.
        if (auto self = self_weak.lock()) {
            self->metadata->on_piece_complete(piece_index);
            if (!self->metadata->is_file_complete()) {
                return;
            }
            // Downloading has finished. Extract the torrent if its necessary.
            self->extract_torrent();
            self->stop();
        }
    });
}

void Pieces::extract_file(
    std::size_t offset,
    std::size_t length,
    const std::string& path
) {
    std::ofstream output_file(path, std::ios::binary | std::ios::trunc);
    if (!output_file) {
        BOOST_LOG_TRIVIAL(error) << "Could not create file: " << path;
        return;
    } else {
        BOOST_LOG_TRIVIAL(info) << "Created file: " << path;
    }
    const auto buffer = read_some_at(offset, length);
    output_file.write(
        reinterpret_cast<const char*>(buffer.data()),
        static_cast<std::streamsize>(length)
    );
}

void Pieces::extract_torrent() {
    // File is complete and its time to extract it.
    namespace fs = std::filesystem;

    const auto& files = metadata->get_files();
    if (files.size() == 1) {
        // Torrent is in single file mode.
        auto [length, path] = files[0];
        extract_file(0, length, path);
        return;
    }

    BOOST_LOG_TRIVIAL(info) << "Started extracting the torrent file.";
    const std::string folder_path = "./" + metadata->get_name();
    try {
        fs::create_directory(folder_path);
        BOOST_LOG_TRIVIAL(info) << "Created the folder in: " << folder_path;
    } catch (const std::exception& exception) {
        BOOST_LOG_TRIVIAL(error)
            << "Error while creating the folder: " << exception.what();
        return;
    }

    std::size_t offset = 0;
    for (auto [length, path] : files) {
        extract_file(offset, length, folder_path + path);
        offset += length;
    }
}

void Pieces::wait() {
    std::unique_lock<std::mutex> lock {running_cv_mutex};
    running_cv.wait(lock, [self = get_ptr()] { return !self->running; });
}

void Pieces::stop() {
    std::scoped_lock<std::mutex> lock {running_cv_mutex};
    running = false;
    running_cv.notify_all();
}

bool Pieces::check_sha1_piece(
    std::size_t piece_index,
    const std::string_view piece
) {
    unsigned char hash[20];
    SHA1(
        reinterpret_cast<const unsigned char*>(piece.data()),
        piece.size(),
        hash
    );
    const auto& pieces = metadata->get_pieces();
    int sha1_check = std::memcmp(
        static_cast<const void*>(&pieces[piece_index * 20]),
        static_cast<const void*>(hash),
        20
    );
    return sha1_check == 0;
}

void Pieces::check_pieces_sha1(std::size_t start_piece, std::size_t end_piece) {
    std::string piece_buffer;
    for (std::size_t i = start_piece; i < end_piece; i += 1) {
        std::size_t length = piece_length;
        if (i == piece_count - 1) {
            // Last pieces can be shorter then usual.
            length = file.size() - i * piece_length;
        }

        piece_buffer.resize(length);
        file.read_some_at(i * piece_length, asio::buffer(piece_buffer));

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

    if (metadata->is_file_complete()) {
        running = false; // File is already ready.
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(end - start);

    BOOST_LOG_TRIVIAL(info) << "Finished SHA1 checksum in " << elapsed.count()
                            << " seconds. Found " << metadata->get_pieces_done()
                            << " valid pieces out of " << piece_count << ".";
}

} // namespace torrent
