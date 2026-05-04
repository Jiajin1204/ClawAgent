#include "ClawAgent.hpp"
#include "config/ConfigManager.hpp"
#include "llm/LlmClientFactory.hpp"
#include "message/MessageManager.hpp"
#include "tools/ToolManager.hpp"
#include "workspace/WorkspaceManager.hpp"
#include "skill/SkillManager.hpp"
#include "agent/AgentRuntime.hpp"
#include "utils/Logger.hpp"
#include "utils/Output.hpp"

#include <iostream>
#include <csignal>
#include <memory>
#include <sstream>
#include <vector>
#include <algorithm>
#include <unistd.h>

using namespace ClawAgent;

ClawAgentCore::ClawAgentCore(const std::string& config_path)
    : running_(true)
    , streaming_enabled_(true)
    , output_callback_(&Output::instance())
    , owns_output_callback_(false) {

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

    // 初始化输出管理器
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

    // 初始化 WorkspaceManager (单例)
    auto clawagent_config = config_manager_->getClawAgentConfig();
    workspace_manager_ = &WorkspaceManager::instance();
    workspace_manager_->initialize(clawagent_config.home);

    // 创建必要的目录结构
    workspace_manager_->createDirectories();

    // 切换到 workspace 工作目录
    if (chdir(workspace_manager_->getWorkspace().c_str()) != 0) {
        Logger::instance().warning("无法切换到工作目录: " + workspace_manager_->getWorkspace());
    } else {
        Logger::instance().info("已切换工作目录到: " + workspace_manager_->getWorkspace());
    }

    // 初始化 SkillManager
    auto skills_config = config_manager_->getSkillsConfig();
    SkillManager::LoadMode load_mode = (skills_config.load_mode == "dynamic")
        ? SkillManager::LoadMode::Dynamic
        : SkillManager::LoadMode::Startup;
    skill_manager_ = std::make_shared<SkillManager>(
        workspace_manager_->getWorkspace() + "/skills",
        workspace_manager_->getGlobalSkillsDir(),
        load_mode,
        skills_config.full_content_skills
    );
    skill_manager_->loadSkills();

    // 初始化Agent运行时
    agent_runtime_ = std::make_shared<AgentRuntime>(
        config_manager_,
        llm_client_,
        message_manager_,
        tool_manager_,
        workspace_manager_,
        skill_manager_
    );

    // 设置输出回调到 AgentRuntime
    agent_runtime_->setOutputCallback(output_callback_);

    Logger::instance().info("ClawAgent 初始化完成");
    output_callback_->onAssistantMessage("已连接到: " + model_config.name
        + " (" + model_config.provider + ")");
}

ClawAgentCore::~ClawAgentCore() {
    running_ = false;
    if (owns_output_callback_) {
        delete output_callback_;
    }
}

void ClawAgentCore::initialize() {
    config_manager_ = std::make_shared<ConfigManager>();
    logger_ = &Logger::instance();
}

void ClawAgentCore::setOutputCallback(IOutputCallback* callback) {
    output_callback_ = callback ? callback : &Output::instance();
    agent_runtime_->setOutputCallback(output_callback_);
}

IOutputCallback* ClawAgentCore::getOutputCallback() {
    return output_callback_;
}

bool ClawAgentCore::process(const std::string& user_input, std::string& final_response) {
    if (!running_) {
        final_response = "Agent已停止";
        return false;
    }

    // 检查是否命令
    if (!user_input.empty() && user_input[0] == '/') {
        return handleCommand(user_input);
    }

    // 处理输入
    std::string response;
    bool success = agent_runtime_->run(user_input, response);

    if (success) {
        final_response = response;
    } else {
        final_response = response;  // 错误信息也在 response 中
    }

    return success;
}

bool ClawAgentCore::handleCommand(const std::string& cmd) {
    std::istringstream iss(cmd);
    std::string command;
    iss >> command;

    if (command == "/help" || command == "/h") {
        printHelp();
        return true;
    } else if (command == "/new" || command == "/n") {
        newSession();
        return true;
    } else if (command == "/quit" || command == "/q" || command == "/exit") {
        stop();
        return true;
    } else if (command == "/clear" || command == "/c") {
        message_manager_->clearHistory();
        output_callback_->onAssistantMessage("历史记录已清除");
        return true;
    } else if (command == "/history" || command == "/hist") {
        auto history = message_manager_->getHistory();
        std::stringstream ss;
        ss << "\n=== 消息历史 (" << history.size() << " 条) ===";
        for (const auto& msg : history) {
            ss << "\n[" << msg.role << "] " << msg.content;
        }
        output_callback_->onAssistantMessage(ss.str());
        return true;
    } else if (command == "/tools" || command == "/t") {
        auto tools = tool_manager_->getToolDefinitions();
        std::stringstream ss;
        ss << "\n=== 可用工具 ===";
        for (const auto& tool : tools) {
            ss << "\n- " << tool["name"].get<std::string>() << ": "
               << tool["description"].get<std::string>();
        }
        output_callback_->onAssistantMessage(ss.str());
        return true;
    } else if (command == "/stats") {
        auto stats = agent_runtime_->getStats();
        std::stringstream ss;
        ss << "\n=== 运行统计 ===";
        ss << "\n迭代次数: " << stats.iterations;
        ss << "\n工具调用: " << stats.total_tool_calls;
        ss << "\n总耗时: " << stats.total_time_ms << "ms";
        ss << "\n状态: " << (stats.stopped ? "已停止" : "运行中");
        if (!stats.stop_reason.empty()) {
            ss << "\n停止原因: " << stats.stop_reason;
        }
        output_callback_->onAssistantMessage(ss.str());
        return true;
    } else if (command == "/save") {
        if (message_manager_->saveToFile()) {
            output_callback_->onAssistantMessage("消息历史已保存");
        } else {
            output_callback_->onError("保存失败");
        }
        return true;
    } else if (command == "/load") {
        if (message_manager_->loadFromFile()) {
            output_callback_->onAssistantMessage("消息历史已加载");
        } else {
            output_callback_->onError("加载失败");
        }
        return true;
    } else {
        output_callback_->onError("未知命令: " + command);
        output_callback_->onAssistantMessage("输入 /help 查看可用命令");
        return false;
    }
}

std::string ClawAgentCore::getHelpText() const {
    return R"(
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
)";
}

void ClawAgentCore::printHelp() {
    output_callback_->onAssistantMessage(getHelpText());
}

void ClawAgentCore::printSystem(const std::string& message) {
    output_callback_->onAssistantMessage(message);
}

void ClawAgentCore::newSession() {
    message_manager_->newSession();
    output_callback_->onAssistantMessage("新会话已创建，短期记忆已清除");
}

void ClawAgentCore::clearHistory() {
    message_manager_->clearHistory();
}

void ClawAgentCore::stop() {
    message_manager_->saveToFile();
    running_ = false;
    output_callback_->onAssistantMessage("再见!");
}

void ClawAgentCore::cancel() {
    Logger::instance().info("ClawAgentCore::cancel() called");
    if (agent_runtime_) {
        agent_runtime_->cancel();
    }
}

bool ClawAgentCore::isRunning() const {
    return running_;
}

AgentRuntime::RuntimeStats ClawAgentCore::getStats() const {
    return agent_runtime_->getStats();
}

std::vector<ChatMessage> ClawAgentCore::getHistory() const {
    return message_manager_->getHistory();
}

void ClawAgentCore::ensureOutputCallback() {
    if (!output_callback_) {
        output_callback_ = &Output::instance();
    }
}