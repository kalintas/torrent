#ifndef TORRENT_MESSAGE_HPP
#define TORRENT_MESSAGE_HPP

#include <array>
#include <boost/endian/conversion.hpp>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <stdexcept>
#include <vector>

namespace torrent {

class Message {
  public:
    enum class Id : std::uint8_t {
        Choke = 0,
        Unchoke = 1,
        Interested = 2,
        NotInterested = 3,
        Have = 4,
        Bitfield = 5,
        Request = 6,
        Piece = 7,
        Cancel = 8,
        InvalidMessage,
    };

    template<typename Iterator>
    Message(Id id, Iterator it, std::size_t payload_length) : id(id) {
        if (static_cast<std::uint8_t>(id)
            > static_cast<std::uint8_t>(Id::InvalidMessage)) {
            id = Id::InvalidMessage;
        }

        payload.resize(payload_length);
        std::copy(it, it + payload_length, payload.begin());
    }

    /*
     * Creates a message with no payload.
     * */
    Message(Id id) : id(id) {}

    Message(Message&& message) :
        id(message.id),
        payload(std::move(message.payload)) {}

    Id get_id() const {
        return id;
    }

    const auto& get_payload() const {
        return payload;
    }

    friend std::ostream& operator<<(std::ostream& os, const Message& message) {
        os << "Message{ id: ";
        switch (message.id) {
            case Id::Choke:
                os << "Choke";
                break;
            case Id::Unchoke:
                os << "Unchoke";
                break;
            case Id::Interested:
                os << "Interested";
                break;
            case Id::NotInterested:
                os << "NotInterested";
                break;
            case Id::Have:
                os << "Have";
                break;
            case Id::Bitfield:
                os << "Bitfield";
                break;
            case Id::Request:
                os << "Request";
                break;
            case Id::Piece:
                os << "Piece";
                break;
            case Id::Cancel:
                os << "Cancel";
                break;
            case Id::InvalidMessage:
                os << "Invalid";
                break;
        }
        os << ", payload: std::uint8_t[" << message.payload.size() << "] }";
        return os;
    }

    /*
     * Function will convert the object to bytes by consuming it.
     * Objects payload will be empty after this function.
     * It will also add the length of the message as prefix.
     * @return An std::vector with the length of 5 + payload.size()
     * */
    [[nodiscard]] std::vector<std::uint8_t> into_bytes() const {
        std::array<std::uint8_t, 5> arr;
        std::uint32_t length = boost::endian::native_to_big(1 + payload.size());
        std::memcpy(
            static_cast<void*>(arr.data()),
            static_cast<void*>(&length),
            sizeof(length)
        );
        arr[4] = static_cast<int>(id);
        std::vector<std::uint8_t> result = std::move(payload);

        result.reserve(result.size() + arr.size());
        result.insert(result.begin(), arr.begin(), arr.end());

        return result;
    }

  private:
    Id id;
    std::vector<std::uint8_t> payload;
};
} // namespace torrent

#endif
