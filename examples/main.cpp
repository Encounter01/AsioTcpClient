/**
 * @file main.cpp
 * @brief TCP client usage example
 */

#include <iostream>
#include <thread>
#include <chrono>
#include "TcpClient.h"

using namespace asioclient;

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    uint16_t port = 10086;

    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = static_cast<uint16_t>(std::atoi(argv[2]));
    }

    std::cout << "=== Asio TCP Client Example ===" << std::endl;
    std::cout << "Connecting to " << host << ":" << port << std::endl;

    try {
        asio::io_context ioContext;
        auto client = createClient(ioContext);

        // Configure reconnect
        ReconnectConfig config;
        config.enabled = true;
        config.initialDelay = std::chrono::milliseconds(1000);
        config.maxDelay = std::chrono::milliseconds(30000);
        config.backoffMultiplier = 2.0;
        config.maxRetries = -1;
        client->setReconnectConfig(config);

        // Set callbacks
        client->setOnConnected([&client]() {
            std::cout << "[Connected] Successfully connected to server!" << std::endl;
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

        // Connect
        client->connect(host, port);

        // Start IO thread
        std::thread ioThread([&ioContext]() {
            auto workGuard = asio::make_work_guard(ioContext);
            ioContext.run();
        });

        // Command loop
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

            // Default: send input as message
            if (client->isConnected()) {
                client->send(line);
            } else {
                std::cout << "Not connected." << std::endl;
            }
        }

        // Shutdown
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
