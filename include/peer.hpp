#ifndef TORRENT_PEER_HPP
#define TORRENT_PEER_HPP

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <cstdint>
#include <iostream>
#include <ostream>

namespace torrent {

using namespace boost::asio::ip;
namespace asio = boost::asio;

class PeerManager;

class Peer: public std::enable_shared_from_this<Peer> {
  public:
    enum class Status { Disconnected, Connected, Handshook };

    Peer(
        PeerManager& peer_manager,
        asio::io_context& io_context,
        tcp::endpoint endpoint
    ) :
        peer_manager(peer_manager),
        io_context(io_context),
        endpoint(std::move(endpoint)),
        socket(io_context) {}

    Peer(Peer&& peer) :
        peer_manager(peer.peer_manager),
        io_context(peer.io_context),
        socket(std::move(peer.socket)),
        endpoint(std::move(peer.endpoint)) {}

    Peer(const Peer&) = delete;
    const Peer& operator=(const Peer&) = delete;

    void connect();

    friend std::ostream& operator<<(std::ostream& os, const Peer& peer) {
        os << "(";
        if (!peer.remote_peer_id.empty()) {
            os << peer.remote_peer_id;
        } else {
            os << peer.endpoint;
        }
        os << ")";
        return os;
    }

  private:
    void change_status(Status new_status);
    void listen_peer();
    void start_handshake();

  private:
    asio::io_context& io_context;
    tcp::socket socket;
    tcp::endpoint endpoint;

    static constexpr std::size_t BUFFER_SIZE = 1024;

    std::array<std::uint8_t, BUFFER_SIZE> buffer;

    std::string remote_peer_id;

    Status status;
    PeerManager& peer_manager;
};

} // namespace torrent
#endif
