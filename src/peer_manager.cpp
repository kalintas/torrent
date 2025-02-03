#include "peer_manager.hpp"

#include <boost/log/trivial.hpp>
#include <cstring>
#include <stdexcept>

namespace torrent {

void PeerManager::calculate_handshake(
    std::string_view info_hash,
    std::string_view peer_id
) {
    if (info_hash.size() != 20 || peer_id.size() != 20) {
        throw new std::runtime_error(
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
    auto peer = std::make_shared<Peer>(*this, io_context, endpoint);
    peer->connect();
    peers.insert({std::move(endpoint), std::move(peer)});
}

void PeerManager::remove(const tcp::endpoint& endpoint) {
    const auto peer_it = peers.find(endpoint);

    BOOST_LOG_TRIVIAL(info)
        << "Peer count: " << peers.size()
        << ", Connection lost with peer: " << *peer_it->second;

    peers.erase(peer_it);
}
} // namespace torrent
