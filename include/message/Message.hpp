#pragma once

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace ClawAgent {

// 消息结构 - 用于LLM
struct Message {
    std::string role;        // "user", "assistant", "system", "tool"
    std::string content;
    std::string name;        // for tool messages
    std::string tool_call_id; // for tool messages
    std::vector<json> content_blocks; // for assistant messages with tool_use

    Message() = default;
    Message(const std::string& r, const std::string& c) : role(r), content(c) {}

    json toJson() const;
    static Message fromJson(const json& j);
};

// Chat消息结构 - 用于消息管理
struct ChatMessage {
    std::string role;
    std::string content;
    std::string timestamp;
    std::string tool_call_id;
    std::string tool_name;
    std::vector<json> content_blocks; // for assistant messages with tool_use

    ChatMessage() = default;
    ChatMessage(const std::string& r, const std::string& c)
        : role(r), content(c) {}

    json toJson() const;
    static ChatMessage fromJson(const json& j);
};

} // namespace ClawAgent
