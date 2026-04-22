#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace ClawAgent {

// 工具定义
struct Tool {
    std::string name;
    std::string description;
    json parameters;  // JSON Schema for tool parameters
    std::string execute(const json& arguments) const;

    // 工具类型
    enum class Type { Read, Write, Exec, Custom };
    Type type;
};

// 工具执行结果
struct ToolExecutionResult {
    std::string tool_call_id;
    std::string result;
    bool success;
    std::string error_message;
    long execution_time_ms;

    json toJson() const {
        return json{
            {"tool_call_id", tool_call_id},
            {"result", result},
            {"success", success},
            {"error", error_message},
            {"execution_time_ms", execution_time_ms}
        };
    }
};

// 工具定义回调
using ToolDefinitionCallback = std::function<json()>;
using ToolExecutionCallback = std::function<json(const json& arguments)>;

/**
 * @brief 工具管理器 - 管理所有可用工具
 *
 * 核心工具：
 * 1. read - 读取文件
 * 2. write - 写入文件
 * 3. exec - 执行命令或脚本
 */
class ToolManager {
public:
    ToolManager(bool enable_read = true,
                bool enable_write = true,
                bool enable_exec = true,
                int exec_timeout_ms = 300000);
    ~ToolManager();

    // 禁用拷贝
    ToolManager(const ToolManager&) = delete;
    ToolManager& operator=(const ToolManager&) = delete;

    // 获取所有工具定义（用于发送给LLM）
    std::vector<json> getToolDefinitions() const;

    // 注册自定义工具
    void registerTool(const std::string& name,
                      const std::string& description,
                      const json& parameters,
                      ToolExecutionCallback callback);

    // 执行工具
    ToolExecutionResult executeTool(const std::string& tool_name,
                                     const json& arguments,
                                     const std::string& tool_call_id = "");

    // 批量执行工具调用
    std::vector<ToolExecutionResult> executeTools(
        const std::vector<std::pair<std::string, json>>& tools,
        const std::vector<std::string>& tool_call_ids);

    // 检查工具是否存在
    bool hasTool(const std::string& name) const;

    // 获取工具描述
    std::string getToolDescription(const std::string& name) const;

private:
    void registerDefaultTools();

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Tool> tools_;
    std::unordered_map<std::string, ToolExecutionCallback> custom_callbacks_;
    bool enable_read_;
    bool enable_write_;
    bool enable_exec_;
    int exec_timeout_ms_;
};

/**
 * @brief 系统工具 - 实际的文件读写和命令执行
 */
class SystemTools {
public:
    // 读取文件
    static ToolExecutionResult readFile(const std::string& filepath);

    // 写入文件
    static ToolExecutionResult writeFile(const std::string& filepath, const std::string& content);

    // 执行命令
    static ToolExecutionResult execCommand(const std::string& command,
                                          int timeout_ms = 300000);

    // 执行脚本文件
    static ToolExecutionResult execScript(const std::string& script_path,
                                          const std::vector<std::string>& args,
                                          int timeout_ms = 300000);

    // 检查路径是否存在
    static bool pathExists(const std::string& path);

    // 获取系统信息
    static std::string getOSInfo();
    static std::string getHostname();
};

} // namespace ClawAgent
