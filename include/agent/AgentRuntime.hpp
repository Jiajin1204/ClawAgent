#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <chrono>
#include "llm/LLMClient.hpp"

namespace ClawAgent {

class ConfigManager;
class MessageManager;
class ToolManager;

/**
 * @brief Agent运行时 - 核心智能体循环
 *
 * 功能：
 * 1. 构建提示词（固定+动态部分）
 * 2. 运行智能体循环
 * 3. 处理工具调用
 * 4. 停止机制
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
                 std::shared_ptr<LLMClient> llm,
                 std::shared_ptr<MessageManager> messages,
                 std::shared_ptr<ToolManager> tools);
    ~AgentRuntime();

    // 禁用拷贝
    AgentRuntime(const AgentRuntime&) = delete;
    AgentRuntime& operator=(const AgentRuntime&) = delete;

    // 运行一次交互（用户输入 -> LLM响应 -> 工具调用 -> 响应）
    bool run(const std::string& user_input, std::string& final_response);

    // 运行交互式循环（持续直到停止）
    void runLoop();

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

    // 处理LLM响应（可能是内容或工具调用）
    bool processResponse(const std::string& llm_content,
                         const std::vector<ToolCall>& tool_calls,
                         std::string& response);

    // 执行工具并格式化结果
    std::string executeToolsFormatted(const std::vector<ToolCall>& tool_calls);

    // 检查停止条件
    bool shouldStop();

    // 记录步骤
    void recordStep(const std::string& action, const std::string& details);

    std::shared_ptr<ConfigManager> config_;
    std::shared_ptr<LLMClient> llm_;
    std::shared_ptr<MessageManager> messages_;
    std::shared_ptr<ToolManager> tools_;

    std::atomic<bool> running_;
    std::atomic<bool> stop_requested_;
    RuntimeStats stats_;

    // 循环检测
    std::vector<std::string> recent_actions_;
    static constexpr int MAX_RECENT_ACTIONS = 10;
};

/**
 * @brief 停止检测器 - 防止无限循环
 */
class StopDetector {
public:
    StopDetector(int max_iterations = 50, int max_repeated_actions = 5);

    // 记录动作
    void recordAction(const std::string& action);

    // 检查是否应该停止
    bool shouldStop() const;

    // 获取停止原因
    std::string getStopReason() const;

    // 重置
    void reset();

private:
    int max_iterations_;
    int max_repeated_actions_;
    int current_iteration_;
    std::vector<std::string> action_history_;
    mutable std::string stop_reason_;
};

} // namespace ClawAgent
