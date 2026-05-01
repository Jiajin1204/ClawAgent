#pragma once

#include <memory>
#include <string>
#include <vector>
#include "agent/AgentRuntime.hpp"
#include "message/Message.hpp"
#include "utils/OutputCallback.hpp"
#include "workspace/WorkspaceManager.hpp"
#include "skill/SkillManager.hpp"

namespace ClawAgent {

class Logger;

/**
 * @brief ClawAgent主类 - 可作为库集成到其他工程
 *
 * 设计原则：
 * 1. 非阻塞 API：process() 处理一条输入即返回
 * 2. 输出回调：所有输出通过 IOutputCallback 通知，可自定义
 * 3. 默认使用 Output 类作为回调，保持现有行为
 */
class ClawAgentCore {
public:
    ClawAgentCore(const std::string& config_path = "config.json");
    ~ClawAgentCore();

    // 禁用拷贝
    ClawAgentCore(const ClawAgentCore&) = delete;
    ClawAgentCore& operator=(const ClawAgentCore&) = delete;

    // ============ 回调设置 ============

    // 设置输出回调（可选，不设置则使用默认 Output）
    void setOutputCallback(IOutputCallback* callback);

    // 获取输出回调
    IOutputCallback* getOutputCallback();

    // ============ 核心 API（非阻塞）============

    // 处理一条输入，返回最终响应
    // 返回值：true 成功，false 失败
    bool process(const std::string& user_input, std::string& final_response);

    // ============ 辅助 API ============

    // 命令处理
    bool handleCommand(const std::string& cmd);

    // 获取帮助文本
    std::string getHelpText() const;

    // 新建会话
    void newSession();

    // 清除历史
    void clearHistory();

    // 停止运行
    void stop();

    // 取消当前任务（可从另一线程调用，使当前任务快速停止）
    void cancel();

    // 检查是否正在运行
    bool isRunning() const;

    // 获取运行时统计
    AgentRuntime::RuntimeStats getStats() const;

    // 获取消息历史
    std::vector<ChatMessage> getHistory() const;

    // ============ 兼容接口（供 main.cpp 使用）============

    // 打印帮助
    void printHelp();

    // 打印系统消息
    void printSystem(const std::string& message);

private:
    void initialize();
    void ensureOutputCallback();

    // 成员声明顺序（与构造函数初始化列表顺序一致）
    bool running_;
    bool streaming_enabled_;
    IOutputCallback* output_callback_;
    bool owns_output_callback_;
    Logger* logger_;
    std::shared_ptr<ConfigManager> config_manager_;
    std::shared_ptr<ILlmClient> llm_client_;
    std::shared_ptr<MessageManager> message_manager_;
    std::shared_ptr<ToolManager> tool_manager_;
    WorkspaceManager* workspace_manager_;  // singleton, 不需要 shared_ptr
    std::shared_ptr<SkillManager> skill_manager_;
    std::shared_ptr<AgentRuntime> agent_runtime_;
};

} // namespace ClawAgent