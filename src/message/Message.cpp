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
    try {
        json j;
        j["role"] = role;

        // For assistant messages with tool_use blocks (OpenAI format):
        // - content should be a string (the text content from LLM)
        // - tool_calls should be an array
        // We do NOT put content_blocks in the "content" field for OpenAI compatibility
        if (role == "assistant" && !content_blocks.empty()) {
            // content stays as string (may be empty if LLM only returned tool calls)
            j["content"] = content.empty() ? "" : content;

            // Add tool_calls array for OpenAI
            json tool_calls = json::array();
            for (const auto& block : content_blocks) {
                if (block.contains("type") && block["type"] == "tool_use") {
                    json tc;
                    tc["id"] = block.value("id", "");
                    tc["type"] = "function";
                    tc["function"]["name"] = block.value("name", "");

                    // Handle input properly - could be string, number, binary, or object
                    json input_val;
                    try {
                        input_val = block.value("input", json::object());
                    } catch (const json::type_error& e) {
                        Logger::instance().error("Failed to get input from block: " + std::string(e.what()));
                        continue;
                    }

                    if (input_val.is_string()) {
                        // If input is a string like "ls -la", convert to {"command": "ls -la"}
                        tc["function"]["arguments"] = json{{"command", input_val.get<std::string>()}}.dump();
                    } else if (input_val.is_object()) {
                        tc["function"]["arguments"] = input_val.dump();
                    } else if (input_val.is_number()) {
                        tc["function"]["arguments"] = json{{"value", input_val}}.dump();
                    } else if (input_val.is_binary()) {
                        // Binary type - serialize as-is using dump()
                        tc["function"]["arguments"] = input_val.dump();
                    } else if (input_val.is_null()) {
                        tc["function"]["arguments"] = json::object().dump();
                    } else {
                        // For other types (boolean, etc), convert to string
                        tc["function"]["arguments"] = json{{"value", input_val.dump()}}.dump();
                    }
                    tool_calls.push_back(tc);
                }
            }
            if (!tool_calls.empty()) {
                j["tool_calls"] = tool_calls;
            }
        } else if (!content_blocks.empty()) {
            // For other cases (shouldn't happen normally), use content_blocks
            j["content"] = content_blocks;
        } else {
            j["content"] = content.empty() ? "" : content;
        }

        if (!name.empty()) {
            j["name"] = name;
        }
        if (!tool_call_id.empty()) {
            j["tool_call_id"] = tool_call_id;
        }

        return j;
    } catch (const json::type_error& e) {
        Logger::instance().error("JSON type error in Message::toJson: " + std::string(e.what()));
        // Return minimal valid message on error
        json j;
        j["role"] = role;
        j["content"] = "";
        return j;
    }
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
