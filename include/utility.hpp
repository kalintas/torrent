#ifndef TORRENT_UTILITY_HPP
#define TORRENT_UTILITY_HPP

#include <string>
#include <string_view>

namespace torrent {

/*
 * Returns the SHA1 string result of the given input string.
 * */
std::string get_sha1(const std::string_view input);
} // namespace torrent
#endif
