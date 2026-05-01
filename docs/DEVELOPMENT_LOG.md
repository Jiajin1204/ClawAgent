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

## 2026-04-22

### 代码质量重构

**目标**: 提升代码可读性、可维护性，统一输出风格

#### 1. 修复 exec 超时 bug

**问题**: `read()` 在非阻塞模式下，`n == 0` 被误判为 EOF，实际上 `n == -1 && errno == EAGAIN` 才表示无数据可读

**修复**:
```cpp
ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
if (n > 0) {
    output += buffer;
} else if (n == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        usleep(10000);  // 等待后重试
        continue;
    }
    break;
} else {
    break;  // n == 0 表示 EOF
}
```

#### 2. 统一 read/write/exec 返回风格

**问题**: read 抛异常，write 返回 bool，exec 返回 ToolExecutionResult，风格不统一

**修复**: 三个工具都返回 `ToolExecutionResult`，语义清晰：
- `result.result` - 存储实际输出/结果
- `result.error_message` - 存储错误描述
- `result.success` - 布尔状态

#### 3. 新增统一输出层 Output

**目的**: 分离用户可见输出与系统日志

| 类 | 用途 |
|----|------|
| `Logger` | 系统日志，记录运行状态，可写入文件 |
| `Output` | 用户可见输出，仅输出到终端，支持颜色 |

#### 4. 清理 Tool 描述

**问题**: description 中包含参数说明，与 OpenAI tool schema 规范不符

**修复**: description 只保留功能描述，parameters 包含完整参数定义

#### 5. 删除死代码

移除未使用的 `executeToolsFormatted()` 函数

#### 修改的文件

- `src/tools/ToolManager.cpp` - 统一返回风格，修复超时 bug
- `include/tools/ToolManager.hpp` - 更新函数签名
- `src/agent/AgentRuntime.cpp` - 使用 Output 类
- `src/ClawAgent.cpp` - 使用 Output 类
- `include/utils/Output.hpp` - 新增统一输出层
- `src/utils/Output.cpp` - 新增统一输出层
- `CMakeLists.txt` - 添加 Output 源文件

## 2026-04-24

### LLMClient 重构 - 工厂模式 + 接口分离

**目标**: 遵循"面向接口编程"原则，实现高内聚、低耦合的模块化设计

#### 重构内容

1. **新增接口层** `ILlmClient`
   - 抽象出 `chat()`、`getProvider()`、`getModelName()`、`healthCheck()` 接口
   - 所有 LLM 客户端实现必须继承此接口

2. **新增工厂类** `LlmClientFactory`
   - 根据配置创建对应的 LLM 客户端实例
   - 支持 OpenAI 和 Anthropic 两种提供商

3. **新增具体客户端**
   - `OpenAIClient`: OpenAI 兼容接口的标准实现
   - `AnthropicClient`: Anthropic 兼容接口的标准实现

4. **非标准格式解析**
   - 支持多种 LLM 输出格式的自动解析
   - 格式包括:
     - 标准 OpenAI `tool_calls`
     - XML 格式 `<tool_call>...</tool_call>`
     - JSON 对象 `{"tool": "exec", "input": {"command": "pwd"}}`
     - 函数调用格式 `exec(command="pwd")`
     - `{"command": "tool_name", "arguments": {...}}`

#### 修复的问题

1. **修复"parameters"被误识为工具名**
   - `tryParseAsSingleToolCall` 中错误遍历 JSON 键
   - 移除将 "tool_call" 等键名当作工具名的逻辑

2. **增强 extractToolFromJsonObject**
   - 支持 `{"tool": ..., "input": {...}}` 格式
   - 支持 `{"command": "tool_name", "arguments": {...}}` 格式

3. **修复函数调用格式解析**
   - 确保 `arguments` 始终为 JSON 对象
   - 避免 `cannot use at() with string` 错误

#### 验证结果

| 配置文件 | 模型 | 提供商 | 状态 |
|----------|------|--------|------|
| config.json | qwen-plus | OpenAI | ✅ |
| config.anthropic.json | MiniMax-M2.7 | Anthropic | ✅ |
| config.openai.json | qwen3.6-plus-cc | OpenAI | ✅ |

#### 修改的文件

- `include/llm/ILlmClient.hpp` - 新增接口
- `include/llm/LlmClientFactory.hpp` - 新增工厂
- `include/llm/OpenAIClient.hpp` - 新增
- `include/llm/AnthropicClient.hpp` - 新增
- `src/llm/LlmClientFactory.cpp` - 新增
- `src/llm/OpenAIClient.cpp` - 新增
- `src/llm/AnthropicClient.cpp` - 新增
- `src/llm/LLMClient.cpp.bak` - 备份旧代码（已删除）

---

## 2026-04-25

### 库化重构 - 输出回调接口

**目标**: 将 ClawAgent 作为库集成到其他工程

**问题**:
- `g_agent->run()` 在 main.cpp 中阻塞，不适合集成
- 输出直接打印到终端，无法自定义

**解决方案**:
1. 新增 `IOutputCallback` 接口，所有输出通过回调通知
2. `ClawAgentCore::process()` 非阻塞，每次调用返回最终响应
3. 默认使用 `Output` 类作为回调，保持现有行为

**新增文件**:
- `include/utils/OutputCallback.hpp` - 输出回调接口

**修改文件**:
- `include/ClawAgent.hpp` - 添加 setOutputCallback、process 等方法
- `src/ClawAgent.cpp` - 移除阻塞循环，使用回调输出
- `include/utils/Output.hpp` / `src/utils/Output.cpp` - 实现 IOutputCallback
- `include/agent/AgentRuntime.hpp` / `src/agent/AgentRuntime.cpp` - 使用回调
- `src/main.cpp` - 非阻塞循环

**API 变更**:
- 移除 `ClawAgentCore::run()` 阻塞方法
- 新增 `ClawAgentCore::process(input, response)` 非阻塞方法
- 新增 `ClawAgentCore::setOutputCallback(callback)` 设置自定义回调

**使用示例**:
```cpp
// 其他项目集成
ClawAgentCore agent("config.json");
agent.setOutputCallback(myCallback);  // 自定义输出

std::string response;
agent.process("execute pwd", response);
// 响应通过 myCallback 的 onAssistantMessage 返回
```

---

**开发人员**: Claude Code
**项目状态**: 开发中
**版本**: 1.0.0

---

## 2026-04-26

### 交互优化 - [you] 提示符

**问题**: 用户在终端执行 `./build/linux/clawagent` 时，不知道何时可以输入

**修复**: 在 main.cpp 的交互循环中添加 `[you]` 提示符

```cpp
while (g_agent->isRunning()) {
    std::cout << "[you] ";
    if (!std::getline(std::cin, input)) {
        break;
    }
    // ...
}
```

**效果**:
```
[you] 执行pwd
[调用模型...]
  [LLM耗时: 1039ms]
[检测到 1 个工具调用]
[执行工具: exec]
  参数: {"command":"pwd"}
  结果: /home/jason/projects/ClawAgent
  耗时: 10ms
[调用模型...]
  [LLM耗时: 1388ms]
[ClawAgent] 当前工作目录是 `/home/jason/projects/ClawAgent`。

[you]
```

---

**开发人员**: Claude Code
**项目状态**: 开发中
**版本**: 1.0.0

---

## 2026-05-01

### Workspace 和 Skill 系统

**目标**: 实现类似 OpenClaw 的 Workspace 和 Skill 架构，支持 agent 的 home 目录管理和可复用 skill 系统

#### 1. WorkspaceManager

管理 ClawAgent 的 home 和 workspace 目录：

```cpp
class WorkspaceManager {
    // 目录优先级: config > env > default (~/.clawagent)
    void initialize(const std::string& config_home);

    // 目录结构
    std::string getHome();           // ~/.clawagent
    std::string getWorkspace();     // ~/.clawagent/workspace
    std::string getGlobalSkillsDir(); // ~/.clawagent/skills
    std::string getSessionsDir();   // ~/.clawagent/sessions
    std::string getAgentsMdPath();  // workspace/AGENTS.md
    std::string getMemoryDir();     // workspace/memory

    // 递归创建目录结构
    void createDirectories();

    // 动态读取 AGENTS.md
    std::string readAgentsMd() const;
};
```

#### 2. SkillManager

管理 skill 的加载和上下文注入：

```cpp
class SkillManager {
    enum class LoadMode { Startup, Dynamic };

    // 加载模式
    // - startup: 启动时加载所有 skill
    // - dynamic: 运行时按需加载

    // 上下文注入配置
    // - inject_all: 所有 skill 注入上下文
    // - enabled: 仅注入列表中的 skill
};
```

#### 3. 目录结构

```
~/.clawagent/              # 或 $CLAWAGENT_HOME
├── workspace/             # 工作区 (chdir 切换到这里)
│   ├── AGENTS.md         # agent 行为规范 (动态读取)
│   ├── memory/          # 记忆存储
│   └── skills/          # 工作区 skill
├── skills/              # 全局/托管 skill
├── sessions/            # 会话历史
└── ...
```

#### 4. 系统提示词优先级

1. `config.json` → `system_prompt_path` 文件
2. `config.json` → `system_prompt` 字段
3. `~/.clawagent/workspace/AGENTS.md` (最后备选)

#### 5. 动态上下文

提示词中新增 workspace 路径信息：

```
工作目录: /home/jason/.clawagent/workspace
Skills: /home/jason/.clawagent/skills
会话历史: /home/jason/.clawagent/sessions
```

#### 6. 新增文件

| 文件 | 说明 |
|------|------|
| `include/workspace/WorkspaceManager.hpp` | Workspace 管理器 |
| `src/workspace/WorkspaceManager.cpp` | 实现 |
| `include/skill/SkillManager.hpp` | Skill 管理器 |
| `include/skill/Skill.hpp` | Skill 数据结构 |
| `src/skill/SkillManager.cpp` | 实现 |
| `skills/example-skill/SKILL.md` | 示例 skill |

#### 7. 配置变更

```json
{
    "clawagent": {
        "home": "~/.clawagent"
    },
    "skills": {
        "load_mode": "startup",
        "inject_all": false,
        "enabled": []
    }
}
```

#### 8. 修复的问题

1. `addSkillsFromDirectory` 重复解析 SKILL.md - 改用 `parseSkillFile`
2. 目录递归创建 - `createDirectory` 递归创建父目录
3. Skill frontmatter 解析错误 - `name:` 正确赋值给 `name` 而非 `description`

---

**开发人员**: Claude Code
**项目状态**: 开发中
**版本**: 1.0.0
