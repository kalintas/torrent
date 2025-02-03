#include "client.hpp"
int main() {
    auto client = torrent::Client("./res/debian.iso.torrent");
    client.start();
}
