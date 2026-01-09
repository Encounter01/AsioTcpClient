/**
 * TcpClient.cpp - 自动重连 TCP 客户端实现
 */

#include "TcpClient.h"
#include <iostream>
#include <algorithm>

namespace asioclient {

TcpClient::TcpClient(asio::io_context& ioContext)
    : ioContext_(ioContext)
    , socket_(ioContext)
    , resolver_(ioContext)
    , reconnectTimer_(ioContext)
    , port_(0)
{
}

TcpClient::~TcpClient() {
    disconnect();
}

void TcpClient::connect(const std::string& host, uint16_t port) {
    host_ = host;
    port_ = port;
    userDisconnect_ = false;
    resetReconnectState();

    state_ = ClientState::Connecting;
    doResolve();
}

void TcpClient::disconnect() {
    userDisconnect_ = true;
    reconnectTimer_.cancel();

    boost::system::error_code ec;
    socket_.close(ec);

    state_ = ClientState::Disconnected;
}

void TcpClient::send(const Message& message) {
    auto encoded = message.encode();

    asio::post(ioContext_, [this, data = std::move(encoded)]() mutable {
        bool writeInProgress;
        {
            std::lock_guard<std::mutex> lock(writeMutex_);
            writeInProgress = !writeQueue_.empty();
            writeQueue_.push(std::move(data));
        }

        if (!writeInProgress && isConnected()) {
            doWrite();
        }
    });
}

void TcpClient::send(const std::string& data) {
    send(Message(data));
}

void TcpClient::doResolve() {
    auto self = shared_from_this();

    resolver_.async_resolve(
        host_,
        std::to_string(port_),
        [this, self](const boost::system::error_code& ec, asio::ip::tcp::resolver::results_type results) {
            if (ec) {
                if (onError_) {
                    onError_(ec);
                }
                handleDisconnect();
                return;
            }

            doConnect();
        }
    );
}

void TcpClient::doConnect() {
    auto self = shared_from_this();

    auto endpoints = resolver_.resolve(host_, std::to_string(port_));

    asio::async_connect(
        socket_,
        endpoints,
        [this, self](const boost::system::error_code& ec, const asio::ip::tcp::endpoint&) {
            handleConnect(ec);
        }
    );
}

void TcpClient::handleConnect(const boost::system::error_code& ec) {
    if (ec) {
        if (onError_) {
            onError_(ec);
        }
        handleDisconnect();
        return;
    }

    // 连接成功
    state_ = ClientState::Connected;
    resetReconnectState();

    // 设置 TCP 选项
    asio::ip::tcp::no_delay noDelay(true);
    socket_.set_option(noDelay);

    // 触发连接回调
    if (onConnected_) {
        onConnected_();
    }

    // 开始读取数据
    doReadHeader();

    // 发送队列中的数据
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        if (!writeQueue_.empty()) {
            doWrite();
        }
    }
}

void TcpClient::handleDisconnect() {
    if (state_ == ClientState::Disconnected) {
        return;
    }

    // 关闭 socket
    boost::system::error_code ec;
    socket_.close(ec);

    // 如果是用户主动断开，不触发重连
    if (userDisconnect_) {
        state_ = ClientState::Disconnected;
        if (onDisconnected_) {
            onDisconnected_();
        }
        return;
    }

    // 触发断开回调
    if (state_ == ClientState::Connected && onDisconnected_) {
        onDisconnected_();
    }

    // 尝试重连
    if (reconnectConfig_.enabled) {
        doReconnect();
    } else {
        state_ = ClientState::Disconnected;
    }
}

void TcpClient::doReconnect() {
    // 检查是否超过最大重试次数
    if (reconnectConfig_.maxRetries >= 0 &&
        reconnectAttempts_ >= reconnectConfig_.maxRetries) {
        state_ = ClientState::Disconnected;
        return;
    }

    state_ = ClientState::Reconnecting;
    auto delay = calculateReconnectDelay();
    reconnectAttempts_++;

    auto self = shared_from_this();
    reconnectTimer_.expires_after(delay);
    reconnectTimer_.async_wait([this, self](const boost::system::error_code& ec) {
        if (ec) {
            // 定时器被取消
            return;
        }

        if (userDisconnect_) {
            return;
        }

        // 重新创建 socket
        socket_ = asio::ip::tcp::socket(ioContext_);
        state_ = ClientState::Connecting;
        doResolve();
    });
}

std::chrono::milliseconds TcpClient::calculateReconnectDelay() {
    if (reconnectAttempts_ == 0) {
        return reconnectConfig_.initialDelay;
    }

    // 指数退避计算
    double multiplier = std::pow(reconnectConfig_.backoffMultiplier, reconnectAttempts_);
    auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
        reconnectConfig_.initialDelay * multiplier
    );

    // 限制最大延迟
    return std::min(delay, reconnectConfig_.maxDelay);
}

void TcpClient::resetReconnectState() {
    reconnectAttempts_ = 0;
}

void TcpClient::doReadHeader() {
    auto self = shared_from_this();

    asio::async_read(
        socket_,
        asio::buffer(headerBuffer_),
        [this, self](const boost::system::error_code& ec, std::size_t /*length*/) {
            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    if (onError_) {
                        onError_(ec);
                    }
                    handleDisconnect();
                }
                return;
            }

            // 解析消息长度
            uint32_t bodyLen = Message::decodeHeader(headerBuffer_.data());

            // 验证消息长度
            if (!Message::isValidLength(bodyLen)) {
                boost::system::error_code invalidEc =
                    boost::system::errc::make_error_code(boost::system::errc::message_size);
                if (onError_) {
                    onError_(invalidEc);
                }
                handleDisconnect();
                return;
            }

            // 读取消息体
            if (bodyLen > 0) {
                doReadBody(bodyLen);
            } else {
                // 空消息
                if (onMessage_) {
                    onMessage_(Message());
                }
                doReadHeader();
            }
        }
    );
}

void TcpClient::doReadBody(uint32_t bodyLen) {
    auto self = shared_from_this();

    bodyBuffer_.resize(bodyLen);

    asio::async_read(
        socket_,
        asio::buffer(bodyBuffer_),
        [this, self](const boost::system::error_code& ec, std::size_t /*length*/) {
            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    if (onError_) {
                        onError_(ec);
                    }
                    handleDisconnect();
                }
                return;
            }

            // 触发消息回调
            if (onMessage_) {
                Message msg(std::move(bodyBuffer_));
                onMessage_(msg);
            }

            // 继续读取下一条消息
            doReadHeader();
        }
    );
}

void TcpClient::doWrite() {
    auto self = shared_from_this();

    std::vector<char> data;
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        if (writeQueue_.empty()) {
            return;
        }
        data = std::move(writeQueue_.front());
    }

    asio::async_write(
        socket_,
        asio::buffer(data),
        [this, self](const boost::system::error_code& ec, std::size_t /*length*/) {
            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    if (onError_) {
                        onError_(ec);
                    }
                    handleDisconnect();
                }
                return;
            }

            // 移除已发送的消息
            {
                std::lock_guard<std::mutex> lock(writeMutex_);
                writeQueue_.pop();
            }

            // 继续发送队列中的其他消息
            {
                std::lock_guard<std::mutex> lock(writeMutex_);
                if (!writeQueue_.empty() && isConnected()) {
                    doWrite();
                }
            }
        }
    );
}

} // namespace asioclient
