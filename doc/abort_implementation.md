# Abort/Cancel 机制实现方案

## 概述

本文档描述 ClawAgent 中 LLM 请求中止机制的实现方案。该机制允许从另一个线程安全地中止正在进行的 HTTP 请求，实现快速响应的取消功能。

## 设计目标

1. **可中止**: 能够从另一线程终止正在进行的 LLM 调用
2. **快速响应**: 中止信号发出后能在 1 秒内生效
3. **可恢复**: 中止后系统状态干净，可立即处理新请求
4. **线程安全**: abort() 可从任意线程调用

## 技术方案

### 核心技术: socket pair + poll 循环

使用 Linux 的 socket pair 机制创建一个唤醒文件描述符，配合标准的 poll() 系统调用替代 curl_multi_wait()，实现可中断的 event loop。

```
┌─────────────────────────────────────────────────────────────┐
│                     makeRequest()                            │
│                                                              │
│  ┌───────────────────────────────────────────────────────┐  │
│  │              poll() 循环                                │  │
│  │                                                        │  │
│  │   监控的文件描述符:                                     │  │
│  │   - curl_sockets_ (curl 内部管理的 socket)            │  │
│  │   - wakeup_fds_[0] (socketpair 读端)                   │  │
│  │                                                        │  │
│  │   poll() 返回时:                                       │  │
│  │   - wakeup_triggered → abort_requested_ → 退出循环   │  │
│  │   - curl socket 事件 → curl_multi_socket_action()     │  │
│  └───────────────────────────────────────────────────────┘  │
│                                                              │
│  abort() 被调用时:                                           │
│    → 关闭 wakeup_fds_[1] (socketpair 写端)                 │
│    → poll() 立即返回 (POLLHUP/POLLERR)                       │
│    → 下次循环检测到 abort_requested_ → 退出                  │
└─────────────────────────────────────────────────────────────┘
```

## 关键组件

### 1. Socket Pair

```cpp
// 创建一对互相连接的 socket
int wakeup_fds_[2];
if (socketpair(AF_UNIX, SOCK_STREAM, 0, wakeup_fds_) != 0) {
    throw std::runtime_error("无法创建wakeup socket pair");
}
```

- `wakeup_fds_[0]`: 读端，加入 poll 监控
- `wakeup_fds_[1]`: 写端，abort() 时关闭

### 2. CURL Socket Callback

通过 `CURLMOPT_SOCKETFUNCTION` 设置回调，curl 会在其内部管理的 socket 发生变化时通知我们：

```cpp
static int socketCallback(CURL* easy, curl_socket_t s, int action, void* userp) {
    switch (action) {
        case CURL_POLL_IN:
        case CURL_POLL_OUT:
        case CURL_POLL_INOUT:
            ctx->client->onCurlSocket(s, action);  // 添加到监控列表
            break;
        case CURL_POLL_REMOVE:
            ctx->client->onCurlSocketRemove(s);     // 从监控列表移除
            break;
    }
    return 0;
}
```

### 3. 追踪 curl 的 socket

```cpp
std::map<curl_socket_t, int> curl_sockets_;  // socket -> events (POLLIN/POLLOUT)
```

当 curl socket callback 被调用时，更新这个 map。poll 循环中用这个 map 来监控所有 curl socket。

### 4. Abort 标志

```cpp
std::atomic<bool> abort_requested_{false};
```

使用 atomic 布尔值，确保多线程安全访问。

## 实现流程

### makeRequest() 流程

```
makeRequest()
  │
  ├─ 1. abort_requested_ 复位为 false
  │
  ├─ 2. curl_sockets_ 清空
  │
  ├─ 3. 如果 wakeup_fds_[1] < 0（上次 abort 关闭了）
  │     └─→ 重新创建 socket pair
  │
  ├─ 4. curl_multi_add_handle()
  │
  ├─ 5. curl_multi_perform() 初始化请求
  │
  ├─ 6. poll() 循环
  │     │
  │     ├─ poll(curl_sockets_ + wakeup_fds_[0], timeout=1000ms)
  │     │
  │     ├─ 如果 wakeup_triggered
  │     │     └─→ break (被中止)
  │     │
  │     └─ 处理 curl socket 事件
  │           └─→ curl_multi_socket_action()
  │
  └─ 7. 清理并返回
```

### abort() 流程

```
abort()
  │
  ├─ 1. abort_requested_ = true
  │
  └─ 2. 关闭 wakeup_fds_[1]
        └─→ poll() 立即返回 (POLLHUP)
```

## 关键代码

### OpenAIClient.hpp 新增成员

```cpp
#ifndef NO_CURL
// curl socket事件处理 (public for static callback access)
void onCurlSocket(curl_socket_t s, int action);
void onCurlSocketRemove(curl_socket_t s);

// Socket callbacks for curl multi interface
static int socketCallback(CURL* easy, curl_socket_t s, int action, void* userp);
static int timerCallback(CURLM* multi, long timeout_ms, void* userp);

// 中止请求标志
std::atomic<bool> abort_requested_{false};

// Wakeup socket pair for interrupt
int wakeup_fds_[2];           // socketpair for wakeup

// 追踪curl的socket，用于我们自己的poll loop
std::map<curl_socket_t, int> curl_sockets_;  // socket -> events
#endif
```

### makeRequest() 中的 poll 循环

```cpp
while (mres == CURLM_OK && still_running && !abort_requested_.load(std::memory_order_relaxed)) {
    // 构建poll fd数组
    std::vector<struct pollfd> pollfds;

    // 添加curl的socket
    for (const auto& pair : curl_sockets_) {
        struct pollfd pfd;
        pfd.fd = pair.first;
        pfd.events = pair.second;
        pfd.revents = 0;
        pollfds.push_back(pfd);
    }

    // 添加wakeup socket
    if (wakeup_fds_[0] >= 0) {
        struct pollfd pfd;
        pfd.fd = wakeup_fds_[0];
        pfd.events = POLLIN;
        pfd.revents = 0;
        pollfds.push_back(pfd);
    }

    // 调用poll
    int poll_res = poll(pollfds.data(), pollfds.size(), 1000);

    // 检查wakeup socket是否触发
    bool wakeup_triggered = false;
    for (const auto& pfd : pollfds) {
        if (pfd.fd == wakeup_fds_[0] && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
            wakeup_triggered = true;
            break;
        }
    }

    if (wakeup_triggered) {
        Logger::instance().info("请求被中止 (wakeup triggered)");
        break;
    }

    // 处理curl socket事件
    for (const auto& pfd : pollfds) {
        if (pfd.fd == wakeup_fds_[0]) continue;
        if (pfd.revents) {
            int ev = 0;
            if (pfd.revents & (POLLIN | POLLERR | POLLHUP)) ev |= CURL_CSELECT_IN;
            if (pfd.revents & POLLOUT) ev |= CURL_CSELECT_OUT;
            mres = curl_multi_socket_action(curl_multi_, pfd.fd, ev, &still_running);
        }
    }
}
```

## 响应时间

- **poll 超时**: 1 秒（最坏情况下中止延迟为 ~1秒+执行时间）
- **实际响应**: 通常 **< 10ms**（因 curl socket 事件立即返回）

## Socket Pair 重建

abort() 会关闭 `wakeup_fds_[1]`，下次 `makeRequest()` 时需要检测并重建：

```cpp
if (wakeup_fds_[1] < 0) {
    // 关闭旧的读端
    if (wakeup_fds_[0] >= 0) {
        close(wakeup_fds_[0]);
        wakeup_fds_[0] = -1;
    }
    // 重新创建socket pair
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, wakeup_fds_) != 0) {
        Logger::instance().error("无法重新创建wakeup socket pair");
    }
}
```

## 与旧方案对比

| 方面 | 旧方案 (curl_easy_reset) | 新方案 (socket pair) |
|------|------------------------|----------------------|
| 中止原理 | 重置 curl handle 状态 | 关闭 socket 唤醒 poll |
| 响应时间 | 依赖 curl timeout | **< 10ms** (通常) |
| 可靠性 | 低（请求仍在后台运行）| 高（poll 立即返回）|
| 实现复杂度 | 简单 | 中等 |
| 状态恢复 | 需要手动清理 | 自动重建 |

## 受影响的文件

- `include/llm/OpenAIClient.hpp`
- `include/llm/AnthropicClient.hpp`
- `src/llm/OpenAIClient.cpp`
- `src/llm/AnthropicClient.cpp`
