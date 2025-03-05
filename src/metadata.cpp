#include "metadata.hpp"

#include <openssl/sha.h> // For SHA1

#include <boost/log/trivial.hpp>
#include <boost/url/urls.hpp>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include "bencode_parser.hpp"

namespace torrent {

std::shared_ptr<Metadata>
Metadata::from_torrent_file(const std::string_view path, const Config& config) {
    auto metadata = std::make_shared<Metadata>(Private {}, config);
    BencodeParser bencode_parser {path};
    bencode_parser.parse();
    BOOST_LOG_TRIVIAL(info) << "Parsed the .torrent file: " << path;

    auto& dictionary =
        std::get<BencodeParser::Dictionary>(bencode_parser.get().value);

    auto& info = dictionary["info"];
    auto info_hash = get_info_hash(info);

    // Load the info directory.
    metadata->load_info(std::move(info), std::move(info_hash));

    BOOST_LOG_TRIVIAL(info)
        << "File length: " << metadata->total_length
        << ", piece_length: " << metadata->piece_length << ".";

    // Get the announces.
    BencodeParser::Dictionary::iterator announce_element;
    if ((announce_element = dictionary.find("announce")) != dictionary.end()) {
        metadata->trackers.emplace_back(
            std::move(announce_element->second.get<std::string>())
        );
    } else if ((announce_element = dictionary.find("announce-list"))
               != dictionary.end()) {
        // announce-list: (optional) this is an extention to the official specification,
        //      offering backwards-compatibility. (list of lists of strings).
        for (auto& list : announce_element->second.get<BencodeParser::List>()) {
            for (auto& element : list.get<BencodeParser::List>()) {
                metadata->trackers.emplace_back(
                    std::move(element.get<std::string>())
                );
            }
        }
    } else if ((announce_element = dictionary.find("url-list"))
               != dictionary.end()) {
        // More about url-list: https://www.bittorrent.org/beps/bep_0019.html
        // TODO
        throw std::runtime_error("Metadata: url-list unimplemented");
    } else {
        throw std::runtime_error(
            "Could not create the metadata, invalid .torrent file"
        );
    }

    return metadata;
}

void Metadata::load_info(
    BencodeParser::Element info_element,
    std::string info_hash_param
) {
    std::unique_lock<std::mutex> lock {mutex};

    info_hash = std::move(info_hash_param);

    auto& info = info_element.get<BencodeParser::Dictionary>();

    name = info["name"].get<std::string>();
    // BitTorrent also supports donwloading multiple files under a folder.
    // But we will always download the torrent to a single file.
    // After that we can parse the file to folders.
    file_name = std::move(info["name"].get<std::string>()) + ".tmp";
    piece_length = static_cast<std::size_t>(
        info["piece length"].get<BencodeParser::Integer>()
    );
    total_length = 0;
    pieces = std::move(info["pieces"].get<std::string>());

    if (info.find("files") != info.end()) {
        // Multiple file mode.
        for (auto& element :
             info["files"].get<BencodeParser::List>()) { // Iterate the files.
            auto& file = element.get<BencodeParser::Dictionary>();
            // Get the length of this file.
            std::size_t file_length = static_cast<std::size_t>(
                file["length"].get<BencodeParser::Integer>()
            );
            // Extract the file path from the list.
            auto& file_dict = element.get<BencodeParser::Dictionary>();
            std::string path;
            for (auto& path_element :
                 file_dict["path"].get<BencodeParser::List>()) {
                path += '/' + path_element.get<BencodeParser::String>();
            }
            // Add a new file.
            files.emplace_back(file_length, std::move(path));
            total_length += file_length;
        }
    } else {
        // Single file mode.
        auto file_length = info["length"].get<BencodeParser::Integer>();
        total_length = static_cast<std::size_t>(file_length);
        files.emplace_back(file_length, name);
    }

    left = total_length;

    ready = true;
    if (on_ready_callback.has_value()) {
        lock.unlock();
        on_ready_callback.value()();
    }
}

std::shared_ptr<Metadata> Metadata::from_magnet(const boost::url_view url, const Config& config) {
    if (url.scheme() != "magnet") {
        throw std::runtime_error(
            "Could not create the metadata, invalid url scheme"
        );
    }

    auto metadata = std::make_shared<Metadata>(Private {}, config);

    for (const auto& param : url.params()) {
        if (param.key == "xt") { // Exact Topic
            constexpr std::string_view URN_PREFIX = "urn:btih:";
            if (param.value.starts_with(URN_PREFIX)) {
                metadata->info_hash = param.value.substr(URN_PREFIX.size());
            }
        } else if (param.key == "dn") { // Display name
            metadata->name = param.value;
            metadata->file_name = metadata->name + ".tmp";
        } else if (param.key == "xl") { // eXact Length
            metadata->total_length = static_cast<std::size_t>(
                std::stoi(static_cast<std::string>(param.value))
            );
        } else if (param.key == "tr") { // address TRacker
            metadata->trackers.emplace_back(static_cast<std::string>(param.value
            ));
        } else if (param.key == "ws") { // Web Seed
            // Unimplemented
            BOOST_LOG_TRIVIAL(info)
                << "Metadata magnet link " << param.key
                << " not supported with value: " << param.value;
        } else if (param.key == "as") { // Acceptable Source
            // Unimplemented
            BOOST_LOG_TRIVIAL(info)
                << "Metadata magnet link " << param.key
                << " not supported with value: " << param.value;
        } else if (param.key == "xs") { // eXact Source
            // Unimplemented
            BOOST_LOG_TRIVIAL(info)
                << "Metadata magnet link " << param.key
                << " not supported with value: " << param.value;
        } else if (param.key == "kt") { // Keyword Topic
            // Unimplemented
            BOOST_LOG_TRIVIAL(info)
                << "Metadata magnet link " << param.key
                << " not supported with value: " << param.value;
        } else if (param.key == "mt") { // Manifest Topic
            // Unimplemented
            BOOST_LOG_TRIVIAL(info)
                << "Metadata magnet link " << param.key
                << " not supported with value: " << param.value;
        } else if (param.key == "so") { // Select Only
            // Unimplemented Unimplemented
            BOOST_LOG_TRIVIAL(info)
                << "Metadata magnet link " << param.key
                << " not supported with value: " << param.value;
        } else if (param.key == "x.pe ") { // PEer
            // Unimplemented
            BOOST_LOG_TRIVIAL(info)
                << "Metadata magnet link " << param.key
                << " not supported with value: " << param.value;
        } else {
            BOOST_LOG_TRIVIAL(info)
                << "Metadata magnet link unknown parameter: " << param.key;
        }
    }

    BOOST_LOG_TRIVIAL(info) << "Parsed the magnet link.";

    metadata->left = metadata->total_length;

    return metadata;
}

std::shared_ptr<Metadata> Metadata::create(const std::string_view torrent, const Config& config) {
    boost::url_view url {torrent};
    if (url.scheme() == "magnet") {
        return from_magnet(url, config);
    }
    return from_torrent_file(torrent, config);
}

std::string Metadata::get_info_hash(const BencodeParser::Element& info) {
    std::string info_hash(20, '\0');
    auto info_bencode = info.to_bencode();
    // Calculate the SHA1 from the info directory.
    SHA1(
        reinterpret_cast<const unsigned char*>(info_bencode.data()),
        info_bencode.size(),
        reinterpret_cast<unsigned char*>(info_hash.data())
    );
    return info_hash;
}

std::ostream& operator<<(std::ostream& os, const Metadata& metadata) {
    std::scoped_lock<std::mutex> lock {metadata.mutex};
    os << "Metadata{";
    os << "\n  info_hash: " << metadata.info_hash;
    os << "\n  trackers: std::vector{";
    for (const auto& announce : metadata.trackers) {
        os << "\n    announce: " << announce;
    }
    os << (metadata.trackers.empty() ? "  }" : "\n  }");
    os << "\n  name: " << metadata.name;
    os << "\n  file_name: " << metadata.file_name;
    os << "\n  piece_length: " << metadata.piece_length;
    os << "\n  total_length: " << metadata.total_length;
    os << "\n  files: std::vector{";
    for (const auto& file : metadata.files) {
        os << "\n    length:" << file.first << ", name: " << file.second;
    }
    os << (metadata.files.empty() ? "  }" : "\n  }");
    os << "\n  pieces: std::string[" << metadata.pieces.size() << "]";
    os << "\n  downloaded: " << metadata.downloaded;
    os << "\n  uploaded: " << metadata.uploaded;
    os << "\n  left: " << metadata.left;
    os << "\n  pieces_done: " << metadata.pieces_done;
    os << "\n}";

    return os;
}

} // namespace torrent
