#include "peer_manager.hpp"

#include <boost/log/trivial.hpp>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>

#include "message.hpp"

namespace torrent {

void PeerManager::calculate_handshake(
    std::string_view info_hash,
    std::string_view peer_id
) {
    if (info_hash.size() != 20 || peer_id.size() != 20) {
        throw std::runtime_error(
            "Error while calculating the peer handshake. Illegal arguments"
        );
    }

    static constexpr std::string_view protocol_identifier =
        "BitTorrent protocol";
    handshake[0] = 19;
    std::memcpy(
        handshake.data() + 1,
        protocol_identifier.data(),
        protocol_identifier.size()
    );
    std::memset(handshake.data() + 20, 0, 8); // Reserved bytes. Set all to 0
    std::memcpy(handshake.data() + 28, info_hash.data(), info_hash.size());
    std::memcpy(handshake.data() + 48, peer_id.data(), peer_id.size());
}

void PeerManager::add(tcp::endpoint endpoint) {
    std::scoped_lock<std::mutex> lock {mutex};
    auto peer = std::make_shared<Peer>(*this, io_context, endpoint);
    peer->connect();
    peers.insert({std::move(endpoint), std::move(peer)});
}

void PeerManager::remove(const tcp::endpoint& endpoint) {
    std::scoped_lock<std::mutex> lock {mutex};
    const auto peer_it = peers.find(endpoint);
    if (peer_it == peers.end()) {
        return;
    }
    if (peer_it->second->get_handshook()) {
        active_peers -= 1;
    }

    BOOST_LOG_TRIVIAL(info) << "Active peers: " << active_peers
                            << ", Connection lost with " << *peer_it->second;

    peers.erase(peer_it);
}

void PeerManager::on_handshake(Peer& peer) {
    std::scoped_lock<std::mutex> lock {mutex};
    auto temp = std::move(peer.remote_peer_id);
    auto str = peer.to_string();
    peer.remote_peer_id = std::move(temp);

    active_peers += 1;

    BOOST_LOG_TRIVIAL(info)
        << "Active peers: " << active_peers << ", Handshake complete: " << str
        << " -> " << peer;
}

void PeerManager::accept_new_peers() {
    acceptor.async_accept(new_peer_socket, [this](auto error_code) {
        if (!error_code) {
            auto peer = std::make_shared<Peer>(
                *this,
                io_context,
                std::move(new_peer_socket)
            );

            peers.insert({peer->get_endpoint(), std::move(peer)});

            new_peer_socket = tcp::socket {io_context};
        }
        accept_new_peers();
    });
}

} // namespace torrent
