#include "ClawAgent.hpp"
#include "config/ConfigManager.hpp"
#include "llm/LlmClientFactory.hpp"
#include "message/MessageManager.hpp"
#include "tools/ToolManager.hpp"
#include "agent/AgentRuntime.hpp"
#include "utils/Logger.hpp"
#include "utils/Output.hpp"

#include <iostream>
#include <csignal>
#include <memory>
#include <sstream>
#include <vector>
#include <algorithm>

using namespace ClawAgent;

ClawAgentCore::ClawAgentCore(const std::string& config_path)
    : running_(true), streaming_enabled_(true) {

    initialize();

    // 加载配置
    if (!config_manager_->load(config_path)) {
        throw std::runtime_error("无法加载配置文件: " + config_path);
    }

    // 根据配置初始化各模块
    auto model_config = config_manager_->getModelConfig();
    auto msg_config = config_manager_->getMessageConfig();
    auto tools_config = config_manager_->getToolsConfig();

    streaming_enabled_ = model_config.stream;

    // 初始化输出管理器（需要在其他组件之前，以便早期输出）
    auto output_config = config_manager_->getOutputConfig();
    Output::instance().init(output_config.color_output, output_config.show_tools);

    // 初始化LLM客户端
    llm_client_ = LlmClientFactory::create(
        model_config.provider,
        model_config.name,
        model_config.api_key,
        model_config.base_url,
        model_config.stream,
        model_config.timeout_ms
    );

    // 初始化消息管理器
    message_manager_ = std::make_shared<MessageManager>(
        msg_config.max_history,
        msg_config.persist_path,
        msg_config.enable_compression
    );

    // 初始化工具管理器
    tool_manager_ = std::make_shared<ToolManager>(
        tools_config.enable_read,
        tools_config.enable_write,
        tools_config.enable_exec,
        tools_config.exec_timeout_ms
    );

    // 初始化Agent运行时
    agent_runtime_ = std::make_shared<AgentRuntime>(
        config_manager_,
        llm_client_,
        message_manager_,
        tool_manager_
    );

    Logger::instance().info("ClawAgent 初始化完成");
    Output::instance().printSystem("已连接到: " + model_config.name
        + " (" + model_config.provider + ")");
}

ClawAgentCore::~ClawAgentCore() {
    running_ = false;
}

void ClawAgentCore::initialize() {
    config_manager_ = std::make_shared<ConfigManager>();
    logger_ = &Logger::instance();
}

void ClawAgentCore::run() {
    std::string input;

    while (running_) {
        // 显示提示符
        Output::instance().printPrompt("[ClawAgent]");

        // 使用标准输入读取
        if (!std::getline(std::cin, input)) {
            // EOF (Ctrl+D)
            std::cout << "\n再见!" << std::endl;
            break;
        }

        // 去除空白
        input.erase(0, input.find_first_not_of(" \t\n\r"));
        input.erase(input.find_last_not_of(" \t\n\r") + 1);

        if (input.empty()) {
            continue;
        }

        // 检查命令
        if (input[0] == '/') {
            handleCommand(input);
        } else {
            processInput(input);
        }
    }
}

void ClawAgentCore::handleCommand(const std::string& cmd) {
    std::istringstream iss(cmd);
    std::string command;
    iss >> command;

    if (command == "/help" || command == "/h") {
        printHelp();
    } else if (command == "/new" || command == "/n") {
        newSession();
    } else if (command == "/quit" || command == "/q" || command == "/exit") {
        quit();
    } else if (command == "/clear" || command == "/c") {
        message_manager_->clearHistory();
        Output::instance().printSystem("历史记录已清除");
    } else if (command == "/history" || command == "/hist") {
        auto history = message_manager_->getHistory();
        Output::instance().printSystem("\n=== 消息历史 (" + std::to_string(history.size()) + " 条) ===");
        for (const auto& msg : history) {
            Output::instance().printSystem("[" + msg.role + "] " + msg.content);
        }
    } else if (command == "/tools" || command == "/t") {
        auto tools = tool_manager_->getToolDefinitions();
        Output::instance().printSystem("\n=== 可用工具 ===");
        for (const auto& tool : tools) {
            Output::instance().printSystem("- " + tool["name"].get<std::string>() + ": "
                + tool["description"].get<std::string>());
        }
    } else if (command == "/stats") {
        auto stats = agent_runtime_->getStats();
        Output::instance().printSystem("\n=== 运行统计 ===");
        Output::instance().printSystem("迭代次数: " + std::to_string(stats.iterations));
        Output::instance().printSystem("工具调用: " + std::to_string(stats.total_tool_calls));
        Output::instance().printSystem("总耗时: " + std::to_string(stats.total_time_ms) + "ms");
        Output::instance().printSystem("状态: " + std::string(stats.stopped ? "已停止" : "运行中"));
        if (!stats.stop_reason.empty()) {
            Output::instance().printSystem("停止原因: " + stats.stop_reason);
        }
    } else if (command == "/save") {
        if (message_manager_->saveToFile()) {
            Output::instance().printSystem("消息历史已保存");
        } else {
            Output::instance().printSystem("保存失败");
        }
    } else if (command == "/load") {
        if (message_manager_->loadFromFile()) {
            Output::instance().printSystem("消息历史已加载");
        } else {
            Output::instance().printSystem("加载失败");
        }
    } else {
        Output::instance().printSystem("未知命令: " + command);
        Output::instance().printSystem("输入 /help 查看可用命令");
    }
}

void ClawAgentCore::printHelp() {
    Output::instance().printSystem(R"(
=== ClawAgent 帮助 ===

命令:
  /help, /h     显示此帮助信息
  /new, /n      新建会话(清除短期记忆)
  /clear, /c    清除历史记录
  /history, /hist  显示消息历史
  /tools, /t    显示可用工具
  /stats        显示运行统计
  /save         保存消息历史到文件
  /load         从文件加载消息历史
  /quit, /q     退出程序

使用说明:
  - 直接输入问题与智能体对话
  - 智能体可以调用工具完成任务
  - 按两次Ctrl+C可强制退出
)");
}

void ClawAgentCore::newSession() {
    message_manager_->newSession();
    Output::instance().printSystem("新会话已创建，短期记忆已清除");
}

void ClawAgentCore::quit() {
    // 保存历史
    message_manager_->saveToFile();
    running_ = false;
    Output::instance().printSystem("再见!");
}

void ClawAgentCore::processInput(const std::string& input) {
    Output::instance().printProcessing();

    std::string response;
    bool success = agent_runtime_->run(input, response);

    if (success) {
        Output::instance().printAssistant(response);
    } else {
        Output::instance().printError(response);
    }
}
