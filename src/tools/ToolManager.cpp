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

std::string SystemTools::readFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        throw std::runtime_error("无法打开文件: " + filepath);
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string content(size, '\0');
    if (!file.read(&content[0], size)) {
        throw std::runtime_error("读取文件失败: " + filepath);
    }

    return content;
}

bool SystemTools::writeFile(const std::string& filepath, const std::string& content) {
    std::ofstream file(filepath, std::ios::binary | std::ios::trunc);

    if (!file.is_open()) {
        LOG_ERROR("无法创建文件: " + filepath);
        return false;
    }

    file.write(content.data(), content.size());
    return file.good();
}

ToolExecutionResult SystemTools::execCommand(const std::string& command,
                                            int timeout_ms) {
    ToolExecutionResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        result.success = false;
        result.error_message = "无法执行命令";
        return result;
    }

    // 设置非阻塞读取
    int fd = fileno(pipe);
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    std::string output;
    char buffer[4096];
    bool timed_out = false;

    auto deadline = std::chrono::high_resolution_clock::now() +
                   std::chrono::milliseconds(timeout_ms);

    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        if (now >= deadline) {
            timed_out = true;
            pclose(pipe);
            result.success = false;
            result.error_message = "命令执行超时";
            return result;
        }

        ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            output += buffer;
        } else if (n == 0) {
            break;
        }

        usleep(10000);  // 10ms
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
        read_tool.description = "读取文件内容。参数：filepath（文件路径）。返回文件内容。";
        read_tool.type = Tool::Type::Read;
        read_tool.parameters = json{
            {"type", "object"},
            {"properties", json{
                {"filepath", json{
                    {"type", "string"},
                    {"description", "要读取的文件路径"}
                }}
            }},
            {"required", {"filepath"}}
        };
        tools_["read"] = read_tool;
    }

    // Write工具
    if (enable_write_) {
        Tool write_tool;
        write_tool.name = "write";
        write_tool.description = "写入内容到文件。参数：filepath（文件路径），content（要写入的内容）。返回是否成功。";
        write_tool.type = Tool::Type::Write;
        write_tool.parameters = json{
            {"type", "object"},
            {"properties", json{
                {"filepath", json{
                    {"type", "string"},
                    {"description", "要写入的文件路径"}
                }},
                {"content", json{
                    {"type", "string"},
                    {"description", "要写入的内容"}
                }}
            }},
            {"required", {"filepath", "content"}}
        };
        tools_["write"] = write_tool;
    }

    // Exec工具
    if (enable_exec_) {
        Tool exec_tool;
        exec_tool.name = "exec";
        exec_tool.description = "执行命令或shell脚本。参数：command（要执行的命令）。返回命令输出。";
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
            {"name", tool.name},
            {"description", tool.description},
            {"parameters", tool.parameters}
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
                std::string filepath = arguments.at("filepath").get<std::string>();
                result.result = SystemTools::readFile(filepath);
                result.success = true;
                break;
            }
            case Tool::Type::Write: {
                std::string filepath = arguments.at("filepath").get<std::string>();
                std::string content = arguments.at("content").get<std::string>();
                result.success = SystemTools::writeFile(filepath, content);
                result.result = result.success ? "文件写入成功" : "文件写入失败";
                if (!result.success) {
                    result.error_message = "无法写入文件: " + filepath + "，请检查权限或路径是否正确";
                }
                break;
            }
            case Tool::Type::Exec: {
                std::string command = arguments.at("command").get<std::string>();
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

std::string Tool::execute(const json& arguments) const {
    // 默认执行，由ToolManager调用具体实现
    return "";
}
