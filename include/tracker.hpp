#ifndef TORRENT_TRACKER_HPP
#define TORRENT_TRACKER_HPP

#include <boost/asio/connect.hpp>
#include <boost/asio/detail/chrono.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/address_v6.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/log/trivial.hpp>
#include <boost/url.hpp>

#include "bencode_parser.hpp"

namespace torrent {

namespace http = boost::beast::http;
namespace beast = boost::beast;
namespace asio = boost::asio;
using namespace boost::asio::ip;

class Tracker {
  public:
    Tracker(asio::io_context& io_context) :
        io_context(io_context),
        resolver(io_context),
        timer(io_context),
        socket(io_context) {}

    /*
     * This function will initiate a connection with the tracker.
     * @param tracker_url url of the tracker
     * @param add_peer_func a lambda that will get called when a Peer is fetched
     *     lambda should be in this signature: add_peer_func(tcp::endpoint)
     */
    void
    initiate_connection(boost::url tracker_url, const auto& add_peer_func) {
        url = std::move(tracker_url);
        // Firstly resolve the given url to an ip address.
        resolver.async_resolve(
            url.host(),
            url.port(),
            [this, &add_peer_func](auto error, auto endpoints) {
                if (error) {
                    throw std::runtime_error(
                        "Could not resolve the given url: " + error.message()
                        + '\n'
                    );
                }
                asio::async_connect(
                    socket,
                    endpoints,
                    [&](auto error, auto result) {
                        if (error) {
                            throw std::runtime_error(
                                "Could not connect to the tracker: "
                                + error.message() + '\n'
                            );
                        }

                        fetch_peers(); // Request peer list from the tracker.
                        listen_packet(add_peer_func); // Listen response.
                    }
                );
            }
        );
    }

  private:
    /*
     * Send a GET request to the tracker. 
     * */
    void fetch_peers() {
        request = {http::verb::get, url.encoded_target(), 11};
        request.set(http::field::host, url.host());
        request.set(http::field::close, "close");
        request.set(http::field::accept, "*/*");

        http::async_write(
            socket,
            request,
            [](std::error_code error, std::size_t bytes_sent) {
                if (error) {
                    throw std::runtime_error(
                        "Could not fetch peers: " + error.message() + '\n'
                    );
                }
            }
        );
    }

    /*
     * Listen a HTTP packet from the tracker. 
     * Tracker should give the list of peers in bencode format.
     * */
    void listen_packet(const auto& add_peer_func) {
        http::async_read(
            socket,
            buffer,
            response,
            [this,
             &add_peer_func](std::error_code error, std::size_t bytes_read) {
                if (error) {
                    throw std::runtime_error(
                        "Error while listening a packet from tracker: "
                        + error.message() + '\n'
                    );
                }

                BOOST_LOG_TRIVIAL(info)
                    << "Read a " << bytes_read
                    << " bytes long http response from  the tracker "
                    << url.host();

                BencodeParser input_parser(
                    std::make_unique<std::stringstream>(response.body())
                );
                input_parser.parse();
                auto element = input_parser.get();
                auto dict = element.get<BencodeParser::Dictionary>();

                // Interval tells us how often we should
                //      fetch the peer list again from the tracker
                const std::size_t interval =
                    static_cast<std::size_t>(dict.at("interval").get<int>());
                // Add peers
                auto peer_string = dict.at("peers").get<std::string>();
                for (int i = 0; i < peer_string.size() - 6; i += 6) {
                    std::array<std::uint8_t, 4> ip;
                    std::copy(
                        peer_string.begin() + i,
                        peer_string.begin() + i + 4,
                        ip.begin()
                    );
                    std::uint16_t port =
                        ((std::uint16_t)peer_string[i + 5] << 8)
                        | (std::uint16_t)peer_string[i + 4];
                    // Port is always big endian. So convert it to the native
                    auto endpoint = tcp::endpoint {
                        address_v4(ip),
                        boost::endian::big_to_native(port)
                    };

                    add_peer_func(std::move(endpoint));
                }
                BOOST_LOG_TRIVIAL(info)
                    << "Fetched " << (peer_string.size() / 6) << " peers";

                timer.expires_after(asio::chrono::seconds(interval));
                timer.async_wait([&](auto error) {
                    if (error) {
                        throw new std::runtime_error(
                            "Error in async_wait: " + error.message() + '\n'
                        );
                    }
                    // Fetch the peer list again.
                    fetch_peers(); // Request peer list from the tracker.
                    listen_packet(add_peer_func); // Listen response.
                });
            }
        );
    }

  private:
    asio::io_context& io_context;

    boost::url url;

    beast::flat_static_buffer<4096> buffer;
    tcp::socket socket;

    asio::steady_timer timer;

    tcp::resolver resolver;
    http::request<http::string_body> request;
    http::response<http::string_body> response;
};
} // namespace torrent
#endif
