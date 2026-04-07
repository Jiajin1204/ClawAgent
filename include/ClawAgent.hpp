#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ClawAgent {

class ConfigManager;
class LLMClient;
class MessageManager;
class ToolManager;
class AgentRuntime;
class Logger;

/**
 * @brief ClawAgent主类
 */
class ClawAgentCore {
public:
    ClawAgentCore(const std::string& config_path = "config.json");
    ~ClawAgentCore();

    // 禁用拷贝
    ClawAgentCore(const ClawAgentCore&) = delete;
    ClawAgentCore& operator=(const ClawAgentCore&) = delete;

    // 运行主循环
    void run();

    // 命令处理
    void handleCommand(const std::string& cmd);
    void printHelp();
    void newSession();
    void quit();

private:
    void initialize();
    void processInput(const std::string& input);

    std::shared_ptr<ConfigManager> config_manager_;
    std::shared_ptr<LLMClient> llm_client_;
    std::shared_ptr<MessageManager> message_manager_;
    std::shared_ptr<ToolManager> tool_manager_;
    std::shared_ptr<AgentRuntime> agent_runtime_;
    Logger* logger_;

    bool running_;
    bool streaming_enabled_;
};

} // namespace ClawAgent
