#include "llm/OpenAIClient.hpp"
#include "message/Message.hpp"
#include "utils/Logger.hpp"

#include <sstream>
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
} // anonymous namespace
#endif

OpenAIClient::OpenAIClient(const std::string& model,
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

OpenAIClient::~OpenAIClient() {
#ifndef NO_CURL
    if (curl_) {
        curl_easy_cleanup(curl_);
    }
#endif
}

bool OpenAIClient::chat(const std::vector<Message>& messages,
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

    return parseResponse(resp, response);
}

bool OpenAIClient::healthCheck() {
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

bool OpenAIClient::makeRequest(const std::string& url,
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

bool OpenAIClient::parseResponse(const std::string& response_str, LLMResponse& response) {
    try {
        json resp = json::parse(response_str);

        if (resp.contains("error")) {
            response.content = resp["error"]["message"].get<std::string>();
            return false;
        }

        if (resp.contains("choices") && !resp["choices"].empty()) {
            auto& choice = resp["choices"][0];
            response.content = choice.value("message", json::object()).value("content", "");

            if (choice.contains("message")) {
                response.role = choice["message"].value("role", "assistant");
            }

            response.stop_reason = choice.value("finish_reason", "");

            // 提取标准工具调用
            if (choice.contains("message") && choice["message"].contains("tool_calls")) {
                for (const auto& tc : choice["message"]["tool_calls"]) {
                    ToolCall call;
                    call.id = tc["id"];
                    call.name = tc["function"]["name"];
                    auto& args_val = tc["function"]["arguments"];
                    if (args_val.is_string()) {
                        call.arguments = json::parse(args_val.get<std::string>());
                    } else {
                        call.arguments = args_val;
                    }
                    response.tool_calls.push_back(call);
                }
            }

            // 尝试解析非标准格式
            if (response.tool_calls.empty() && !response.content.empty()) {
                // 去除首尾空白后检查
                std::string trimmed = response.content;
                while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t' || trimmed.front() == '\n' || trimmed.front() == '\r')) {
                    trimmed.erase(trimmed.begin());
                }
                while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' || trimmed.back() == '\n' || trimmed.back() == '\r')) {
                    trimmed.pop_back();
                }

                // 检查是否是 JSON 数组格式
                if ((trimmed.front() == '[' && trimmed.back() == ']') ||
                    trimmed.find("[{\"type\"") != std::string::npos ||
                    trimmed.find("exec pwd") != std::string::npos ||
                    trimmed.find("exec(") != std::string::npos ||
                    trimmed.find("<tool_call>") != std::string::npos ||
                    trimmed.find("{\"tool_call\"") != std::string::npos) {
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

// ============ 非标准格式解析 ============

namespace {

std::string generateToolId(size_t index) {
    return "tool_" + std::to_string(index);
}

bool extractToolFromJsonObject(const json& j, ToolCall& tc, size_t index) {
    if (j.contains("name") && j.contains("arguments")) {
        tc.id = generateToolId(index);
        tc.name = j["name"].get<std::string>();
        tc.arguments = j["arguments"].is_object() ? j["arguments"] : json::object();
        return true;
    }
    if (j.contains("tool") && j.contains("arguments")) {
        tc.id = generateToolId(index);
        tc.name = j["tool"].get<std::string>();
        tc.arguments = j["arguments"].is_object() ? j["arguments"] : json::object();
        return true;
    }
    // 支持 {"tool": ..., "input": {...}} 格式
    if (j.contains("tool") && j.contains("input")) {
        tc.id = generateToolId(index);
        tc.name = j["tool"].get<std::string>();
        tc.arguments = j["input"].is_object() ? j["input"] : json::object();
        return true;
    }
    // 支持 {"command": "tool_name", "arguments": {...}} 格式
    if (j.contains("command") && j.contains("arguments")) {
        auto cmd = j["command"];
        if (cmd.is_string()) {
            tc.id = generateToolId(index);
            tc.name = cmd.get<std::string>();
            tc.arguments = j["arguments"].is_object() ? j["arguments"] : json::object();
            return true;
        }
    }
    return false;
}

bool tryParseAsSingleToolCall(const std::string& content, std::vector<ToolCall>& tool_calls) {
    try {
        json j = json::parse(content);
        if (j.is_object()) {
            ToolCall tc;
            if (extractToolFromJsonObject(j, tc, tool_calls.size())) {
                tool_calls.push_back(tc);
                Logger::instance().info("JSON单工具格式: name=" + tc.name);
                return true;
            }
            // 提取失败后，不再继续遍历键（避免把 "tool_call" 这样的键名当作工具名）
            return false;
        }
        if (j.is_array()) {
            // 支持 JSON 数组格式: [{"type": "function", "function": {"name": "exec", "arguments": {...}}}]
            for (size_t i = 0; i < j.size(); ++i) {
                const json& item = j[i];
                if (item.is_object() && item.contains("type") && item["type"] == "function") {
                    if (item.contains("function") && item["function"].is_object()) {
                        const json& func = item["function"];
                        ToolCall tc;
                        tc.id = generateToolId(tool_calls.size());
                        tc.name = func.value("name", "");
                        auto args_val = func.value("arguments", json::object());
                        if (args_val.is_string()) {
                            tc.arguments = json::parse(args_val.get<std::string>());
                        } else {
                            tc.arguments = args_val;
                        }
                        if (!tc.name.empty()) {
                            tool_calls.push_back(tc);
                            Logger::instance().info("JSON数组工具格式: name=" + tc.name);
                        }
                    }
                }
            }
            if (!tool_calls.empty()) {
                return true;
            }
        }
    } catch (...) {}
    return false;
}

void tryParseXmlFormat(const std::string& content, std::vector<ToolCall>& tool_calls) {
    std::regex xml_pattern(R"(<tool_call>\s*<function=(\w+)>\s*<parameter=(\w+)>\s*([\s\S]+?)(?:\n|</tool_call>))");
    std::smatch match;
    std::string::const_iterator search_start = content.cbegin();

    while (std::regex_search(search_start, content.cend(), match, xml_pattern)) {
        ToolCall tc;
        tc.id = generateToolId(tool_calls.size());
        tc.name = match[1].str();
        std::string param_name = match[3].str();
        std::string param_value = match[4].str();

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

void tryParseFunctionCallFormat(const std::string& content, std::vector<ToolCall>& tool_calls) {
    std::regex func_pattern(R"((\w+)\s*\(\s*([^)]+)\s*\))");
    std::smatch match;
    if (!std::regex_search(content, match, func_pattern)) return;

    ToolCall tc;
    tc.id = generateToolId(0);
    tc.name = match[1].str();
    // 处理函数调用格式 exec(command="pwd")
    std::string args_str = match[2].str();

    // 首先尝试作为JSON解析
    try {
        json parsed = json::parse(args_str);
        // 确保 arguments 是对象类型
        if (parsed.is_object()) {
            tc.arguments = parsed;
        } else {
            // 如果解析结果不是对象（如纯字符串），用 command 包装
            tc.arguments = json{{"command", parsed.is_string() ? parsed.get<std::string>() : args_str}};
        }
    } catch (...) {
        // 不是JSON，尝试解析 key="value" 格式
        std::regex kv_pattern(R"_((\w+)\s*=\s*"([^"]*)")_");
        std::smatch kv_match;
        if (std::regex_match(args_str, kv_match, kv_pattern)) {
            tc.arguments = json{{kv_match[1].str(), kv_match[2].str()}};
        } else {
            // 如果无法解析，使用command作为键
            tc.arguments = json{{"command", args_str}};
        }
    }
    tool_calls.push_back(tc);
    Logger::instance().info("函数调用格式: name=" + tc.name);
}

} // anonymous namespace

bool OpenAIClient::parseNonStandardToolCall(const std::string& content, std::vector<ToolCall>& tool_calls) {
    tool_calls.clear();

    // 处理纯文本格式: "exec pwd" 或 "read file.txt"
    {
        std::regex simple_pattern(R"(^(\w+)\s+([^\s].*)?$)");
        std::smatch match;
        if (std::regex_match(content, match, simple_pattern)) {
            std::string tool_name = match[1].str();
            std::string args = match.size() > 2 ? match[2].str() : "";
            // 去除首尾空白
            while (!args.empty() && (args.front() == ' ' || args.front() == '\t')) args.erase(args.begin());
            while (!args.empty() && (args.back() == ' ' || args.back() == '\t')) args.pop_back();

            // 判断是工具名还是命令
            if (tool_name == "exec" || tool_name == "read" || tool_name == "write") {
                ToolCall tc;
                tc.id = generateToolId(0);
                tc.name = tool_name;
                if (tool_name == "exec") {
                    tc.arguments = json{{"command", args}};
                } else if (tool_name == "read") {
                    tc.arguments = json{{"path", args}};
                } else if (tool_name == "write") {
                    // write 需要两个参数，这里只处理简单情况
                    tc.arguments = json{{"path", args}};
                }
                tool_calls.push_back(tc);
                Logger::instance().info("纯文本工具格式: name=" + tool_name);
                return true;
            }
        }
    }

    // 尝试解析 call:default_api:exec{"command": "pwd"} 格式
    {
        std::regex call_pattern(R"(call:default_api:(\w+)\{([^}]+)\})");
        std::smatch match;
        if (std::regex_search(content, match, call_pattern)) {
            ToolCall tc;
            tc.id = generateToolId(0);
            tc.name = match[1].str();
            std::string args_str = "{" + match[2].str() + "}";
            try {
                tc.arguments = json::parse(args_str);
            } catch (...) {
                tc.arguments = json{{"command", match[2].str()}};
            }
            tool_calls.push_back(tc);
            Logger::instance().info("call:default_api 格式: name=" + tc.name);
            return true;
        }
    }

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
