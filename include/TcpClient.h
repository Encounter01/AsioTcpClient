/**
 * @file TcpClient.h
 * @brief Async TCP client with auto-reconnect support
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

namespace asio = boost::asio;

namespace asioclient {

/**
 * @struct ReconnectConfig
 * @brief Auto-reconnect configuration with exponential backoff
 */
struct ReconnectConfig {
    bool enabled = true;
    std::chrono::milliseconds initialDelay{1000};  // Initial delay: 1s
    std::chrono::milliseconds maxDelay{30000};     // Max delay: 30s
    double backoffMultiplier = 2.0;                // Backoff multiplier
    int maxRetries = -1;                           // Max retries (-1 = infinite)
};

/**
 * @enum ClientState
 * @brief Client connection state
 */
enum class ClientState {
    Disconnected,  // Not connected
    Connecting,    // Connecting (DNS resolution or TCP handshake)
    Connected,     // Connected and ready
    Reconnecting   // Waiting for reconnect timer
};

/**
 * @class TcpClient
 * @brief Async TCP client with Proactor pattern
 */
class TcpClient : public std::enable_shared_from_this<TcpClient> {
public:
    // Callback type definitions
    using ConnectedCallback = std::function<void()>;
    using DisconnectedCallback = std::function<void()>;
    using MessageCallback = std::function<void(const Message&)>;
    using ErrorCallback = std::function<void(const boost::system::error_code&)>;

    explicit TcpClient(asio::io_context& ioContext);
    ~TcpClient();

    // Non-copyable
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    // Connection management
    void connect(const std::string& host, uint16_t port);
    void disconnect();

    // Message sending (thread-safe)
    void send(const Message& message);
    void send(const std::string& data);

    // State query
    ClientState state() const { return state_; }
    bool isConnected() const { return state_ == ClientState::Connected; }

    // Reconnect configuration
    void setReconnectConfig(const ReconnectConfig& config) { reconnectConfig_ = config; }
    const ReconnectConfig& reconnectConfig() const { return reconnectConfig_; }

    // Callback setters
    void setOnConnected(ConnectedCallback cb) { onConnected_ = std::move(cb); }
    void setOnDisconnected(DisconnectedCallback cb) { onDisconnected_ = std::move(cb); }
    void setOnMessage(MessageCallback cb) { onMessage_ = std::move(cb); }
    void setOnError(ErrorCallback cb) { onError_ = std::move(cb); }

private:
    // Async operation methods
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

    // Core components
    asio::io_context& ioContext_;
    asio::ip::tcp::socket socket_;
    asio::ip::tcp::resolver resolver_;
    asio::steady_timer reconnectTimer_;

    // Connection info (for reconnect)
    std::string host_;
    uint16_t port_;

    // Buffers
    std::array<char, HEADER_SIZE> headerBuffer_;
    std::vector<char> bodyBuffer_;
    std::queue<std::vector<char>> writeQueue_;
    std::mutex writeMutex_;

    // State management
    std::atomic<ClientState> state_{ClientState::Disconnected};
    ReconnectConfig reconnectConfig_;
    int reconnectAttempts_{0};
    std::atomic<bool> userDisconnect_{false};

    // Callbacks
    ConnectedCallback onConnected_;
    DisconnectedCallback onDisconnected_;
    MessageCallback onMessage_;
    ErrorCallback onError_;
};

using TcpClientPtr = std::shared_ptr<TcpClient>;

/**
 * @brief Factory function to create TcpClient instance
 * @param ioContext IO context
 * @return Shared pointer to TcpClient
 */
inline TcpClientPtr createClient(asio::io_context& ioContext) {
    return std::make_shared<TcpClient>(ioContext);
}

} // namespace asioclient
