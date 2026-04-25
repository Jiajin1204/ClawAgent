#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <chrono>
#include "llm/ILlmClient.hpp"
#include "utils/OutputCallback.hpp"

namespace ClawAgent {

class ConfigManager;
class MessageManager;
class ToolManager;

/**
 * @brief Agent运行时 - 核心智能体循环
 *
 * 设计原则：
 * 1. 非阻塞：run() 处理一次交互即返回
 * 2. 输出回调：所有输出通过 IOutputCallback，不直接打印
 * 3. 循环由外部控制：AgentRuntime 不管理主循环
 */
class AgentRuntime {
public:
    struct RuntimeStats {
        int iterations;
        int total_tool_calls;
        int total_tokens_used;
        long total_time_ms;
        bool stopped;
        std::string stop_reason;
    };

    AgentRuntime(std::shared_ptr<ConfigManager> config,
                 std::shared_ptr<ILlmClient> llm,
                 std::shared_ptr<MessageManager> messages,
                 std::shared_ptr<ToolManager> tools);
    ~AgentRuntime();

    // 禁用拷贝
    AgentRuntime(const AgentRuntime&) = delete;
    AgentRuntime& operator=(const AgentRuntime&) = delete;

    // ============ 回调设置 ============

    // 设置输出回调
    void setOutputCallback(IOutputCallback* callback);

    // ============ 核心 API ============

    // 运行一次交互（用户输入 -> LLM响应 -> 工具调用 -> 响应）
    bool run(const std::string& user_input, std::string& final_response);

    // 停止运行
    void stop();

    // 获取运行时统计
    RuntimeStats getStats() const;

    // 检查是否正在运行
    bool isRunning() const { return running_.load(); }

    // 获取系统信息用于动态提示词
    std::string getDynamicContext() const;

private:
    // 构建完整提示词
    std::string buildSystemPrompt();

    // 单步执行
    bool step(const std::string& user_input, std::string& response);

    // 检查停止条件
    bool shouldStop();

    // 记录步骤
    void recordStep(const std::string& action, const std::string& details);

    std::shared_ptr<ConfigManager> config_;
    std::shared_ptr<ILlmClient> llm_;
    std::shared_ptr<MessageManager> messages_;
    std::shared_ptr<ToolManager> tools_;

    // 输出回调（默认为 Output::instance()）
    IOutputCallback* output_callback_;

    std::atomic<bool> running_;
    std::atomic<bool> stop_requested_;
    RuntimeStats stats_;

    // 循环检测
    std::vector<std::string> recent_actions_;
    static constexpr int MAX_RECENT_ACTIONS = 10;
};

} // namespace ClawAgent