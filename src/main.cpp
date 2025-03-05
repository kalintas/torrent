#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/verify_mode.hpp>
#include <boost/bind/bind.hpp>
#include <boost/log/trivial.hpp>
#include <exception>
#include <memory>
#include <thread>
#include <vector>

#include "client.hpp"
#include "config.hpp"

namespace asio = boost::asio;

int main(const int, const char* argv[]) {
    auto start = std::chrono::steady_clock::now(); // Start the timer.

    asio::io_context io_context;
    asio::ssl::context ssl_context(asio::ssl::context::tls_client
    ); // Create the ssl context.
    ssl_context.set_default_verify_paths();

    torrent::Config config = torrent::ConfigBuilder::default_config()
    .build();

    auto client = std::make_shared<torrent::Client>(io_context, ssl_context, config);
    client->start(argv[1]);
    std::vector<std::thread> thread_pool;

    for (std::size_t i = 0; i < std::thread::hardware_concurrency(); ++i) {
        thread_pool.emplace_back(std::thread {[&io_context, client]() {
            try {
                io_context.run();
            } catch (const std::exception& exception) {
                BOOST_LOG_TRIVIAL(error)
                    << "Fatal error running the client: " << exception.what();
                client->stop(); // Stop waiting and close the program.
            }
        }});
    }
    // Wait until the client is finished.
    client->wait();

    // Stop the context and the worker threads.
    io_context.stop();
    for (auto& thread : thread_pool) {
        thread.join();
    }

    // End the timer.
    auto end = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(end - start);

    BOOST_LOG_TRIVIAL(info) << "Finished downloading the file in "
                            << elapsed.count() << " seconds.";
}
