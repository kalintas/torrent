#include "client.hpp"

#include <boost/log/trivial.hpp>
#include <cstdint>
#include <memory>
#include <sstream>

#include "tracker.hpp"
#include "utility.hpp"

namespace torrent {

Client::Client(
    asio::io_context& io_context,
    const std::string_view path,
    std::uint16_t port
) :
    pieces(std::make_shared<Pieces>(io_context)),
    peer_manager(io_context, port, pieces),
    bencode_parser(path),
    tracker(io_context),
    random_engine(std::random_device {}()),
    port(port) {
    // Generate 20 random characters for the peer id.
    static constexpr std::string_view alphanum =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    std::uniform_int_distribution<> dist(0, alphanum.size());
    peer_id.reserve(20);
    peer_id += "-KK1000-"; // Optional client identifier.
    while (peer_id.size() < 20) {
        peer_id.push_back(alphanum[dist(random_engine)]);
    }

    // Print out the peer id in a printable form.
    std::stringstream ss;
    ss << std::hex;
    for (const auto c : peer_id) {
        if (std::isprint(c)) {
            ss << c;
        } else {
            ss << "\\x" << (int)((std::uint8_t)c);
        }
    }
    BOOST_LOG_TRIVIAL(info) << "Peer id: " << ss.str();
}

void Client::start() {
    try {
        bencode_parser.parse();

        auto dictionary =
            std::get<BencodeParser::Dictionary>(bencode_parser.get().value);

        auto announce = dictionary["announce"].get<std::string>();

        BOOST_LOG_TRIVIAL(info)
            << "Parsed the torrent file. Announce: " << announce;
        auto info_element = dictionary["info"];
        auto info = info_element.get<BencodeParser::Dictionary>();
        auto info_hash = get_sha1(info_element.to_bencode());

        std::string file_name = info["name"].get<std::string>();
        int file_length = info["length"].get<int>();
        int piece_length = info["piece length"].get<int>();
        // Initialize the pieces with the info.
        pieces->init(
            std::move(file_name),
            file_length,
            piece_length,
            info["pieces"].get<std::string>()
        );
        // Calculate the peer handshake.
        peer_manager.calculate_handshake(info_hash, peer_id);

        auto url = boost::url(announce);
        auto params = url.encoded_params();

        params.append({"info_hash", info_hash});
        params.append({"peer_id", peer_id});
        params.append({"port", std::to_string(port)});
        params.append({"uploaded", "0"});
        params.append({"downloaded", "0"});
        params.append({"compact", "1"});
        params.append({"left", "1"});

        BOOST_LOG_TRIVIAL(info) << "Tracker url: " << url;

        tracker.initiate_connection(
            std::move(url),
            [this](tcp::endpoint endpoint) {
                peer_manager.add(std::move(endpoint));
            }
        );
    } catch (std::runtime_error e) {
        std::cerr << e.what();
    }
}

void Client::wait() {
    pieces->wait();
}

void Client::stop() {
    pieces->stop();
}

} // namespace torrent
