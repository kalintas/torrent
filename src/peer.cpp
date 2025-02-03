#include "peer.hpp"

#include <algorithm>
#include <boost/log/trivial.hpp>
#include <cstring>

#include "peer_manager.hpp"

namespace torrent {

void Peer::connect() {
    // Capturing a copy of the shared pointer into the lambda will
    //      effectively make the object alive until the lambda gets dropped.
    auto ptr = shared_from_this();
    socket.async_connect(endpoint, [this, ptr](const auto& error) {
        if (error) {
            change_status(Status::Disconnected);
        } else {
            change_status(Status::Connected);
        }
    });
}

void Peer::change_status(Status new_status) {
    status = new_status;
    switch (status) {
        case Status::Connected:
            BOOST_LOG_TRIVIAL(info)
                << "Peer count: " << peer_manager.peer_count()
                << ", Connected to peer: " << *this;
            start_handshake();
            break;
        case Status::Disconnected:
            peer_manager.remove(endpoint); // Remove this peer.
            break;
        case Status::Handshook:
            break;
    }
}

void Peer::listen_peer() {
    auto ptr = shared_from_this();
    socket.async_receive(
        asio::buffer(buffer),
        [this, ptr](const auto& error, const auto bytes_read) {
            if (error) {
                if (!socket.is_open()) {
                    change_status(Status::Disconnected);
                }
            } else {
                if (status == Status::Connected) {
                    // Still in the handshake phase.
                    // Compare them and disconnect if there is an error.
                    const auto& our_handshake = peer_manager.get_handshake();
                    bool is_header_equal = std::equal(
                        buffer.begin(),
                        buffer.begin() + 20,
                        our_handshake.begin(),
                        our_handshake.begin() + 20
                    );

                    bool is_info_hash_equal = std::equal(
                        buffer.begin() + 28,
                        buffer.begin() + 48,
                        our_handshake.begin() + 28,
                        our_handshake.begin() + 48
                    );

                    if (!is_header_equal || !is_info_hash_equal) {
                        change_status(Status::Disconnected);
                        return;
                    }

                    remote_peer_id.resize(20);
                    std::memcpy(
                        remote_peer_id.data(),
                        buffer.data() + 48,
                        remote_peer_id.size()
                    );
                    change_status(Status::Handshook);
                }

                BOOST_LOG_TRIVIAL(info)
                    << "Read " << bytes_read << " bytes from peer: " << *this;
                listen_peer();
            }
        }
    );
}

void Peer::start_handshake() {
    auto ptr = shared_from_this();
    socket.async_send(
        asio::buffer(peer_manager.get_handshake()),
        [this, ptr](const auto& error, const auto bytes_send) {
            if (error && !socket.is_open()) {
                change_status(Status::Disconnected);
                return;
            }
            BOOST_LOG_TRIVIAL(info) << "Sent handshake to peer: " << *this;
            listen_peer();
        }
    );
}

} // namespace torrent
