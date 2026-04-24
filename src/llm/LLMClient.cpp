#include "llm/LLMClient.hpp"
#include "message/Message.hpp"
#include "utils/Logger.hpp"

#include <sstream>
#include <cstring>
#include <algorithm>
#include <regex>

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

size_t headerCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    (void)contents;
    (void)userp;
    return size * nmemb;
}

} // anonymous namespace
#endif

json ClawAgent::ToolResult::toJson() const {
    return json{
        {"tool_call_id", tool_call_id},
        {"content", content},
        {"success", success}
    };
}

json ClawAgent::LLMResponse::toJson() const {
    json j;
    j["content"] = content;
    j["role"] = role;
    j["is_complete"] = is_complete;
    j["stop_reason"] = stop_reason;

    if (!tool_calls.empty()) {
        json tools = json::array();
        for (const auto& tc : tool_calls) {
            tools.push_back(json{
                {"id", tc.id},
                {"name", tc.name},
                {"arguments", tc.arguments}
            });
        }
        j["tool_calls"] = tools;
    }

    return j;
}

// ============ LLMClient Implementation ============

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
#ifndef NO_CURL
    , curl_(curl_easy_init()) {

    if (!curl_) {
        throw std::runtime_error("无法初始化CURL");
    }

    // 设置默认选项
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
#else
    , curl_(nullptr) {
#endif
}

LLMClient::~LLMClient() {
#ifndef NO_CURL
    if (curl_) {
        curl_easy_cleanup(curl_);
    }
#endif
}

bool LLMClient::chat(const std::vector<Message>& messages,
                     const std::vector<json>& tools,
                     LLMResponse& response,
                     TerminalOutputCallback terminal_output) {
    // 工具调用时使用非流式版本，因为需要完整解析工具调用
    // 流式主要用于纯文本响应
    if (stream_ && tools.empty()) {
        std::string accumulated;
        bool success = chatStream(messages, tools,
            [&accumulated, &terminal_output](const std::string& chunk, bool is_final) {
                if (!is_final) {
                    accumulated += chunk;
                    // 如果提供了终端输出回调，立即输出
                    if (terminal_output) {
                        terminal_output(chunk);
                    }
                }
            },
            [&response](const LLMResponse& final_resp) {
                response = final_resp;
            });

        if (success && response.content.empty()) {
            response.content = accumulated;
        }
        return success;
    }

    if (provider_ == "openai") {
        return chatOpenAI(messages, tools, response);
    } else if (provider_ == "anthropic") {
        return chatAnthropic(messages, tools, response);
    }
    return false;
}

bool LLMClient::chatStream(const std::vector<Message>& messages,
                           const std::vector<json>& tools,
                           StreamCallback on_chunk,
                           CompleteCallback on_complete) {
    if (provider_ == "openai") {
        return chatStreamOpenAI(messages, tools, on_chunk, on_complete);
    } else if (provider_ == "anthropic") {
        return chatStreamAnthropic(messages, tools, on_chunk, on_complete);
    }
    return false;
}

bool LLMClient::healthCheck() {
    std::string test_url = base_url_ + "/models";
    std::string response;

    curl_easy_setopt(curl_, CURLOPT_URL, test_url.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key_).c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    bool success = makeRequest(test_url, json::object(), response);
    curl_slist_free_all(headers);

    return success;
}

bool LLMClient::makeRequest(const std::string& url,
                            const json& body,
                            std::string& response,
                            bool stream) {
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());

    if (!stream) {
        curl_easy_setopt(curl_, CURLOPT_POST, 1L);
    }

    std::string body_str = body.dump();
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body_str.c_str());

    response.clear();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key_).c_str());
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

bool LLMClient::chatOpenAI(const std::vector<Message>& messages,
                           const std::vector<json>& tools,
                           LLMResponse& response) {
    json body;
    body["model"] = model_;
    body["stream"] = false;

    json msgs = json::array();
    for (const auto& msg : messages) {
        msgs.push_back(msg.toJson());
    }
    body["messages"] = msgs;

    if (!tools.empty()) {
        body["tools"] = tools;
        body["tool_choice"] = "auto";
    }

    std::string url = base_url_ + "/chat/completions";
    std::string resp;

    if (!makeRequest(url, body, resp)) {
        return false;
    }

    return parseOpenAIResponse(resp, response);
}

bool LLMClient::chatStreamOpenAI(const std::vector<Message>& messages,
                                 const std::vector<json>& tools,
                                 StreamCallback on_chunk,
                                 CompleteCallback on_complete) {
    json body;
    body["model"] = model_;
    body["stream"] = true;

    json msgs = json::array();
    for (const auto& msg : messages) {
        msgs.push_back(msg.toJson());
    }
    body["messages"] = msgs;

    if (!tools.empty()) {
        body["tools"] = tools;
        body["tool_choice"] = "auto";
    }

    std::string url = base_url_ + "/chat/completions";
    std::string response;

    // 设置流式选项
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);

    std::string body_str = body.dump();
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body_str.c_str());

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key_).c_str());
    headers = curl_slist_append(headers, "Accept: text/event-stream");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl_);

    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        on_chunk("", true);
        Logger::instance().error("流式请求失败: " + std::string(curl_easy_strerror(res)));
        return false;
    }

    // 解析SSE流
    std::stringstream ss(response);
    std::string line;
    std::string full_content;

    while (std::getline(ss, line)) {
        if (line.substr(0, 6) == "data: ") {
            std::string data = line.substr(6);
            if (data == "[DONE]") {
                break;
            }

            std::string content, tool_name;
            json args;
            if (parseSSEStream(data, content, tool_name, args)) {
                if (!content.empty()) {
                    on_chunk(content, false);
                    full_content += content;
                }
            }
        }
    }

    LLMResponse final_response;
    final_response.content = full_content;
    final_response.is_complete = true;
    on_complete(final_response);

    return true;
}

bool LLMClient::parseOpenAIResponse(const std::string& response_str,
                                    LLMResponse& response) {
    try {
        json resp = json::parse(response_str);

        if (resp.contains("error")) {
            response.content = resp["error"]["message"].get<std::string>();
            return false;
        }

        // 提取内容
        if (resp.contains("choices") && !resp["choices"].empty()) {
            auto& choice = resp["choices"][0];
            response.content = choice.value("message", json::object()).value("content", "");

            if (choice.contains("message")) {
                response.role = choice["message"].value("role", "assistant");
            }

            response.stop_reason = choice.value("finish_reason", "");

            // 提取工具调用
            if (choice.contains("message") && choice["message"].contains("tool_calls")) {
                for (const auto& tc : choice["message"]["tool_calls"]) {
                    ToolCall call;
                    call.id = tc["id"];
                    call.name = tc["function"]["name"];
                    // arguments can be string or already-parsed object
                    auto& args_val = tc["function"]["arguments"];
                    if (args_val.is_string()) {
                        call.arguments = json::parse(args_val.get<std::string>());
                    } else {
                        call.arguments = args_val;
                    }
                    response.tool_calls.push_back(call);
                }
            }

            // 如果没有标准工具调用，尝试解析 qwen3.6-plus-cc 非标准格式
            if (response.tool_calls.empty() && !response.content.empty()) {
                if (response.content.find("<tool_call>") != std::string::npos ||
                    response.content.find("{\"tool_call\"") != std::string::npos ||
                    response.content.find("\"name\"") != std::string::npos ||
                    response.content.find("exec(") != std::string::npos) {
                    if (parseNonStandardToolCall(response.content, response.tool_calls)) {
                        Logger::instance().info("检测到非标准工具调用格式");
                    }
                }
            }
        }

        response.is_complete = true;
        return true;

    } catch (const json::parse_error& e) {
        Logger::instance().error("解析响应失败: " + std::string(e.what()));
        return false;
    }
}

bool LLMClient::parseSSEStream(const std::string& data,
                              std::string& content,
                              std::string& tool_name,
                              json& args) {
    try {
        json chunk = json::parse(data);

        if (chunk.contains("choices") && !chunk["choices"].empty()) {
            auto& delta = chunk["choices"][0]["delta"];

            if (delta.contains("content")) {
                content = delta["content"];
            }

            if (delta.contains("tool_calls")) {
                for (const auto& tc : delta["tool_calls"]) {
                    if (tc.contains("function")) {
                        tool_name = tc["function"].value("name", "");
                        std::string args_str = tc["function"].value("arguments", "");
                        if (!args_str.empty()) {
                            args = json::parse(args_str);
                        }
                    }
                }
            }
        }

        return true;
    } catch (...) {
        return false;
    }
}

// 解析非标准工具调用格式
// 支持多种LLM输出格式:
// 格式1 (XML格式): <tool_call>\n<function=NAME>\n<parameter=KEY>\nVALUE\n
// 格式2 (JSON格式): {"tool_call":{"function":{"name":"exec","arguments":{"command":"ls -la"}}}}
// 格式3: {"name": "...", "arguments": {...}} 或 {"tool": "...", "arguments": {...}}
// 格式4: {"name": {...}} (工具名作为键，参数作为值)
// 格式5: exec(pwd && ls -l)

namespace {

// 生成唯一的工具调用ID
std::string generateToolId(size_t index) {
    return "tool_" + std::to_string(index);
}

// 从JSON对象中提取工具调用信息
// 返回true如果成功提取
bool extractToolFromJsonObject(const json& j, ToolCall& tc, size_t index) {
    // 检查 {"name": "...", "arguments": {...}} 格式
    if (j.contains("name") && j.contains("arguments")) {
        tc.id = generateToolId(index);
        tc.name = j["name"].get<std::string>();
        tc.arguments = j["arguments"].is_object() ? j["arguments"] : json::object();
        return true;
    }
    // 检查 {"tool": "...", "arguments": {...}} 格式
    if (j.contains("tool") && j.contains("arguments")) {
        tc.id = generateToolId(index);
        tc.name = j["tool"].get<std::string>();
        tc.arguments = j["arguments"].is_object() ? j["arguments"] : json::object();
        return true;
    }
    return false;
}

// 尝试将内容解析为单一工具调用
// 返回true如果成功
bool tryParseAsSingleToolCall(const std::string& content, std::vector<ToolCall>& tool_calls) {
    try {
        json j = json::parse(content);
        if (!j.is_object()) return false;

        ToolCall tc;
        if (extractToolFromJsonObject(j, tc, tool_calls.size())) {
            tool_calls.push_back(tc);
            Logger::instance().info("JSON单工具格式: name=" + tc.name);
            return true;
        }

        // 检查 {"exec": {"command": "..."}} 格式 (工具名作为键)
        for (auto& [key, value] : j.items()) {
            if (value.is_object() && !key.empty()) {
                tc.id = generateToolId(tool_calls.size());
                tc.name = key;
                tc.arguments = value;
                tool_calls.push_back(tc);
                Logger::instance().info("JSON工具名作为键格式: name=" + tc.name);
                return true;
            }
        }
    } catch (...) {
        // JSON解析失败
    }
    return false;
}

// 尝试解析XML格式的工具调用
// 格式: <tool_call>\n<function=NAME>\n<parameter=KEY>\nVALUE\n
void tryParseXmlFormat(const std::string& content, std::vector<ToolCall>& tool_calls) {
    std::regex xml_pattern(R"(<tool_call>\s*<function=(\w+)>\s*<parameter=(\w+)>\s*([\s\S]+?)(?:\n|$))");
    std::smatch match;
    std::string::const_iterator search_start = content.cbegin();

    while (std::regex_search(search_start, content.cend(), match, xml_pattern)) {
        ToolCall tc;
        tc.id = generateToolId(tool_calls.size());
        tc.name = match[1].str();
        std::string param_name = match[3].str();
        std::string param_value = match[4].str();

        // 解析参数值
        try {
            std::string trimmed = param_value;
            while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r')) {
                trimmed.pop_back();
            }
            tc.arguments = json::parse(trimmed);
        } catch (...) {
            std::string trimmed = param_value;
            while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r')) {
                trimmed.pop_back();
            }
            size_t start = trimmed.find_first_not_of(" \t\r\n");
            if (start != std::string::npos) {
                trimmed = trimmed.substr(start);
            }
            tc.arguments = json{{param_name, trimmed}};
        }

        tool_calls.push_back(tc);
        search_start = match.suffix().first;
    }
}

// 尝试解析 {"tool_call": {...}} 格式
void tryParseToolCallJsonFormat(const std::string& content, std::vector<ToolCall>& tool_calls) {
    std::regex json_pattern(R"(\{"tool_call":\s*\{)");
    if (!std::regex_search(content, json_pattern)) return;

    try {
        size_t start_pos = content.find("{\"tool_call\"");
        if (start_pos == std::string::npos) return;

        size_t brace_count = 0;
        size_t end_pos = start_pos;
        for (size_t i = start_pos; i < content.size(); ++i) {
            if (content[i] == '{') brace_count++;
            else if (content[i] == '}') {
                brace_count--;
                if (brace_count == 0) {
                    end_pos = i + 1;
                    break;
                }
            }
        }

        std::string tool_json_str = content.substr(start_pos, end_pos - start_pos);
        json tool_json = json::parse(tool_json_str);

        if (tool_json.contains("tool_call") && tool_json["tool_call"].contains("function")) {
            ToolCall tc;
            tc.id = generateToolId(0);
            tc.name = tool_json["tool_call"]["function"].value("name", "");
            tc.arguments = tool_json["tool_call"]["function"].value("arguments", json::object());
            tool_calls.push_back(tc);
        }
    } catch (const json::parse_error& e) {
        Logger::instance().warning("解析 tool_call JSON 格式失败: " + std::string(e.what()));
    }
}

// 尝试解析函数调用格式: exec(pwd && ls -l)
void tryParseFunctionCallFormat(const std::string& content, std::vector<ToolCall>& tool_calls) {
    std::regex func_pattern(R"((\w+)\s*\(\s*([^)]+)\s*\))");
    std::smatch match;
    if (!std::regex_search(content, match, func_pattern)) return;

    ToolCall tc;
    tc.id = generateToolId(0);
    tc.name = match[1].str();
    std::string args_str = match[2].str();

    if (args_str.find("{") != std::string::npos) {
        try {
            tc.arguments = json::parse(args_str);
        } catch (...) {
            tc.arguments = json{{"command", args_str}};
        }
    } else {
        tc.arguments = json{{"command", args_str}};
    }
    tool_calls.push_back(tc);
    Logger::instance().info("函数调用格式: name=" + tc.name);
}

} // anonymous namespace

bool LLMClient::parseNonStandardToolCall(const std::string& content, std::vector<ToolCall>& tool_calls) {
    tool_calls.clear();

    // 提取 <tool_call> 和 </tool_call> 之间的内容
    std::string inner_content = content;
    size_t start = content.find("<tool_call>");
    if (start != std::string::npos) {
        size_t end = content.find("</tool_call>", start);
        if (end != std::string::npos) {
            inner_content = content.substr(start + 11, end - start - 11);
            while (!inner_content.empty() && (inner_content.front() == '\n' || inner_content.front() == '\r' || inner_content.front() == ' ')) {
                inner_content.erase(0, 1);
            }
            while (!inner_content.empty() && (inner_content.back() == '\n' || inner_content.back() == '\r' || inner_content.back() == ' ')) {
                inner_content.pop_back();
            }
        }
    }

    // 1. 尝试解析inner_content为JSON格式
    if (tryParseAsSingleToolCall(inner_content, tool_calls)) {
        return true;
    }

    // 2. 尝试XML格式
    size_t xml_count_before = tool_calls.size();
    tryParseXmlFormat(content, tool_calls);
    if (tool_calls.size() > xml_count_before) {
        return true;
    }

    // 3. 尝试 {"tool_call": {...}} 格式
    size_t tool_call_count_before = tool_calls.size();
    tryParseToolCallJsonFormat(content, tool_calls);
    if (tool_calls.size() > tool_call_count_before) {
        return true;
    }

    // 4. 尝试函数调用格式: exec(pwd && ls -l)
    size_t func_count_before = tool_calls.size();
    tryParseFunctionCallFormat(content, tool_calls);
    if (tool_calls.size() > func_count_before) {
        return true;
    }

    return !tool_calls.empty();
}

bool LLMClient::chatAnthropic(const std::vector<Message>& messages,
                              const std::vector<json>& tools,
                              LLMResponse& response) {
    // Anthropic API格式
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
        } else {
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

    // Anthropic需要特殊的header
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);

    std::string body_str = body.dump();
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body_str.c_str());

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("x-api-key: " + api_key_).c_str());
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &resp);

    CURLcode res = curl_easy_perform(curl_);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        Logger::instance().error("Anthropic请求失败: " + std::string(curl_easy_strerror(res)));
        return false;
    }

    return parseAnthropicResponse(resp, response);
}

bool LLMClient::chatStreamAnthropic(const std::vector<Message>& messages,
                                    const std::vector<json>& tools,
                                    StreamCallback on_chunk,
                                    CompleteCallback on_complete) {
    // Anthropic API流式响应格式
    json body;
    body["model"] = model_;
    body["max_tokens"] = 4096;
    body["stream"] = true;

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
        } else {
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
    std::string response;

    // 设置流式请求
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);

    std::string body_str = body.dump();
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body_str.c_str());

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("x-api-key: " + api_key_).c_str());
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    headers = curl_slist_append(headers, "Accept: text/event-stream");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl_);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        Logger::instance().error("流式请求失败: " + std::string(curl_easy_strerror(res)));
        return false;
    }

    // 解析SSE流
    LLMResponse final_response;
    final_response.is_streaming = true;

    std::stringstream ss(response);
    std::string line;
    std::string current_event;
    std::string text_content;
    std::vector<ToolCall> tool_calls;
    bool message_complete = false;

    while (std::getline(ss, line)) {
        // 去掉尾部的\r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.substr(0, 7) == "event: ") {
            current_event = line.substr(7);
        } else if (line.substr(0, 6) == "data: ") {
            std::string data_str = line.substr(6);

            if (current_event == "content_block_delta") {
                try {
                    json data = json::parse(data_str);
                    if (data.contains("delta")) {
                        std::string delta_type = data["delta"].value("type", "");
                        if (delta_type == "text_delta") {
                            std::string text = data["delta"].value("text", "");
                            text_content += text;
                            on_chunk(text, false);
                        } else if (delta_type == "input_json_delta") {
                            // 工具参数增量（简化处理）
                            std::string partial = data["delta"].value("partial_json", "");
                            // 这里简化处理，实际应该跟踪参数构建
                        }
                    }
                } catch (...) {
                    // 忽略解析错误
                }
            } else if (current_event == "content_block_start") {
                try {
                    json data = json::parse(data_str);
                    if (data.contains("content_block")) {
                        std::string block_type = data["content_block"].value("type", "");
                        if (block_type == "tool_use") {
                            ToolCall tc;
                            tc.id = data["content_block"].value("id", "");
                            tc.name = data["content_block"].value("name", "");
                            tool_calls.push_back(tc);
                        }
                    }
                } catch (...) {
                    // 忽略解析错误
                }
            } else if (current_event == "message_stop") {
                message_complete = true;
            }
        }
    }

    final_response.content = text_content;
    final_response.is_complete = message_complete;
    final_response.tool_calls = tool_calls;
    final_response.role = "assistant";

    on_complete(final_response);

    return true;
}

bool LLMClient::parseAnthropicResponse(const std::string& response_str,
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

// ============ MessageFormatter Implementation ============

MessageFormatter::MessageFormatter(Provider provider)
    : provider_(provider) {
}

json MessageFormatter::formatMessages(const std::vector<Message>& messages,
                                     const std::vector<json>& tools) const {
    json result;

    if (provider_ == Provider::OpenAI) {
        result["model"] = "gpt-4";
        json msg_array = json::array();
        for (const auto& msg : messages) {
            msg_array.push_back(msg.toJson());
        }
        result["messages"] = msg_array;
        if (!tools.empty()) {
            result["tools"] = tools;
        }
    } else if (provider_ == Provider::Anthropic) {
        result["model"] = "claude-3";
        result["max_tokens"] = 4096;
        json msg_array = json::array();
        for (const auto& msg : messages) {
            msg_array.push_back(msg.toJson());
        }
        result["messages"] = msg_array;
        if (!tools.empty()) {
            result["tools"] = tools;
        }
    }

    return result;
}

std::vector<ToolCall> MessageFormatter::extractToolCalls(const LLMResponse& response) const {
    return response.tool_calls;
}

std::string MessageFormatter::extractContent(const json& response) const {
    if (provider_ == Provider::OpenAI) {
        if (response.contains("choices") && !response["choices"].empty()) {
            return response["choices"][0]["message"]["content"];
        }
    } else if (provider_ == Provider::Anthropic) {
        if (response.contains("content")) {
            std::string content;
            for (const auto& block : response["content"]) {
                if (block["type"] == "text") {
                    content += block["text"];
                }
            }
            return content;
        }
    }
    return "";
}
