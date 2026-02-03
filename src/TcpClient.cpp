/**
 * @file TcpClient.cpp
 * @brief TcpClient implementation
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
        [this, self](const boost::system::error_code& ec,
                     asio::ip::tcp::resolver::results_type results) {
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
        [this, self](const boost::system::error_code& ec,
                     const asio::ip::tcp::endpoint& /*endpoint*/) {
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

    state_ = ClientState::Connected;
    resetReconnectState();

    // Set TCP_NODELAY to disable Nagle's algorithm
    asio::ip::tcp::no_delay noDelay(true);
    socket_.set_option(noDelay);

    if (onConnected_) {
        onConnected_();
    }

    doReadHeader();

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

    boost::system::error_code ec;
    socket_.close(ec);

    if (userDisconnect_) {
        state_ = ClientState::Disconnected;
        if (onDisconnected_) {
            onDisconnected_();
        }
        return;
    }

    if (state_ == ClientState::Connected && onDisconnected_) {
        onDisconnected_();
    }

    if (reconnectConfig_.enabled) {
        doReconnect();
    } else {
        state_ = ClientState::Disconnected;
    }
}

void TcpClient::doReconnect() {
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
            return;
        }

        if (userDisconnect_) {
            return;
        }

        // Recreate socket for reconnection
        socket_ = asio::ip::tcp::socket(ioContext_);
        state_ = ClientState::Connecting;
        doResolve();
    });
}

std::chrono::milliseconds TcpClient::calculateReconnectDelay() {
    if (reconnectAttempts_ == 0) {
        return reconnectConfig_.initialDelay;
    }

    double multiplier = std::pow(reconnectConfig_.backoffMultiplier, reconnectAttempts_);
    auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
        reconnectConfig_.initialDelay * multiplier
    );

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

            uint32_t bodyLen = Message::decodeHeader(headerBuffer_.data());

            // Validate message length
            if (!Message::isValidLength(bodyLen)) {
                boost::system::error_code invalidEc =
                    boost::system::errc::make_error_code(boost::system::errc::message_size);
                if (onError_) {
                    onError_(invalidEc);
                }
                handleDisconnect();
                return;
            }

            if (bodyLen > 0) {
                doReadBody(bodyLen);
            } else {
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

            if (onMessage_) {
                Message msg(std::move(bodyBuffer_));
                onMessage_(msg);
            }

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

            {
                std::lock_guard<std::mutex> lock(writeMutex_);
                writeQueue_.pop();
            }

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
