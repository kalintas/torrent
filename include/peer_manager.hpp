#ifndef PEER_MANAGER_HPP
#define PEER_MANAGER_HPP

#include <boost/lockfree/queue.hpp>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

#include "message.hpp"
#include "peer.hpp"
#include "pieces.hpp"
#include "thread_safe_queue.hpp"

namespace torrent {

class PeerManager {
  public:
    PeerManager(
        asio::io_context& io_context,
        std::uint16_t port,
        std::shared_ptr<Pieces> pieces
    ) :
        io_context(io_context),
        pieces(pieces),
        acceptor(io_context, tcp::endpoint(tcp::v4(), port)),
        new_peer_socket(io_context) {}

    /*
     * Creates a new peer with the given endpoint if it does not already exist.
     * */
    void add(tcp::endpoint endpoint);

    /*
     * Removes the peer with the given endpoint.
     * */
    void remove(const tcp::endpoint& endpoint);

    /*
     * Calculates the handshake for peer connections later on. 
     * */
    void
    calculate_handshake(std::string_view info_hash, std::string_view peer_id);

    /*
     * Other peers might try to connect with us. Accept them through this function.
     * */
    void accept_new_peers();

    /*
     * Adds message to internal send queue. Message will be send effectively.
     * */
    void send_message(std::shared_ptr<Peer> peer, Message message);

    void on_handshake(Peer& peer);

  private:
    void send_all_messages();

  public:
    std::size_t peer_count() const {
        return peers.size();
    }

    const auto& get_handshake() {
        return handshake;
    }

    int get_active_peers() const {
        return active_peers;
    }

  public:
    std::shared_ptr<Pieces> pieces;
  
  private:
    asio::io_context& io_context;
    tcp::acceptor acceptor;
    tcp::socket new_peer_socket;

    static constexpr std::size_t HANDSHAKE_SIZE = 68;
    std::array<std::uint8_t, HANDSHAKE_SIZE> handshake;

    int active_peers = 0;

    std::unordered_map<tcp::endpoint, std::shared_ptr<Peer>> peers;


    using SendQueueElement = std::
        pair<std::shared_ptr<Peer>, std::shared_ptr<std::vector<std::uint8_t>>>;
    ThreadSafeQueue<SendQueueElement> send_queue;
};
} // namespace torrent

#endif
