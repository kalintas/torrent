#include "tracker.hpp"

#include <boost/asio/ssl/stream.hpp>
#include <boost/url/scheme.hpp>
#include <boost/url/urls.hpp>
#include <memory>
#include <string>

#include "http_tracker.hpp"
#include "tracker_manager.hpp"
#include "udp_tracker.hpp"

namespace torrent {

std::shared_ptr<Tracker>
Tracker::create_tracker(TrackerManager& tracker_manager, std::string announce) {
    std::shared_ptr<Tracker> tracker;
    if (announce.starts_with("udp")) {
        // Udp tracker
        tracker =
            UdpTracker::create(tracker_manager, tracker_manager.io_context);
        tracker->initiate_connection(boost::url {announce});
        tracker->announce = std::move(announce);

        BOOST_LOG_TRIVIAL(info) << "New udp tracker: " << *tracker;
        return tracker;
    }
    // Http/Https tracker
    auto url = boost::url(announce);
    auto params = url.encoded_params();

    params.append({"info_hash", tracker_manager.metadata->get_info_hash()});
    params.append({"peer_id", tracker_manager.get_peer_id()});
    params.append({"port", std::to_string(tracker_manager.get_port())});
    params.append(
        {"uploaded", std::to_string(tracker_manager.metadata->get_uploaded())}
    );
    params.append(
        {"downloaded",
         std::to_string(tracker_manager.metadata->get_downloaded())}
    );
    params.append({"compact", "1"});
    params.append({"left", std::to_string(tracker_manager.metadata->get_left())}
    );

    switch (url.scheme_id()) {
        case boost::urls::scheme::http:
            tracker = HttpTracker::create(
                tracker_manager,
                tracker_manager.io_context,
                tcp::socket {tracker_manager.io_context}
            );
            break;
        case boost::urls::scheme::https:
            tracker = HttpsTracker::create(
                tracker_manager,
                tracker_manager.io_context,
                asio::ssl::stream<tcp::socket> {
                    tracker_manager.io_context,
                    tracker_manager.ssl_context
                }
            );
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
    tracker_manager.remove(announce);
}

void Tracker::on_new_peer(tcp::endpoint endpoint) {
    tracker_manager.on_new_peer(std::move(endpoint));
}

} // namespace torrent
