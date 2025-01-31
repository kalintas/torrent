#ifndef TORRENT_CLIENT_HPP
#define TORRENT_CLIENT_HPP

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <random>
#include <limits>
#include <stdexcept>
#include <thread>

#include <boost/asio.hpp>
#include <boost/url.hpp>
#include <boost/endian.hpp>
#include <boost/asio/detail/socket_ops.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/registered_buffer.hpp>
#include <boost/uuid/detail/sha1.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>

#include "bencode_parser.hpp"

namespace asio = boost::asio;
namespace beast = boost::beast;
using namespace boost::asio::ip;
namespace http = boost::beast::http;
using tcp = boost::asio::ip::tcp;

namespace torrent {

class Client {
private:

    BencodeParser bencode_parser;
    beast::flat_static_buffer<4096> buffer;
    std::string response_body;
    asio::io_context& io_context;
    tcp::socket socket;

    std::string peer_id;
    std::default_random_engine random_engine;

public:
    
    Client(const char* path, asio::io_context& io_context) : 
        bencode_parser(path), io_context(io_context), 
        socket(io_context), random_engine(std::random_device{}()) {
        // Generate 20 random characters for the peer id.
        static constexpr std::string_view alphanum =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
        
        std::uniform_int_distribution<> dist(0, alphanum.size());
        peer_id.reserve(20);
        // Optional: Add a client identifier. 
        while (peer_id.size() < 20) {
            peer_id.push_back(alphanum[dist(random_engine)]);
        } 

        std::cout << peer_id << "\n"; 
    }
        
    std::string get_sha1(const std::string& input) {
        boost::uuids::detail::sha1 sha1;
        sha1.process_bytes(input.c_str(), input.size()); 
        std::uint8_t hash[20] = { 0 };
        sha1.get_digest(hash);

        std::string result(20, '\0');
        std::memcpy(static_cast<void*>(result.data()) , static_cast<void*>(hash), 20);         

        return result; 
    }


    void start() {
        try {
            bencode_parser.parse();
            
            auto dictionary = std::get<BencodeParser::Dictionary>(bencode_parser.get().value);
        
            auto announce = dictionary["announce"].get<std::string>();
            auto info_element = dictionary["info"];
            auto info = info_element.get<BencodeParser::Dictionary>();

            std::cout << "announce: " << announce << std::endl;

            auto url = boost::url(announce);
            auto params = url.encoded_params(); 
            auto info_hash = get_sha1(info_element.to_bencode()); 

            params.append({ "info_hash", info_hash });
            params.append({ "peer_id", peer_id });
            params.append({ "port", "0" });
            params.append({ "uploaded", "0" });
            params.append({ "downloaded", "0" });
            params.append({ "compact", "1" });
            params.append({ "left", "1" });

            std::cout << "url: " << url << std::endl;

            tcp::resolver resolver(io_context);
            auto endpoints = resolver.resolve(url.host(), url.port());
            asio::connect(socket, endpoints); 
            if (!socket.is_open()) {
                std::cout << "socket is not open";
                return;
            } 
            http::request<http::string_body> req{ http::verb::get, url.encoded_target(), 11 };
            req.set(http::field::host, url.host());
            req.set(http::field::close, "close");
            req.set(http::field::accept, "*/*");
            
            http::async_write(socket, req,
            [] (std::error_code error, std::size_t bytes_sent) {
                if (!error) {
                    std::cout << "bytes sent: " << bytes_sent << "\n";
                } else {
                    std::cout << "error send";
                }
            }); 
            
            http::response<http::string_body> res;
            listen_packet(res);

            auto context_thread = std::thread { [&] () { io_context.run(); } };
            context_thread.join();
        } catch (std::runtime_error e) {
            std::cerr << e.what(); 
        }
    }
private:
    void listen_packet(http::response<http::string_body> res) {
        http::async_read(socket, buffer, res, 
            [&] (std::error_code error, std::size_t bytes_read) {
                if (!error) {
                    std::cout << "read bytes: " << bytes_read << std::endl;

                    BencodeParser input_parser(std::make_unique<std::stringstream>(res.body()));
                    input_parser.parse();
                    
                    auto element = input_parser.get();
                    
                    std::cout << "result:\n" << element.to_json() << "\n";

                } else {
                    std::cout << "receive message: " << error.message();
                } 
        });
    }
    };
}

#endif


