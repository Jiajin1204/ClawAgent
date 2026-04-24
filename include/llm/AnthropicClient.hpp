#pragma once

#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include "message/Message.hpp"
#include "llm/ILlmClient.hpp"

#ifndef NO_CURL
#include <curl/curl.h>
#endif

using json = nlohmann::json;

namespace ClawAgent {

/**
 * @brief Anthropic兼容接口客户端
 * 支持标准Anthropic格式
 */
class AnthropicClient : public ILlmClient {
public:
    AnthropicClient(const std::string& model,
                   const std::string& api_key,
                   const std::string& base_url,
                   bool stream = true,
                   int timeout_ms = 120000);
    ~AnthropicClient() override;

    // 禁用拷贝
    AnthropicClient(const AnthropicClient&) = delete;
    AnthropicClient& operator=(const AnthropicClient&) = delete;

    // ILlmClient接口实现
    bool chat(const std::vector<Message>& messages,
              const std::vector<json>& tools,
              LLMResponse& response) override;

    std::string getProvider() const override { return "anthropic"; }
    std::string getModelName() const override { return model_; }
    bool healthCheck() override;

private:
    // HTTP请求
    bool makeRequest(const std::string& url,
                     const json& body,
                     std::string& response,
                     bool stream = false);

    // 解析Anthropic响应
    bool parseResponse(const std::string& response_str, LLMResponse& response);

    std::string model_;
    std::string api_key_;
    std::string base_url_;
    bool stream_;
    int timeout_ms_;

#ifndef NO_CURL
    CURL* curl_;
#else
    void* curl_;
#endif
};

} // namespace ClawAgent
