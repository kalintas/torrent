#ifndef TORRENT_BENCODE_PARSER_HPP
#define TORRENT_BENCODE_PARSER_HPP

#include <cctype>
#include <fstream>
#include <iostream>
#include <istream>
#include <map>
#include <memory>
#include <sstream>
#include <variant>
#include <vector>

namespace torrent {

// A simple class to parse bencode streams.
// https://en.wikipedia.org/wiki/Bencode
class BencodeParser {
  private:
    std::unique_ptr<std::basic_istream<char>> stream;

  public:
    struct Element;
    using List = std::vector<Element>;
    using Dictionary = std::map<std::string, Element>;

    struct Element {
        using Type = std::variant<int, std::string, List, Dictionary>;
        Type value;

        Element(Element&& element) : value(std::move(element.value)) {}

        Element(const Element& element) : value(element.value) {}

        Element() {}

        Element(Type value) : value(std::move(value)) {}

        Element& operator=(Element element) {
            this->value = std::move(element.value);
            return *this;
        }

        template<typename T>
        constexpr const T& get() const {
            return std::get<T>(value);
        }

        template<typename T>
        constexpr T& get() {
            return std::get<T>(value);
        }

        std::string to_bencode() const {
            std::stringstream stream;
            element_to_bencode(*this, stream);
            return stream.str();
        }

        std::string to_json() const {
            std::stringstream stream;
            element_to_json(*this, stream);
            return stream.str();
        }

      private:
        static void convert_to_valid_json(
            const std::string& str,
            std::stringstream& stream
        );

        static void
        element_to_json(const Element& element, std::stringstream& stream);
        static void
        element_to_bencode(const Element& element, std::stringstream& stream);
    };

  private:
    Element element;

  public:
    BencodeParser(std::unique_ptr<std::basic_istream<char>>&& stream) :
        stream(std::move(stream)) {}

    BencodeParser(const std::string_view path) {
        stream = std::make_unique<std::ifstream>(
            path.data(),
            std::fstream::ios_base::binary
        );
    }

    const Element& get() const {
        return element;
    }

    Element& get() {
        return element;
    }

    /*
     * Consumes the inner stream until eof.
     * It should only be called once after the constructor.
     * */
    void parse();

  private:
    Element parse_next(char next_char);
    Element parse_int();
    Element parse_string();
    Element parse_list();
    Element parse_dictionary();
};

} // namespace torrent
#endif
