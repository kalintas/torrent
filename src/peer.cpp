#include "peer.hpp"

#include <algorithm>
#include <boost/log/trivial.hpp>
#include <boost/range/iterator_range_core.hpp>
#include <boost/range/join.hpp>
#include <cstdint>
#include <cstring>

#include "message.hpp"
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
                << "Active peers: " << peer_manager.get_active_peers()
                << ", Connected to " << *this;
            start_handshake();
            break;
        case Status::Disconnected:
            peer_manager.remove(endpoint); // Remove this peer.
            break;
        case Status::Handshook:
            peer_manager.on_handshake(*this);
            break;
    }
}

void Peer::listen_peer() {
    auto ptr = shared_from_this();
    socket.async_receive(
        asio::buffer(buffer),
        [this, ptr](
            const auto& error,
            const auto bytes_read
        ) mutable {
            if (error) {
                if (!socket.is_open()) {
                    change_status(Status::Disconnected);
                }
                return;
            }
            BOOST_LOG_TRIVIAL(info)
                << "Read " << bytes_read << " bytes from " << *this;

            if (bytes_read <= 4) {
                // Possibly a keep alive message.
                listen_peer();
                return;
            }

            auto packet = boost::join(
                remainder_buffer,
                boost::make_iterator_range(
                    buffer.begin(),
                    buffer.begin() + bytes_read
                )
            );
            auto it = packet.begin();

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
                it += our_handshake.size();
            }

            while (5 <= std::distance(it, packet.end())) {
                std::uint32_t length = (std::uint32_t)(*(it))
                    | ((std::uint32_t)(*(it + 1)) << 8)
                    | ((std::uint32_t)(*(it + 2)) << 16)
                    | ((std::uint32_t)(*(it + 3)) << 24);
                it += 4;
                length = boost::endian::big_to_native(length);
                if (length > std::distance(it, packet.end())) {
                    break;
                }
                Message message {
                    static_cast<Message::Id>(*it),
                    it + 1,
                    length - 1
                };
                it += length;
                BOOST_LOG_TRIVIAL(info) << *this << " sent: " << message;
            }
            // Add remains to this buffer.
            remainder_buffer.resize(std::distance(it, packet.end()));
            std::copy(it, packet.end(), remainder_buffer.begin());

            listen_peer();
        }
    );
}

void Peer::start_handshake() {
    auto ptr = shared_from_this();
    socket.async_send(
        asio::buffer(peer_manager.get_handshake()),
        [this, ptr](const auto& error, const auto bytes_send) {
            if (error) {
                change_status(Status::Disconnected);
                return;
            }
            BOOST_LOG_TRIVIAL(info) << "Sent handshake to " << *this;
            listen_peer();
        }
    );
}

} // namespace torrent
