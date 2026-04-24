#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include "message/Message.hpp"

#ifndef NO_CURL
#include <curl/curl.h>
#endif

using json = nlohmann::json;

namespace ClawAgent {

// 工具调用结构
struct ToolCall {
    std::string id;
    std::string name;
    json arguments;  // arguments as json object
};

// 工具结果结构
struct ToolResult {
    std::string tool_call_id;
    std::string content;
    bool success;
    std::string error;

    json toJson() const;
};

// LLM响应结构
struct LLMResponse {
    std::string content;
    std::string role;
    bool is_streaming;
    bool is_complete;
    bool success;
    std::vector<ToolCall> tool_calls;
    std::string stop_reason;

    LLMResponse() : is_streaming(false), is_complete(false), success(true) {}
    json toJson() const;
};

// 回调函数类型
using StreamCallback = std::function<void(const std::string& chunk, bool is_final)>;
using CompleteCallback = std::function<void(const LLMResponse& response)>;
// 流式输出到终端的回调（可选）
using TerminalOutputCallback = std::function<void(const std::string& text)>;

/**
 * @brief LLM客户端 - 支持OpenAI和Anthropic兼容接口
 */
class LLMClient {
public:
    LLMClient(const std::string& provider,
             const std::string& model,
             const std::string& api_key,
             const std::string& base_url,
             bool stream = true,
             int timeout_ms = 120000);
    ~LLMClient();

    // 禁用拷贝
    LLMClient(const LLMClient&) = delete;
    LLMClient& operator=(const LLMClient&) = delete;

    // 发送聊天请求（带终端流式输出回调）
    bool chat(const std::vector<Message>& messages,
              const std::vector<json>& tools,
              LLMResponse& response,
              TerminalOutputCallback terminal_output = nullptr);

    // 流式聊天
    bool chatStream(const std::vector<Message>& messages,
                    const std::vector<json>& tools,
                    StreamCallback on_chunk,
                    CompleteCallback on_complete);

    // 检查API连接
    bool healthCheck();

    // 获取模型名称
    std::string getModelName() const { return model_; }
    std::string getProvider() const { return provider_; }

private:
    // OpenAI兼容接口
    bool chatOpenAI(const std::vector<Message>& messages,
                    const std::vector<json>& tools,
                    LLMResponse& response);
    bool chatStreamOpenAI(const std::vector<Message>& messages,
                          const std::vector<json>& tools,
                          StreamCallback on_chunk,
                          CompleteCallback on_complete);

    // Anthropic兼容接口
    bool chatAnthropic(const std::vector<Message>& messages,
                       const std::vector<json>& tools,
                       LLMResponse& response);
    bool chatStreamAnthropic(const std::vector<Message>& messages,
                             const std::vector<json>& tools,
                             StreamCallback on_chunk,
                             CompleteCallback on_complete);

    // HTTP请求
    bool makeRequest(const std::string& url,
                     const json& body,
                     std::string& response,
                     bool stream = false);

    // 解析响应
    bool parseOpenAIResponse(const std::string& response, LLMResponse& result);
    bool parseAnthropicResponse(const std::string& response, LLMResponse& result);
    bool parseSSEStream(const std::string& data, std::string& content, std::string& tool_name, json& args);
    // 解析非标准工具调用格式 (支持多种LLM输出格式)
    bool parseNonStandardToolCall(const std::string& content, std::vector<ToolCall>& tool_calls);

    std::string provider_;
    std::string model_;
    std::string api_key_;
    std::string base_url_;
    bool stream_;
    int timeout_ms_;
#ifndef NO_CURL
    CURL* curl_;
#else
    void* curl_;  // Placeholder when curl is not available
#endif
};

/**
 * @brief 消息格式化器 - 统一处理不同接口的消息格式
 */
class MessageFormatter {
public:
    enum class Provider { OpenAI, Anthropic };

    MessageFormatter(Provider provider);

    // 格式化消息列表为请求体
    json formatMessages(const std::vector<Message>& messages,
                        const std::vector<json>& tools) const;

    // 提取工具调用
    std::vector<ToolCall> extractToolCalls(const LLMResponse& response) const;

    // 从响应中提取内容
    std::string extractContent(const json& response) const;

private:
    Provider provider_;
};

} // namespace ClawAgent
