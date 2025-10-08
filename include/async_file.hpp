#ifndef TORRENT_ASYNC_FILE_HPP
#define TORRENT_ASYNC_FILE_HPP

#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/file_base.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/system/detail/error_code.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <mutex>

#ifdef BOOST_ASIO_HAS_IO_URING
    #include <boost/asio/random_access_file.hpp>
#endif

namespace torrent {

namespace asio = boost::asio;

#ifdef BOOST_ASIO_HAS_IO_URING

enum class AsyncFileOpenMode : std::uint32_t {
    ReadOnly = asio::file_base::flags::read_only,
    WriteOnly = asio::file_base::flags::write_only,
    ReadWrite = asio::file_base::flags::read_write,
    Binary = 0,
    Trunc = asio::file_base::flags::truncate,
    Append = asio::file_base::flags::append,
};

class AsyncFile {
  public:
    AsyncFile(asio::io_context& io_context) : file(io_context) {}

    void open(const auto path, const AsyncFileOpenMode open_mode) {
        file.open(
            path,
            static_cast<asio::file_base::flags>(open_mode)
                | asio::file_base::flags::create
        );
    }

    bool is_open() {
        return file.is_open();
    }

    void
    read_some_at(std::uint64_t offset, const asio::mutable_buffer& buffer) {
        file.read_some_at(offset, buffer);
    }

    void write_some_at(std::uint64_t offset, const asio::const_buffer& buffer) {
        file.write_some_at(offset, buffer);
    }

    void async_read_some_at(
        std::uint64_t offset,
        const asio::mutable_buffer& buffer,
        auto callback
    ) {
        file.async_read_some_at(offset, buffer, callback);
    }

    void async_write_some_at(
        std::uint64_t offset,
        const asio::const_buffer& buffer,
        auto callback
    ) {
        file.async_write_some_at(offset, buffer, callback);
    }

    std::uint64_t size() {
        return file.size();
    }

    void resize(std::uint64_t new_size) {
        file.resize(new_size);
    }

  private:
    asio::random_access_file file;
};

#else

enum AsyncFileOpenMode : std::ios::openmode {
    Append = std::ios::app,
    ReadOnly = std::ios::in,
    WriteOnly = std::ios::out,
    ReadWrite = std::ios::in | std::ios::out,
    Binary = std::ios::binary,
    Trunc = std::ios::trunc,
};

class AsyncFile {
  public:
    AsyncFile(asio::io_context& io_context) : io_context(io_context) {}

    void open(const auto path, const AsyncFileOpenMode open_mode) {
        std::scoped_lock<std::mutex> sl {mutex};
        file.open(path, open_mode);
        if (!file.is_open()) {
            file.clear();
            file.open(path, std::ios::out | std::ios::binary);
            file.close();
            file.open(path, open_mode);
        }

        file_path = path;
    }

    bool is_open() {
        std::scoped_lock<std::mutex> sl {mutex};
        return file.is_open();
    }

    void
    read_some_at(std::uint64_t offset, const asio::mutable_buffer& buffer) {
        std::scoped_lock<std::mutex> sl {mutex};
        file.seekg(
            static_cast<std::ifstream::off_type>(offset),
            std::ios::seekdir::beg
        );
        file.read(
            static_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(buffer.size())
        );
    }

    void write_some_at(std::uint64_t offset, const asio::const_buffer& buffer) {
        std::scoped_lock<std::mutex> sl {mutex};
        file.seekg(
            static_cast<std::ifstream::off_type>(offset),
            std::ios::seekdir::beg
        );
        file.write(
            static_cast<const char*>(buffer.data()),
            static_cast<std::streamsize>(buffer.size())
        );
    }

    void async_read_some_at(
        std::uint64_t offset,
        const asio::mutable_buffer& buffer,
        auto callback
    ) {
        this->read_some_at(offset, buffer);
        callback(boost::system::error_code(), buffer.size());
    }

    void async_write_some_at(
        std::uint64_t offset,
        const asio::const_buffer& buffer,
        auto callback
    ) {
        this->write_some_at(offset, buffer);
        callback(boost::system::error_code(), buffer.size());
    }

    std::uint64_t size() {
        std::scoped_lock<std::mutex> sl {mutex};
        file.seekg(0, std::ios::seekdir::end);
        return static_cast<std::uint64_t>(file.tellg());
    }

    void resize(std::uint64_t new_size) {
        std::filesystem::resize_file(file_path, new_size);
    }

  private:
    asio::io_context& io_context;

    std::mutex mutex;
    std::string file_path;
    std::fstream file;
};

#endif

inline AsyncFileOpenMode operator&(AsyncFileOpenMode x, AsyncFileOpenMode y) {
    return static_cast<AsyncFileOpenMode>(
        static_cast<unsigned int>(x) & static_cast<unsigned int>(y)
    );
}

inline AsyncFileOpenMode operator|(AsyncFileOpenMode x, AsyncFileOpenMode y) {
    return static_cast<AsyncFileOpenMode>(
        static_cast<unsigned int>(x) | static_cast<unsigned int>(y)
    );
}

inline AsyncFileOpenMode operator^(AsyncFileOpenMode x, AsyncFileOpenMode y) {
    return static_cast<AsyncFileOpenMode>(
        static_cast<unsigned int>(x) ^ static_cast<unsigned int>(y)
    );
}

inline AsyncFileOpenMode operator~(AsyncFileOpenMode x) {
    return static_cast<AsyncFileOpenMode>(~static_cast<unsigned int>(x));
}

inline AsyncFileOpenMode&
operator&=(AsyncFileOpenMode& x, AsyncFileOpenMode y) {
    x = x & y;
    return x;
}

inline AsyncFileOpenMode&
operator|=(AsyncFileOpenMode& x, AsyncFileOpenMode y) {
    x = x | y;
    return x;
}

inline AsyncFileOpenMode&
operator^=(AsyncFileOpenMode& x, AsyncFileOpenMode y) {
    x = x ^ y;
    return x;
}

} // namespace torrent

#endif
