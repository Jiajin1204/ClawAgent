#include "llm/AnthropicClient.hpp"
#include "message/Message.hpp"
#include "utils/Logger.hpp"

#include <sstream>

#ifndef NO_CURL
#include <curl/curl.h>
#endif

using namespace ClawAgent;

#ifndef NO_CURL
namespace {
size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLen = size * nmemb;
    try {
        s->append((char*)contents, newLen);
    } catch (std::bad_alloc& e) {
        return 0;
    }
    return newLen;
}
} // anonymous namespace
#endif

AnthropicClient::AnthropicClient(const std::string& model,
                                 const std::string& api_key,
                                 const std::string& base_url,
                                 bool stream,
                                 int timeout_ms)
    : model_(model)
    , api_key_(api_key)
    , base_url_(base_url)
    , stream_(stream)
    , timeout_ms_(timeout_ms)
#ifndef NO_CURL
    , curl_(curl_easy_init()) {

    if (!curl_) {
        throw std::runtime_error("无法初始化CURL");
    }
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
#else
    , curl_(nullptr) {
#endif
}

AnthropicClient::~AnthropicClient() {
#ifndef NO_CURL
    if (curl_) {
        curl_easy_cleanup(curl_);
    }
#endif
}

bool AnthropicClient::chat(const std::vector<Message>& messages,
                          const std::vector<json>& tools,
                          LLMResponse& response) {
    json body;
    body["model"] = model_;
    body["max_tokens"] = 4096;

    // 构建消息
    json msgs = json::array();
    std::string system_prompt;

    for (const auto& msg : messages) {
        if (msg.role == "system") {
            system_prompt += msg.content + "\n";
        } else if (msg.role == "tool") {
            // Anthropic工具结果格式
            json tool_result = json::array();
            tool_result.push_back(json{
                {"type", "tool_result"},
                {"tool_use_id", msg.tool_call_id},
                {"content", msg.content}
            });
            msgs.push_back(json{
                {"role", "user"},
                {"content", tool_result}
            });
        } else if (msg.role == "assistant") {
            // Anthropic assistant message with content blocks
            json anthropic_msg;
            anthropic_msg["role"] = "assistant";

            // Handle content_blocks (tool_use blocks from Anthropic format)
            if (!msg.content_blocks.empty()) {
                json content = json::array();
                for (const auto& block : msg.content_blocks) {
                    if (block.contains("type") && block["type"] == "tool_use") {
                        content.push_back(json{
                            {"type", "tool_use"},
                            {"id", block.value("id", "")},
                            {"name", block.value("name", "")},
                            {"input", block.value("input", json::object())}
                        });
                    } else if (block.contains("type") && block["type"] == "text") {
                        content.push_back(block);
                    }
                }
                // If we have text content, add it as a text block
                if (!msg.content.empty()) {
                    content.push_back(json{{"type", "text"}, {"text", msg.content}});
                }
                anthropic_msg["content"] = content;
            } else if (!msg.content.empty()) {
                // Simple text message
                anthropic_msg["content"] = msg.content;
            } else {
                anthropic_msg["content"] = "";
            }
            msgs.push_back(anthropic_msg);
        } else {
            // user or other roles - use standard format
            msgs.push_back(msg.toJson());
        }
    }

    if (!system_prompt.empty()) {
        body["system"] = system_prompt;
    }
    body["messages"] = msgs;

    if (!tools.empty()) {
        json anthropic_tools = json::array();
        for (const auto& tool : tools) {
            json t;
            t["name"] = tool["name"];
            t["description"] = tool["description"];
            t["input_schema"] = tool["parameters"];
            anthropic_tools.push_back(t);
        }
        body["tools"] = anthropic_tools;
    }

    std::string url = base_url_ + "/messages";
    std::string resp;

    if (!makeRequest(url, body, resp)) {
        return false;
    }

    return parseResponse(resp, response);
}

bool AnthropicClient::healthCheck() {
    std::string test_url = base_url_ + "/models";
    std::string response;

    curl_easy_setopt(curl_, CURLOPT_URL, test_url.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("x-api-key: " + api_key_).c_str());
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    bool success = makeRequest(test_url, json::object(), response);
    curl_slist_free_all(headers);

    return success;
}

bool AnthropicClient::makeRequest(const std::string& url,
                                 const json& body,
                                 std::string& response,
                                 bool stream) {
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);

    std::string body_str = body.dump();
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body_str.c_str());

    response.clear();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("x-api-key: " + api_key_).c_str());
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    if (stream) {
        headers = curl_slist_append(headers, "Accept: text/event-stream");
    }
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl_);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        Logger::instance().error("CURL请求失败: " + std::string(curl_easy_strerror(res)));
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code >= 400) {
        std::string error_msg;
        switch (http_code) {
            case 400: error_msg = "请求参数错误"; break;
            case 401: error_msg = "认证失败，请检查 API Key"; break;
            case 403: error_msg = "访问被拒绝"; break;
            case 404: error_msg = "资源不存在"; break;
            case 429: error_msg = "请求过于频繁，请稍后重试"; break;
            case 500: error_msg = "服务器内部错误"; break;
            case 502: error_msg = "网关错误"; break;
            case 503: error_msg = "服务暂不可用"; break;
            default: error_msg = "未知错误";
        }
        Logger::instance().error("HTTP " + std::to_string(http_code) + " - " + error_msg + ": " + response);
        return false;
    }

    return true;
}

bool AnthropicClient::parseResponse(const std::string& response_str,
                                   LLMResponse& response) {
    try {
        json resp = json::parse(response_str);

        if (resp.contains("error")) {
            response.content = resp["error"]["message"].get<std::string>();
            return false;
        }

        // Anthropic响应格式
        if (resp.contains("content")) {
            for (const auto& block : resp["content"]) {
                if (block["type"] == "text") {
                    response.content += block["text"];
                } else if (block["type"] == "tool_use") {
                    ToolCall call;
                    // MiniMax可能使用tool_use_id而不是id
                    call.id = block.value("tool_use_id", block.value("id", ""));
                    call.name = block["name"];
                    call.arguments = block["input"];
                    Logger::instance().info("Tool call: id=" + call.id + ", name=" + call.name);
                    response.tool_calls.push_back(call);
                }
            }
        }

        response.role = "assistant";
        response.is_complete = true;
        response.stop_reason = resp.value("stop_reason", "");

        return true;

    } catch (const json::parse_error& e) {
        Logger::instance().error("解析Anthropic响应失败: " + std::string(e.what()));
        return false;
    }
}