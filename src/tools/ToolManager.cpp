#include "tools/ToolManager.hpp"
#include "utils/Logger.hpp"

#include <fstream>
#include <sstream>
#include <chrono>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

using namespace ClawAgent;

// ============ SystemTools Implementation ============

ToolExecutionResult SystemTools::readFile(const std::string& filepath) {
    ToolExecutionResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    std::ifstream file(filepath, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        result.success = false;
        result.error_message = "无法打开文件: " + filepath;
    } else {
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::string content(size, '\0');
        if (!file.read(&content[0], size)) {
            result.success = false;
            result.error_message = "读取文件失败: " + filepath;
        } else {
            result.success = true;
            result.result = content;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.execution_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();

    return result;
}

ToolExecutionResult SystemTools::writeFile(const std::string& filepath, const std::string& content) {
    ToolExecutionResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    std::ofstream file(filepath, std::ios::binary | std::ios::trunc);

    if (!file.is_open()) {
        result.success = false;
        result.error_message = "无法创建文件: " + filepath;
    } else {
        file.write(content.data(), content.size());
        if (file.good()) {
            result.success = true;
            result.result = filepath;  // 成功时返回文件路径
        } else {
            result.success = false;
            result.error_message = "写入文件失败: " + filepath;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.execution_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();

    return result;
}

ToolExecutionResult SystemTools::execCommand(const std::string& command,
                                            int timeout_ms) {
    ToolExecutionResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        result.success = false;
        result.error_message = "无法执行命令";
        result.execution_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start_time).count();
        return result;
    }

    // 设置非阻塞读取
    int fd = fileno(pipe);
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    std::string output;
    char buffer[4096];

    auto deadline = std::chrono::high_resolution_clock::now() +
                   std::chrono::milliseconds(timeout_ms);

    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        if (now >= deadline) {
            pclose(pipe);
            result.success = false;
            result.error_message = "命令执行超时";
            result.result = output;
            result.execution_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - start_time).count();
            return result;
        }

        ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            output += buffer;
        } else if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 无数据可读，非阻塞模式正常情况，等待后重试
                usleep(10000);  // 10ms
                continue;
            }
            // 其他错误
            break;
        } else {
            // n == 0 表示 EOF（管道关闭）
            break;
        }
    }

    int status = pclose(pipe);

    auto end_time = std::chrono::high_resolution_clock::now();
    result.execution_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();

    result.success = (status == 0);
    result.result = output;

    if (status != 0) {
        std::stringstream ss;
        ss << "命令退出码: " << WEXITSTATUS(status);
        result.error_message = ss.str();
    }

    return result;
}

ToolExecutionResult SystemTools::execScript(const std::string& script_path,
                                          const std::vector<std::string>& args,
                                          int timeout_ms) {
    // 构建完整命令
    std::string command = script_path;
    for (const auto& arg : args) {
        command += " \"" + arg + "\"";
    }

    return execCommand(command, timeout_ms);
}

bool SystemTools::pathExists(const std::string& path) {
    return access(path.c_str(), F_OK) == 0;
}

std::string SystemTools::getOSInfo() {
    std::string info;

    // 读取/etc/os-release
    std::ifstream release("/etc/os-release");
    if (release.is_open()) {
        std::string line;
        while (std::getline(release, line)) {
            if (line.substr(0, 5) == "NAME=" || line.substr(0, 7) == "VERSION=") {
                info += line + "\n";
            }
        }
    }

    if (info.empty()) {
        info = "Linux (unknown distribution)";
    }

    return info;
}

std::string SystemTools::getHostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "unknown";
}

// ============ ToolManager Implementation ============

ToolManager::ToolManager(bool enable_read,
                         bool enable_write,
                         bool enable_exec,
                         int exec_timeout_ms)
    : enable_read_(enable_read)
    , enable_write_(enable_write)
    , enable_exec_(enable_exec)
    , exec_timeout_ms_(exec_timeout_ms) {

    registerDefaultTools();
}

ToolManager::~ToolManager() = default;

void ToolManager::registerDefaultTools() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Read工具
    if (enable_read_) {
        Tool read_tool;
        read_tool.name = "read";
        read_tool.description = "Read file content";
        read_tool.type = Tool::Type::Read;
        read_tool.parameters = json{
            {"type", "object"},
            {"properties", json{
                {"path", json{
                    {"type", "string"},
                    {"description", "Path to file to read"}
                }}
            }},
            {"required", {"path"}}
        };
        tools_["read"] = read_tool;
    }

    // Write工具
    if (enable_write_) {
        Tool write_tool;
        write_tool.name = "write";
        write_tool.description = "Write content to file";
        write_tool.type = Tool::Type::Write;
        write_tool.parameters = json{
            {"type", "object"},
            {"properties", json{
                {"path", json{
                    {"type", "string"},
                    {"description", "Path to file to write"}
                }},
                {"text", json{
                    {"type", "string"},
                    {"description", "Text content to write"}
                }}
            }},
            {"required", {"path", "text"}}
        };
        tools_["write"] = write_tool;
    }

    // Exec工具
    if (enable_exec_) {
        Tool exec_tool;
        exec_tool.name = "exec";
        exec_tool.description = "Execute shell command";
        exec_tool.type = Tool::Type::Exec;
        exec_tool.parameters = json{
            {"type", "object"},
            {"properties", json{
                {"command", json{
                    {"type", "string"},
                    {"description", "要执行的命令或脚本"}
                }}
            }},
            {"required", {"command"}}
        };
        tools_["exec"] = exec_tool;
    }

    LOG_INFO("已注册 " + std::to_string(tools_.size()) + " 个工具");
}

std::vector<json> ToolManager::getToolDefinitions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<json> definitions;

    for (const auto& pair : tools_) {
        const auto& tool = pair.second;
        definitions.push_back(json{
            {"type", "function"},
            {"function", json{
                {"name", tool.name},
                {"description", tool.description},
                {"parameters", tool.parameters}
            }}
        });
    }

    return definitions;
}

void ToolManager::registerTool(const std::string& name,
                               const std::string& description,
                               const json& parameters,
                               ToolExecutionCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);

    Tool tool;
    tool.name = name;
    tool.description = description;
    tool.parameters = parameters;
    tool.type = Tool::Type::Custom;

    tools_[name] = tool;
    custom_callbacks_[name] = callback;

    LOG_INFO("注册自定义工具: " + name);
}

ToolExecutionResult ToolManager::executeTool(const std::string& tool_name,
                                             const json& arguments,
                                             const std::string& tool_call_id) {
    ToolExecutionResult result;
    result.tool_call_id = tool_call_id;
    auto start_time = std::chrono::high_resolution_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tools_.find(tool_name);
    if (it == tools_.end()) {
        result.success = false;
        result.error_message = "未知工具: " + tool_name;
        return result;
    }

    const auto& tool = it->second;

    try {
        switch (tool.type) {
            case Tool::Type::Read: {
                // 支持 filepath, file_path, path 等多种参数名
                std::string filepath;
                if (arguments.contains("filepath")) {
                    filepath = arguments.at("filepath").get<std::string>();
                } else if (arguments.contains("file_path")) {
                    filepath = arguments.at("file_path").get<std::string>();
                } else if (arguments.contains("path")) {
                    filepath = arguments.at("path").get<std::string>();
                } else {
                    throw std::out_of_range("filepath");
                }
                result = SystemTools::readFile(filepath);
                break;
            }
            case Tool::Type::Write: {
                // 支持 filepath, file_path, path 等多种参数名
                std::string filepath;
                std::string content;
                if (arguments.contains("filepath")) {
                    filepath = arguments.at("filepath").get<std::string>();
                } else if (arguments.contains("file_path")) {
                    filepath = arguments.at("file_path").get<std::string>();
                } else if (arguments.contains("path")) {
                    filepath = arguments.at("path").get<std::string>();
                } else {
                    throw std::out_of_range("filepath");
                }
                if (arguments.contains("content")) {
                    content = arguments.at("content").get<std::string>();
                } else if (arguments.contains("text")) {
                    content = arguments.at("text").get<std::string>();
                } else {
                    throw std::out_of_range("content");
                }
                result = SystemTools::writeFile(filepath, content);
                if (!result.success && result.error_message.empty()) {
                    result.error_message = "无法写入文件: " + filepath;
                }
                break;
            }
            case Tool::Type::Exec: {
                // 支持 command, cmd 等多种参数名
                std::string command;
                if (arguments.contains("command")) {
                    command = arguments.at("command").get<std::string>();
                } else if (arguments.contains("cmd")) {
                    command = arguments.at("cmd").get<std::string>();
                } else {
                    throw std::out_of_range("command");
                }
                result = SystemTools::execCommand(command, exec_timeout_ms_);
                if (!result.success && result.error_message.empty()) {
                    result.error_message = "命令执行失败";
                }
                break;
            }
            case Tool::Type::Custom: {
                auto callback_it = custom_callbacks_.find(tool_name);
                if (callback_it != custom_callbacks_.end()) {
                    json r = callback_it->second(arguments);
                    result.result = r.value("result", "");
                    result.success = r.value("success", true);
                    result.error_message = r.value("error", "");
                } else {
                    result.success = false;
                    result.error_message = "工具回调未找到";
                }
                break;
            }
        }
    } catch (const json::out_of_range& e) {
        result.success = false;
        result.error_message = "参数缺失，请检查工具调用参数是否完整";
    } catch (const std::out_of_range& e) {
        result.success = false;
        result.error_message = "参数缺失，请检查工具调用参数是否完整";
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = std::string(e.what());
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.execution_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();

    return result;
}

std::vector<ToolExecutionResult> ToolManager::executeTools(
    const std::vector<std::pair<std::string, json>>& tools,
    const std::vector<std::string>& tool_call_ids) {

    std::vector<ToolExecutionResult> results;
    results.reserve(tools.size());

    for (size_t i = 0; i < tools.size(); ++i) {
        const auto& tool = tools[i];
        const std::string& tool_call_id = (i < tool_call_ids.size()) ?
            tool_call_ids[i] : "";
        results.push_back(executeTool(tool.first, tool.second, tool_call_id));
    }

    return results;
}

bool ToolManager::hasTool(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tools_.find(name) != tools_.end();
}

std::string ToolManager::getToolDescription(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tools_.find(name);
    if (it != tools_.end()) {
        return it->second.description;
    }
    return "";
}

std::string Tool::execute(const json& /*arguments*/) const {
    // 默认执行，由ToolManager调用具体实现
    return "";
}
