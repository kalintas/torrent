#include "utility.hpp"

#include <boost/uuid/detail/sha1.hpp>

namespace torrent {

std::string get_sha1(const std::string_view input) {
    boost::uuids::detail::sha1 sha1;
    sha1.process_bytes(input.data(), input.size());
    std::uint8_t hash[20] = {0};
    sha1.get_digest(hash);

    std::string result(20, '\0');
    std::memcpy(
        static_cast<void*>(result.data()),
        static_cast<void*>(hash),
        20
    );
    return result;
}

} // namespace torrent
