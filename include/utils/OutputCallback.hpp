#pragma once

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace ClawAgent {

/**
 * @brief 输出回调接口 - 用于自定义输出行为
 *
 * 当 ClawAgent 作为库集成到其他项目时，可以通过实现此接口
 * 来捕获和处理所有输出事件，而不是默认打印到终端。
 */
class IOutputCallback {
public:
    virtual ~IOutputCallback() = default;

    // LLM 调用
    virtual void onLlmCalling() = 0;
    virtual void onLlmResponse(long time_ms) = 0;

    // 工具调用
    virtual void onToolCallsFound(size_t count) = 0;
    virtual void onToolExecuting(const std::string& name, const json& params) = 0;
    virtual void onToolResult(const std::string& name, const std::string& result, long time_ms, bool success) = 0;

    // 消息输出
    virtual void onAssistantMessage(const std::string& message) = 0;
    virtual void onError(const std::string& error) = 0;
};

} // namespace ClawAgent