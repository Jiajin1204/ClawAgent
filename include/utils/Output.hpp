#pragma once

#include "OutputCallback.hpp"
#include <string>
#include <mutex>

namespace ClawAgent {

/**
 * @brief 终端输出管理器 - 统一处理所有用户可见输出
 *
 * 此类实现 IOutputCallback 接口，作为默认的输出处理者。
 * 集成到其他项目时，可通过 setOutputCallback 替换此实现。
 *
 * 与 Logger 区分：
 * - Logger: 系统日志，记录运行状态，可写入文件
 * - Output: 用户可见输出，仅输出到终端
 */
class Output : public IOutputCallback {
public:
    static Output& instance();

    // IOutputCallback 实现
    void onLlmCalling() override;
    void onLlmResponse(long time_ms) override;
    void onToolCallsFound(size_t count) override;
    void onToolExecuting(const std::string& name, const json& params) override;
    void onToolResult(const std::string& name, const std::string& result, long time_ms, bool success) override;
    void onAssistantMessage(const std::string& message) override;
    void onError(const std::string& error) override;

    // 初始化
    void init(bool color_output = true, bool show_tools = true);

    // 用户消息输出（兼容旧接口）
    void printPrompt(const std::string& prompt);
    void printAssistant(const std::string& message);
    void printError(const std::string& error);
    void printSystem(const std::string& message);

    // 工具执行过程输出（兼容旧接口）
    void printCallingModel();
    void printLlmTime(long ms);
    void printToolCalls(size_t count);
    void printExecutingTool(const std::string& name);
    void printToolParams(const std::string& params);
    void printToolResult(const std::string& result, bool truncated = false);
    void printToolError(const std::string& error);
    void printToolTime(long ms);

    // 处理中状态
    void printProcessing();
    void printProcessingEnd();

    // 颜色支持
    enum class Color {
        Reset,
        Red,
        Green,
        Yellow,
        Blue,
        Cyan,
        White
    };

    void setColor(Color color);
    void resetColor();

private:
    Output() = default;
    ~Output() = default;

    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;

    bool color_output_ = true;
    bool show_tools_ = true;
    std::mutex mutex_;
};

} // namespace ClawAgent