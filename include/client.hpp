#ifndef TORRENT_CLIENT_HPP
#define TORRENT_CLIENT_HPP

#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <cstdint>
#include <random>

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
    std::default_random_engine random_engine;

    std::shared_ptr<Pieces> pieces;
    Tracker tracker;
    PeerManager peer_manager;

    static constexpr std::uint16_t DEFAULT_PORT = 8000;

  public:
    Client(
        asio::io_context& io_context,
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
  private:
    std::uint16_t port;
};
} // namespace torrent
#endif
