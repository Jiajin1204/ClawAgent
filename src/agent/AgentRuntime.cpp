#include "agent/AgentRuntime.hpp"
#include "config/ConfigManager.hpp"
#include "llm/LlmClientFactory.hpp"
#include "message/MessageManager.hpp"
#include "tools/ToolManager.hpp"
#include "utils/Logger.hpp"
#include "utils/Output.hpp"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <chrono>

using namespace ClawAgent;

AgentRuntime::AgentRuntime(std::shared_ptr<ConfigManager> config,
                           std::shared_ptr<ILlmClient> llm,
                           std::shared_ptr<MessageManager> messages,
                           std::shared_ptr<ToolManager> tools)
    : config_(config)
    , llm_(llm)
    , messages_(messages)
    , tools_(tools)
    , output_callback_(&Output::instance())
    , running_(false)
    , stop_requested_(false)
    , cancelled_(false) {

    stats_ = RuntimeStats{};
    stats_.iterations = 0;
    stats_.total_tool_calls = 0;
    stats_.total_tokens_used = 0;
    stats_.total_time_ms = 0;
    stats_.stopped = false;
}

AgentRuntime::~AgentRuntime() = default;

void AgentRuntime::setOutputCallback(IOutputCallback* callback) {
    output_callback_ = callback ? callback : &Output::instance();
}

std::string AgentRuntime::getDynamicContext() const {
    std::stringstream ss;

    // 当前时间
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    ss << "当前时间: " << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << "\n";

    // 操作系统信息
    ss << "操作系统: " << SystemTools::getOSInfo();
    ss << "主机名: " << SystemTools::getHostname() << "\n";

    // 可用工具
    ss << "可用工具:\n";
    auto tool_defs = tools_->getToolDefinitions();
    for (const auto& tool : tool_defs) {
        ss << "  - " << tool["function"].value("name", "unknown") << ": "
           << tool["function"].value("description", "") << "\n";
    }

    return ss.str();
}

std::string AgentRuntime::buildSystemPrompt() {
    auto agent_config = config_->getAgentConfig();

    std::stringstream ss;
    ss << agent_config.system_prompt << "\n\n";
    ss << "=== 动态上下文 ===\n";
    ss << getDynamicContext();
    ss << "\n=== 工具使用说明 ===\n";
    ss << "你可以使用以下工具来完成任务:\n";
    ss << "- read: 读取文件内容\n";
    ss << "- write: 写入文件内容\n";
    ss << "- exec: 执行命令或脚本\n";
    ss << "\n重要:\n";
    ss << "1. 如果需要执行多条命令，可以一次调用多个工具\n";
    ss << "2. 工具调用后请等待结果再决定下一步\n";
    ss << "3. 如果出错，请分析原因并尝试修复\n";
    ss << "4. 如果无法完成任务，请明确告知用户\n";

    return ss.str();
}

bool AgentRuntime::run(const std::string& user_input, std::string& final_response) {
    running_ = true;
    stop_requested_ = false;

    // 重置统计和循环检测
    stats_.iterations = 0;
    recent_actions_.clear();

    auto start_time = std::chrono::high_resolution_clock::now();

    // 添加用户消息
    messages_->addMessage("user", user_input);

    bool success = step(user_input, final_response);

    auto end_time = std::chrono::high_resolution_clock::now();
    stats_.total_time_ms += std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();

    running_ = false;
    return success;
}

bool AgentRuntime::step(const std::string& /*user_input*/, std::string& response) {
    Logger::instance().info("STEP START");

    // 检查停止条件（包括取消）
    if (shouldStop()) {
        if (cancelled_.load(std::memory_order_relaxed)) {
            response = "任务已取消";
            cancelled_.store(false, std::memory_order_relaxed);
            output_callback_->onError(response);
        } else {
            response = "Agent已停止: " + stats_.stop_reason;
            output_callback_->onError(response);
        }
        return false;
    }

    try {
        auto agent_config = config_->getAgentConfig();

        // 构建消息列表
        std::vector<Message> all_messages;

        // 系统提示
        Message system_msg;
        system_msg.role = "system";
        system_msg.content = buildSystemPrompt();
        all_messages.push_back(system_msg);

        // 历史消息
        auto history = messages_->getMessagesForLLM();
        all_messages.insert(all_messages.end(), history.begin(), history.end());

        // 获取工具定义
        auto tool_defs = tools_->getToolDefinitions();

        // 调用LLM
        LLMResponse llm_response;

        Logger::instance().info("准备调用llm_->chat, messages=" + std::to_string(all_messages.size()) + ", tools=" + std::to_string(tool_defs.size()));

        output_callback_->onLlmCalling();

        // 计时开始
        auto llm_start = std::chrono::high_resolution_clock::now();

        // 非流式模式
        if (!llm_->chat(all_messages, tool_defs, llm_response)) {
            // 检查是否因为取消而失败
            if (cancelled_.load(std::memory_order_relaxed)) {
                response = "任务已取消";
                cancelled_.store(false, std::memory_order_relaxed);
                output_callback_->onError(response);
                return false;
            }
            response = "LLM调用失败: " + llm_response.content;
            output_callback_->onError(response);
            return false;
        }

        // 再次检查取消标志（LLM调用成功后）
        if (cancelled_.load(std::memory_order_relaxed)) {
            response = "任务已取消";
            cancelled_.store(false, std::memory_order_relaxed);
            output_callback_->onError(response);
            return false;
        }

        // 计时结束
        auto llm_end = std::chrono::high_resolution_clock::now();
        auto llm_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(llm_end - llm_start).count();
        stats_.total_tokens_used += llm_time_ms;
        output_callback_->onLlmResponse(llm_time_ms);

        stats_.iterations++;

        // 处理响应
        if (!llm_response.tool_calls.empty()) {
            // 有工具调用
            output_callback_->onToolCallsFound(llm_response.tool_calls.size());

            stats_.total_tool_calls += llm_response.tool_calls.size();

            // 添加助手消息（包含tool_use块）
            {
                ChatMessage assistant_msg;
                assistant_msg.role = "assistant";
                assistant_msg.content = llm_response.content;
                if (!llm_response.tool_calls.empty()) {
                    for (const auto& tc : llm_response.tool_calls) {
                        assistant_msg.content_blocks.push_back(json{
                            {"type", "tool_use"},
                            {"id", tc.id},
                            {"name", tc.name},
                            {"input", tc.arguments}
                        });
                    }
                }
                messages_->addMessage(assistant_msg);
            }

            // 逐个执行工具并添加工具结果消息
            for (const auto& call : llm_response.tool_calls) {
                output_callback_->onToolExecuting(call.name, call.arguments);

                auto result = tools_->executeTool(call.name, call.arguments, call.id);

                if (result.success) {
                    output_callback_->onToolResult(call.name, result.result, result.execution_time_ms, true);
                } else {
                    output_callback_->onToolResult(call.name, result.error_message, result.execution_time_ms, false);
                }

                // 添加工具结果消息（每个工具调用单独一条消息）
                ChatMessage tool_msg;
                tool_msg.role = "tool";
                tool_msg.tool_call_id = call.id;
                // 即使失败也添加结果，以便 LLM 知道发生了什么
                tool_msg.content = result.success ? result.result : "工具执行失败: " + result.error_message;
                tool_msg.tool_name = call.name;
                messages_->addMessage(tool_msg);

                // 记录动作用于循环检测
                recordStep(call.name, call.arguments.dump());
            }

            // 递归调用直到没有工具调用
            return step("", response);
        } else {
            // 最终响应
            response = llm_response.content;
            messages_->addMessage("assistant", llm_response.content);
            output_callback_->onAssistantMessage(response);
            return true;
        }
    } catch (const std::exception& e) {
        Logger::instance().error("step异常: " + std::string(e.what()));
        response = "执行异常: " + std::string(e.what());
        try {
            output_callback_->onError(response);
        } catch (...) {
            Logger::instance().error("onError回调也异常了");
        }
        return false;
    }
}

bool AgentRuntime::shouldStop() {
    auto agent_config = config_->getAgentConfig();

    if (stop_requested_) {
        stats_.stop_reason = "用户请求停止";
        return true;
    }

    if (cancelled_.load(std::memory_order_relaxed)) {
        stats_.stop_reason = "任务已取消";
        return true;
    }

    if (stats_.iterations >= agent_config.max_iterations) {
        stats_.stop_reason = "达到最大迭代次数: " + std::to_string(agent_config.max_iterations);
        return true;
    }

    // 检测循环
    if (recent_actions_.size() >= 5) {
        bool all_same = true;
        for (const auto& action : recent_actions_) {
            if (action != recent_actions_[0]) {
                all_same = false;
                break;
            }
        }
        if (all_same) {
            stats_.stop_reason = "检测到重复动作循环";
            return true;
        }
    }

    return false;
}

void AgentRuntime::stop() {
    stop_requested_ = true;
    running_ = false;
    stats_.stop_reason = "手动停止";
    stats_.stopped = true;
}

void AgentRuntime::cancel() {
    Logger::instance().info("AgentRuntime::cancel() called");
    cancelled_.store(true, std::memory_order_relaxed);
    // 中止 LLM 调用
    if (llm_) {
        llm_->abort();
    }
}

AgentRuntime::RuntimeStats AgentRuntime::getStats() const {
    return stats_;
}

void AgentRuntime::recordStep(const std::string& action, const std::string& /*details*/) {
    recent_actions_.push_back(action);
    if (recent_actions_.size() > MAX_RECENT_ACTIONS) {
        recent_actions_.erase(recent_actions_.begin());
    }
}