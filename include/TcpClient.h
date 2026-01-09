/**
 * TcpClient.h - 自动重连 TCP 客户端
 *
 * 特性:
 * - 异步 IO (基于 Asio)
 * - 自动重连 (指数退避策略)
 * - 长度前缀协议支持
 * - 线程安全的消息发送
 */
#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <functional>
#include <chrono>
#include "Message.h"

// 命名空间别名，保持与 Standalone Asio 代码兼容
namespace asio = boost::asio;

namespace asioclient {

/**
 * 重连配置
 */
struct ReconnectConfig {
    bool enabled = true;                                  // 是否启用自动重连
    std::chrono::milliseconds initialDelay{1000};         // 初始重连间隔 (1秒)
    std::chrono::milliseconds maxDelay{30000};            // 最大重连间隔 (30秒)
    double backoffMultiplier = 2.0;                       // 退避因子
    int maxRetries = -1;                                  // 最大重试次数 (-1 = 无限)
};

/**
 * 客户端状态
 */
enum class ClientState {
    Disconnected,    // 未连接
    Connecting,      // 连接中
    Connected,       // 已连接
    Reconnecting     // 重连中
};

/**
 * TCP 客户端类
 */
class TcpClient : public std::enable_shared_from_this<TcpClient> {
public:
    // 回调类型定义
    using ConnectedCallback = std::function<void()>;
    using DisconnectedCallback = std::function<void()>;
    using MessageCallback = std::function<void(const Message&)>;
    using ErrorCallback = std::function<void(const boost::system::error_code&)>;

    /**
     * 构造函数
     * @param ioContext Asio IO 上下文
     */
    explicit TcpClient(asio::io_context& ioContext);

    ~TcpClient();

    // 禁止拷贝
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    /**
     * 连接到服务器
     * @param host 主机地址
     * @param port 端口号
     */
    void connect(const std::string& host, uint16_t port);

    /**
     * 断开连接
     */
    void disconnect();

    /**
     * 发送消息
     * @param message 要发送的消息
     */
    void send(const Message& message);

    /**
     * 发送字符串消息
     * @param data 要发送的字符串
     */
    void send(const std::string& data);

    // 状态查询
    ClientState state() const { return state_; }
    bool isConnected() const { return state_ == ClientState::Connected; }

    // 重连配置
    void setReconnectConfig(const ReconnectConfig& config) { reconnectConfig_ = config; }
    const ReconnectConfig& reconnectConfig() const { return reconnectConfig_; }

    // 回调设置
    void setOnConnected(ConnectedCallback cb) { onConnected_ = std::move(cb); }
    void setOnDisconnected(DisconnectedCallback cb) { onDisconnected_ = std::move(cb); }
    void setOnMessage(MessageCallback cb) { onMessage_ = std::move(cb); }
    void setOnError(ErrorCallback cb) { onError_ = std::move(cb); }

private:
    // 内部方法
    void doConnect();
    void doResolve();
    void doReadHeader();
    void doReadBody(uint32_t bodyLen);
    void doWrite();
    void doReconnect();

    void handleConnect(const boost::system::error_code& ec);
    void handleDisconnect();
    void resetReconnectState();
    std::chrono::milliseconds calculateReconnectDelay();

    // 组件
    asio::io_context& ioContext_;
    asio::ip::tcp::socket socket_;
    asio::ip::tcp::resolver resolver_;
    asio::steady_timer reconnectTimer_;

    // 连接信息
    std::string host_;
    uint16_t port_;

    // 读写缓冲区
    std::array<char, HEADER_SIZE> headerBuffer_;
    std::vector<char> bodyBuffer_;
    std::queue<std::vector<char>> writeQueue_;
    std::mutex writeMutex_;

    // 状态
    std::atomic<ClientState> state_{ClientState::Disconnected};
    ReconnectConfig reconnectConfig_;
    int reconnectAttempts_{0};
    std::atomic<bool> userDisconnect_{false};

    // 回调
    ConnectedCallback onConnected_;
    DisconnectedCallback onDisconnected_;
    MessageCallback onMessage_;
    ErrorCallback onError_;
};

// 便捷类型定义
using TcpClientPtr = std::shared_ptr<TcpClient>;

/**
 * 创建客户端实例
 */
inline TcpClientPtr createClient(asio::io_context& ioContext) {
    return std::make_shared<TcpClient>(ioContext);
}

} // namespace asioclient
