# ClawAgent 开发日记

## 2026-04-06

### 项目初始化

今天开始开发ClawAgent智能体项目。

### 设计决策

1. **模块化架构**:
   - `ConfigManager`: 统一配置管理，使用JSON配置文件
   - `LLMClient`: 支持OpenAI和Anthropic两种API接口
   - `MessageManager`: 消息历史管理，支持持久化
   - `ToolManager`: 工具系统管理
   - `AgentRuntime`: Agent核心运行时

2. **设计模式**:
   - 单例模式: Logger、ConfigManager
   - 工厂模式: LLMClient的创建
   - 策略模式: 不同provider的消息格式化

3. **依赖选择**:
   - nlohmann/json: 单一头文件，API简洁
   - libcurl: 成熟的HTTP客户端库
   - readline: 命令行交互支持

### 项目结构

```
include/
├── ClawAgent.hpp           # 主入口类
├── config/ConfigManager.hpp
├── llm/LLMClient.hpp
├── message/MessageManager.hpp
├── tools/ToolManager.hpp
├── agent/AgentRuntime.hpp
└── utils/Logger.hpp

src/
├── main.cpp
├── ClawAgent.cpp
├── config/ConfigManager.cpp
├── llm/LLMClient.cpp
├── message/MessageManager.cpp
├── tools/ToolManager.cpp
├── agent/AgentRuntime.cpp
└── utils/Logger.cpp
```

### 待完成

1. ~~完善错误处理机制~~ ✓
2. ~~添加单元测试~~ ✓
3. 支持更多工具类型
4. ~~流式输出的完整实现~~ ✓

## 2026-04-06 下午更新

### 已完成

1. **单元测试框架** - 使用 Google Test
   - test_config.cpp: 配置管理测试 (9 tests)
   - test_message.cpp: 消息管理测试 (12 tests)
   - test_tool.cpp: 工具系统测试 (18 tests)
   - 共 39 个测试，全部通过

2. **流式输出**
   - chatStreamAnthropic 实现真正的 SSE 流式解析
   - 有工具调用时自动切换到非流式
   - 无工具调用时使用流式响应
   - 终端流式输出：模型返回的文本实时显示到终端

3. **错误处理**
   - HTTP 错误码详细分类（401、403、429、500 等）
   - 工具执行失败时的详细错误信息
   - JSON 解析错误处理
   - 网络请求失败处理

4. **编译脚本 build.sh**
   - `./build.sh linux` 编译 Linux 版本
   - `./build.sh android` 编译 Android 版本 (需要 NDK)
   - `./build.sh -c linux` 清理后编译
   - `./build.sh -t linux` 编译并运行测试

### 修复的问题

1. LLMClient provider 路由错误
2. Anthropic 工具结果格式（tool_use_id）
3. Assistant 消息丢失 tool_use 块
4. MiniMax API 端点和模型名称
5. 终端流式输出实时显示

## 2026-04-21

### OpenAI 工具调用修复

**问题描述**: 使用 OpenAI API (qwen-plus) 进行工具调用时失败，报错 "cannot use at() with string"

**根本原因**:

1. **LLMClient.cpp:363** - `arguments` 字段可能是字符串或 JSON 对象，但原代码直接赋值
2. **Message.cpp:31** - 带有 `tool_calls` 的 assistant 消息格式不正确，content 应为字符串，tool_calls 应为独立数组

**修复内容**:

1. `src/llm/LLMClient.cpp`:
```cpp
auto& args_val = tc["function"]["arguments"];
if (args_val.is_string()) {
    call.arguments = json::parse(args_val.get<std::string>());
} else {
    call.arguments = args_val;
}
```

2. `src/message/Message.cpp`:
```cpp
if (role == "assistant" && !content_blocks.empty()) {
    j["content"] = content;  // 保持为字符串
    json tool_calls = json::array();
    for (const auto& block : content_blocks) {
        if (block.contains("type") && block["type"] == "tool_use") {
            json tc;
            tc["id"] = block.value("id", "");
            tc["type"] = "function";
            tc["function"]["name"] = block.value("name", "");
            tc["function"]["arguments"] = block.value("input", json::object()).dump();
            tool_calls.push_back(tc);
        }
    }
    if (!tool_calls.empty()) {
        j["tool_calls"] = tool_calls;
    }
}
```

### 新增配置文件

- `config.openai.json` - OpenAI 配置模板（qwen-plus）
- `config.anthropic.json` - Anthropic 配置模板（MiniMax-M2.7）

---

**开发人员**: Claude Code
**项目状态**: 开发中
**版本**: 1.0.0
