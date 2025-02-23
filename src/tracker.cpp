#include "tracker.hpp"

#include <boost/asio/ssl/stream.hpp>
#include <boost/url/scheme.hpp>
#include <boost/url/urls.hpp>
#include <memory>
#include <string>

#include "client.hpp"
#include "http_tracker.hpp"
#include "udp_tracker.hpp"

namespace torrent {

std::shared_ptr<Tracker>
Tracker::create_tracker(Client& client, std::string announce) {
    if (announce.starts_with("udp")) {
        // Udp tracker
        auto tracker =
            std::shared_ptr<Tracker>(new UdpTracker(client, client.io_context));
        tracker->initiate_connection(boost::url {announce});
        tracker->announce = std::move(announce);

        BOOST_LOG_TRIVIAL(info) << "New udp tracker: " << *tracker;
        return tracker;
    }
    // Http/Https tracker
    auto url = boost::url(announce);
    auto params = url.encoded_params();

    params.append({"info_hash", client.get_info_hash()});
    params.append({"peer_id", client.get_peer_id()});
    params.append({"port", std::to_string(client.get_port())});
    params.append({"uploaded", std::to_string(client.get_uploaded())});
    params.append({"downloaded", std::to_string(client.get_downloaded())});
    params.append({"compact", "1"});
    params.append({"left", std::to_string(client.get_left())});

    std::shared_ptr<Tracker> tracker;

    switch (url.scheme_id()) {
        case boost::urls::scheme::http:
            tracker = std::shared_ptr<Tracker>(new HttpTracker(
                client,
                client.io_context,
                tcp::socket {client.io_context}
            ));
            break;
        case boost::urls::scheme::https:
            tracker = std::shared_ptr<Tracker>(new HttpsTracker {
                client,
                client.io_context,
                asio::ssl::stream<tcp::socket> {
                    client.io_context,
                    client.ssl_context
                }
            });
            break;
        default:
            return nullptr; // Unknown scheme.
    }
    tracker->announce = std::move(announce);
    tracker->initiate_connection(std::move(url));
    BOOST_LOG_TRIVIAL(info) << "New http tracker: " << *tracker;
    return tracker;
}

void Tracker::on_disconnect() {
    client.trackers.erase(announce);
}

void Tracker::on_new_peer(tcp::endpoint endpoint) {
    client.peer_manager.add(std::move(endpoint));
}
} // namespace torrent
