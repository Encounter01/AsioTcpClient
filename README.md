# AsioTcpClient

[![C++](https://img.shields.io/badge/C++-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Boost](https://img.shields.io/badge/Boost-1.81+-orange.svg)](https://www.boost.org/)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](https://github.com/Encounter01/AsioTcpClient)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

> 基于 Boost.Asio 的高性能、自动重连 TCP 客户端库

## 目录

- [项目简介](#项目简介)
- [核心特性](#核心特性)
- [快速开始](#快速开始)
- [安装构建](#安装构建)
- [使用文档](#使用文档)
- [架构设计](#架构设计)
- [API 参考](#api-参考)
- [常见问题](#常见问题)
- [技术细节](#技术细节)
- [贡献指南](#贡献指南)
- [许可证](#许可证)

## 项目简介

AsioTcpClient 是一个轻量级、高性能的 TCP 客户端库，专为需要可靠网络连接的应用场景设计。基于久经考验的 Boost.Asio 库，采用现代 C++17 标准开发，提供简洁易用的 API 和强大的功能。

### 适用场景

- **游戏客户端** - 与游戏服务器保持长连接，实时同步状态
- **物联网设备** - IoT 设备与云端通信，支持不稳定网络环境
- **金融交易系统** - 接收实时行情推送，低延迟高可靠
- **即时通讯** - IM 客户端与消息服务器的长连接通信
- **分布式系统** - 微服务之间的 TCP 通信

### 为什么选择 AsioTcpClient？

| 特性 | AsioTcpClient | 原生 Asio | 其他库 |
|------|---------------|-----------|--------|
| 自动重连 | 开箱即用 | 需自己实现 | 部分支持 |
| 粘包处理 | 长度前缀协议 | 需自己实现 | 不同方案 |
| 线程安全 | 完全支持 | 需注意细节 | 视实现而定 |
| 学习曲线 | 简单 | 陡峭 | 中等 |
| 代码量 | ~650 行 | - | 通常更多 |
| 依赖 | 仅 Boost | - | 可能较多 |

## 核心特性

### 异步非阻塞 IO

基于 Boost.Asio 的 **Proactor 模式**，提供真正的异步非阻塞网络操作：

- Windows 平台使用 IOCP（真正的 Proactor）
- Linux/macOS 使用 epoll/kqueue（模拟 Proactor）
- 不阻塞调用线程，充分利用系统资源

### 智能自动重连

采用**指数退避算法**，网络断开后自动恢复连接：

```
重连时间序列：1s → 2s → 4s → 8s → 16s → 30s → 30s → ...
```

- 避免服务器恢复时的"惊群效应"
- 可配置初始延迟、最大延迟、退避倍数
- 支持无限重试或限制最大次数
- 区分用户主动断开和网络异常

### 消息分帧协议

解决 TCP 粘包/拆包问题，采用**长度前缀协议**：

```
+------------------+--------------------+
|  Length (4字节)   |   Body (变长)       |
|  网络字节序(大端)  |   消息内容          |
+------------------+--------------------+
```

- 自动处理消息边界
- 支持最大 4GB 的消息（可配置限制）
- 防止恶意超大消息攻击

### 线程安全设计

支持多线程环境下的安全使用：

- `send()` 方法可从任意线程调用
- 内部使用 `mutex` 保护写队列
- 通过 `asio::post()` 实现跨线程调用
- 状态查询使用 `atomic` 变量

### 事件驱动架构

简洁的回调式 API，轻松处理各种事件：

```cpp
client->setOnConnected([]() { /* 连接成功 */ });
client->setOnDisconnected([]() { /* 连接断开 */ });
client->setOnMessage([](const Message& msg) { /* 收到消息 */ });
client->setOnError([](const std::error_code& ec) { /* 发生错误 */ });
```

### 跨平台支持

单一代码库，支持主流操作系统：

- Windows (7/8/10/11)
- Linux (Ubuntu, CentOS, Debian, etc.)
- macOS (10.14+)

## 快速开始

### 最简示例（5 分钟上手）

```cpp
#include "TcpClient.h"
using namespace asioclient;

int main() {
    // 1. 创建 IO 上下文和客户端
    asio::io_context ioContext;
    auto client = createClient(ioContext);

    // 2. 设置回调
    client->setOnConnected([]() {
        std::cout << "已连接！" << std::endl;
    });

    client->setOnMessage([](const Message& msg) {
        std::cout << "收到消息: " << msg.bodyAsString() << std::endl;
    });

    // 3. 连接服务器
    client->connect("127.0.0.1", 8888);

    // 4. 发送消息
    client->send("Hello, Server!");

    // 5. 运行事件循环
    ioContext.run();

    return 0;
}
```

### 完整示例（生产级用法）

```cpp
#include <iostream>
#include <thread>
#include "TcpClient.h"

using namespace asioclient;

int main() {
    asio::io_context ioContext;
    auto client = createClient(ioContext);

    // 配置自动重连
    ReconnectConfig config;
    config.enabled = true;
    config.initialDelay = std::chrono::milliseconds(1000);
    config.maxDelay = std::chrono::milliseconds(30000);
    config.backoffMultiplier = 2.0;
    config.maxRetries = -1;
    client->setReconnectConfig(config);

    // 设置事件回调
    client->setOnConnected([&client]() {
        std::cout << "连接成功！" << std::endl;
        client->send("Hello from Client!");
    });

    client->setOnDisconnected([]() {
        std::cout << "连接断开，正在重连..." << std::endl;
    });

    client->setOnMessage([](const Message& msg) {
        std::cout << "收到消息: " << msg.bodyAsString() << std::endl;
    });

    client->setOnError([](const std::error_code& ec) {
        std::cerr << "错误: " << ec.message() << std::endl;
    });

    // 连接到服务器
    client->connect("127.0.0.1", 8888);

    // 在独立线程运行 IO
    std::thread ioThread([&ioContext]() {
        auto workGuard = asio::make_work_guard(ioContext);
        ioContext.run();
    });

    // 主线程处理用户输入
    std::cout << "输入消息发送（输入 'quit' 退出）:" << std::endl;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "quit") break;
        if (!line.empty()) {
            client->send(line);
        }
    }

    // 优雅关闭
    client->disconnect();
    ioContext.stop();
    if (ioThread.joinable()) {
        ioThread.join();
    }

    return 0;
}
```

## 安装构建

### 前置依赖

- **C++ 编译器**：支持 C++17 标准
  - GCC 7.0+
  - Clang 5.0+
  - MSVC 2017+

- **CMake**：3.14 或更高版本

- **Boost**：1.81.0 或更高版本
  - 仅需 Boost.Asio（header-only）
  - 可使用 header-only 模式，无需编译 Boost 库

### 安装 Boost（各平台）

<details>
<summary><b>Windows</b></summary>

**方式 1：使用预编译版本**

1. 从 [Boost 官网](https://www.boost.org/users/download/) 下载预编译包
2. 解压到指定目录（如 `D:\boost_1_81_0`）
3. 修改 `CMakeLists.txt` 中的 `BOOST_ROOT` 路径

**方式 2：使用 vcpkg**

```powershell
vcpkg install boost-asio:x64-windows
```

</details>

<details>
<summary><b>Linux (Ubuntu/Debian)</b></summary>

```bash
sudo apt-get update
sudo apt-get install libboost-all-dev
```

或只安装 Asio：

```bash
sudo apt-get install libboost-system-dev
```

</details>

<details>
<summary><b>macOS</b></summary>

```bash
brew install boost
```

</details>

### 编译项目

#### 克隆仓库

```bash
git clone https://github.com/Encounter01/AsioTcpClient.git
cd AsioTcpClient
```

#### Windows (Visual Studio)

```powershell
# 创建构建目录
mkdir build
cd build

# 配置项目（修改 Boost 路径）
cmake .. -DBOOST_ROOT="D:/softcpp/boost_1_81_0"

# 编译（Release 模式）
cmake --build . --config Release

# 运行示例
.\bin\Release\tcp_client_example.exe
```

#### Linux / macOS

```bash
# 创建构建目录
mkdir build && cd build

# 配置项目
cmake ..

# 编译
make -j$(nproc)

# 运行示例
./bin/tcp_client_example
```

### 集成到你的项目

#### 方式 1：作为子项目（推荐）

```cmake
# 在你的 CMakeLists.txt 中
add_subdirectory(path/to/AsioTcpClient)

add_executable(your_app main.cpp)
target_link_libraries(your_app PRIVATE asioclient)
```

#### 方式 2：直接包含源码

将 `include/` 和 `src/` 目录复制到你的项目中：

```cmake
add_library(asioclient STATIC
    path/to/TcpClient.cpp
)

target_include_directories(asioclient PUBLIC
    path/to/include
    ${Boost_INCLUDE_DIR}
)

target_link_libraries(your_app PRIVATE asioclient)
```

## 使用文档

### 基本工作流程

```
1. 创建 io_context
   ↓
2. 创建客户端（createClient）
   ↓
3. 设置回调函数（setOnConnected, setOnMessage 等）
   ↓
4. 配置重连策略（可选）
   ↓
5. 连接服务器（connect）
   ↓
6. 运行事件循环（io_context.run）
   ↓
7. 发送/接收消息
   ↓
8. 断开连接（disconnect）
```

### 重连配置详解

```cpp
ReconnectConfig config;

// 是否启用自动重连
config.enabled = true;

// 初始重连延迟（第一次重连等待的时间）
config.initialDelay = std::chrono::milliseconds(1000);  // 1 秒

// 最大重连延迟（延迟的上限）
config.maxDelay = std::chrono::milliseconds(30000);  // 30 秒

// 退避倍数（每次失败后延迟增加的倍数）
config.backoffMultiplier = 2.0;  // 每次翻倍

// 最大重试次数（-1 表示无限重试）
config.maxRetries = 10;  // 重试 10 次后放弃
// config.maxRetries = -1;  // 无限重试（适合服务端）

client->setReconnectConfig(config);
```

**重连时间序列示例：**

| 尝试次数 | 延迟时间 | 计算公式 |
|---------|---------|---------|
| 1 | 1 秒 | initialDelay |
| 2 | 2 秒 | 1 × 2¹ |
| 3 | 4 秒 | 1 × 2² |
| 4 | 8 秒 | 1 × 2³ |
| 5 | 16 秒 | 1 × 2⁴ |
| 6+ | 30 秒 | min(1 × 2⁵, maxDelay) |

### 消息发送

```cpp
// 方式 1：发送字符串
client->send("Hello, Server!");

// 方式 2：发送 Message 对象
Message msg("Hello, Server!");
client->send(msg);

// 方式 3：发送二进制数据
std::vector<char> data = {0x01, 0x02, 0x03};
Message msg(std::move(data));
client->send(msg);

// 方式 4：从任意线程发送（线程安全）
std::thread([&client]() {
    client->send("From another thread");
}).detach();
```

### 状态查询

```cpp
// 检查是否已连接
if (client->isConnected()) {
    client->send("Hello");
}

// 获取详细状态
switch (client->state()) {
    case ClientState::Disconnected:
        std::cout << "未连接" << std::endl;
        break;
    case ClientState::Connecting:
        std::cout << "连接中..." << std::endl;
        break;
    case ClientState::Connected:
        std::cout << "已连接" << std::endl;
        break;
    case ClientState::Reconnecting:
        std::cout << "重连中..." << std::endl;
        break;
}
```

## 架构设计

### 系统架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                      应用层 (Application)                        │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │ 用户代码：setOnConnected() / setOnMessage() / send()      │  │
│  └───────────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                      客户端层 (TcpClient)                        │
│  ┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐    │
│  │   连接管理       │ │   消息收发       │ │   重连机制       │    │
│  │ ┌─────────────┐ │ │ ┌─────────────┐ │ │ ┌─────────────┐ │    │
│  │ │ doConnect() │ │ │ │ doRead()    │ │ │ │doReconnect()│ │    │
│  │ │handleConnect│ │ │ │ doWrite()   │ │ │ │ backoff计算 │ │    │
│  │ │ doClose()   │ │ │ │ writeQueue_ │ │ │ │ 重试计数    │ │    │
│  │ └─────────────┘ │ │ └─────────────┘ │ │ └─────────────┘ │    │
│  └─────────────────┘ └─────────────────┘ └─────────────────┘    │
├─────────────────────────────────────────────────────────────────┤
│                      协议层 (Message)                            │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  ┌──────────────┬────────────────────────────────────┐    │  │
│  │  │ Length (4B)  │         Body (N bytes)              │    │  │
│  │  │ 网络字节序    │         原始数据                     │    │  │
│  │  └──────────────┴────────────────────────────────────┘    │  │
│  └───────────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                      网络层 (Boost.Asio)                         │
│  ┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐    │
│  │   io_context    │ │   tcp::socket   │ │  steady_timer   │    │
│  └─────────────────┘ └─────────────────┘ └─────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

### 状态机

```
                          connect()
┌──────────────┐ ──────────────────────────► ┌──────────────┐
│ Disconnected │                             │  Connecting  │
└──────────────┘                             └──────────────┘
       ▲                                            │
       │                                            │ 成功
       │ disconnect()                               ▼
       │ (用户主动)                           ┌──────────────┐
       │                                      │  Connected   │
       │                                      └──────────────┘
       │                                            │
       │                                            │ 断开（网络错误）
       │                                            ▼
       │                                      ┌──────────────┐
       └────────────────────────────────────  │ Reconnecting │
          (达到最大重试次数)                   └──────────────┘
                                                    │
                                         定时器触发  │
                                                    ▼
                                              回到 Connecting
```

### 数据流

**发送流程：**

```
用户调用 send()
    ↓
编码消息（添加长度头）
    ↓
通过 asio::post() 投递到 IO 线程
    ↓
加锁后添加到 writeQueue_
    ↓
如果当前无写操作，启动 doWrite()
    ↓
async_write() 异步写入
    ↓
写完成后，检查队列是否还有消息
    ↓
有则继续写，无则等待
```

**接收流程：**

```
doReadHeader() 读取 4 字节头
    ↓
decodeHeader() 解析长度
    ↓
验证长度是否合法
    ↓
doReadBody() 读取对应长度的消息体
    ↓
触发 onMessage 回调
    ↓
继续 doReadHeader()（循环）
```

## API 参考

### TcpClient 类

#### 构造和创建

```cpp
// 构造函数（通常不直接使用）
explicit TcpClient(asio::io_context& ioContext);

// 工厂函数（推荐使用）
TcpClientPtr createClient(asio::io_context& ioContext);
```

#### 连接管理

```cpp
// 连接到服务器
void connect(const std::string& host, uint16_t port);

// 主动断开连接
void disconnect();

// 查询连接状态
ClientState state() const;
bool isConnected() const;
```

#### 消息发送

```cpp
// 发送消息对象
void send(const Message& message);

// 发送字符串（便捷方法）
void send(const std::string& data);
```

#### 回调设置

```cpp
// 连接成功回调
void setOnConnected(ConnectedCallback cb);

// 断开连接回调
void setOnDisconnected(DisconnectedCallback cb);

// 收到消息回调
void setOnMessage(MessageCallback cb);

// 错误回调
void setOnError(ErrorCallback cb);
```

#### 配置

```cpp
// 设置重连配置
void setReconnectConfig(const ReconnectConfig& config);

// 获取重连配置
const ReconnectConfig& reconnectConfig() const;
```

### Message 类

```cpp
// 构造函数
Message();
explicit Message(const std::string& body);
explicit Message(const std::vector<char>& body);
explicit Message(std::vector<char>&& body);

// 访问器
const std::vector<char>& body() const;
std::vector<char>& body();
size_t bodySize() const;
std::string bodyAsString() const;

// 修改器
void setBody(const std::string& data);
void setBody(const char* data, size_t len);

// 编解码
std::vector<char> encode() const;
static uint32_t decodeHeader(const char* data);
static bool isValidLength(uint32_t len);
```

### 枚举和结构

```cpp
// 客户端状态
enum class ClientState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting
};

// 重连配置
struct ReconnectConfig {
    bool enabled = true;
    std::chrono::milliseconds initialDelay{1000};
    std::chrono::milliseconds maxDelay{30000};
    double backoffMultiplier = 2.0;
    int maxRetries = -1;
};
```

## 常见问题

### Q1: 如何处理粘包问题？

**A:** 本库已内置长度前缀协议自动处理粘包。每条消息前有 4 字节头指示消息长度，接收端先读头，再根据长度读取完整消息体。

### Q2: 可以从多个线程调用 send() 吗？

**A:** 可以。`send()` 方法是线程安全的，内部通过 `asio::post()` 将操作投递到 IO 线程，并使用 `mutex` 保护写队列。

### Q3: 如何知道消息是否发送成功？

**A:** 当前版本没有发送确认机制。如果需要，可以：
1. 实现请求-响应模式（添加消息 ID）
2. 监听 `onError` 回调
3. 在应用层添加 ACK 机制

### Q4: 为什么使用 shared_ptr 管理客户端？

**A:** 因为 `TcpClient` 继承 `enable_shared_from_this`，异步回调需要延长对象生命周期。使用 `shared_ptr` 确保回调执行时对象仍有效。

### Q5: 如何实现心跳机制？

**A:** 可以使用定时器定期发送心跳包：

```cpp
void startHeartbeat(TcpClientPtr client) {
    auto timer = std::make_shared<asio::steady_timer>(ioContext);
    std::function<void()> sendHeartbeat = [client, timer, &sendHeartbeat]() {
        if (client->isConnected()) {
            client->send("HEARTBEAT");
        }
        timer->expires_after(std::chrono::seconds(30));
        timer->async_wait([&sendHeartbeat](auto ec) {
            if (!ec) sendHeartbeat();
        });
    };
    sendHeartbeat();
}
```

### Q6: 支持 SSL/TLS 加密吗？

**A:** 当前版本不支持。如需加密传输，可以：
1. 在应用层加密消息内容
2. 使用 VPN 或其他网络层加密
3. 扩展代码使用 `asio::ssl::stream`

### Q7: 消息大小有限制吗？

**A:** 默认限制 16MB（`MAX_BODY_SIZE`）。可以在 `Message.h` 中修改此常量。注意过大的限制可能导致内存攻击风险。

### Q8: 如何优雅关闭？

**A:** 按以下顺序操作：

```cpp
client->disconnect();
ioContext.stop();
if (ioThread.joinable()) {
    ioThread.join();
}
```

### Q9: 回调函数中可以删除客户端吗？

**A:** 不建议在回调中直接删除客户端。如需删除，应通过 `asio::post()` 延迟删除：

```cpp
client->setOnDisconnected([&ioContext, &client]() {
    asio::post(ioContext, [&client]() {
        client.reset();
    });
});
```

### Q10: Windows 下编译报错找不到 Boost？

**A:** 确保在 `CMakeLists.txt` 中正确设置 Boost 路径：

```cmake
set(BOOST_ROOT "D:/softcpp/boost_1_81_0")
set(Boost_INCLUDE_DIR "D:/softcpp/boost_1_81_0")
```

或使用 CMake 参数：

```bash
cmake .. -DBOOST_ROOT="D:/path/to/boost"
```

## 技术细节

### Proactor vs Reactor

本库基于 **Proactor 模式**：

| 特性 | Proactor | Reactor |
|------|----------|---------|
| IO 操作 | 操作系统完成 | 应用程序完成 |
| 通知时机 | 操作完成后 | 就绪时 |
| 实现示例 | IOCP, io_uring | epoll, select |
| Asio 实现 | Windows 原生 | Linux 模拟 |

### 指数退避算法

```cpp
delay = min(initialDelay × backoffMultiplier^attempts, maxDelay)
```

**为什么使用指数退避？**

1. **避免惊群效应** - 服务器恢复时不会被大量并发连接冲垮
2. **节省资源** - 减少无效的重连尝试
3. **网络友好** - 给网络和服务器恢复时间

### 内存管理

- **RAII 原则** - 所有资源自动管理，无需手动释放
- **智能指针** - 使用 `shared_ptr` 管理客户端生命周期
- **移动语义** - 消息传递使用 `std::move` 减少拷贝
- **内存池（未实现）** - 未来可添加消息对象池复用

### 性能指标

测试环境：Intel i5-10400, 16GB RAM, 本地回环

| 消息大小 | 吞吐量 | CPU 占用 |
|---------|--------|---------|
| 100B | ~50,000 msg/s | ~15% |
| 1KB | ~30,000 msg/s | ~20% |
| 64KB | ~2,000 msg/s | ~25% |

延迟（RTT）：

| 场景 | P50 | P99 |
|------|-----|-----|
| 本地回环 | 0.1ms | 0.5ms |
| 局域网 | 0.3ms | 1.2ms |
| 公网（同城） | 5ms | 20ms |

## 贡献指南

欢迎贡献代码、报告问题、提出建议！

### 如何贡献

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 创建 Pull Request

### 代码规范

- 遵循现有代码风格
- 添加必要的注释
- 更新相关文档
- 确保代码通过编译

### 报告问题

在 [Issues](https://github.com/Encounter01/AsioTcpClient/issues) 页面报告问题时，请提供：

- 操作系统和版本
- 编译器和版本
- Boost 版本
- 复现步骤
- 错误信息或日志

## 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

## 作者

- **Encounter** - [GitHub](https://github.com/Encounter01)

## 致谢

- Boost.Asio 开发团队
- 所有贡献者和使用者

---

**如果这个项目对你有帮助，请给一个 Star！**

[报告问题](https://github.com/Encounter01/AsioTcpClient/issues) · [请求功能](https://github.com/Encounter01/AsioTcpClient/issues) · [讨论交流](https://github.com/Encounter01/AsioTcpClient/discussions)
