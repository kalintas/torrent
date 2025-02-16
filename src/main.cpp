#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/bind/bind.hpp>
#include <boost/log/trivial.hpp>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "client.hpp"

namespace asio = boost::asio;

int main() {
    asio::io_context io_context;
    auto client = std::make_shared<torrent::Client>(io_context, "./res/debian.iso.torrent");
    client->start();

    std::vector<std::thread> thread_pool;

    for (std::size_t i = 0; i < std::thread::hardware_concurrency(); ++i) {
        thread_pool.emplace_back(
            std::thread {[&io_context, client] () {
                try {
                    io_context.run();
                } catch (std::runtime_error error) {
                    BOOST_LOG_TRIVIAL(error) << "Fatal error running the client: " << error.what();
                    client->stop(); // Stop waiting and close the program.
                }
            } }
        );
    }
    // Wait until the client is finished.
    client->wait();

    // Stop the context and the worker threads.
    io_context.stop();
    for (auto& thread : thread_pool) {
        thread.join();
    }
}
