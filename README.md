# ClawAgent

智能体开发框架 - 基于C++的AI智能体运行时

## 功能特性

- **多模型支持**: 支持OpenAI兼容接口和Anthropic兼容接口
- **消息管理**: 短期记忆管理，支持历史消息持久化到JSONL文件
- **工具系统**: 内置read/write/exec三个核心工具
- **流式输出**: 支持模型响应和终端输出的双重流式
- **多实例支持**: 线程安全的设计
- **停止/取消机制**: 防止Agent循环，支持从另一线程中止LLM调用（socket pair + poll方案）
- **单元测试**: 完整的Google Test测试框架 (39个测试用例)

## 系统要求

- C++17 或更高版本
- CMake 3.14+
- libcurl
- Ubuntu / Android (NDK)

## 依赖安装

### Ubuntu

```bash
# 安装依赖
sudo apt-get update
sudo apt-get install -y build-essential cmake libcurl4-openssl-dev libreadline-dev

# 安装readline开发库
sudo apt-get install -y libreadline-dev
```

## 编译

### 方式一：使用编译脚本（推荐）

```bash
# 编译 Linux 版本
./build.sh linux

# 编译 Android 版本
./build.sh android

# 清理并编译
./build.sh -c linux

# 编译并运行测试
./build.sh -t linux
```

### 方式二：手动编译

```bash
cd /home/jason/projects/ClawAgent

# 编译 Linux 版本
mkdir -p build/linux && cd build/linux
cmake ../.. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 编译测试
cmake ../.. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
make -j$(nproc) clawagent_tests
```

### Android 编译

```bash
# 设置 Android NDK 环境变量
export ANDROID_NDK_HOME=/path/to/android-ndk

# 编译 Android 版本
./build.sh android
```

## 运行

```bash
# 运行主程序（注意：二进制文件在 build/linux/ 目录下）
./build/linux/clawagent

# 或直接运行
./build.sh linux && ./build/linux/clawagent

# 运行测试
./build/linux/clawagent_tests
```

## 配置

项目提供三个配置文件模板：

- `config.json` - 默认配置（使用 qwen-plus）
- `config.openai.json` - OpenAI 兼容接口配置（qwen3.6-plus-cc）
- `config.anthropic.json` - Anthropic 兼容接口配置（MiniMax-M2.7）

### 配置文件对比

| 配置文件 | 模型 | 提供商 | 工具调用格式 |
|----------|------|--------|--------------|
| `config.json` | qwen-plus | OpenAI | 标准格式 |
| `config.openai.json` | qwen3.6-plus-cc | OpenAI | 标准格式 |
| `config.anthropic.json` | MiniMax-M2.7 | Anthropic | 标准格式 |

```bash
# 使用 OpenAI 配置 (qwen-plus)
cp config.json config.json.bak
cp config.json config.json

# 或使用 Anthropic 配置
cp config.anthropic.json config.json

# 或使用非标准格式支持的 OpenAI 配置
cp config.openai.json config.json

# 编辑 config.json 填入你的 API Key
# 注意：config.openai.json 使用的是 qwen3.6-plus-cc 模型
```

### API Key 配置

项目支持通过**环境变量**配置 API Key，配置文件中的 key 优先级更高：

```bash
# 设置环境变量
export CLAWAGENT_LLM_KEY="your-api-key-here"
```

配置文件中的 `api_key` 优先使用配置文件中的值，如果留空则读取环境变量 `CLAWAGENT_LLM_KEY`。

```json
{
    "model": {
        "api_key": "",  // 为空时自动读取 CLAWAGENT_LLM_KEY 环境变量
        ...
    }
}
```

**安全提示**: 切勿将包含真实 API Key 的配置文件提交到仓库！

### 配置项说明

| 配置项 | 说明 |
|--------|------|
| `model.provider` | 模型提供商: `openai` 或 `anthropic` |
| `model.name` | 模型名称 (如 qwen-plus, MiniMax-M2.7) |
| `model.api_key` | API密钥 |
| `model.base_url` | API基础URL |
| `model.stream` | 是否启用流式输出 |
| `message.max_history` | 最大历史消息条数 |
| `agent.max_iterations` | Agent最大迭代次数（防止无限循环）|
| `agent.system_prompt` | 系统提示词（回退值） |
| `agent.system_prompt_path` | 系统提示词文件路径（优先级高于 system_prompt）|
| `tools.enable_*` | 启用/禁用各工具 |

### 系统提示词文件

对于较长的系统提示词，可以使用独立的 Markdown 文件来配置，避免 JSON 中的转义问题：

```bash
# 1. 创建系统提示词文件
echo "你是一个有帮助的AI助手。" > system_prompt.md

# 2. 在 config.json 中指定文件路径
{
    "agent": {
        "system_prompt_path": "./system_prompt.md",
        "system_prompt": "回退提示词（当文件不存在时使用）",
        ...
    }
}
```

**优先级**：`system_prompt_path` > `system_prompt`

- 如果 `system_prompt_path` 指向的文件存在，读取文件内容作为系统提示词
- 如果文件不存在或路径为空，使用 `system_prompt` 作为回退

## 使用

### 命令

| 命令 | 说明 |
|------|------|
| `/help` 或 `/h` | 显示帮助信息 |
| `/new` 或 `/n` | 新建会话（清除短期记忆）|
| `/clear` 或 `/c` | 清除历史记录 |
| `/history` 或 `/hist` | 显示消息历史 |
| `/tools` 或 `/t` | 显示可用工具 |
| `/stats` | 显示运行统计 |
| `/save` | 保存消息历史到文件 |
| `/load` | 从文件加载消息历史 |
| `/quit` 或 `/q` | 退出程序 |

### 使用示例

```
[ClawAgent] 你好，请帮我查看当前目录下的文件
[处理中...]

[调用模型...]

[执行工具: exec]
  参数: {"command": "ls -la"}
  结果: total 32
drwxr-xr-x 18 jason jason 4096 Apr  6 16:30 .
drwxr-xr-x  2 jason jason 4096 Apr  6 16:30 ..

[ClawAgent] 当前目录包含以下文件和文件夹：
- . (当前目录)
- .. (父目录)
...（具体文件列表）
```

## 项目结构

```
ClawAgent/
├── CMakeLists.txt          # CMake构建文件
├── build.sh                # 编译脚本
├── config.example.json     # 配置模板（环境变量版本）
├── config.openai.json      # OpenAI配置文件模板
├── config.anthropic.json   # Anthropic配置文件模板
├── README.md               # 本文档
├── docs/                   # 开发文档
│   └── abort_implementation.md  # abort机制实现方案
├── include/                # 头文件
│   ├── ClawAgent.hpp       # 主类
│   ├── config/             # 配置管理
│   ├── llm/                # LLM客户端
│   ├── message/            # 消息管理
│   ├── tools/              # 工具系统
│   ├── agent/              # Agent运行时
│   └── utils/              # 工具类
│       ├── Logger.hpp      # 日志系统
│       └── Output.hpp      # 统一输出层
├── src/                    # 源文件
├── tests/                  # 测试
│   ├── test_abort.cpp      # abort/cancel功能测试
│   ├── test_config.cpp     # 配置测试
│   ├── test_message.cpp    # 消息测试
│   └── test_tool.cpp       # 工具测试
└── thirdparty/             # 第三方库 (FetchContent自动下载)
    └── nlohmann/           # JSON单文件头库
```

## 工具说明

### read
读取文件内容。

参数:
- `filepath`: 文件路径

### write
写入内容到文件。

参数:
- `filepath`: 文件路径
- `content`: 要写入的内容

### exec
执行命令或脚本。

参数:
- `command`: 要执行的命令

## 开发者

- 设计模式: 工厂模式、单例模式、策略模式
- 线程安全: 使用mutex和shared_mutex保护共享资源
- 错误处理: 异常安全和错误码返回
- 单元测试: Google Test框架，39个测试用例

## 许可证

MIT License

## 问题排查

### 编译错误

1. 确保安装了libcurl开发库: `sudo apt-get install libcurl4-openssl-dev`
2. 确保CMake版本 >= 3.14: `cmake --version`

### 运行错误

1. 检查API Key是否正确配置
2. 检查网络连接
3. 查看日志文件 `clawagent.log`

### readline错误

如果出现readline相关错误，确保安装了readline开发库:
```bash
sudo apt-get install libreadline-dev
```

### 测试失败

```bash
# 运行单个测试
./build/linux/clawagent_tests --gtest_filter=ConfigTest.LoadConfig

# 运行特定测试套件
./build/linux/clawagent_tests --gtest_filter=ToolTest.*
```
