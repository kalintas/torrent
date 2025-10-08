#include "bencode_parser.hpp"
#include "extensions.hpp"


namespace torrent {

Message Extensions::as_handshake_message(std::size_t metadata_size) const {
    BencodeParser::Dictionary dictionary;
    BencodeParser::Dictionary m;

    for (const auto& [extension, extension_id]: map) {
        switch (extension) {
            case Extension::MetadaExchange:
                m.insert({ "ut_metadata", BencodeParser::Element(static_cast<BencodeParser::Integer>(extension_id)) });
                dictionary.insert({ "metadata_size", 
                    BencodeParser::Element(static_cast<BencodeParser::Integer>(metadata_size)) }); 
                break;
            case Extension::ExtensionProtocol:
                break;
        }
    }

    dictionary.insert({ "m", BencodeParser::Element(std::move(m)) });

    BencodeParser::Element element(std::move(dictionary));
    const std::string bencode_str = element.to_bencode(); 
    std::vector<std::uint8_t> payload(bencode_str.size() + 1);
    std::memcpy(payload.data() + 1, bencode_str.data(), bencode_str.size());
    
    Message message{ Message::Id::Extended, std::move(payload) };
    //message.write_int<std::uint8_t>(0, 0);

    return message;
} 

void Extensions::load_extensions(const BencodeParser::Dictionary& m) {
    for (const auto& [name, id_element]: m) {
        std::uint8_t id = static_cast<std::uint8_t>(id_element.get<BencodeParser::Integer>());

        if (name == "ut_metadata") {
            map.insert({ Extension::MetadaExchange, id });
        }
    }
}

}
