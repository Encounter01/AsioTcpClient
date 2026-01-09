/**
 * Message.h - 消息协议定义
 *
 * 协议格式:
 * +----------------+------------------+
 * | 4 字节长度头    |   消息体 (变长)   |
 * | (网络字节序)    |                  |
 * +----------------+------------------+
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <arpa/inet.h>
#endif

namespace asioclient {

// 消息头大小 (4字节)
constexpr size_t HEADER_SIZE = 4;

// 最大消息体大小 (16MB)
constexpr size_t MAX_BODY_SIZE = 16 * 1024 * 1024;

/**
 * 消息类 - 封装协议的编解码
 */
class Message {
public:
    Message() = default;

    explicit Message(const std::string& body)
        : body_(body.begin(), body.end()) {}

    explicit Message(const std::vector<char>& body)
        : body_(body) {}

    explicit Message(std::vector<char>&& body)
        : body_(std::move(body)) {}

    // 获取消息体
    const std::vector<char>& body() const { return body_; }
    std::vector<char>& body() { return body_; }

    // 获取消息体大小
    size_t bodySize() const { return body_.size(); }

    // 获取消息体为字符串
    std::string bodyAsString() const {
        return std::string(body_.begin(), body_.end());
    }

    // 设置消息体
    void setBody(const std::string& data) {
        body_.assign(data.begin(), data.end());
    }

    void setBody(const char* data, size_t len) {
        body_.assign(data, data + len);
    }

    // 编码消息 (添加长度头)
    std::vector<char> encode() const {
        std::vector<char> result;
        result.resize(HEADER_SIZE + body_.size());

        // 写入长度头 (网络字节序)
        uint32_t len = htonl(static_cast<uint32_t>(body_.size()));
        std::memcpy(result.data(), &len, HEADER_SIZE);

        // 写入消息体
        if (!body_.empty()) {
            std::memcpy(result.data() + HEADER_SIZE, body_.data(), body_.size());
        }

        return result;
    }

    // 从缓冲区解析长度头
    static uint32_t decodeHeader(const char* data) {
        uint32_t len;
        std::memcpy(&len, data, HEADER_SIZE);
        return ntohl(len);
    }

    // 验证消息长度是否有效
    static bool isValidLength(uint32_t len) {
        return len <= MAX_BODY_SIZE;
    }

private:
    std::vector<char> body_;
};

} // namespace asioclient
