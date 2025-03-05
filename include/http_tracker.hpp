#ifndef TORRENT_HTTP_TRACKER_HPP
#define TORRENT_HTTP_TRACKER_HPP

#include <boost/asio/connect.hpp>
#include <boost/asio/detail/chrono.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/address_v6.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/trivial.hpp>
#include <boost/url.hpp>
#include <concepts>
#include <cstddef>
#include <exception>
#include <memory>
#include <variant>

#include "bencode_parser.hpp"
#include "tracker.hpp"

namespace torrent {

namespace http = boost::beast::http;
namespace beast = boost::beast;
namespace asio = boost::asio;
using namespace boost::asio::ip;

template<typename StreamType>
concept StreamTypeConcept = std::same_as<StreamType, tcp::socket>
    || std::same_as<StreamType, asio::ssl::stream<tcp::socket>>;

/*
 * A BitTorrent tracker abstraction that uses HTTP/HTTPS protocol.
 * */
template<StreamTypeConcept StreamType>
class BasicHttpTracker: public Tracker {
  private:
    struct Private {
        explicit Private() = default;
    };

  public:
    BasicHttpTracker(
        Private,
        TrackerManager& tracker_manager_ref,
        asio::io_context& io_context_ref,
        StreamType&& input_stream
    ) :
        Tracker(tracker_manager_ref),
        stream(std::forward<StreamType>(input_stream)),
        timer(io_context_ref),
        resolver(io_context_ref) {}

    ~BasicHttpTracker() {}

    static std::shared_ptr<Tracker> create(
        TrackerManager& tracker_manager,
        asio::io_context& io_context,
        StreamType&& stream
    ) {
        return std::make_shared<BasicHttpTracker<StreamType>>(
            Private {},
            tracker_manager,
            io_context,
            std::forward<StreamType>(stream)
        );
    }

    std::shared_ptr<BasicHttpTracker<StreamType>> get_ptr() {
        return std::dynamic_pointer_cast<BasicHttpTracker<StreamType>>(
            shared_from_this()
        );
    }

    /*
     * This function will initiate a connection with the tracker.
     * @param tracker_url url of the tracker
     */
    void initiate_connection(boost::url tracker_url) override {
        url = std::move(tracker_url);
        // Firstly resolve the given url to an ip address.
        resolver.async_resolve(
            url.host(),
            url.port(),
            [self = get_ptr()](auto error, auto endpoints) {
                if (error) {
                    BOOST_LOG_TRIVIAL(error)
                        << *self << " could not resolve the given url: "
                        << error.message();
                    return self->on_disconnect();
                }

                // Then connect to the tracker with found endpoints.
                self->connect(endpoints);
            }
        );
    }

  private:
    void connect(const tcp::resolver::results_type& endpoints);

    /*
     * Send a GET request to the tracker. 
     * */
    void fetch_peers() {
        request = {http::verb::get, url.encoded_target(), 11};
        request.set(http::field::host, url.host());
        request.set(http::field::close, "close");
        request.set(http::field::accept, "*/*");

        http::async_write(
            stream,
            request,
            [self = get_ptr()](std::error_code error, std::size_t) {
                if (error) {
                    BOOST_LOG_TRIVIAL(error)
                        << *self << " could not fetch peers" << error.message();
                    return self->on_disconnect();
                }
            }
        );
    }

    /*
     * Listen a HTTP packet from the tracker. 
     * Tracker should give the list of peers in bencode format.
     * */
    void listen_packet() {
        http::async_read(
            stream,
            buffer,
            response,
            [self = get_ptr()](std::error_code error, std::size_t bytes_read) {
                if (error) {
                    BOOST_LOG_TRIVIAL(error)
                        << "Error while listening a packet from " << *self
                        << ": " << error.message();
                    return self->on_disconnect();
                }

                BOOST_LOG_TRIVIAL(info)
                    << "Read a " << bytes_read
                    << " bytes long http response from  the " << *self;
                try {
                    BencodeParser input_parser(
                        std::make_unique<std::stringstream>(self->response.body(
                        ))
                    );
                    input_parser.parse();

                    auto element = input_parser.get();
                    auto dict = element.get<BencodeParser::Dictionary>();
                    BencodeParser::Dictionary::iterator interval_element,
                        peers_element;

                    if ((interval_element = dict.find("interval")) == dict.end()
                        || (peers_element = dict.find("peers")) == dict.end()) {
                        BOOST_LOG_TRIVIAL(error)
                            << "Received an invalid bencode string from the "
                            << *self;
                        return self->on_disconnect();
                    }

                    if (!std::holds_alternative<BencodeParser::Integer>(
                            interval_element->second.value
                        )
                        || !std::holds_alternative<BencodeParser::String>(
                            peers_element->second.value
                        )) {
                        BOOST_LOG_TRIVIAL(error)
                            << "Received an invalid bencode string from the "
                            << *self;
                        return self->on_disconnect();
                    }

                    // Interval tells us how often we should
                    //      fetch the peer list again from the tracker
                    const std::size_t interval = static_cast<std::size_t>(
                        interval_element->second.get<BencodeParser::Integer>()
                    );
                    // Add peers
                    auto peer_string =
                        peers_element->second.get<BencodeParser::String>();
                    std::array<std::uint8_t, 4> ip;
                    for (std::size_t i = 0; i + 6 <= peer_string.size();
                         i += 6) {
                        std::copy(
                            peer_string.begin()
                                + static_cast<std::ptrdiff_t>(i),
                            peer_string.begin() + static_cast<std::ptrdiff_t>(i)
                                + 4,
                            ip.begin()
                        );
                        std::uint16_t port =
                            static_cast<std::uint16_t>(
                                static_cast<std::uint16_t>(peer_string[i + 5])
                                << 8
                            )
                            | static_cast<std::uint16_t>(peer_string[i + 4]);
                        // Port is always big endian. So convert it to the native
                        auto endpoint = tcp::endpoint {
                            address_v4(ip),
                            boost::endian::big_to_native(port)
                        };

                        self->on_new_peer(std::move(endpoint));
                    }
                    BOOST_LOG_TRIVIAL(info)
                        << "Fetched " << (peer_string.size() / 6) << " peers";

                    self->timer.expires_after(asio::chrono::seconds(interval));
                    self->timer.async_wait([self](auto wait_error) {
                        if (wait_error) {
                            BOOST_LOG_TRIVIAL(error)
                                << *self << " error in async_wait"
                                << wait_error.message();
                            return;
                        }
                        // Fetch the peer list again.
                        self->fetch_peers(
                        ); // Request peer list from the tracker.
                        self->listen_packet(); // Listen response.
                    });

                } catch (const std::exception& exception) {
                    BOOST_LOG_TRIVIAL(error)
                        << "Error while parsing the message from " << *self
                        << ": " << exception.what();
                    return self->on_disconnect();
                }
            }
        );
    }

  private:
    boost::url url;

    beast::flat_static_buffer<4096> buffer;
    StreamType stream;

    asio::steady_timer timer;

    tcp::resolver resolver;
    http::request<http::string_body> request;
    http::response<http::string_body> response;
};

using HttpTracker = BasicHttpTracker<tcp::socket>;
using HttpsTracker = BasicHttpTracker<asio::ssl::stream<tcp::socket>>;

template<>
inline void HttpTracker::connect(const tcp::resolver::results_type& endpoints) {
    asio::async_connect(
        stream,
        endpoints,
        [self = get_ptr()](auto error, auto) {
            if (error) {
                BOOST_LOG_TRIVIAL(error) << "Could not connect to the " << *self
                                         << ": " << error.message();
                return self->on_disconnect();
            }
            // Tracker uses HTTP protocol. Is ready to fetch peers.
            self->fetch_peers(); // Request peer list from the tracker.
            self->listen_packet(); // Listen response.
        }
    );
}

template<>
inline void HttpsTracker::connect(const tcp::resolver::results_type& endpoints
) {
    asio::async_connect(
        stream.lowest_layer(),
        endpoints,
        [self = get_ptr()](auto error, auto) {
            if (error) {
                BOOST_LOG_TRIVIAL(error) << "Could not connect to the " << *self
                                         << ": " << error.message();
                return self->on_disconnect();
            }
            // https://stackoverflow.com/a/72797139/14959432
            // Set SNI Hostname (many hosts need this to handshake successfully)
            if (!SSL_set_tlsext_host_name(
                    self->stream.native_handle(),
                    self->url.host().data()
                )) {
                BOOST_LOG_TRIVIAL(error)
                    << "SNI Hostname could not be set: " << ::ERR_get_error();
                return self->on_disconnect();
            }

            // Tracker uses HTTPs protocol. First do the ssl handshake.
            self->stream.async_handshake(
                asio::ssl::stream_base::client,
                [self](const auto& handshake_error) {
                    if (handshake_error) {
                        BOOST_LOG_TRIVIAL(error)
                            << "Could not ssl handshake with the " << *self
                            << ": " << handshake_error.message();
                        return self->on_disconnect();
                    }
                    self->fetch_peers(); // Request peer list from the tracker.
                    self->listen_packet(); // Listen response.
                }
            );
        }
    );
}

} // namespace torrent
#endif
