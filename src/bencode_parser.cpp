#include "bencode_parser.hpp"

#include <iomanip>

namespace torrent {

void BencodeParser::Element::convert_to_valid_json(
    const std::string& str,
    std::stringstream& stream
) {
    bool not_valid = true;
    bool is_hex = false;

    for (const char c : str) {
        if (!std::isspace(c) && !std::isprint(c)) {
            is_hex = true;
            break;
        }
        if (c == '\\' || c == '"') {
            not_valid = true;
        }
    }
    if (is_hex) {
        for (const char c : str) {
            stream << std::uppercase << std::setfill('0') << std::setw(2)
                   << std::hex
                   << static_cast<int>(static_cast<unsigned char>(c)) << ' ';
        }
    } else if (not_valid) {
        for (const char c : str) {
            if (c == '\\' || c == '"') {
                stream << '\\';
            }
            stream << c;
        }
    } else {
        stream << str;
    }
}

// Helpers for type matching.
template<class... Ts>
struct overloaded: Ts... {
    using Ts::operator()...;
};
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

void BencodeParser::Element::element_to_json(
    const Element& element,
    std::stringstream& stream
) {
    std::visit(
        overloaded {
            [&](const Integer value) { stream << value; },
            [&](const String& value) {
                stream << '"';
                convert_to_valid_json(value, stream);
                stream << '"';
            },
            [&](const List& list) {
                stream << '[';
                for (int i = 0; i < list.size(); ++i) {
                    element_to_json(list[i], stream);
                    if (i != list.size() - 1) {
                        stream << ", ";
                    }
                }
                stream << ']';
            },
            [&](const Dictionary& dictionary) {
                stream << '{';

                for (auto it = dictionary.begin();;) {
                    stream << '"' << it->first << "\":";
                    element_to_json(it->second, stream);
                    ++it;
                    if (it != dictionary.end()) {
                        stream << ", ";
                    } else {
                        break;
                    }
                }

                stream << '}';
            }
        },
        element.value
    );
}

void BencodeParser::Element::element_to_bencode(
    const Element& element,
    std::stringstream& stream
) {
    std::visit(
        overloaded {
            [&](const int value) { stream << 'i' << value << 'e'; },
            [&](const std::string& value) {
                stream << value.size() << ':' << value;
            },
            [&](const List& list) {
                stream << 'l';
                for (std::size_t i = 0; i < list.size(); ++i) {
                    element_to_bencode(list[i], stream);
                }
                stream << 'e';
            },
            [&](const Dictionary& dictionary) {
                stream << 'd';
                for (const auto& pair : dictionary) {
                    stream << pair.first.size() << ':' << pair.first; // key
                    element_to_bencode(pair.second, stream); // value
                }
                stream << 'e';
            }
        },
        element.value
    );
}

void BencodeParser::parse() {
    if (!stream) {
        throw std::runtime_error("There is no open streams for to parse.");
    }

    auto next_char = stream->peek();
    if (next_char != EOF) {
        // Skip any leading whitespace
        while (std::isspace(next_char)) {
            next_char = stream->get();
        }
        element = parse_next(next_char);
        stream = nullptr; // Close the stream.
    }
}

BencodeParser::Element BencodeParser::parse_next(char next_char) {
    if (std::isdigit(next_char)) {
        return parse_string();
    }
    switch (next_char) {
        case 'i':
            return parse_int();
        case 'l':
            return parse_list();
        case 'd':
            return parse_dictionary();
        default:
            throw std::runtime_error {
                "Could not parse, invalid input."
                + std::to_string((int)next_char)
            };
    }
}

BencodeParser::Element BencodeParser::parse_int() {
    stream->get();
    Integer value;
    *stream >> value;
    if (stream->get() != 'e') {
        throw std::runtime_error {"Parsing error while parsing an integer."};
    }
    return Element {value};
}

BencodeParser::Element BencodeParser::parse_string() {
    Integer length;
    *stream >> length;
    if (stream->get() != ':') {
        throw std::runtime_error {"Parsing error while parsing a byte string."};
    }
    std::string value;
    value.resize(length);
    stream->read(value.data(), length);
    return Element {value};
}

BencodeParser::Element BencodeParser::parse_list() {
    stream->get();
    List list;
    char next_char;
    while ((next_char = stream->peek()) != 'e') {
        if (next_char == EOF) {
            throw std::runtime_error {"EOF while parsing."};
        }
        list.push_back(parse_next(next_char));
    }
    stream->get(); // consume 'e'

    return Element {list};
}

BencodeParser::Element BencodeParser::parse_dictionary() {
    stream->get();
    Dictionary dictionary;
    char next_char;
    while ((next_char = stream->peek()) != 'e') {
        if (next_char == EOF) {
            throw std::runtime_error {"EOF while parsing."};
        }
        Element key = parse_string();
        Element value = parse_next(stream->peek());

        dictionary.emplace(std::get<std::string>(key.value), value);
    }
    stream->get(); // consume 'e'

    return Element {dictionary};
}

} // namespace torrent
