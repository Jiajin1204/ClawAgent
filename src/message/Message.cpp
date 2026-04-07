#include "message/Message.hpp"
#include "utils/Logger.hpp"

using namespace ClawAgent;

Message ClawAgent::Message::fromJson(const json& j) {
    Message msg;
    msg.role = j.value("role", "user");
    msg.content = j.value("content", "");

    if (j.contains("name")) {
        msg.name = j["name"];
    }
    if (j.contains("tool_call_id")) {
        msg.tool_call_id = j["tool_call_id"];
    }

    return msg;
}

json ClawAgent::Message::toJson() const {
    json j;
    j["role"] = role;

    // 如果有content_blocks，使用它们作为content
    if (!content_blocks.empty()) {
        j["content"] = content_blocks;
    } else {
        j["content"] = content;
    }

    if (!name.empty()) {
        j["name"] = name;
    }
    if (!tool_call_id.empty()) {
        j["tool_call_id"] = tool_call_id;
    }

    return j;
}

ChatMessage ClawAgent::ChatMessage::fromJson(const json& j) {
    ChatMessage msg;
    msg.role = j.value("role", "user");
    msg.content = j.value("content", "");
    msg.timestamp = j.value("timestamp", "");
    msg.tool_call_id = j.value("tool_call_id", "");
    msg.tool_name = j.value("tool_name", "");
    return msg;
}

json ClawAgent::ChatMessage::toJson() const {
    json j;
    j["role"] = role;

    if (!content_blocks.empty()) {
        j["content"] = content_blocks;
    } else {
        j["content"] = content;
    }

    j["timestamp"] = timestamp;
    if (!tool_call_id.empty()) {
        j["tool_call_id"] = tool_call_id;
    }
    if (!tool_name.empty()) {
        j["tool_name"] = tool_name;
    }
    return j;
}
