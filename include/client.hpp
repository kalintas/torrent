#ifndef TORRENT_CLIENT_HPP
#define TORRENT_CLIENT_HPP

#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl.hpp>
#include <cstdint>

#include "bencode_parser.hpp"
#include "peer_manager.hpp"
#include "tracker.hpp"

namespace torrent {

namespace asio = boost::asio;
using namespace boost::asio::ip;
using tcp = boost::asio::ip::tcp;

class Client {
  private:
    BencodeParser bencode_parser;

    std::string peer_id;
    std::string info_hash;

    std::shared_ptr<Pieces> pieces;
    std::unordered_map<std::string, std::shared_ptr<Tracker>> trackers;
    PeerManager peer_manager;

    static constexpr std::uint16_t DEFAULT_PORT = 8000;

  public:
    Client(
        asio::io_context& io_context,
        asio::ssl::context& ssl_context,
        const std::string_view path,
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
     * */
    void start();

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
     * Returns a reference to the peer id of the Client object.
     * */
    const std::string& get_peer_id() const {
        return peer_id;
    }

    /*
     * Returns a reference to the info hash of the Client object.
     * */
    const std::string& get_info_hash() const {
        return info_hash;
    }

    /*
     * Returns the port that Client is using to listen incoming peers.
     * */
    std::uint16_t get_port() const {
        return port;
    }

    /*
     * Returns the total amount of bytes downloaded since the start of the Client.
     * */
    std::size_t get_downloaded() const {
        return 0; // TODO
    }

    /*
     * Returns the number of bytes the client still 
     *      needs too download before the torrent is complete. 
     * */
    std::size_t get_left() const {
        return 0; // TODO
    }

    /*
     * Returns the total amount of bytes uploaded since the start of the Client.
     * */
    std::size_t get_uploaded() const {
        return 0; // TODO
    }

  private:
    // Add Tracker as a friend for improved encapsulation.
    friend Tracker;
    void add_peer(tcp::endpoint endpoint);
    void remove_tracker();

  private:
    asio::io_context& io_context;
    asio::ssl::context& ssl_context;
    std::uint16_t port;
};
} // namespace torrent
#endif
