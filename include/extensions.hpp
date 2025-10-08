#ifndef TORRENT_EXTENSIONS_HPP
#define TORRENT_EXTENSIONS_HPP

#include <cstdint>
#include <array>
#include <cstring>
#include <unordered_map>
#include "bencode_parser.hpp"
#include "message.hpp"

namespace torrent {

enum class Extension : std::uint8_t {
    ExtensionProtocol = 0,  // BEP 10
    MetadaExchange = 3,      // BEP 9  
    //DHT = ,                 // BEP 5 
    //FastExtension = ,       // BEP 6 
    //Ipv6Support = ,         // BEP 7
    //PeerExchange = ,       // BEP 11 
    //TransportProtocol = ,  // BEP 29
};

/*
inline std::ostream& operator<<(std::ostream& os, Extension extension) {
    switch (extension) {
        case Extension::ExtensionProtocol:
            os << "Extension Protocol";
            break;
        case Extension::MetadaExchange:
            os << "Metadata Exchange";
            break;
        default:
            os << "Unknown Extension";
            break;
    }
    return os;
}*/

class Extensions {
public:
    
    Extensions() = default;
    Extensions(const Extensions&) = default;  

    Extensions(Extensions&& extensions) : map(std::move(extensions.map)) {}

    Extensions& operator=(const Extensions&) = default;
    Extensions& operator=(Extensions&& extensions) {
        map = std::move(extensions.map);
        return *this;
    }

    void add(Extension extension) {
        map.insert({ extension, static_cast<std::uint8_t>(extension) }) ;
    }
    
    void remove(Extension extension) {
        map.erase(extension);
    }

    /* 
     * Returns whether an extension is enabled.
     * */
    bool has(Extension extension) const {
        return map.find(extension) != map.end();
    }

    std::array<std::uint8_t, 8> as_reserved_bytes() const {
        std::array<std::uint8_t, 8> bytes;
        bytes.fill(0);
        if (has(Extension::ExtensionProtocol)) {
            bytes[5] |= 0x10;  
        }
        return bytes;
    }
    
    template<typename Iterator>
    static Extensions from_reserved_bytes(Iterator it) {
        Extensions extensions;
        if ((*(it + 5) & 0x10) != 0) {
            extensions.add(Extension::ExtensionProtocol);
        } 
        return extensions;
    }

    /*
     * Creates a message object from the Extensions object.
     * This is for the BEP10(Extension Protocol) handshake.
     * @return An Extended message that is ready to send to other peers.
     * */
    Message as_handshake_message(std::size_t metadata_size) const;
    
    /*
     * Loads the given given extensions acquired from the BEP10(Extension Protocol) handshake.
     * @param m The m dictionary from the received Extended message.
     * @throws Whatever error BencodeParser will throw while trying to fetch the values. 
     * */
    void load_extensions(const BencodeParser::Dictionary& m); 

private:
    
    std::unordered_map<Extension, std::uint8_t> map;
};

}

#endif
