#ifndef TORRENT_UTILITY_HPP
#define TORRENT_UTILITY_HPP

#include <string>

namespace torrent {

/*
 * Returns the SHA1 string result of the given input string.
 * */
std::string get_sha1(const std::string& input);
} // namespace torrent
#endif
