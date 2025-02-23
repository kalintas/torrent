#ifndef TORRENT_UDP_TRACKER_HPP
#define TORRENT_UDP_TRACKER_HPP

#include <boost/endian/conversion.hpp>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>

#include "tracker.hpp"

namespace torrent {

namespace asio = boost::asio;
using namespace boost::asio::ip;

/*
 * A BitTorrent tracker abstraction that uses UDP protocol.
 * https://www.bittorrent.org/beps/bep_0015.html
 * */
class UdpTracker:
    public Tracker,
    public std::enable_shared_from_this<UdpTracker> {
  public:
    UdpTracker(Client& client, asio::io_context& io_context) :
        Tracker(client),
        resolver(io_context),
        random_engine(std::random_device {}()),
        state(State::Disconnected),
        connection_id_timer(io_context),
        interval_timer(io_context, std::chrono::steady_clock::now()),
        socket(io_context) {}

    ~UdpTracker() {}

    void initiate_connection(boost::url tracker_url) override;

  private:
    enum class State {
        Connected,
        Disconnected,
        HasConnectionId,
    };

    void change_state(State new_state);

  private:
    /*
     * An enum for the Action used in UDP packets.
     * This integer signifies the type of the request/response.
     * */
    enum class Action : std::uint32_t {
        Connect = 0,
        Announce = 1,
        Scrape = 2,
        Error = 3
    };

    class Packet;

  private:
    void send_request(Packet packet, auto on_response);

  private:
    State state;
    std::uint64_t connection_id = 0;

    asio::steady_timer connection_id_timer;
    asio::steady_timer interval_timer;

    udp::resolver resolver;
    udp::socket socket;

    static constexpr std::size_t RECEIVE_BUFFER_LENGTH = 1024;
    std::array<std::uint8_t, RECEIVE_BUFFER_LENGTH> receive_buffer;

    std::default_random_engine random_engine;
};

} // namespace torrent

#endif
