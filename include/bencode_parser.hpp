#ifndef TORRENT_BENCODE_PARSER_HPP
#define TORRENT_BENCODE_PARSER_HPP

#include <cctype>
#include <fstream>
#include <istream>
#include <memory>
#include <variant>
#include <vector>
#include <map>
#include <sstream>
#include <iostream>
#include <iomanip>

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

        Element& operator= (Element element) {
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
        static void convert_to_valid_json(const std::string& str, std::stringstream& stream) {
            bool not_valid = true;
            bool is_hex = false;

            for (const char c: str) {
                if (!std::isspace(c) && !std::isprint(c)) {
                    is_hex = true;
                    break;
                }
                if (c == '\\' || c == '"') {
                    not_valid = true;
                } 
            }
            if (is_hex) {
                for (const char c: str) {
                    stream << std::uppercase << std::setfill('0') << std::setw(2) << 
                        std::hex << static_cast<int>(static_cast<unsigned char>(c)) << ' ';
                } 
            } else if (not_valid) {
                for (const char c: str) {
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
        struct overloaded : Ts... { using Ts::operator()...; };
        template<class... Ts>
        overloaded(Ts...) -> overloaded<Ts...>;

        static void element_to_json(const Element& element, std::stringstream& stream) {
            std::visit(overloaded{
                [&] (const int value) { stream << value; },
                [&] (const std::string& value){ 
                    stream << '"'; 
                    convert_to_valid_json(value, stream);
                    stream << '"';
                },
                [&] (const List& list) {
                    stream << '[';
                    for (int i = 0; i < list.size(); ++i) {
                        element_to_json(list[i], stream); 
                        if (i != list.size() - 1) {
                            stream << ", ";
                        }
                    } 
                    stream << ']';
                },
                [&] (const Dictionary& dictionary) {
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
                }},
                element.value); 
        }

        static void element_to_bencode(const Element& element, std::stringstream& stream) {
            std::visit(overloaded{
                [&] (const int value) { stream << 'i' << value << 'e'; },
                [&] (const std::string& value){ 
                    stream << value.size() << ':' << value; 
                },
                [&] (const List& list) {
                    stream << 'l';
                    for (int i = 0; i < list.size(); ++i) {
                        element_to_bencode(list[i], stream); 
                    } 
                    stream << 'e';
                },
                [&] (const Dictionary& dictionary) {
                    stream << 'd';   
                    for (const auto& pair: dictionary) {
                        stream << pair.first.size() << ':' << pair.first; // key
                        element_to_bencode(pair.second, stream); // value
                    }
                    stream << 'e';
                }},
                element.value); 
        }
    };
private:
    Element element;
public:    

    BencodeParser(std::unique_ptr<std::basic_istream<char>>&& stream): stream(std::move(stream)) {}
    BencodeParser(const char* path) {
        stream = std::make_unique<std::ifstream>(path, std::fstream::ios_base::binary);
    }
    
    void parse() {
        auto next_char = stream->peek();
        if (next_char != EOF) {
            // Skip any leading whitespace
            while (std::isspace(next_char)) {
                next_char = stream->get(); 
            }
            element = parse_next(next_char);
        }
    } 
    
    const Element& get() const {
        return element;
    }

    Element& get() {
        return element;
    }
    

private:
    
    Element parse_next(char next_char) {
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
                throw std::runtime_error{ "Could not parse, invalid input." + std::to_string((int)next_char) };
        }
    }
    
    Element parse_int() {
        stream->get();
        int value;
        *stream >> value;
        if (stream->get() != 'e') {
            throw std::runtime_error{ "Parsing error while parsing an integer." };
        }
        return Element{ value };
    }

    Element parse_string() {
        int length;
        *stream >> length;
        if (stream->get() != ':') {
            throw std::runtime_error{ "Parsing error while parsing a byte string." };
        }
        std::string value; 
        value.resize(length);
        stream->read(value.data(), length);
        return Element{ value };  
    }

    Element parse_list() {
        stream->get(); 
        List list; 
        char next_char; 
        while ((next_char = stream->peek()) != 'e') {
            if (next_char == EOF) {
                throw std::runtime_error{ "EOF while parsing." };
            }
            list.push_back(parse_next(next_char));
        }
        stream->get(); // consume 'e'

        return Element{ list };  
    }
    Element parse_dictionary() {
        stream->get(); 
        Dictionary dictionary; 
        char next_char; 
        while ((next_char = stream->peek()) != 'e') {
            if (next_char == EOF) {
                throw std::runtime_error{ "EOF while parsing." };
            }
            Element key = parse_string();
            Element value = parse_next(stream->peek());
            
            dictionary.emplace(std::get<std::string>(key.value), value);
        }
        stream->get(); // consume 'e'

        return Element{ dictionary };  
    }
};

}
#endif
