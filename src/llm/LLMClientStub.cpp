// Stub LLMClient for Android builds without curl
// This provides a non-functional LLMClient that returns errors

#include "llm/LLMClient.hpp"
#include "utils/Logger.hpp"

using namespace ClawAgent;

LLMClient::LLMClient(const std::string& provider,
                     const std::string& model,
                     const std::string& api_key,
                     const std::string& base_url,
                     bool stream,
                     int timeout_ms)
    : provider_(provider)
    , model_(model)
    , api_key_(api_key)
    , base_url_(base_url)
    , stream_(stream)
    , timeout_ms_(timeout_ms)
    , curl_(nullptr) {
    Logger::instance().warning("LLMClient running in stub mode (no network)");
}

LLMClient::~LLMClient() = default;

bool LLMClient::chat(const std::vector<Message>& messages,
                     const std::vector<json>& tools,
                     LLMResponse& response,
                     TerminalOutputCallback terminal_output) {
    (void)messages;
    (void)tools;
    (void)terminal_output;
    response.success = false;
    response.content = "LLM not available: curl library not found";
    return false;
}

bool LLMClient::chatStream(const std::vector<Message>& messages,
                           const std::vector<json>& tools,
                           StreamCallback on_chunk,
                           CompleteCallback on_complete) {
    (void)messages;
    (void)tools;
    LLMResponse resp;
    resp.success = false;
    resp.content = "LLM not available: curl library not found";
    on_complete(resp);
    return false;
}

bool LLMClient::healthCheck() {
    return false;
}

bool LLMClient::chatOpenAI(const std::vector<Message>& messages,
                           const std::vector<json>& tools,
                           LLMResponse& response) {
    (void)messages;
    (void)tools;
    response.success = false;
    response.content = "LLM not available";
    return false;
}

bool LLMClient::chatStreamOpenAI(const std::vector<Message>& messages,
                                 const std::vector<json>& tools,
                                 StreamCallback on_chunk,
                                 CompleteCallback on_complete) {
    (void)messages;
    (void)tools;
    (void)on_chunk;
    LLMResponse resp;
    resp.success = false;
    resp.content = "LLM not available";
    on_complete(resp);
    return false;
}

bool LLMClient::chatAnthropic(const std::vector<Message>& messages,
                              const std::vector<json>& tools,
                              LLMResponse& response) {
    (void)messages;
    (void)tools;
    response.success = false;
    response.content = "LLM not available";
    return false;
}

bool LLMClient::chatStreamAnthropic(const std::vector<Message>& messages,
                                    const std::vector<json>& tools,
                                    StreamCallback on_chunk,
                                    CompleteCallback on_complete) {
    (void)messages;
    (void)tools;
    (void)on_chunk;
    LLMResponse resp;
    resp.success = false;
    resp.content = "LLM not available";
    on_complete(resp);
    return false;
}

bool LLMClient::makeRequest(const std::string& url,
                            const json& body,
                            std::string& response,
                            bool stream) {
    (void)url;
    (void)body;
    (void)stream;
    response = "{\"error\": \"Network not available\"}";
    return false;
}

bool LLMClient::parseOpenAIResponse(const std::string& response, LLMResponse& result) {
    (void)response;
    result.success = false;
    return false;
}

bool LLMClient::parseAnthropicResponse(const std::string& response, LLMResponse& result) {
    (void)response;
    result.success = false;
    return false;
}

bool LLMClient::parseSSEStream(const std::string& data,
                              std::string& content,
                              std::string& tool_name,
                              json& args) {
    (void)data;
    (void)content;
    (void)tool_name;
    (void)args;
    return false;
}
