/**
 * @file Message.h
 * @brief TCP 消息协议封装 - 长度前缀协议实现
 *
 * 【技术概述】
 * 本文件实现了 TCP 通信中最常用的"长度前缀协议"（Length-Prefixed Protocol）。
 * 由于 TCP 是流式协议，不保证消息边界，因此需要应用层协议来区分消息边界。
 * 长度前缀协议通过在每条消息前添加固定长度的头部来指示消息体的大小。
 *
 * 协议格式:
 * +------------------+--------------------+
 * |    Length (4B)   |    Body (变长)      |
 * |   网络字节序      |                    |
 * +------------------+--------------------+
 *
 * 【面试要点】
 * 1. Q: 什么是 TCP 粘包/拆包问题？如何解决？
 *    A: TCP 是字节流协议，不保证消息边界。粘包指多条消息粘在一起，
 *       拆包指一条消息被拆成多个 TCP 包。解决方案包括：
 *       - 长度前缀（本项目采用）
 *       - 分隔符（如 HTTP 的 \r\n\r\n）
 *       - 固定长度消息
 *
 * 2. Q: 为什么长度头使用网络字节序？
 *    A: 网络字节序（大端序）是网络通信的标准，确保不同架构的机器
 *       （大端/小端）能正确解析数据。x86 是小端序，需要转换。
 *
 * 3. Q: 为什么选择 4 字节长度头？
 *    A: 4 字节可表示最大 4GB 消息，足够大多数场景使用。
 *       如果消息较小（<64KB），可考虑 2 字节头节省带宽。
 *
 * 【生产实践】
 * 1. MAX_BODY_SIZE 应根据业务需求调整，过大可能导致内存攻击
 * 2. 对于二进制协议，建议添加魔数（Magic Number）用于协议校验
 * 3. 生产环境应考虑添加版本号字段，便于协议升级
 * 4. 敏感数据传输应在应用层加密后再编码
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>

/**
 * 【跨平台字节序转换】
 * - Windows: 使用 winsock2.h 中的 htonl/ntohl
 * - Linux/macOS: 使用 arpa/inet.h 中的 htonl/ntohl
 *
 * htonl: Host TO Network Long (主机序转网络序，32位)
 * ntohl: Network TO Host Long (网络序转主机序，32位)
 *
 * 【面试考点】
 * Q: htonl 在大端机器上做什么？
 * A: 大端机器的主机序就是网络序，所以 htonl 什么都不做（空操作）
 */
#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <arpa/inet.h>
#endif

namespace asioclient {

/**
 * 【协议常量定义】
 *
 * 【设计决策】
 * - HEADER_SIZE = 4: 使用 uint32_t 存储长度，最大支持 4GB 消息
 * - MAX_BODY_SIZE = 16MB: 防止恶意客户端发送超大消息耗尽内存
 *
 * 【生产实践】
 * - 游戏协议: 通常限制在 64KB 以内（2字节头足够）
 * - 文件传输: 可能需要更大限制，但建议分块传输
 * - 即时通讯: 文本消息通常限制在 4KB 以内
 */
constexpr size_t HEADER_SIZE = 4;           // 消息头大小（4字节）
constexpr size_t MAX_BODY_SIZE = 16 * 1024 * 1024;  // 最大消息体（16MB）

// 最大消息体大小 (16MB)
constexpr size_t MAX_BODY_SIZE = 16 * 1024 * 1024;

/**
 * @class Message
 * @brief 消息类 - 封装协议的编解码
 *
 * 【设计模式】
 * 采用值语义设计，支持拷贝和移动，方便在异步回调中传递。
 *
 * 【内存管理】
 * 使用 std::vector<char> 存储消息体：
 * - 自动管理内存，无需手动释放
 * - 支持动态扩容
 * - 连续内存，便于网络发送
 *
 * 【面试考点】
 * Q: 为什么用 vector<char> 而不是 string？
 * A: vector<char> 更适合二进制数据：
 *    - string 的 c_str() 会添加 '\0'
 *    - string 的某些实现有 SSO（小字符串优化），可能影响性能
 *    - 语义上 vector<char> 更明确表示二进制数据
 */
class Message {
public:
    /**
     * 【构造函数系列】
     *
     * 【设计理念】
     * 提供多种构造方式，方便不同使用场景：
     * 1. 默认构造 - 创建空消息
     * 2. 字符串构造 - 适合文本协议
     * 3. vector 拷贝构造 - 适合二进制数据
     * 4. vector 移动构造 - 零拷贝，高性能
     *
     * 【C++ 特性】
     * explicit 关键字防止隐式类型转换，避免意外的对象创建
     */
    Message() = default;

    /**
     * @brief 从字符串构造消息
     * @param body 消息内容字符串
     *
     * 【实现细节】
     * 使用迭代器范围构造 vector，适用于任意字符（包括 '\0'）
     */
    explicit Message(const std::string& body)
        : body_(body.begin(), body.end()) {}

    /**
     * @brief 从 vector 拷贝构造
     * @param body 消息内容
     *
     * 【性能提示】
     * 会发生一次内存拷贝，对于大消息考虑使用移动构造
     */
    explicit Message(const std::vector<char>& body)
        : body_(body) {}

    /**
     * @brief 从 vector 移动构造（零拷贝）
     * @param body 消息内容（移动后原对象为空）
     *
     * 【面试考点】
     * Q: std::move 做了什么？
     * A: std::move 只是类型转换，将左值转为右值引用，
     *    真正的移动发生在 vector 的移动构造函数中。
     *
     * 【性能对比】
     * - 拷贝构造: O(n) 时间，O(n) 空间
     * - 移动构造: O(1) 时间，O(1) 空间（只交换指针）
     */
    explicit Message(std::vector<char>&& body)
        : body_(std::move(body)) {}

    /**
     * 【访问器方法】
     *
     * 提供 const 和非 const 两个版本：
     * - const 版本用于只读访问
     * - 非 const 版本允许修改内容
     */
    const std::vector<char>& body() const { return body_; }
    std::vector<char>& body() { return body_; }

    /**
     * @brief 获取消息体大小
     * @return 消息体字节数
     *
     * 【注意】这是消息体大小，不包括 4 字节头
     */
    size_t bodySize() const { return body_.size(); }

    /**
     * @brief 获取消息体为字符串
     * @return 消息内容的字符串表示
     *
     * 【使用场景】
     * 适合文本协议或调试输出
     *
     * 【注意事项】
     * 如果消息体包含二进制数据，转换为 string 可能丢失信息
     */
    std::string bodyAsString() const {
        return std::string(body_.begin(), body_.end());
    }

    /**
     * @brief 设置消息体（从字符串）
     * @param data 消息内容
     */
    void setBody(const std::string& data) {
        body_.assign(data.begin(), data.end());
    }

    /**
     * @brief 设置消息体（从原始指针）
     * @param data 数据指针
     * @param len 数据长度
     *
     * 【安全提示】
     * 调用者需确保 data 指向的内存有效且长度正确
     */
    void setBody(const char* data, size_t len) {
        body_.assign(data, data + len);
    }

    /**
     * @brief 编码消息（添加长度头）
     * @return 完整的网络数据包（头部 + 消息体）
     *
     * 【核心方法】这是发送消息的关键步骤
     *
     * 【实现步骤】
     * 1. 分配 header_size + body_size 的缓冲区
     * 2. 将消息体长度转为网络字节序，写入头部
     * 3. 拷贝消息体到头部之后
     *
     * 【面试考点】
     * Q: 为什么返回值而不是输出参数？
     * A: 现代 C++ 编译器会进行 RVO（返回值优化）或 NRVO，
     *    直接在调用者的内存位置构造对象，无额外拷贝。
     *
     * 【性能分析】
     * - 时间复杂度: O(n)，需要拷贝消息体
     * - 空间复杂度: O(n)，需要新的缓冲区
     *
     * 【优化建议】
     * 对于超高频发送场景，可考虑预分配缓冲区复用
     */
    std::vector<char> encode() const {
        std::vector<char> result;
        result.resize(HEADER_SIZE + body_.size());

        /**
         * 【字节序转换】
         * htonl: host to network long
         * 将主机字节序（通常是小端）转换为网络字节序（大端）
         *
         * 示例: body_.size() = 256 (0x00000100)
         * 小端存储: 00 01 00 00
         * 网络序:   00 00 01 00
         */
        uint32_t len = htonl(static_cast<uint32_t>(body_.size()));
        std::memcpy(result.data(), &len, HEADER_SIZE);

        // 写入消息体（如果有）
        if (!body_.empty()) {
            std::memcpy(result.data() + HEADER_SIZE, body_.data(), body_.size());
        }

        return result;
    }

    /**
     * @brief 从缓冲区解析长度头
     * @param data 至少包含 HEADER_SIZE 字节的缓冲区
     * @return 消息体长度（主机字节序）
     *
     * 【静态方法】不需要 Message 对象即可调用
     *
     * 【使用场景】
     * 接收端先读取 4 字节头，解析出长度后再读取对应长度的消息体
     *
     * 【面试考点】
     * Q: 为什么用 memcpy 而不是直接类型转换？
     * A: 直接转换 *(uint32_t*)data 可能导致：
     *    1. 对齐问题（某些 CPU 不支持非对齐访问）
     *    2. 违反严格别名规则（Strict Aliasing）
     *    memcpy 是标准推荐的安全做法
     */
    static uint32_t decodeHeader(const char* data) {
        uint32_t len;
        std::memcpy(&len, data, HEADER_SIZE);
        return ntohl(len);  // 网络序转主机序
    }

    /**
     * @brief 验证消息长度是否有效
     * @param len 消息长度
     * @return true 表示长度有效
     *
     * 【安全防护】
     * 防止恶意客户端发送超大长度值，导致服务器尝试分配巨大内存
     *
     * 【生产实践】
     * 1. 收到长度后必须先验证再分配内存
     * 2. 可考虑添加最小长度验证（排除无效的空消息）
     * 3. 可记录异常长度值用于安全审计
     */
    static bool isValidLength(uint32_t len) {
        return len <= MAX_BODY_SIZE;
    }

private:
    /**
     * 【消息体存储】
     *
     * 使用 vector<char> 的优势：
     * 1. 自动内存管理（RAII）
     * 2. 支持动态扩容
     * 3. 连续内存布局，便于网络 I/O
     * 4. 移动语义支持，高效传递
     */
    std::vector<char> body_;
};

} // namespace asioclient

/**
 * 【扩展阅读】
 *
 * 1. 协议设计的演进：
 *    - 基础版：长度 + 数据（本项目）
 *    - 进阶版：魔数 + 版本 + 类型 + 长度 + 数据 + 校验
 *    - 成熟版：使用 Protobuf/FlatBuffers 等序列化框架
 *
 * 2. 常见的应用层协议对比：
 *    | 协议 | 分帧方式 | 特点 |
 *    |------|---------|------|
 *    | HTTP/1.1 | Content-Length 或 chunked | 文本协议，易读 |
 *    | WebSocket | 帧头包含长度 | 支持二进制和文本 |
 *    | gRPC | Protobuf + 帧头 | 高效二进制协议 |
 *    | Redis | \r\n 分隔 + 长度前缀 | 混合方式 |
 *
 * 3. 性能优化方向：
 *    - 内存池：复用消息对象，减少分配
 *    - 零拷贝：使用 scatter-gather I/O
 *    - 压缩：对大消息进行压缩传输
 */
