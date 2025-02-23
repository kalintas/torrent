#include "client.hpp"

#include <openssl/sha.h>

#include <boost/log/trivial.hpp>
#include <boost/url/scheme.hpp>
#include <cstdint>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>

#include "bencode_parser.hpp"

namespace torrent {

Client::Client(
    asio::io_context& io_context,
    asio::ssl::context& ssl_context,
    const std::string_view path,
    std::uint16_t port
) :
    io_context(io_context),
    ssl_context(ssl_context),
    pieces(std::make_shared<Pieces>(io_context)),
    peer_manager(io_context, port, pieces),
    bencode_parser(path),
    port(port) {
    // Generate 20 random characters for the peer id.
    static constexpr std::string_view alphanum =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    std::default_random_engine random_engine(std::random_device {}());
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
        BOOST_LOG_TRIVIAL(info) << "Parsed the .torrent file";

        auto dictionary =
            std::get<BencodeParser::Dictionary>(bencode_parser.get().value);

        auto info_element = dictionary["info"];
        auto info = info_element.get<BencodeParser::Dictionary>();
        info_hash = std::string(20, '\0');
        auto info_bencode = info_element.to_bencode();
        // Calculate the SHA1 from the info directory.
        SHA1(
            reinterpret_cast<const unsigned char*>(info_bencode.data()),
            info_bencode.size(),
            reinterpret_cast<unsigned char*>(info_hash.data())
        );

        // BitTorrent also supports donwloading multiple files under a folder.
        // But we will always download the torrent to a single file.
        // After that we can parse the file to folders.
        std::string file_name = info["name"].get<std::string>();
        std::size_t piece_length = static_cast<std::size_t>(
            info["piece length"].get<BencodeParser::Integer>()
        );
        std::size_t file_length = 0;

        if (info.find("files") != info.end()) {
            // Multiple file mode.
            // TODO: Currently we only download the file into a big chunk
            // Parse the file to a folder after downloading.
            for (auto& element : info["files"].get<BencodeParser::List>()) {
                auto file = element.get<BencodeParser::Dictionary>();
                file_length += static_cast<std::size_t>(
                    file["length"].get<BencodeParser::Integer>()
                );
            }
        } else {
            // Single file mode.
            file_length = static_cast<std::size_t>(
                info["length"].get<BencodeParser::Integer>()
            );
        }

        BOOST_LOG_TRIVIAL(info) << "File length: " << file_length
                                << ", piece_length: " << piece_length << ".";
        // Initialize the pieces with the info.
        pieces->init(
            std::move(file_name),
            file_length,
            piece_length,
            info["pieces"].get<std::string>()
        );
        // Calculate the peer handshake.
        peer_manager.calculate_handshake(info_hash, peer_id);

        // Get the announces.
        std::vector<std::string> announces;
        BencodeParser::Dictionary::iterator announce_element;
        if ((announce_element = dictionary.find("announce"))
            != dictionary.end()) {
            announces.emplace_back(announce_element->second.get<std::string>());
        } else if ((announce_element = dictionary.find("announce-list"))
                   != dictionary.end()) {
            // announce-list: (optional) this is an extention to the official specification,
            //      offering backwards-compatibility. (list of lists of strings).
            for (auto& list :
                 announce_element->second.get<BencodeParser::List>()) {
                for (auto& element : list.get<BencodeParser::List>()) {
                    announces.emplace_back(element.get<std::string>());
                }
            }
        } else if ((announce_element = dictionary.find("url-list"))
                   != dictionary.end()) {
            // More about url-list: https://www.bittorrent.org/beps/bep_0019.html
            // TODO
            throw std::runtime_error("url-list unimplemented");
        } else {
            throw std::runtime_error(
                "Could not start the client, invalid .torrent file"
            );
        }
        // Populate trackers from the announces we got from the torrent file.
        for (auto& announce : announces) {
            auto tracker = Tracker::create_tracker(*this, announce);
            if (tracker != nullptr) {
                trackers.emplace(std::move(announce), std::move(tracker));
            }
        }
    } catch (std::runtime_error e) {
        BOOST_LOG_TRIVIAL(error) << "Fatal client error: " << e.what();
    }
}

void Client::wait() {
    pieces->wait();
}

void Client::stop() {
    pieces->stop();
}

} // namespace torrent
