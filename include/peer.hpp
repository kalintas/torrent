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
    enum class State {
        Disconnected,
        Connected,
        Handshook,
        Idle,
        DownloadingPiece
    };

    Peer(
        PeerManager& peer_manager_ref,
        asio::io_context& io_context_ref,
        tcp::endpoint peer_endpoint
    ) :
        io_context(io_context_ref),
        socket(io_context_ref),
        endpoint(std::move(peer_endpoint)),
        peer_manager(peer_manager_ref),
        timer(io_context_ref) {}

    Peer(
        PeerManager& peer_manager_ref,
        asio::io_context& io_context_ref,
        tcp::socket peer_socket
    ) :
        io_context(io_context_ref),
        socket(std::move(peer_socket)),
        endpoint(socket.remote_endpoint()),
        peer_manager(peer_manager_ref),
        timer(io_context_ref) {
        change_state(State::Connected);
    }

    Peer(Peer&& peer) :
        io_context(peer.io_context),
        socket(std::move(peer.socket)),
        endpoint(std::move(peer.endpoint)),
        peer_manager(peer.peer_manager),
        timer(io_context) {}

    Peer(const Peer&) = delete;
    const Peer& operator=(const Peer&) = delete;

    std::shared_ptr<Peer> get_ptr() {
        return shared_from_this();
    }

    void connect();

    friend std::ostream& operator<<(std::ostream& os, const Peer& peer) {
        os << "Peer{ ";
        if (!peer.remote_peer_id.empty()) {
            for (const auto c : peer.remote_peer_id) {
                if (std::isprint(c)) {
                    os << c;
                } else {
                    os << "\\x" << std::hex << static_cast<int>(c);
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

    bool get_handshook() const {
        return handshook;
    }

    const tcp::endpoint& get_endpoint() const {
        return endpoint;
    }

    friend class PeerManager;

  private:
    void change_state(State new_state);
    void listen_peer();
    void listen_message();

    void listen_handshake();
    void start_handshake();

    template<typename... Func>
    void send_message(Message message, Func... func) {
        std::string message_str = message.to_string();
        auto buffer_ptr =
            std::make_shared<std::vector<std::uint8_t>>(message.into_bytes());

        send_message_impl(
            std::move(buffer_ptr),
            std::move(message_str),
            0,
            func...
        );
    }

    template<typename... Func>
    void send_message_impl(
        std::shared_ptr<std::vector<std::uint8_t>> buffer_ptr,
        std::string message_str,
        std::size_t start,
        Func... func
    ) {
        socket.async_send(
            asio::buffer(
                buffer_ptr->data() + start,
                buffer_ptr->size() - start
            ),
            [self = get_ptr(),
             buffer_ptr,
             str = std::move(message_str),
             func...](const auto& error, const auto bytes_send) {
                if (error) {
                    BOOST_LOG_TRIVIAL(error)
                        << "Error while sending a message to " << *self << ": "
                        << error.message();
                } else if (buffer_ptr->size() != bytes_send) {
                    // Message is not sent fully.
                    // Send the remaining part of the message.
                    self->send_message_impl(
                        std::move(buffer_ptr),
                        std::move(str),
                        bytes_send,
                        func...
                    );
                } else {
                // Sent the message.
#ifndef NDEBUG
                    BOOST_LOG_TRIVIAL(debug)
                        << "Sent " << str << " to " << *self;
#endif
                    (func(self), ...);
                }
            }
        );
    }

    void on_message(Message message);
    void send_requests();
    void assign_piece();

  private:
    asio::io_context& io_context;
    tcp::socket socket;
    tcp::endpoint endpoint;

    std::vector<std::uint8_t> buffer;
    std::size_t read_message_bytes = 0;

    std::string remote_peer_id;

    State state = State::Disconnected;
    PeerManager& peer_manager;
    PieceIndex current_piece_index;

    std::mutex mutex;
    std::size_t current_block = 0;
    std::size_t piece_received = 0;

    // Constants
    static constexpr std::size_t REQUEST_COUNT_PER_CALL = 6;
    static constexpr std::size_t MAX_MESSAGE_LENGTH = 1 << 17;

    asio::steady_timer timer;

  private:
    bool am_choking = true;
    bool am_interested = false;
    bool peer_choking = true;
    bool peer_interested = false;

    bool handshook = false;

    // Bitfield of the remote peer.
    // Ours is stored in pieces and shared among peers.
    std::unique_ptr<Bitfield> peer_bitfield;
};

} // namespace torrent
#endif
