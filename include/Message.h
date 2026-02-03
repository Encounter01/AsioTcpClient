/**
 * @file Message.h
 * @brief Length-prefixed protocol for TCP message framing
 *
 * Protocol format: [Length (4B, network byte order)][Body (variable length)]
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>

// Cross-platform byte order conversion functions (htonl/ntohl)
#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <arpa/inet.h>
#endif

namespace asioclient {

// Protocol constants
constexpr size_t HEADER_SIZE = 4;                    // Header size: 4 bytes
constexpr size_t MAX_BODY_SIZE = 16 * 1024 * 1024;  // Max body size: 16MB

/**
 * @class Message
 * @brief Message class for protocol encoding/decoding
 */
class Message {
public:
    // Constructors
    Message() = default;
    explicit Message(const std::string& body)
        : body_(body.begin(), body.end()) {}
    explicit Message(const std::vector<char>& body)
        : body_(body) {}
    explicit Message(std::vector<char>&& body)
        : body_(std::move(body)) {}

    // Accessors
    const std::vector<char>& body() const { return body_; }
    std::vector<char>& body() { return body_; }
    size_t bodySize() const { return body_.size(); }
    std::string bodyAsString() const {
        return std::string(body_.begin(), body_.end());
    }

    // Mutators
    void setBody(const std::string& data) {
        body_.assign(data.begin(), data.end());
    }
    void setBody(const char* data, size_t len) {
        body_.assign(data, data + len);
    }

    /**
     * @brief Encode message with length header
     * @return Complete packet (header + body) ready for network transmission
     */
    std::vector<char> encode() const {
        std::vector<char> result;
        result.resize(HEADER_SIZE + body_.size());

        // Convert length to network byte order
        uint32_t len = htonl(static_cast<uint32_t>(body_.size()));
        std::memcpy(result.data(), &len, HEADER_SIZE);

        // Copy body if present
        if (!body_.empty()) {
            std::memcpy(result.data() + HEADER_SIZE, body_.data(), body_.size());
        }

        return result;
    }

    /**
     * @brief Decode length header from buffer
     * @param data Buffer containing at least HEADER_SIZE bytes
     * @return Body length in host byte order
     */
    static uint32_t decodeHeader(const char* data) {
        uint32_t len;
        std::memcpy(&len, data, HEADER_SIZE);
        return ntohl(len);
    }

    /**
     * @brief Validate message length
     * @param len Message length to validate
     * @return true if length is valid
     */
    static bool isValidLength(uint32_t len) {
        return len <= MAX_BODY_SIZE;
    }

private:
    std::vector<char> body_;  // Message body storage
};

} // namespace asioclient
