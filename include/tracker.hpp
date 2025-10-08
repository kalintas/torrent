#ifndef TORRENT_TRACKER_HPP
#define TORRENT_TRACKER_HPP

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/url/urls.hpp>
#include <memory>

namespace torrent {
using namespace boost::asio::ip;

class TrackerManager;

class Tracker: public std::enable_shared_from_this<Tracker> {
  public:
    Tracker(TrackerManager& manager) : tracker_manager(manager) {}

    Tracker(const Tracker&) = delete;
    Tracker& operator=(const Tracker&) = delete;

    virtual ~Tracker() = default;

    /*
     * Creates a Tracker object. 
     * Tracker will use either UDP or HTTP/HTTPs protocols appropriately.
     * @param client A reference to Client object.
     * @param announce Announce string acquired from the .torrent file.
     * */
    static std::shared_ptr<Tracker>
    create_tracker(TrackerManager& tracker_manager, std::string announce);

    virtual void initiate_connection(boost::url tracker_url) = 0;

    friend std::ostream& operator<<(std::ostream& os, const Tracker& tracker) {
        os << "Tracker{ " << tracker.announce << " }";
        return os;
    }

  protected:
    void on_disconnect();
    void on_new_peer(tcp::endpoint endpoint);

  protected:
    std::string announce;

    TrackerManager& tracker_manager;
};

} // namespace torrent
#endif
