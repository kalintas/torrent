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
    // io_context should be on top of all other asio components.
    asio::io_context io_context;

    BencodeParser bencode_parser;

    std::string peer_id;
    std::default_random_engine random_engine;

    Tracker tracker;
    PeerManager peer_manager;

    static constexpr std::uint16_t DEFAULT_PORT = 8000;

  public:
    Client(const std::string_view path, std::uint16_t port = DEFAULT_PORT);
    // Object must be pinned to its memory address because
    //      Peers contain a reference to it.
    Client(const Client&) = delete;
    const Client& operator=(const Client&) = delete;

    void start();

  private:
    std::uint16_t port;
};
} // namespace torrent
#endif
