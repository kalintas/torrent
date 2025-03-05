#include "client.hpp"

#include <openssl/sha.h>

#include <boost/log/trivial.hpp>
#include <boost/url/scheme.hpp>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>

namespace torrent {

Client::Client(
    asio::io_context& io_context_ref,
    asio::ssl::context& ssl_context_ref,
    Config client_config
) :
    io_context(io_context_ref),
    ssl_context(ssl_context_ref),
    config(client_config) {
    // Generate 20 random characters for the peer id.
    static constexpr std::string_view alphanum =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    std::default_random_engine random_engine(std::random_device {}());
    std::uniform_int_distribution<std::size_t> dist(0, alphanum.size());
    peer_id.reserve(20);
    peer_id += "-KK1000-"; // Optional client identifier.
    while (peer_id.size() < 20) {
        peer_id.push_back(alphanum[dist(random_engine)]);
    }

    // Print out the peer id in a printable form.
    std::stringstream ss;
    ss << std::hex;
    for (const auto c : peer_id) {
        if (std::isprint(c)) {
            ss << c;
        } else {
            ss << "\\x" << static_cast<int>(c);
        }
    }
    BOOST_LOG_TRIVIAL(info) << "Peer id: " << ss.str();
}

void Client::start(const std::string_view torrent) {
    try {
        // Create the metadata from the input.
        metadata = Metadata::create(torrent, config);

        // Pieces will manage piece IO for us.
        pieces = Pieces::create(io_context, metadata);

        // Create managers.
        peer_manager =
            std::make_unique<PeerManager>(io_context, config, pieces, metadata);
        tracker_manager = std::make_unique<TrackerManager>(
            io_context,
            ssl_context,
            config,
            peer_id,
            metadata
        );

        // Magnet links only carry enough information
        //      to fetch the info directory from other peers.
        // So we need to wait until all the information is gathered before downloading.
        metadata->on_ready([this]() {
            pieces->init_file(); // Initialize the file.
            peer_manager->calculate_handshake(
                metadata->get_info_hash(),
                peer_id
            );
        });

        // Set a handler so when a new peer is fetched from
        //      the tracker it will be sent to the PeerManager.
        tracker_manager->set_on_new_peer([this](auto endpoint) {
            peer_manager->add(std::move(endpoint));
        });

        // Populate trackers from the tracker urls we got from the metadata.
        for (const auto& url : metadata->get_trackers()) {
            tracker_manager->add(url);
        }
    } catch (const std::runtime_error& e) {
        BOOST_LOG_TRIVIAL(error) << "Fatal client error: " << e.what();
    }
}

void Client::wait() {
    // First wait until the metadata is ready.
    if (metadata) {
        metadata->wait();
    }
    // Then wait until the torrent has been downloaded.
    if (pieces) {
        pieces->wait();
    }
}

void Client::stop() {
    if (metadata) {
        metadata->stop();
    }
    if (pieces) {
        pieces->stop();
    }
    if (tracker_manager) {
        tracker_manager->stop();
    }
    if (peer_manager) {
        tracker_manager->stop();
    }
}

} // namespace torrent
