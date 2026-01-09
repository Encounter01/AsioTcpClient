# AsioTcpClient 项目介绍（STAR 法则）

> 本文档采用 STAR 法则（Situation-Task-Action-Result）描述项目，适用于简历撰写、面试展示等场景。

---

## 项目名称
**AsioTcpClient - 高性能自动重连 TCP 客户端**

## 技术栈
`C++17` `Boost.Asio` `CMake` `跨平台` `异步IO` `网络编程`

---

## S - Situation（情境/背景）

### 项目背景

在分布式系统和实时通信应用开发中，经常需要实现 TCP 客户端与服务器保持长连接。然而，传统的同步阻塞式客户端存在以下痛点：

| 问题 | 影响 |
|------|------|
| **网络不稳定导致断连** | 需要手动处理重连逻辑，代码复杂 |
| **同步阻塞模式** | 无法高效处理并发请求，性能受限 |
| **协议解析繁琐** | TCP 流式传输需要手动处理粘包/拆包 |
| **缺乏统一封装** | 每个项目重复造轮子，维护成本高 |

### 应用场景

- 物联网设备与云端通信
- 游戏客户端与服务器连接
- 金融交易系统行情接收
- 即时通讯应用

---

## T - Task（任务/目标）

### 核心目标

设计并实现一个**高性能、可靠的 TCP 客户端库**，具备以下能力：

1. **自动重连机制**
   - 网络断开后自动尝试重新连接
   - 采用指数退避策略，避免频繁重连导致服务器过载

2. **异步非阻塞 IO**
   - 基于事件驱动模型，支持高并发场景
   - 不阻塞调用线程，提升程序响应性

3. **协议封装**
   - 提供长度前缀协议支持，自动处理 TCP 粘包问题
   - 简洁的消息编解码接口

4. **易用性**
   - 事件回调式 API，降低使用门槛
   - 跨平台支持（Windows/Linux/macOS）

### 技术要求

| 要求 | 指标 |
|------|------|
| 语言标准 | C++17 |
| 依赖管理 | Boost.Asio（Boost 生态系统） |
| 跨平台 | Windows / Linux / macOS |
| 线程安全 | 支持多线程环境下安全调用 |

---

## A - Action（行动/实现）

### 1. 技术选型

| 方面 | 选择 | 理由 |
|------|------|------|
| 网络库 | Boost.Asio | 成熟稳定、跨平台、功能完善、Boost 生态系统支持 |
| 异步模型 | Proactor 模式 | Asio 原生支持，适合 IO 密集型应用 |
| 协议设计 | 长度前缀 (4字节) | 简单可靠、边界清晰、解析高效 |
| 重连策略 | 指数退避 | 业界标准实践，网络友好 |

### 2. 架构设计

```
┌─────────────────────────────────────────┐
│           Application Layer             │
│   (用户代码：回调注册、消息收发)           │
├─────────────────────────────────────────┤
│            TcpClient Layer              │
│  ┌───────────┐ ┌───────────┐ ┌────────┐│
│  │连接管理    │ │消息队列    │ │重连控制││
│  │状态机      │ │线程安全    │ │定时器  ││
│  └───────────┘ └───────────┘ └────────┘│
├─────────────────────────────────────────┤
│            Protocol Layer               │
│  [4字节长度头 | 消息体] 编解码            │
├─────────────────────────────────────────┤
│         Asio Network Layer              │
│  io_context / tcp::socket / timer       │
└─────────────────────────────────────────┘
```

### 3. 核心功能实现

#### 3.1 异步连接与读写

```cpp
// 异步连接
asio::async_connect(socket_, endpoints,
    [this, self](const std::error_code& ec, ...) {
        handleConnect(ec);
    });

// 异步读取消息头
asio::async_read(socket_, asio::buffer(headerBuffer_),
    [this, self](const std::error_code& ec, std::size_t) {
        uint32_t bodyLen = decodeHeader(headerBuffer_);
        doReadBody(bodyLen);
    });
```

#### 3.2 指数退避重连

```cpp
std::chrono::milliseconds calculateReconnectDelay() {
    // delay = min(initial × 2^attempts, max)
    // 序列: 1s → 2s → 4s → 8s → 16s → 30s → 30s...
    double multiplier = std::pow(2.0, reconnectAttempts_);
    auto delay = initialDelay_ * multiplier;
    return std::min(delay, maxDelay_);
}
```

#### 3.3 线程安全的消息发送

```cpp
void send(const Message& msg) {
    asio::post(ioContext_, [this, data = msg.encode()]() {
        std::lock_guard<std::mutex> lock(writeMutex_);
        writeQueue_.push(std::move(data));
        if (writeQueue_.size() == 1) {
            doWrite();  // 启动写操作
        }
    });
}
```

### 4. 项目结构

```
AsioTcpClient/
├── CMakeLists.txt           # 构建配置
├── include/
│   ├── Message.h            # 协议层 (97行)
│   └── TcpClient.h          # 客户端接口 (133行)
├── src/
│   └── TcpClient.cpp        # 核心实现 (280行)
└── examples/
    └── main.cpp             # 使用示例 (140行)

总计: ~650 行 C++ 代码
```

### 5. 质量保证

- **RAII 设计**：资源自动管理，无内存泄漏
- **异常安全**：正确处理网络异常和边界情况
- **生命周期管理**：`shared_from_this` 确保异步回调安全

---

## R - Result（结果/成果）

### 1. 功能成果

| 功能 | 状态 | 说明 |
|------|------|------|
| 异步 TCP 连接 | ✅ 完成 | 非阻塞连接，支持 DNS 解析 |
| 自动重连 | ✅ 完成 | 指数退避，可配置最大重试次数 |
| 消息分帧 | ✅ 完成 | 4字节长度前缀协议 |
| 事件回调 | ✅ 完成 | 连接/断开/消息/错误回调 |
| 线程安全 | ✅ 完成 | 支持多线程环境 |
| 跨平台 | ✅ 完成 | Windows/Linux/macOS |

### 2. 技术亮点

- **Boost 生态**：基于成熟的 Boost.Asio，稳定可靠
- **高性能**：异步 IO + 事件驱动，单线程可处理大量连接
- **易集成**：简洁的回调式 API，5 分钟上手
- **可配置**：重连策略、超时时间等均可定制

### 3. 使用示例

```cpp
auto client = createClient(ioContext);

client->setOnConnected([]() {
    std::cout << "Connected!" << std::endl;
});

client->setOnMessage([](const Message& msg) {
    std::cout << "Received: " << msg.bodyAsString() << std::endl;
});

client->connect("127.0.0.1", 8888);
client->send("Hello, Server!");
```

### 4. 个人收获

| 方面 | 收获 |
|------|------|
| **网络编程** | 深入理解 TCP 协议、异步 IO 模型、Reactor/Proactor 模式 |
| **C++ 进阶** | 智能指针、RAII、移动语义、模板编程实践 |
| **系统设计** | 状态机设计、错误处理、重试策略、API 设计 |
| **工程能力** | CMake 构建、跨平台开发、代码组织 |

---

## 简历描述参考

### 版本一（详细版）

> **AsioTcpClient - 高性能自动重连 TCP 客户端** | C++17, Boost.Asio
>
> - 基于 Boost.Asio 实现异步非阻塞 TCP 客户端，采用 Proactor 模式处理 IO 事件
> - 设计指数退避重连机制，自动处理网络断连场景，提升系统可用性
> - 实现长度前缀协议解析，解决 TCP 粘包/拆包问题
> - 采用回调式 API 设计，支持多线程安全调用，跨平台兼容 Windows/Linux

### 版本二（简洁版）

> **TCP 网络客户端库** | C++17, Boost.Asio, 跨平台
>
> - 实现异步 IO + 自动重连的 TCP 客户端，支持长度前缀协议
> - 采用指数退避重连策略，线程安全设计，跨平台支持

---

## 面试常见问题

**Q1: 为什么选择 Boost.Asio 而不是其他网络库？**
> Boost.Asio 是 C++ 标准委员会推荐的网络库（已纳入 C++23 Networking TS），成熟稳定、跨平台支持好，Boost 生态系统提供丰富的扩展功能（如 SSL/TLS），社区活跃文档完善。

**Q2: 如何处理 TCP 粘包问题？**
> 采用长度前缀协议：每条消息前添加 4 字节长度头（网络字节序），接收端先读取长度头，再根据长度读取对应消息体。

**Q3: 为什么重连采用指数退避？**
> 避免服务器过载：如果立即重连，大量客户端同时尝试会造成服务器压力；指数退避使重连间隔逐渐增大，给服务器恢复时间。

**Q4: 如何保证线程安全？**
> 所有 IO 操作在 io_context 线程执行；跨线程调用通过 asio::post() 投递；共享数据（写队列）使用 mutex 保护。
