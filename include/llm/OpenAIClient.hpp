#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <atomic>
#include <nlohmann/json.hpp>
#include "message/Message.hpp"
#include "llm/ILlmClient.hpp"

#ifndef NO_CURL
#include <curl/curl.h>
#endif

using json = nlohmann::json;

namespace ClawAgent {

/**
 * @brief OpenAI兼容接口客户端
 * 支持标准OpenAI格式以及非标准格式的自动转换
 */
class OpenAIClient : public ILlmClient {
public:
    OpenAIClient(const std::string& model,
                  const std::string& api_key,
                  const std::string& base_url,
                  bool stream = true,
                  int timeout_ms = 120000);
    ~OpenAIClient() override;

    // 禁用拷贝
    OpenAIClient(const OpenAIClient&) = delete;
    OpenAIClient& operator=(const OpenAIClient&) = delete;

    // ILlmClient接口实现
    bool chat(const std::vector<Message>& messages,
              const std::vector<json>& tools,
              LLMResponse& response) override;

    std::string getProvider() const override { return "openai"; }
    std::string getModelName() const override { return model_; }
    bool healthCheck() override;
    void abort() override;

#ifndef NO_CURL
    // curl socket事件处理 (public for static callback access)
    void onCurlSocket(curl_socket_t s, int action);
    void onCurlSocketRemove(curl_socket_t s);

    // Socket callbacks for curl multi interface (must be static to work with curl_multi_setopt)
    static int socketCallback(CURL* easy, curl_socket_t s, int action, void* userp);
    static int timerCallback(CURLM* multi, long timeout_ms, void* userp);
#endif

private:
    // HTTP请求
    bool makeRequest(const std::string& url,
                     const json& body,
                     std::string& response,
                     bool stream = false);

    // 解析标准OpenAI响应
    bool parseResponse(const std::string& response_str, LLMResponse& response);

    // 解析非标准工具调用格式
    bool parseNonStandardToolCall(const std::string& content, std::vector<ToolCall>& tool_calls);

    // 中止请求标志
    std::atomic<bool> abort_requested_{false};

    // Wakeup socket pair for interrupt
#ifndef NO_CURL
    int wakeup_fds_[2];           // socketpair for wakeup
#endif

    std::string model_;
    std::string api_key_;
    std::string base_url_;
    bool stream_;
    int timeout_ms_;

#ifndef NO_CURL
    CURL* curl_;
    CURLM* curl_multi_;       // 用于支持中止功能
    // 追踪curl的socket，用于我们自己的poll loop
    std::map<curl_socket_t, int> curl_sockets_;  // socket -> events (POLLIN/POLLOUT)
#else
    void* curl_;
#endif
};

} // namespace ClawAgent
