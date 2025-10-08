#ifndef TORRENT_CONFIG_HPP
#define TORRENT_CONFIG_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include "extensions.hpp"
namespace torrent {

class ConfigBuilder;

/*
 * A class that holds constant configuration of the client.
 * */
class Config {
private:
    explicit Config() = default;
public:
    
    Config(const Config&) = default;
    Config(Config&& config) : 
        block_length(config.block_length),
        request_per_call(config.request_per_call),
        max_message_length(config.max_message_length),
        port(config.port),
        extensions(std::move(config.extensions)) {}
    
    Config& operator=(const Config&) = default;

    [[nodiscard]] constexpr std::size_t get_block_length() const { return block_length; }
    [[nodiscard]] constexpr std::size_t get_request_per_call() const { return request_per_call; }
    [[nodiscard]] constexpr std::size_t get_max_message_length() const { return max_message_length; }
    [[nodiscard]] constexpr std::uint16_t get_port() const { return port; }
    [[nodiscard]] const Extensions& get_extensions() const { return extensions; }
    
    /*
     * Returns whether given extension is supported.
     * */
    [[nodiscard]] bool is_supported(Extension id) const {
        return extensions.has(id);
    } 

    friend std::ostream& operator<<(std::ostream& os, const Config& config) {
        os << "Config{ block_length: " << config.block_length << ", request_per_call: " << config.request_per_call << ", max_message_length: " << config.max_message_length << ", port: " << config.port << ", extensions: "; 
        for (const auto byte: config.extensions.as_reserved_bytes()) {
            os << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
        }
        os << std::dec << " }";
        return os;
    }

private:
    friend class ConfigBuilder; 

    std::size_t block_length = 0;
    std::size_t request_per_call = 0; 
    std::size_t max_message_length = 0;
    std::uint16_t port = 0;

    Extensions extensions;
};

class ConfigBuilder {
private:
    explicit ConfigBuilder() = default;
public:

    /*
     * Creates a ConfigBuilder object with a default config.
     * This is the default config that the client is using.
     * */
    static ConfigBuilder default_config() {
        ConfigBuilder config_builder;
        config_builder.config.request_per_call = 6;
        config_builder.config.max_message_length = 1 << 17;
        config_builder.config.block_length = 1 << 14; 
        config_builder.config.port = 8000;
        config_builder.config.extensions.add(Extension::ExtensionProtocol);
        config_builder.config.extensions.add(Extension::MetadaExchange);

        return config_builder;
    }

    /*
     * Creates a ConfigBuilder object with a an empty config.
     * Every value wil be set to 0.
     * */
    static ConfigBuilder empty_config() {
        return ConfigBuilder{};
    }
    
    /*
     * Set the block length.
     * Default value is 16384(1 << 14).
     * */
    constexpr ConfigBuilder& set_block_length(std::size_t block_length) {
        config.block_length = block_length;
        return *this;
    }
    
    /*
     * Set the request per call. Value represents the request count that will be send in a single batch.
     * Default value is 6.
     * */
    constexpr ConfigBuilder& set_request_per_call(std::size_t request_per_call) {
        config.request_per_call = request_per_call;
        return *this;
    }
    
    /*
     * The maximum length the client will accept peer messages.
     * Default value is 131072(1 << 17).
     * */
    constexpr ConfigBuilder& set_max_message_length(std::size_t max_message_length) {
        config.max_message_length = max_message_length;
        return *this;
    }
    
    /*
     * Set the extensions that the Client will support.
     * */
    ConfigBuilder& set_extensions(Extensions extensions) {
        config.extensions = std::move(extensions); 
        return *this;
    }

    /*
     * Builds the config.
     * */
    Config build() {
        return std::move(config);
    }
private:
    Config config;
};

}

#endif
