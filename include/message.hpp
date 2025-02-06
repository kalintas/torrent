#ifndef TORRENT_MESSAGE_HPP
#define TORRENT_MESSAGE_HPP

#include <boost/endian/conversion.hpp>
#include <cstdint>
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

    Id get_id() const {
        return id;
    }

    bool has_piece(std::size_t piece_index) {
        if (id != Id::Bitfield) {
            throw new std::runtime_error(
                "Message type is incorrect for this function"
            );
        }
        return (payload[piece_index / 8] >> (7 - (piece_index % 8))) & 1;
    }

    void set_piece(std::size_t piece_index) {
        if (id != Id::Bitfield) {
            throw new std::runtime_error(
                "Message type is incorrect for this function"
            );
        }
        payload[piece_index / 8] |= 1 << (7 - (piece_index % 8));
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

  private:
    Id id;
    std::vector<std::uint8_t> payload;
};
} // namespace torrent

#endif
