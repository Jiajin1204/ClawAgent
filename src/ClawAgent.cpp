#include "ClawAgent.hpp"
#include "config/ConfigManager.hpp"
#include "llm/LLMClient.hpp"
#include "message/MessageManager.hpp"
#include "tools/ToolManager.hpp"
#include "agent/AgentRuntime.hpp"
#include "utils/Logger.hpp"

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

    // 初始化LLM客户端
    llm_client_ = std::make_shared<LLMClient>(
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
    std::cout << "已连接到: " << model_config.name
              << " (" << model_config.provider << ")" << std::endl;
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
        std::cout << "\n[ClawAgent] ";
        std::cout.flush();

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
        std::cout << "历史记录已清除" << std::endl;
    } else if (command == "/history" || command == "/hist") {
        auto history = message_manager_->getHistory();
        std::cout << "\n=== 消息历史 (" << history.size() << " 条) ===" << std::endl;
        for (const auto& msg : history) {
            std::cout << "[" << msg.role << "] " << msg.content << std::endl;
        }
    } else if (command == "/tools" || command == "/t") {
        auto tools = tool_manager_->getToolDefinitions();
        std::cout << "\n=== 可用工具 ===" << std::endl;
        for (const auto& tool : tools) {
            std::cout << "- " << tool["name"].get<std::string>() << ": "
                      << tool["description"].get<std::string>() << std::endl;
        }
    } else if (command == "/stats") {
        auto stats = agent_runtime_->getStats();
        std::cout << "\n=== 运行统计 ===" << std::endl;
        std::cout << "迭代次数: " << stats.iterations << std::endl;
        std::cout << "工具调用: " << stats.total_tool_calls << std::endl;
        std::cout << "总耗时: " << stats.total_time_ms << "ms" << std::endl;
        std::cout << "状态: " << (stats.stopped ? "已停止" : "运行中") << std::endl;
        if (!stats.stop_reason.empty()) {
            std::cout << "停止原因: " << stats.stop_reason << std::endl;
        }
    } else if (command == "/save") {
        if (message_manager_->saveToFile()) {
            std::cout << "消息历史已保存" << std::endl;
        } else {
            std::cout << "保存失败" << std::endl;
        }
    } else if (command == "/load") {
        if (message_manager_->loadFromFile()) {
            std::cout << "消息历史已加载" << std::endl;
        } else {
            std::cout << "加载失败" << std::endl;
        }
    } else {
        std::cout << "未知命令: " << command << std::endl;
        std::cout << "输入 /help 查看可用命令" << std::endl;
    }
}

void ClawAgentCore::printHelp() {
    std::cout << "\n=== ClawAgent 帮助 ===" << std::endl;
    std::cout << "\n命令:" << std::endl;
    std::cout << "  /help, /h     显示此帮助信息" << std::endl;
    std::cout << "  /new, /n      新建会话(清除短期记忆)" << std::endl;
    std::cout << "  /clear, /c    清除历史记录" << std::endl;
    std::cout << "  /history, /hist  显示消息历史" << std::endl;
    std::cout << "  /tools, /t    显示可用工具" << std::endl;
    std::cout << "  /stats        显示运行统计" << std::endl;
    std::cout << "  /save         保存消息历史到文件" << std::endl;
    std::cout << "  /load         从文件加载消息历史" << std::endl;
    std::cout << "  /quit, /q     退出程序" << std::endl;
    std::cout << "\n使用说明:" << std::endl;
    std::cout << "  - 直接输入问题与智能体对话" << std::endl;
    std::cout << "  - 智能体可以调用工具完成任务" << std::endl;
    std::cout << "  - 按两次Ctrl+C可强制退出" << std::endl;
}

void ClawAgentCore::newSession() {
    message_manager_->newSession();
    std::cout << "新会话已创建，短期记忆已清除" << std::endl;
}

void ClawAgentCore::quit() {
    // 保存历史
    message_manager_->saveToFile();
    running_ = false;
    std::cout << "再见!" << std::endl;
}

void ClawAgentCore::processInput(const std::string& input) {
    std::cout << "\n[处理中..." << std::endl;

    std::string response;
    bool success = agent_runtime_->run(input, response);

    if (success) {
        std::cout << "\n[ClawAgent] " << response << std::endl;
    } else {
        std::cout << "\n[错误] " << response << std::endl;
    }
}
