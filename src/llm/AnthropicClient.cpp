#include "llm/AnthropicClient.hpp"
#include "message/Message.hpp"
#include "utils/Logger.hpp"

#include <sstream>
#include <errno.h>

#ifndef NO_CURL
#include <curl/curl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
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

struct SocketContext {
    AnthropicClient* client;
    curl_socket_t wakeup_fd;
};
} // anonymous namespace

// CURLMOPT_SOCKETFUNCTION callback - defined outside anonymous namespace as class static member
int AnthropicClient::socketCallback(CURL* easy, curl_socket_t s, int action, void* userp) {
    (void)easy;
    SocketContext* ctx = static_cast<SocketContext*>(userp);
    if (!ctx || !ctx->client) {
        return 0;
    }

    switch (action) {
        case CURL_POLL_IN:
        case CURL_POLL_OUT:
        case CURL_POLL_INOUT:
            ctx->client->onCurlSocket(s, action);
            break;
        case CURL_POLL_REMOVE:
            ctx->client->onCurlSocketRemove(s);
            break;
        default:
            break;
    }
    return 0;
}

// CURLMOPT_TIMERFUNCTION callback
int AnthropicClient::timerCallback(CURLM* multi, long timeout_ms, void* userp) {
    (void)multi;
    (void)timeout_ms;
    (void)userp;
    return 0;
}
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
    , curl_(curl_easy_init())
    , curl_multi_(curl_multi_init())
{

    if (!curl_) {
        throw std::runtime_error("无法初始化CURL");
    }
    if (!curl_multi_) {
        curl_easy_cleanup(curl_);
        throw std::runtime_error("无法初始化CURL multi");
    }

    // 创建socket pair用于wakeup
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, wakeup_fds_) != 0) {
        curl_multi_cleanup(curl_multi_);
        curl_easy_cleanup(curl_);
        throw std::runtime_error("无法创建wakeup socket pair");
    }

    // 设置socket callback和timer callback
    SocketContext* ctx = new SocketContext{this, wakeup_fds_[0]};
    curl_multi_setopt(curl_multi_, CURLMOPT_SOCKETFUNCTION, socketCallback);
    curl_multi_setopt(curl_multi_, CURLMOPT_SOCKETDATA, ctx);

    curl_multi_setopt(curl_multi_, CURLMOPT_TIMERFUNCTION, timerCallback);
    curl_multi_setopt(curl_multi_, CURLMOPT_TIMERDATA, ctx);

    curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_multi_setopt(curl_multi_, CURLMOPT_MAX_CONCURRENT_STREAMS, 1L);
#else
    , curl_(nullptr) {
#endif
}

AnthropicClient::~AnthropicClient() {
#ifndef NO_CURL
    // 清理socket context
    curl_multi_setopt(curl_multi_, CURLMOPT_SOCKETDATA, nullptr);
    curl_multi_setopt(curl_multi_, CURLMOPT_TIMERDATA, nullptr);

    // 关闭wakeup sockets
    if (wakeup_fds_[0] >= 0) {
        close(wakeup_fds_[0]);
    }
    if (wakeup_fds_[1] >= 0) {
        close(wakeup_fds_[1]);
    }

    if (curl_multi_) {
        curl_multi_cleanup(curl_multi_);
    }
    if (curl_) {
        curl_easy_cleanup(curl_);
    }
#endif
}

void AnthropicClient::abort() {
    Logger::instance().info("AnthropicClient::abort() called");
    abort_requested_.store(true, std::memory_order_relaxed);
#ifndef NO_CURL
    // 关闭wakeup socket，唤醒poll循环
    if (wakeup_fds_[1] >= 0) {
        close(wakeup_fds_[1]);
        wakeup_fds_[1] = -1;
    }
#endif
}

#ifndef NO_CURL
void AnthropicClient::onCurlSocket(curl_socket_t s, int action) {
    int events = 0;
    if (action == CURL_POLL_IN || action == CURL_POLL_INOUT) {
        events |= POLLIN;
    }
    if (action == CURL_POLL_OUT || action == CURL_POLL_INOUT) {
        events |= POLLOUT;
    }
    curl_sockets_[s] = events;
}

void AnthropicClient::onCurlSocketRemove(curl_socket_t s) {
    curl_sockets_.erase(s);
}
#endif

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
            // getToolDefinitions returns OpenAI format: {"type":"function","function":{"name":...,"description":...,"parameters":...}}
            // Anthropic expects: {"name":...,"description":...,"input_schema":...}
            t["name"] = tool["function"].value("name", "");
            t["description"] = tool["function"].value("description", "");
            t["input_schema"] = tool["function"].value("parameters", json::object());
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
    abort_requested_.store(false, std::memory_order_relaxed);
#ifndef NO_CURL
    curl_sockets_.clear();

    // 如果wakeup socket被之前的abort关闭了，需要重新创建
    if (wakeup_fds_[1] < 0) {
        // 关闭旧的读端（如果还有效）
        if (wakeup_fds_[0] >= 0) {
            close(wakeup_fds_[0]);
            wakeup_fds_[0] = -1;
        }
        // 重新创建socket pair
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, wakeup_fds_) != 0) {
            Logger::instance().error("无法重新创建wakeup socket pair");
        }
    }
#endif

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

    // 使用 curl_multi 进行可中止的请求
    curl_multi_remove_handle(curl_multi_, curl_);
    curl_multi_add_handle(curl_multi_, curl_);

    int still_running = 1;
    CURLMcode mres = curl_multi_perform(curl_multi_, &still_running);

#ifndef NO_CURL
    // 使用自己的poll循环，能够被wakeup socket中止
    while (mres == CURLM_OK && still_running && !abort_requested_.load(std::memory_order_relaxed)) {
        // 构建poll fd数组
        std::vector<struct pollfd> pollfds;

        // 添加curl的socket
        for (const auto& pair : curl_sockets_) {
            struct pollfd pfd;
            pfd.fd = pair.first;
            pfd.events = pair.second;
            pfd.revents = 0;
            pollfds.push_back(pfd);
        }

        // 添加wakeup socket (读端)
        if (wakeup_fds_[0] >= 0) {
            struct pollfd pfd;
            pfd.fd = wakeup_fds_[0];
            pfd.events = POLLIN;
            pfd.revents = 0;
            pollfds.push_back(pfd);
        }

        // 调用poll，等待事件或wakeup
        int poll_res = poll(pollfds.data(), pollfds.size(), 1000);

        if (poll_res < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (poll_res == 0) {
            // 超时
            mres = curl_multi_perform(curl_multi_, &still_running);
            continue;
        }

        // 检查wakeup socket是否触发
        bool wakeup_triggered = false;
        for (const auto& pfd : pollfds) {
            if (pfd.fd == wakeup_fds_[0] && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
                wakeup_triggered = true;
                break;
            }
        }

        if (wakeup_triggered) {
            Logger::instance().info("请求被中止 (wakeup triggered)");
            break;
        }

        // 处理curl socket事件
        for (const auto& pfd : pollfds) {
            if (pfd.fd == wakeup_fds_[0]) continue;
            if (pfd.revents) {
                int ev = 0;
                if (pfd.revents & (POLLIN | POLLERR | POLLHUP)) ev |= CURL_CSELECT_IN;
                if (pfd.revents & POLLOUT) ev |= CURL_CSELECT_OUT;
                mres = curl_multi_socket_action(curl_multi_, pfd.fd, ev, &still_running);
            }
        }
    }
#else
    while (mres == CURLM_OK && still_running) {
        int numfds = 0;
        mres = curl_multi_wait(curl_multi_, nullptr, 0, 1000, &numfds);
        if (mres != CURLM_OK) {
            break;
        }
        mres = curl_multi_perform(curl_multi_, &still_running);
    }
#endif

    curl_multi_remove_handle(curl_multi_, curl_);
    curl_slist_free_all(headers);

    // 检查是否被中止
    if (abort_requested_.load(std::memory_order_relaxed)) {
        Logger::instance().info("请求被中止");
        return false;
    }

    // 获取结果
    CURLMsg* msg = nullptr;
    int msgs_left = 0;
    CURLcode res = CURLE_OK;
    while ((msg = curl_multi_info_read(curl_multi_, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            res = msg->data.result;
            break;
        }
    }

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
