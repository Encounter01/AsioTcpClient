/**
 * main.cpp - TCP 客户端使用示例
 *
 * 演示如何使用自动重连 TCP 客户端
 */

#include <iostream>
#include <thread>
#include <chrono>
#include "TcpClient.h"

using namespace asioclient;

int main(int argc, char* argv[]) {
    // 默认连接参数
    std::string host = "127.0.0.1";
    uint16_t port = 8888;

    // 命令行参数
    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = static_cast<uint16_t>(std::atoi(argv[2]));
    }

    std::cout << "=== Asio TCP Client Example ===" << std::endl;
    std::cout << "Connecting to " << host << ":" << port << std::endl;

    try {
        // 创建 IO 上下文
        asio::io_context ioContext;

        // 创建客户端
        auto client = createClient(ioContext);

        // 配置重连策略
        ReconnectConfig config;
        config.enabled = true;
        config.initialDelay = std::chrono::milliseconds(1000);  // 1秒
        config.maxDelay = std::chrono::milliseconds(30000);     // 30秒
        config.backoffMultiplier = 2.0;
        config.maxRetries = -1;  // 无限重试
        client->setReconnectConfig(config);

        // 设置回调
        client->setOnConnected([&client]() {
            std::cout << "[Connected] Successfully connected to server!" << std::endl;

            // 连接成功后发送测试消息
            client->send("Hello, Server!");
        });

        client->setOnDisconnected([]() {
            std::cout << "[Disconnected] Connection lost, will try to reconnect..." << std::endl;
        });

        client->setOnMessage([](const Message& msg) {
            std::cout << "[Message] Received: " << msg.bodyAsString() << std::endl;
        });

        client->setOnError([](const std::error_code& ec) {
            std::cout << "[Error] " << ec.message() << std::endl;
        });

        // 发起连接
        client->connect(host, port);

        // 在单独的线程运行 IO 上下文
        std::thread ioThread([&ioContext]() {
            // 使用 work guard 防止 io_context 在没有任务时退出
            auto workGuard = asio::make_work_guard(ioContext);
            ioContext.run();
        });

        // 主线程处理用户输入
        std::cout << "\nCommands:" << std::endl;
        std::cout << "  send <message>  - Send a message" << std::endl;
        std::cout << "  status          - Show connection status" << std::endl;
        std::cout << "  quit            - Exit the program" << std::endl;
        std::cout << std::endl;

        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) {
                continue;
            }

            if (line == "quit" || line == "exit") {
                break;
            }

            if (line == "status") {
                switch (client->state()) {
                    case ClientState::Disconnected:
                        std::cout << "Status: Disconnected" << std::endl;
                        break;
                    case ClientState::Connecting:
                        std::cout << "Status: Connecting..." << std::endl;
                        break;
                    case ClientState::Connected:
                        std::cout << "Status: Connected" << std::endl;
                        break;
                    case ClientState::Reconnecting:
                        std::cout << "Status: Reconnecting..." << std::endl;
                        break;
                }
                continue;
            }

            if (line.substr(0, 5) == "send ") {
                std::string message = line.substr(5);
                if (!message.empty()) {
                    if (client->isConnected()) {
                        client->send(message);
                        std::cout << "Message sent: " << message << std::endl;
                    } else {
                        std::cout << "Not connected. Message queued." << std::endl;
                        client->send(message);
                    }
                }
                continue;
            }

            // 直接发送输入的内容
            if (client->isConnected()) {
                client->send(line);
            } else {
                std::cout << "Not connected." << std::endl;
            }
        }

        // 清理
        std::cout << "Shutting down..." << std::endl;
        client->disconnect();
        ioContext.stop();

        if (ioThread.joinable()) {
            ioThread.join();
        }

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Goodbye!" << std::endl;
    return 0;
}
