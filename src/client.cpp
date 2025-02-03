#include "client.hpp"
#include <boost/log/trivial.hpp>

#include "tracker.hpp"
#include "utility.hpp"

namespace torrent {

Client::Client(const char* path) :
    peer_manager(io_context),
    bencode_parser(path),
    tracker(io_context),
    random_engine(std::random_device {}()) {
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
    
    BOOST_LOG_TRIVIAL(info) << "Peer id: " << peer_id;
}

void Client::start() {
    try {
        bencode_parser.parse();

        auto dictionary =
            std::get<BencodeParser::Dictionary>(bencode_parser.get().value);

        auto announce = dictionary["announce"].get<std::string>();
        
        BOOST_LOG_TRIVIAL(info) << "Parsed the torrent file. Announce: " << announce;
        auto info_element = dictionary["info"];
        auto info = info_element.get<BencodeParser::Dictionary>();
        auto info_hash = get_sha1(info_element.to_bencode());
        
        // Calculate the peer handshake.
        peer_manager.calculate_handshake(info_hash, peer_id);

        auto url = boost::url(announce);
        auto params = url.encoded_params();

        params.append({"info_hash", info_hash});
        params.append({"peer_id", peer_id});
        params.append({"port", "0"});
        params.append({"uploaded", "0"});
        params.append({"downloaded", "0"});
        params.append({"compact", "1"});
        params.append({"left", "1"});

        BOOST_LOG_TRIVIAL(info) << "Tracker url: " << url;

        tracker.initiate_connection(
            std::move(url),
            [this](tcp::endpoint endpoint) {
                peer_manager.add(std::move(endpoint));
            });

        io_context.run();
    } catch (std::runtime_error e) {
        std::cerr << e.what();
    }
}

}  // namespace torrent
