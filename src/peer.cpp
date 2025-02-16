#include "peer.hpp"

#include <algorithm>
#include <boost/endian/conversion.hpp>
#include <boost/log/trivial.hpp>
#include <boost/range/iterator_range_core.hpp>
#include <boost/range/join.hpp>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>

#include "message.hpp"
#include "peer_manager.hpp"

namespace torrent {

void Peer::connect() {
    // Capturing a copy of the shared pointer into the lambda will
    //      effectively make the object alive until the lambda gets dropped.
    auto ptr = shared_from_this();
    socket.async_connect(endpoint, [this, ptr](const auto& error) {
        if (error) {
            change_state(State::Disconnected);
        } else {
            change_state(State::Connected);
        }
    });
}

void Peer::change_state(State new_state) {
    state = new_state;
    switch (state) {
        case State::Connected:
            BOOST_LOG_TRIVIAL(info)
                << "Active peers: " << peer_manager.get_active_peers()
                << ", Connected to " << *this;
            start_handshake();
            break;
        case State::Disconnected:
            peer_manager.remove(endpoint); // Remove this peer.
            break;
        case State::Handshook:
            peer_manager.on_handshake(*this);
            // Bitfield should be immiediately after the handshake.
            peer_manager.send_message(
                shared_from_this(),
                peer_manager.pieces->bitfield->as_message()
            );
            // Then we send an unchoke message.
            peer_manager.send_message(
                shared_from_this(),
                Message {Message::Id::Unchoke}
            );
            break;
        case State::Idle:
            // Peer is in idle state.
            // Assign a piece to download.
            if (current_piece_index.has_value()) {
                // This should never happen but check anyway.
                // State changed to Idle but we already hold a piece_index
                peer_manager.pieces->bitfield->piece_failed(current_piece_index);
            }

            current_piece_index = peer_manager.pieces->
                bitfield->assign_piece(*peer_bitfield);

            if (current_piece_index.has_value()) {
                BOOST_LOG_TRIVIAL(info) << "Assigned " << current_piece_index.value() << "th piece to " << *this;
                change_state(State::DownloadingPiece);
            } else {
                // Could not assign a piece to this peer.
                BOOST_LOG_TRIVIAL(error) << "No valid piece for " << *this;
            }
            
            break;
        case State::DownloadingPiece:
        {
            if (!current_piece_index.has_value()) {
                // This should never happen but check anyway.
                change_state(State::Idle);
            }

            // Request the piece block by block.
            const auto piece_length = peer_manager.pieces->get_piece_length();
            constexpr std::size_t BLOCK_LENGTH = 1ul << 14;
            const auto block_count = piece_length / BLOCK_LENGTH;
            const int piece_index = static_cast<int>(current_piece_index.value()); 
            std::size_t begin = 0;

            for (std::size_t i = 0; i < block_count; ++i) {
                auto message = Message{ Message::Id::Request, std::vector<std::uint8_t>(3 * sizeof(int)) };
                message.write_int(0, piece_index); 
                message.write_int(1, begin); 
                std::size_t length = BLOCK_LENGTH;
                if (i == block_count - 1) {
                    // Last block, request everything left.
                    length = piece_length - begin;      
                }
                message.write_int(2, length); 

                peer_manager.send_message(shared_from_this(), std::move(message));
                begin += BLOCK_LENGTH;
            }
            break;
        }
    }
}

void Peer::listen_peer() {
    auto ptr = shared_from_this();
    socket.async_receive(
        asio::buffer(buffer),
        [this, ptr](const auto& error, const auto bytes_read) mutable {
            if (error) {
                if (!socket.is_open()) {
                    change_state(State::Disconnected);
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

            if (state == State::Connected) {
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
                    change_state(State::Disconnected);
                    return;
                }

                std::string peer_str;
                peer_str.resize(20);
                std::memcpy(
                    peer_str.data(),
                    buffer.data() + 48,
                    peer_str.size()
                );
                remote_peer_id = {std::move(peer_str)};
                change_state(State::Handshook);
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
                on_message(std::move(message));
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
                change_state(State::Disconnected);
                return;
            }
            BOOST_LOG_TRIVIAL(info) << "Sent handshake to " << *this;
            listen_peer();
        }
    );
}

void Peer::on_message(Message message) {
    auto& payload = message.get_payload();

    switch (message.get_id()) {
        case Message::Id::Unchoke: // choke: <len=0001><id=0>
            peer_choking = false;
            change_state(State::Idle); 
            break;
        case Message::Id::Choke: // unchoke: <len=0001><id=1>
            peer_choking = true;
            break;
        case Message::Id::Interested: // interested: <len=0001><id=2>
            peer_interested = true;
            break;
        case Message::Id::NotInterested: // not interested: <len=0001><id=3>
            peer_interested = false;
            break;
        case Message::Id::Have: { // have: <len=0005><id=4><piece index>
            if (payload.size() < 4) {
                // Invalid payload. Ignore the message.
                break;
            }
            const auto index = message.get_int(0); // Get first int.
            peer_bitfield->set_piece(index);
            break;
        }
        case Message::Id::Bitfield: // bitfield: <len=0001+X><id=5><bitfield>
            if (payload.size() < peer_manager.pieces->bitfield->size()) {
                // Invalid payload. Ignore the message.
                break;
            }
            peer_bitfield = std::make_unique<Bitfield>(payload);
            break;
        case Message::Id::Request: // <len=0013><id=6><index><begin><length>
            // Peer is requesting a piece.
            // First check if we have that piece or not.
            break;
        case Message::Id::Piece: // <len=0009+X><id=7><index><begin><block>
        {
            if (payload.size() < 8 || !current_piece_index.has_value()) {
                // Invalid payload. Ignore the message.
                break;
            } 
            const auto index = message.get_int(0);
            const auto begin = message.get_int(1);
            peer_manager.pieces->write_block_async(index, begin, std::move(payload), 
            [this] (const auto& error_code, bool finished) {
                if (!error_code && finished) {
                    // Finished downloading the piece.
                    peer_manager.pieces->bitfield->piece_success(current_piece_index); 
                    change_state(State::Idle);
                } 
            });
            break;
        }    
        case Message::Id::Cancel:
            break;
        case Message::Id::InvalidMessage:
            break;
    }
}

} // namespace torrent
