#ifndef TORRENT_PEER_HPP
#define TORRENT_PEER_HPP

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <ostream>
#include <vector>

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

    Peer(
        PeerManager& peer_manager,
        asio::io_context& io_context,
        tcp::socket socket
    ) :
        peer_manager(peer_manager),
        io_context(io_context),
        endpoint(socket.remote_endpoint()),
        socket(std::move(socket)) {
        change_status(Status::Connected);
    }

    Peer(Peer&& peer) :
        peer_manager(peer.peer_manager),
        io_context(peer.io_context),
        socket(std::move(peer.socket)),
        endpoint(std::move(peer.endpoint)) {}

    Peer(const Peer&) = delete;
    const Peer& operator=(const Peer&) = delete;

    void connect();

    friend std::ostream& operator<<(std::ostream& os, const Peer& peer) {
        os << "Peer{ ";
        if (!peer.remote_peer_id.empty()) {
            for (const auto c : peer.remote_peer_id) {
                if (std::isprint(c)) {
                    os << c;
                } else {
                    os << "\\x" << std::hex << (int)((std::uint8_t)c);
                }
            }
            os << std::dec;
        } else {
            os << peer.endpoint;
        }
        os << " }";
        return os;
    }

    Status get_status() const {
        return status;
    }

    const tcp::endpoint& get_endpoint() const {
        return endpoint;
    }

    friend class PeerManager;

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
    std::vector<std::uint8_t> remainder_buffer;

    std::string remote_peer_id;

    Status status = Status::Disconnected;
    PeerManager& peer_manager;
};

} // namespace torrent
#endif
