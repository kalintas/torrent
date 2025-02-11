#ifndef TORRENT_PEER_HPP
#define TORRENT_PEER_HPP

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/dynamic_bitset.hpp>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <vector>

#include "bitfield.hpp"
#include "message.hpp"

namespace torrent {

using namespace boost::asio::ip;
namespace asio = boost::asio;

class PeerManager;

class Peer: public std::enable_shared_from_this<Peer> {
  public:
    enum class State { Disconnected, Connected, Handshook };

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
        change_state(State::Connected);
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

    std::string to_string() const {
        std::ostringstream oss;
        oss << *this;
        return oss.str();
    }

    State get_state() const {
        return state;
    }

    const tcp::endpoint& get_endpoint() const {
        return endpoint;
    }

    friend class PeerManager;

  private:
    void change_state(State new_state);
    void listen_peer();
    void start_handshake();

    void send_message(Message message);
    void on_message(Message message);

  private:
    asio::io_context& io_context;
    tcp::socket socket;
    tcp::endpoint endpoint;

    static constexpr std::size_t BUFFER_SIZE = 1024;

    std::array<std::uint8_t, BUFFER_SIZE> buffer;
    std::vector<std::uint8_t> remainder_buffer;

    std::string remote_peer_id;

    State state = State::Disconnected;
    PeerManager& peer_manager;

  private:
    bool am_choking = true;
    bool am_interested = false;
    bool peer_choking = true;
    bool peer_interested = false;

    // Bitfield of the remote peer.
    // Ours is stored in pieces and shared among peers.
    std::unique_ptr<Bitfield> peer_bitfield;
};

} // namespace torrent
#endif
