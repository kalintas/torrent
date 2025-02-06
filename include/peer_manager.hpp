#ifndef PEER_MANAGER_HPP
#define PEER_MANAGER_HPP

#include <cstdint>
#include <string_view>

#include "peer.hpp"

namespace torrent {

class PeerManager {
  public:
    PeerManager(asio::io_context& io_context, std::uint16_t port) :
        io_context(io_context),
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

    void on_handshake(Peer& peer);

    std::size_t peer_count() const {
        return peers.size();
    }

    const auto& get_handshake() {
        return handshake;
    }

    int get_active_peers() const {
        return active_peers;
    }

  private:
    asio::io_context& io_context;
    tcp::acceptor acceptor;
    tcp::socket new_peer_socket;

    static constexpr std::size_t HANDSHAKE_SIZE = 68;

    std::array<std::uint8_t, HANDSHAKE_SIZE> handshake;

    int active_peers = 0;

    std::unordered_map<tcp::endpoint, std::shared_ptr<Peer>> peers;
};
} // namespace torrent

#endif
