#ifndef TORRENT_TRACKER_MANAGER_HPP
#define TORRENT_TRACKER_MANAGER_HPP

#include <boost/asio/ssl.hpp>
#include <boost/log/trivial.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "metadata.hpp"
#include "tracker.hpp"

namespace torrent {
namespace asio = boost::asio;

class TrackerManager {
  public:
    TrackerManager(
        asio::io_context& io_context_ref,
        asio::ssl::context& ssl_context_ref,
        std::uint16_t listen_port,
        std::string client_peer_id,
        std::shared_ptr<Metadata> metadata_ptr
    ) :
        metadata(std::move(metadata_ptr)),
        io_context(io_context_ref),
        ssl_context(ssl_context_ref),
        port(listen_port),
        peer_id(std::move(client_peer_id)) {}

    TrackerManager(const TrackerManager&) = delete;
    TrackerManager& operator=(const TrackerManager&) = delete;

    /*
     * Creates a new tracker with the given announce if it doesn't already exists.
     * */
    void add(std::string announce) {
        std::scoped_lock<std::mutex> lock {mutex};
        auto tracker = Tracker::create_tracker(*this, announce);
        if (tracker) {
            trackers.emplace(std::move(announce), std::move(tracker));
        }
    }

    /*
     * Removes the tracker with the given announce. 
     * */
    void remove(std::string announce) {
        std::scoped_lock<std::mutex> lock {mutex};
        const auto tracker_it = trackers.find(announce);
        if (tracker_it == trackers.end()) {
            return;
        }

        BOOST_LOG_TRIVIAL(info)
            << "Tracker count: " << trackers.size() - 1
            << ", Connection lost with " << *tracker_it->second;

        trackers.erase(tracker_it);
    }

    /*
     * Stops the trackers by deleting all of them.
     * */
    void stop() {
        std::scoped_lock<std::mutex> lock {mutex};
        trackers.clear();
    }

    /*
     * Sets a handler to be called when a new peer endpoint is available. 
     * */
    void set_on_new_peer(std::function<void(tcp::endpoint)> func) {
        on_new_peer = std::move(func);
    }

  public:
    std::shared_ptr<Metadata> metadata;

    const std::string& get_peer_id() const {
        return peer_id;
    }

    std::uint16_t get_port() const {
        return port;
    }

  private:
    asio::io_context& io_context;
    asio::ssl::context& ssl_context;
    std::uint16_t port;
    std::string peer_id;
    friend class Tracker;

    std::function<void(tcp::endpoint)> on_new_peer;

    std::mutex mutex;

    std::unordered_map<std::string, std::shared_ptr<Tracker>> trackers;
};

} // namespace torrent

#endif
