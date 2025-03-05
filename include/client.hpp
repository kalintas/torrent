#ifndef TORRENT_CLIENT_HPP
#define TORRENT_CLIENT_HPP

#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl.hpp>
#include <cstdint>
#include <memory>

#include "metadata.hpp"
#include "peer_manager.hpp"
#include "tracker_manager.hpp"

namespace torrent {

namespace asio = boost::asio;
using namespace boost::asio::ip;
using tcp = boost::asio::ip::tcp;

class Client {
  private:
    std::string peer_id;
    std::shared_ptr<Metadata> metadata;

    std::shared_ptr<Pieces> pieces;
    std::unique_ptr<TrackerManager> tracker_manager;
    std::unique_ptr<PeerManager> peer_manager;

    static constexpr std::uint16_t DEFAULT_PORT = 8000;

  public:
    Client(
        asio::io_context& io_context,
        asio::ssl::context& ssl_context,
        std::uint16_t port = DEFAULT_PORT
    );
    // Object must be pinned to its memory address because
    //      Peers contain a reference to it.
    Client(const Client&) = delete;
    const Client& operator=(const Client&) = delete;

    /*
     * Starts the client.
     * First il will parse the given torrent file.
     * After it will issue the required funcitons.
     * But it will not do any networking because its async.
     * Should only be called once after the constructor.
     * @param torrent Either a path to a .torrent file or a magnet link as a string.
     * */
    void start(const std::string_view torrent);

    /*
     * Waits until the client is finished downloading.
     * Is thread safe to call from other threads.
     * */
    void wait();

    /*
     * Notifies any calls to wait and wakes them in order to stop. 
     * Is thread safe to call from other threads.
     * */
    void stop();

  public:
    /*
     * Returns a const reference to the peer id of the Client object.
     * */
    const std::string& get_peer_id() const {
        return peer_id;
    }

    /*
     * Returns a const reference to current Metadata of the torrent. 
     * */
    const auto& get_metadata() const {
        return metadata;
    }

    /*
     * Returns the port that Client is using to listen incoming peers.
     * */
    std::uint16_t get_port() const {
        return port;
    }

  private:
    asio::io_context& io_context;
    asio::ssl::context& ssl_context;
    std::uint16_t port;
};
} // namespace torrent
#endif
