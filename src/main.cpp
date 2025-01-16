#include <iostream>
#include <stdexcept>
#include "bencode-parser.hpp"

using namespace torrent;

int main() {
    try {
        torrent::BencodeParser parser("./res/arch.torrent");
        parser.parse(); 
       
        auto dictionary = std::get<BencodeParser::Dictionary>(parser.get().value);
        BencodeParser::Element url_list = dictionary.find("url-list")->second;
        std::cout << url_list.to_json();
    } catch (std::runtime_error e) {
        std::cerr << e.what(); 
    }

}
