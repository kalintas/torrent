#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/bind/bind.hpp>
#include <thread>
#include <vector>

#include "client.hpp"

namespace asio = boost::asio;

int main() {
    asio::io_context io_context;
    torrent::Client client {io_context, "./res/debian.iso.torrent"};
    client.start();

    std::vector<std::thread> thread_pool;

    for (std::size_t i = 0; i < std::thread::hardware_concurrency(); ++i) {
        thread_pool.emplace_back(
            std::thread {boost::bind(&asio::io_context::run, &io_context)}
        );
    }
    // Wait until the client is finished.
    client.wait();

    // Stop the context and the worker threads.
    io_context.stop();
    for (auto& thread : thread_pool) {
        thread.join();
    }
}
