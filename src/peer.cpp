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
#include <mutex>

#include "message.hpp"
#include "peer_manager.hpp"

namespace torrent {

void Peer::connect() {
    // Capturing a copy of the shared pointer into the lambda will
    //      effectively make the object alive until the lambda gets dropped.
    socket.async_connect(
        endpoint,
        [self = shared_from_this()](const auto& error) {
            if (error) {
                self->change_state(State::Disconnected);
            } else {
                self->change_state(State::Connected);
            }
        }
    );
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
            peer_manager.pieces->bitfield->piece_failed(current_piece_index);
            peer_manager.remove(endpoint); // Remove this peer.
            break;
        case State::Handshook:
            handshook = true;
            peer_manager.on_handshake(*this);
            // Bitfield should be immiediately after the handshake.

            send_message(
                peer_manager.pieces->bitfield->as_message(),
                [](auto& peer) {
                    peer->send_message(Message {Message::Id::Unchoke});
                    peer->send_message(Message {Message::Id::Interested});
                }
            );

            // Start listening messages from the peer.
            listen_peer();
            break;
        case State::Idle:
            // Peer is in idle state.
            // Assign a piece to download.
            if (current_piece_index.has_value()) {
                // This should never happen but check anyway.
                // State changed to Idle but we already hold a piece_index
                peer_manager.pieces->bitfield->piece_failed(current_piece_index
                );
            }

            if (peer_bitfield == nullptr) {
                // Peers may not bitfield if they have any piece.
                // So create an empty bitfield if they didn't already sent one.
                peer_bitfield = std::make_unique<Bitfield>(
                    peer_manager.pieces->bitfield->get_bit_count()
                );
            }
            assign_piece();

            break;
        case State::DownloadingPiece:
            send_requests();
            break;
    }
}

void Peer::assign_piece() {
    current_piece_index =
        peer_manager.pieces->bitfield->assign_piece(*peer_bitfield);

    if (current_piece_index.has_value()) {
        BOOST_LOG_TRIVIAL(info) << "Assigned " << current_piece_index.value()
                                << "th piece to " << *this;
        assert(peer_bitfield->has_piece(current_piece_index.value()));
        current_block = 0; // Set current block to 0.
        change_state(State::DownloadingPiece);
    } else {
        // TODO: Terrible implementation. Should be a
        //      consumer/producer relation with the Bitfield using a condition variable.
        // Could not assign a piece to this peer.
        // Wait some time before trying again.
        BOOST_LOG_TRIVIAL(error) << "No valid piece for " << *this;
        timer.expires_after(asio::chrono::seconds(10)); // Wait 10 seconds
        timer.async_wait([self = shared_from_this()](auto error) {
            if (error) {
                BOOST_LOG_TRIVIAL(error)
                    << "Error in async_wait: " << error.message();
                return;
            }
            if (self->state == State::Idle
                && !self->current_piece_index.has_value()) {
                // Try to assign a piece again.
                self->assign_piece();
            }
        });
    }
}

void Peer::listen_peer() {
    // First listen the length of the packet, which is 4 bytes exact.
    buffer.resize(4);
    socket.async_receive(
        asio::buffer(buffer),
        [self = shared_from_this()](const auto& error, const auto bytes_read) {
            if (error || bytes_read != self->buffer.size()) {
                self->change_state(State::Disconnected);
                return;
            }
            std::int32_t length;
            std::memcpy(
                static_cast<void*>(&length),
                static_cast<void*>(self->buffer.data()),
                4
            );
            length = boost::endian::big_to_native(length);
            if (length > MAX_MESSAGE_LENGTH) {
                self->change_state(State::Disconnected);
                return;
            }
            if (length == 0) {
                // Probably a keep alive message. Ignore it.
                self->listen_peer();
            } else {
                self->buffer.resize(length);
                // Then listen the actual message.
                self->read_message_bytes = 0;
                self->listen_message();
            }
        }
    );
}

void Peer::listen_message() {
    socket.async_receive(
        asio::buffer(
            buffer.data() + read_message_bytes,
            buffer.size() - read_message_bytes
        ),
        [self = shared_from_this()](const auto& error, const auto bytes_read) {
            if (error) {
                self->change_state(State::Disconnected);
                return;
            }
            self->read_message_bytes += bytes_read;
            if (self->read_message_bytes < self->buffer.size()) {
                self->listen_message(); // Listen the rest of the message.
            } else {
                // Message is complete.
                self->on_message(Message {self->buffer});
                self->listen_peer();
            }
        }
    );
}

void Peer::listen_handshake() {
    buffer.resize(peer_manager.get_handshake().size());
    socket.async_receive(
        asio::buffer(buffer),
        [self = shared_from_this()](const auto& error, const auto bytes_read) {
            if (error || bytes_read != self->buffer.size()) {
                self->change_state(State::Disconnected);
                return;
            }
            // Compare them and disconnect if there is an error.
            const auto& our_handshake = self->peer_manager.get_handshake();
            bool is_header_equal = std::equal(
                self->buffer.begin(),
                self->buffer.begin() + 20,
                our_handshake.begin(),
                our_handshake.begin() + 20
            );

            bool is_info_hash_equal = std::equal(
                self->buffer.begin() + 28,
                self->buffer.begin() + 48,
                our_handshake.begin() + 28,
                our_handshake.begin() + 48
            );

            if (!is_header_equal || !is_info_hash_equal) {
                self->change_state(State::Disconnected);
                return;
            }

            std::string peer_str;
            peer_str.resize(20);
            std::memcpy(
                peer_str.data(),
                self->buffer.data() + 48,
                peer_str.size()
            );
            self->remote_peer_id = {std::move(peer_str)};
            self->change_state(State::Handshook);
        }
    );
}

void Peer::start_handshake() {
    // First send the handshake.
    socket.async_send(
        asio::buffer(peer_manager.get_handshake()),
        [self = shared_from_this()](const auto& error, const auto bytes_send) {
            if (error) {
                self->change_state(State::Disconnected);
                return;
            }
            BOOST_LOG_TRIVIAL(info) << "Sent handshake to " << *self;
            // After sending it start to listen the peer for a handshake.
            self->listen_handshake();
        }
    );
}

void Peer::on_message(Message message) {
#ifndef NDEBUG
    BOOST_LOG_TRIVIAL(debug) << *this << " sent: " << message;
#endif
    auto& payload = message.get_payload();

    switch (message.get_id()) {
        case Message::Id::Unchoke: // choke: <len=0001><id=0>
            peer_choking = false;
            if (state == State::Handshook) {
                change_state(State::Idle);
            }
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
        {
            // Peer is requesting a piece.
            // First check if we have that piece or not.
            const auto index = message.get_int(0);
            const auto begin = message.get_int(1);
            const auto length = message.get_int(2);

            if (length > MAX_MESSAGE_LENGTH) {
                // Close connection when requested a block bigger than 128KB.
                change_state(State::Disconnected);
                break;
            }
            peer_manager.pieces->read_block_async(
                index,
                begin,
                length,
                [self = shared_from_this()](Message message) {
                    self->send_message(std::move(message));
                }
            );
            break;
        }
        case Message::Id::Piece: // <len=0009+X><id=7><index><begin><block>
        {
            if (payload.size() < 8 || !current_piece_index.has_value()) {
                // Invalid payload. Ignore the message.
                break;
            }
            const auto index = message.get_int(0);
            const auto begin = message.get_int(1);
            peer_manager.pieces->write_block_async(
                index,
                begin,
                std::move(payload),
                [self = shared_from_this(
                 )](const auto& error_code, bool finished) {
                    std::scoped_lock<std::mutex> lock {self->mutex};
                    self->piece_received += 1;
                    if (error_code) {
                        self->current_block -= REQUEST_COUNT_PER_CALL;
                    } else if (finished) {
                        // Finished downloading the piece.
                        auto& pieces = self->peer_manager.pieces;
                        BOOST_LOG_TRIVIAL(info)
                            << "["
                            << pieces->bitfield->get_completed_piece_count()
                            << "/" << pieces->get_piece_count()
                            << "]. Finished piece#"
                            << self->current_piece_index.value() << ".";
                        self->peer_manager.pieces->bitfield->piece_success(
                            self->current_piece_index
                        );
                        self->change_state(State::Idle);
                    } else if (self->current_block
                               < self->peer_manager.pieces->get_block_count()) {
                        if (self->piece_received == REQUEST_COUNT_PER_CALL) {
                            self->send_requests(); // Request pieces again.
                        }
                        return;
                    }
                }
            );
            break;
        }
        case Message::Id::Cancel:
            break;
        case Message::Id::InvalidMessage:
            break;
    }
}

void Peer::send_requests() {
    if (!current_piece_index.has_value()) {
        // This should never happen but check anyway.
        change_state(State::Idle);
    }
    // Request the piece block by block.
    const auto piece_length = peer_manager.pieces->get_piece_length();
    const auto block_count = peer_manager.pieces->get_block_count();
    const auto piece_index =
        static_cast<std::uint32_t>(current_piece_index.value());

    const auto end_block =
        std::min(block_count, current_block + REQUEST_COUNT_PER_CALL);
    piece_received = 0;
    for (; current_block < end_block; ++current_block) {
        auto message = Message {
            Message::Id::Request,
            std::vector<std::uint8_t>(3 * sizeof(int))
        };
        message.write_int(0, piece_index);
        const std::uint32_t begin = current_block * Pieces::BLOCK_LENGTH;
        message.write_int(1, begin);
        std::uint32_t length = Pieces::BLOCK_LENGTH;
        if (current_block == block_count - 1) {
            // Last block, request everything left.
            length = piece_length - begin;
        }
        message.write_int(2, length);
        send_message(std::move(message));
    }
}

} // namespace torrent
