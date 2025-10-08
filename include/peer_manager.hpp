#ifndef PEER_MANAGER_HPP
#define PEER_MANAGER_HPP

#include <boost/lockfree/queue.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>

#include "config.hpp"
#include "extensions.hpp"
#include "peer.hpp"
#include "pieces.hpp"

namespace torrent {

class PeerManager {
  public:
    PeerManager(
        asio::io_context& io_context_ref,
        const Config& config_ref,
        std::shared_ptr<Pieces> pieces_ptr,
        std::shared_ptr<Metadata> metadata_ptr
    ) :
        pieces(std::move(pieces_ptr)),
        metadata(std::move(metadata_ptr)),
        config(config_ref),
        io_context(io_context_ref),
        acceptor(io_context, tcp::endpoint(tcp::v4(), config_ref.get_port())),
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
    calculate_handshake(std::string_view info_hash, std::string_view peer_id, Extensions extensions);

    /*
     * Other peers might try to connect with us. Accept them through this function.
     * */
    void accept_new_peers();

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

    /*
     * Deletes all peers and drops connections.
     * */
    void stop() {
        std::scoped_lock<std::mutex> lock {mutex};
        peers.clear();
    }

  public:
    std::shared_ptr<Pieces> pieces;
    std::shared_ptr<Metadata> metadata;
    const Config& config;

  private:
    asio::io_context& io_context;
    tcp::acceptor acceptor;
    tcp::socket new_peer_socket;

    std::mutex mutex;

    static constexpr std::size_t HANDSHAKE_SIZE = 68;
    std::array<std::uint8_t, HANDSHAKE_SIZE> handshake;

    int active_peers = 0;

    std::unordered_map<tcp::endpoint, std::shared_ptr<Peer>> peers;
};
} // namespace torrent

#endif
