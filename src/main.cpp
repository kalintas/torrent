#include "client.hpp"
#include <boost/asio/io_context.hpp>
int main() {
    
    boost::asio::io_context io_context;
    auto client = torrent::Client("./res/debian.iso.torrent", io_context);
    client.start(); 
}
