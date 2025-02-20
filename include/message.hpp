#ifndef TORRENT_MESSAGE_HPP
#define TORRENT_MESSAGE_HPP

#include <array>
#include <boost/endian/conversion.hpp>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <ostream>
#include <sstream>
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

    /*
     * Creates a Message object from given Id and payload.
     * */
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
     * Creates a Message object from given bytes.
     * */
    Message(const std::vector<std::uint8_t>& bytes) :
        id(static_cast<Id>(bytes[0])) {
        if (static_cast<std::uint8_t>(id)
            > static_cast<std::uint8_t>(Id::InvalidMessage)) {
            id = Id::InvalidMessage;
        }
        payload.resize(bytes.size() - 1);
        std::copy(bytes.begin() + 1, bytes.end(), payload.begin());
    }

    /*
     * Creates a Message object from given Id and payload vector. 
     * Moves the given payload.
     * */
    Message(Id id, std::vector<std::uint8_t> payload) :
        id(id),
        payload(std::move(payload)) {
        if (static_cast<std::uint8_t>(id)
            > static_cast<std::uint8_t>(Id::InvalidMessage)) {
            id = Id::InvalidMessage;
        }
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
                os << "Have, piece index: " << message.get_int(0);
                break;
            case Id::Bitfield:
                os << "Bitfield, bitfield: std::uint8_t["
                   << message.payload.size() << "]";
                break;
            case Id::Request:
                os << "Request, index: " << message.get_int(0)
                   << ", begin: " << message.get_int(1)
                   << ", length: " << message.get_int(2);
                break;
            case Id::Piece:
                os << "Piece, index: " << message.get_int(0)
                   << ", begin: " << message.get_int(1)
                   << ", block: std::uint8_t[" << message.payload.size() << "]";
                break;
            case Id::Cancel:
                os << "Cancel, index: " << message.get_int(0)
                   << ", begin: " << message.get_int(1)
                   << ", length: " << message.get_int(2);
                break;
            case Id::InvalidMessage:
                os << "Invalid, listen port: " << message.get_int(0);
                break;
        }
        os << " }";
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
        std::uint32_t length = boost::endian::native_to_big(
            static_cast<std::uint32_t>(1 + payload.size())
        );
        std::memcpy(
            static_cast<void*>(arr.data()),
            static_cast<void*>(&length),
            sizeof(length)
        );
        arr[4] = static_cast<std::uint8_t>(id);
        std::vector<std::uint8_t> result = std::move(payload);

        result.reserve(result.size() + arr.size());
        result.insert(result.begin(), arr.begin(), arr.end());

        return result;
    }

    /*
     * Returns the nth integer from the payload.
     * */
    std::uint32_t get_int(std::size_t int_index) const {
        if (payload.size() < int_index * 4) {
            throw std::runtime_error(
                "Message::get_int called with invalid parameters"
            );
        }
        std::uint32_t result;
        std::memcpy(&result, payload.data() + int_index * 4, 4);
        return boost::endian::big_to_native(result);
    }

    /*
     * Writes the parameter int to the payload. Payload must have the required space for it.
     * */
    void write_int(std::size_t int_index, std::uint32_t value) {
        if (payload.size() < int_index * 4) {
            throw std::runtime_error(
                "Message::get_int called with invalid parameters"
            );
        }
        value = boost::endian::native_to_big(value);
        std::memcpy(payload.data() + int_index * 4, &value, 4);
    }

    std::string to_string() const {
        std::ostringstream oss;
        oss << *this;
        return oss.str();
    }

  private:
    Id id;
    std::vector<std::uint8_t> payload;
};
} // namespace torrent

#endif
