# LLMClient 重构整改书

## 1. 背景与目标

### 1.1 现状问题
- `LLMClient` 类同时处理 OpenAI、Anthropic 两种协议和多种非标准格式
- 违反单一职责原则，代码耦合严重
- 扩展困难，新增模型需要修改大量现有代码

### 1.2 重构目标
- 遵循"面向接口编程"原则
- 实现高内聚、低耦合的模块化设计
- 支持多种 LLM 提供商（OpenAI、Anthropic）
- 兼容非标准工具调用格式（非 OpenAI tool_calls 格式）
- 保持现有功能不变，三个配置文件均能正常工作

## 2. 架构设计

### 2.1 设计原则
- **接口隔离**：抽象出公共接口，具体实现解耦
- **依赖倒置**：高层模块不依赖低层模块实现
- **组合优于继承**：使用组合模式实现功能扩展

### 2.2 类图

```
┌─────────────────────────────────────────────────────────────────┐
│                        AgentRuntime                              │
│                   (通过接口与LLM交互)                              │
└─────────────────────────────┬───────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     LLMClient (抽象基类)                          │
│  + chat(messages, tools, response) = 0                           │
│  + getProvider() const = 0                                      │
│  + getModelName() const = 0                                    │
└─────────────────────────────┬───────────────────────────────────┘
                              │
        ┌─────────────────────┴─────────────────────┐
        │                   │                       │
        ▼                   ▼                       ▼
┌───────────────┐   ┌───────────────┐   ┌───────────────────────┐
│ OpenAIClient  │   │AnthropicClient│   │ NonStandardOpenAIClient│
│  (标准格式)   │   │  (标准格式)    │   │    (组合 + 适配)       │
└───────────────┘   └───────────────┘   └───────────────────────┘
```

### 2.3 类职责

| 类 | 职责 |
|---|---|
| `LLMClient` | 抽象基类，定义公共接口 |
| `OpenAIClient` | OpenAI 兼容接口的标准实现 |
| `AnthropicClient` | Anthropic 兼容接口的标准实现 |
| `NonStandardOpenAIClient` | 组合 OpenAIClient + 非标准格式解析适配 |

### 2.4 工厂模式

```cpp
// 根据配置创建对应的 LLMClient
std::unique_ptr<LLMClient> LLMClientFactory::create(const ModelConfig& config) {
    if (config.provider == "openai") {
        return std::make_unique<OpenAIClient>(...);
    } else if (config.provider == "anthropic") {
        return std::make_unique<AnthropicClient>(...);
    }
    throw std::runtime_error("Unknown provider");
}
```

## 3. 接口设计

### 3.1 LLMClient 抽象基类

```cpp
class LLMClient {
public:
    virtual ~LLMClient() = default;

    // 聊天接口
    virtual bool chat(const std::vector<Message>& messages,
                      const std::vector<json>& tools,
                      LLMResponse& response) = 0;

    // 获取提供商名称
    virtual std::string getProvider() const = 0;

    // 获取模型名称
    virtual std::string getModelName() const = 0;

    // 健康检查
    virtual bool healthCheck() = 0;
};
```

### 3.2 LLMResponse 结构

```cpp
struct LLMResponse {
    std::string content;
    std::string role;
    bool is_complete;
    std::vector<ToolCall> tool_calls;
    std::string stop_reason;
    bool success;
};
```

## 4. 实现细节

### 4.1 OpenAIClient

- 负责与 OpenAI 兼容接口交互
- 处理标准 OpenAI 响应格式
- 不处理非标准格式（由 NonStandardOpenAIClient 处理）

### 4.2 NonStandardOpenAIClient

- **组合** OpenAIClient
- 调用 OpenAIClient.chat() 获取原始响应
- 如果 tool_calls 为空但 content 包含工具调用信息，调用解析器转换
- 保持与原有一致的功能

### 4.3 AnthropicClient

- 负责与 Anthropic 兼容接口交互
- 处理标准 Anthropic 响应格式

## 5. 配置影响

配置文件结构不变：

```json
{
    "model": {
        "provider": "openai",  // 或 "anthropic"
        "name": "qwen-plus",   // 或 "MiniMax-M2.7"
        ...
    }
}
```

## 6. 测试计划

### 6.1 功能测试
| 配置 | 模型 | 测试用例 |
|------|------|----------|
| config.json | qwen-plus (OpenAI) | 标准工具调用 |
| config.openai.json | qwen3.6-plus-cc (OpenAI) | 非标准工具调用 |
| config.anthropic.json | MiniMax-M2.7 (Anthropic) | 标准工具调用 |

### 6.2 测试场景
1. 读取当前目录 (`ls -la`)
2. 读取文件内容
3. 写入文件
4. 执行命令并验证结果

## 7. 实施计划

| 阶段 | 任务 | 变更文件 |
|------|------|----------|
| 1 | 抽象 LLMClient 接口，提取公共方法 | `include/llm/LLMClient.hpp` |
| 2 | 创建 OpenAIClient，迁移 OpenAI 相关代码 | `src/llm/OpenAIClient.cpp` |
| 3 | 创建 AnthropicClient，迁移 Anthropic 相关代码 | `src/llm/AnthropicClient.cpp` |
| 4 | 创建 NonStandardOpenAIClient，处理非标准格式 | `src/llm/NonStandardOpenAIClient.cpp` |
| 5 | 创建工厂类 LLMClientFactory | `src/llm/LLMClientFactory.cpp` |
| 6 | 修改 AgentRuntime 使用工厂创建 | `src/agent/AgentRuntime.cpp` |
| 7 | 验证三个配置文件均能正常工作 | - |
| 8 | 清理旧代码 | 删除 LLMClient.cpp 中的旧代码 |

## 8. 风险与对策

| 风险 | 对策 |
|------|------|
| 重构破坏现有功能 | 分阶段实施，每阶段验证 |
| 非标准格式解析失败 | 保留原有解析逻辑作为兜底 |
| 配置兼容性问题 | 工厂模式确保配置驱动 |

## 9. 预期收益

- **可维护性**：新增模型只需实现接口，无需修改现有代码
- **可测试性**：每个类职责单一，易于单元测试
- **可扩展性**：非标准格式处理作为独立组件，便于后续扩展
- **代码清晰**：消除大文件，提高代码可读性

## 10. 完成状态

✅ **已完成** (2026-04-24)

- ✅ 抽象 LLMClient 接口，提取公共方法
- ✅ 创建 OpenAIClient，迁移 OpenAI 相关代码
- ✅ 创建 AnthropicClient，迁移 Anthropic 相关代码
- ✅ 创建 NonStandardOpenAIClient，处理非标准格式（集成到 OpenAIClient）
- ✅ 创建工厂类 LLMClientFactory
- ✅ 修改 AgentRuntime 使用工厂创建
- ✅ 验证三个配置文件均能正常工作
- ✅ 清理旧代码（LLMClient.cpp.bak 待删除）
