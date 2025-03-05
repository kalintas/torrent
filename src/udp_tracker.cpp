#include "udp_tracker.hpp"

#include <boost/asio/detail/chrono.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/trivial.hpp>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <random>

#include "tracker_manager.hpp"

namespace torrent {
/*
 * A basic abstraction over UDP tracker packets.
 * Object is either a request or a response.
 * */
class UdpTracker::Packet {
  private:
    Packet(Action packet_action) : action(packet_action) {
        generate_transaction_id();
    }

    Packet() {}

  public:
    Packet(Packet&& packet) :
        action(packet.action),
        transaction_id(packet.transaction_id),
        bytes(std::move(packet.bytes)) {}

    static constexpr std::uint64_t PROTOCOL_ID = 0x41727101980;

    static Packet create_connect_request() {
        Packet packet {Action::Connect};
        packet.bytes.resize(16);
        packet.write<std::uint64_t>(0, PROTOCOL_ID);
        packet.write<Action>(8, packet.action);
        packet.write<std::uint32_t>(12, packet.transaction_id);

        return packet;
    }

    static Packet create_announce_request(
        const TrackerManager& tracker_manager,
        std::uint64_t connection_id
    ) {
        Packet packet {Action::Announce};
        packet.bytes.resize(98);
        packet.write<std::uint64_t>(0, connection_id);
        packet.write<Action>(8, packet.action);
        packet.write<std::uint32_t>(12, packet.transaction_id);
        std::memcpy(
            static_cast<void*>(packet.bytes.data() + 16),
            static_cast<const void*>(
                tracker_manager.metadata->get_info_hash().data()
            ),
            20
        );
        std::memcpy(
            static_cast<void*>(packet.bytes.data() + 36),
            static_cast<const void*>(tracker_manager.get_peer_id().data()),
            20
        );
        packet.write<std::uint64_t>(
            56,
            tracker_manager.metadata->get_downloaded()
        ); // downloaded
        packet.write<std::uint64_t>(
            64,
            tracker_manager.metadata->get_left()
        ); // left
        packet.write<std::uint64_t>(
            72,
            tracker_manager.metadata->get_uploaded()
        ); // uploaded
        packet.write<std::uint32_t>(
            80,
            0
        ); // event, 0: none; 1: completed; 2: started; 3: stopped
        packet.write<std::uint32_t>(84, 0); // Ip address, default 0
        packet.write<std::uint32_t>(88, 0); // key
        packet.write<std::uint32_t>(
            92,
            static_cast<std::uint32_t>(-1)
        ); // num_want, default -1
        packet.write<std::uint16_t>(96, tracker_manager.get_port());
        return packet;
    }

    static std::optional<Packet>
    create_response(const Packet& request, const auto it, std::size_t size) {
        if (size < 4) {
            return {};
        }
        Packet packet;
        packet.bytes.resize(size);
        std::copy(it, it + size, packet.bytes.begin());
        packet.action = packet.read<Action>(0);
        packet.transaction_id = packet.read<std::uint32_t>(4);
        std::size_t min_length;
        switch (packet.action) {
            case Action::Connect:
                min_length = 16;
                break;
            case Action::Announce:
                min_length = 20;
                break;
            case Action::Scrape:
                min_length = 8;
                break;
            case Action::Error:
                min_length = 4;
                break;
            default:
                return {};
        }
        if (size < min_length) {
            return {};
        }
        if (packet.transaction_id != request.transaction_id) {
            return {};
        }
        return {std::move(packet)};
    }

    /*
     * Read an integer from the packet. 
     * Converts the integer from big endian to native before returning.
     * @param offset The offset from the start of the packet.
     * */
    template<typename IntegerType>
    IntegerType read(std::size_t offset) const {
        IntegerType value;
        std::memcpy(
            static_cast<void*>(&value),
            static_cast<const void*>(bytes.data() + offset),
            sizeof(IntegerType)
        );
        return boost::endian::big_to_native(value);
    }

    /*
     * Writes an integer from into the packet. 
     * Converts the integer from big endian to native before writing.
     * @param offset The offset from the start of the packet.
     * */
    template<typename IntegerType>
    void write(std::size_t offset, IntegerType value) {
        value = boost::endian::native_to_big(value);
        std::memcpy(
            static_cast<void*>(bytes.data() + offset),
            static_cast<const void*>(&value),
            sizeof(IntegerType)
        );
    }

    /*
     * Returns the error message if the packet is an Error packet.
     * */
    std::string get_error_message() {
        if (action != Action::Error) {
            return {};
        }
        std::string message(bytes.size() - 4, '\0');
        std::copy(bytes.begin() + 4, bytes.end(), message.begin());
        return message;
    }

    const std::vector<std::uint8_t>& get_bytes() const {
        return bytes;
    }

    Action get_action() const {
        return action;
    }

    std::size_t length() const {
        return bytes.size();
    }

  private:
    friend std::ostream& operator<<(std::ostream& os, const Packet& packet) {
        os << "Packet{ action: ";
        switch (packet.action) {
            case Action::Connect:
                os << "Connect(0)";
                break;
            case Action::Announce:
                os << "Announce(1)";
                break;
            case Action::Scrape:
                os << "Scrape(2)";
                break;
            case Action::Error:
                os << "Error(3)";
                break;
        }
        os << ",transaction_id: " << packet.transaction_id << " }";
        return os;
    }

  private:
    void generate_transaction_id() {
        static std::mt19937 generator;
        static std::mutex mutex;
        std::scoped_lock<std::mutex> lock {mutex};

        std::uniform_int_distribution<std::uint32_t> distr(
            0,
            std::numeric_limits<std::uint32_t>::max()
        );
        transaction_id = distr(generator);
    }

  private:
    Action action;
    std::uint32_t transaction_id;
    std::vector<std::uint8_t> bytes;
};

void UdpTracker::change_state(State new_state) {
    state = new_state;
    switch (state) {
        case State::Disconnected:
            on_disconnect();
            break;
        case State::Connected:
            // Connected to the tracker.
            // Now ask for the connection id.
            send_request(
                Packet::create_connect_request(),
                [self = get_ptr()](Packet response) {
                    self->connection_id = response.read<std::uint64_t>(8);
                    self->change_state(State::HasConnectionId);

                    // From the BEP15:
                    // A tracker_manager can use a connection ID until one minute
                    // after it has received it. Trackers should accept
                    // the connection ID until two minutes after it has been send.
                    self->connection_id_timer.expires_after(
                        asio::chrono::minutes(1)
                    ); // Set the timer for 1 minute.
                    self->connection_id_timer.async_wait([self](auto error) {
                        if (error) {
                            BOOST_LOG_TRIVIAL(error)
                                << "Error in async_wait: " << error.message();
                            return;
                        }
                        // Change the state back to the State::Connected
                        // This way the tracker_manager will ask for a connection_id one more time.
                        self->change_state(State::Connected);
                    });
                }
            );
            break;
        case State::HasConnectionId: {
            if (interval_timer.expiry() > std::chrono::steady_clock::now()) {
                // Timer is not yet expired. So don't announce again.
                break;
            }
            // We acquired the connection_id, now its time to announce.
            send_request(
                Packet::create_announce_request(tracker_manager, connection_id),
                [self = get_ptr()](Packet response) {
                    auto interval = response.read<std::uint32_t>(8);
                    for (std::size_t offset = 20;
                         offset < response.length() - 6;
                         ++offset) {
                        auto ip = response.read<std::uint32_t>(offset);
                        auto port = response.read<std::uint16_t>(offset + 4);

                        self->on_new_peer({address_v4(ip), port});
                    }
                    BOOST_LOG_TRIVIAL(info)
                        << "Fetched " << (response.length() - 20) / 6
                        << " peers";

                    self->interval_timer.expires_after(
                        asio::chrono::seconds(interval)
                    );
                    self->interval_timer.async_wait([self](auto error) {
                        if (error) {
                            BOOST_LOG_TRIVIAL(error)
                                << "Error in async_wait: " << error.message();
                            return;
                        }
                        // Time to announce again.
                        // Check if we have the connection id.
                        if (self->state == State::HasConnectionId) {
                            self->change_state(State::HasConnectionId
                            ); // Announce again.
                        }
                    });
                }
            );
            break;
        }
    }
}

void UdpTracker::send_request(Packet request, auto on_response) {
    auto request_ptr = std::make_shared<Packet>(std::move(request));
    // TODO: Implement time outs
    socket.async_send(
        asio::buffer(request_ptr->get_bytes()),
        [self = get_ptr(),
         request_ptr,
         on_response](const auto& send_error, const std::size_t) {
            if (send_error) {
                BOOST_LOG_TRIVIAL(error)
                    << *self
                    << " could not send a message: " << send_error.message();
                return self->change_state(State::Disconnected);
            }
#ifndef NDEBUG
            BOOST_LOG_TRIVIAL(debug)
                << "Sent " << *request_ptr << " to " << *self;
#endif
            self->socket.async_receive(
                asio::buffer(self->receive_buffer),
                [self, request_ptr, on_response](
                    const auto& receive_error,
                    const std::size_t bytes_read
                ) {
                    if (receive_error) {
                        BOOST_LOG_TRIVIAL(error)
                            << *self << " could not receive a message: "
                            << receive_error.message();
                        return self->change_state(State::Disconnected);
                    }

                    auto packet = Packet::create_response(
                        *request_ptr,
                        self->receive_buffer.begin(),
                        bytes_read
                    );

                    if (packet.has_value()) {
                        if (packet->get_action() == Action::Error) {
                            BOOST_LOG_TRIVIAL(error)
                                << "Received an error message from the "
                                << *self << ": " << packet->get_error_message();
                        } else if (packet->get_action()
                                   == request_ptr->get_action()) {
                    // Packet is valid.
#ifndef NDEBUG
                            BOOST_LOG_TRIVIAL(debug)
                                << *self << " sent: " << packet.value();
#endif
                            on_response(std::move(packet.value()));
                        } else {
                            BOOST_LOG_TRIVIAL(error)
                                << "Received the incorrect message from the "
                                << *self;
                        }
                    } else {
                        BOOST_LOG_TRIVIAL(error)
                            << "An invalid response received from the "
                            << *self;
                    }
                }
            );
        }
    );
}

void UdpTracker::initiate_connection(boost::url url) {
    resolver.async_resolve(
        url.host(),
        url.port(),
        [self = get_ptr()](const auto& error, auto endpoints) {
            if (error) {
                BOOST_LOG_TRIVIAL(error)
                    << *self
                    << " could not resolve the given url: " << error.message();
                return self->change_state(State::Disconnected);
            }
            asio::async_connect(
                self->socket,
                endpoints,
                [self](const auto& connect_error, const auto&) {
                    if (connect_error) {
                        BOOST_LOG_TRIVIAL(error)
                            << "Could not connect to the " << *self << ": "
                            << connect_error.message();
                        return self->change_state(State::Disconnected);
                    }
                    self->change_state(State::Connected);
                }
            );
        }
    );
}

} // namespace torrent
