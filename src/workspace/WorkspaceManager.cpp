#include "workspace/WorkspaceManager.hpp"
#include "utils/Logger.hpp"

#include <sstream>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <stdlib.h>

using namespace ClawAgent;

WorkspaceManager& WorkspaceManager::instance() {
    static WorkspaceManager instance;
    return instance;
}

void WorkspaceManager::initialize(const std::string& config_home) {
    if (initialized_) {
        return;
    }

    resolveHome(config_home);
    // workspace 始终在 home 目录下
    resolveWorkspace("");

    // 构建其他目录路径
    global_skills_dir_ = home_ + "/skills";
    sessions_dir_ = home_ + "/sessions";
    memory_dir_ = workspace_ + "/memory";
    agents_md_path_ = workspace_ + "/AGENTS.md";

    // 检查是否是首次运行 (home 目录不存在)
    first_run_ = access(home_.c_str(), F_OK) != 0;

    initialized_ = true;
    Logger::instance().info("WorkspaceManager 初始化: home=" + home_ + ", workspace=" + workspace_);
}

std::string WorkspaceManager::expandPath(const std::string& path) {
    if (path.empty()) {
        return path;
    }

    std::string result = path;

    // 展开 ~
    if (result[0] == '~') {
        const char* home = std::getenv("HOME");
        if (!home) {
            struct passwd* pw = getpwuid(getuid());
            if (pw) {
                home = pw->pw_dir;
            }
        }
        if (home) {
            result = std::string(home) + result.substr(1);
        }
    }

    // 展开环境变量 ${VAR_NAME}
    size_t pos = 0;
    while ((pos = result.find("${", pos)) != std::string::npos) {
        size_t end = result.find('}', pos);
        if (end == std::string::npos) {
            break;
        }
        std::string var_name = result.substr(pos + 2, end - pos - 2);
        const char* var_value = std::getenv(var_name.c_str());
        if (var_value) {
            result.replace(pos, end - pos + 1, var_value);
        } else {
            // 如果环境变量不存在，保留原字符串
            pos = end + 1;
        }
    }

    return result;
}

void WorkspaceManager::resolveHome(const std::string& config_home) {
    // 优先级: config > env > default

    if (!config_home.empty()) {
        home_ = expandPath(config_home);
        return;
    }

    const char* env_home = std::getenv("CLAWAGENT_HOME");
    if (env_home && env_home[0]) {
        home_ = expandPath(env_home);
        return;
    }

    // 默认: ~/.clawagent
    const char* home = std::getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            home = pw->pw_dir;
        }
    }
    home_ = std::string(home) + "/.clawagent";
}

void WorkspaceManager::resolveWorkspace(const std::string& /*config_workspace*/) {
    // workspace 始终在 home 下，不允许外部配置
    workspace_ = home_ + "/workspace";
}

std::string WorkspaceManager::readAgentsMd() const {
    std::ifstream file(agents_md_path_);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool WorkspaceManager::createDirectory(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    // 检查路径是否已存在
    if (access(path.c_str(), F_OK) == 0) {
        return true;
    }

    // 递归创建父目录
    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos && pos > 0) {
        std::string parent = path.substr(0, pos);
        if (!createDirectory(parent)) {
            return false;
        }
    }

    // 创建当前目录
    if (mkdir(path.c_str(), 0755) != 0) {
        if (errno != EEXIST) {
            Logger::instance().error("无法创建目录: " + path);
            return false;
        }
    }
    return true;
}

void WorkspaceManager::createDirectories() {
    // 创建 home
    if (!createDirectory(home_)) {
        return;
    }

    // 创建子目录
    createDirectory(workspace_);
    createDirectory(workspace_ + "/skills");  // workspace 下的 skills 目录
    createDirectory(global_skills_dir_);
    createDirectory(sessions_dir_);
    createDirectory(memory_dir_);

    // 如果 AGENTS.md 不存在，创建默认版本
    if (access(agents_md_path_.c_str(), F_OK) != 0) {
        std::ofstream file(agents_md_path_);
        if (file.is_open()) {
            file << "# AGENTS.md\n\n";
            file << "你是一个有帮助的 AI 助手。\n\n";
            file << "## 行为规范\n\n";
            file << "- 如果需要执行多条命令，可以一次调用多个工具\n";
            file << "- 工具调用后请等待结果再决定下一步\n";
            file << "- 如果出错，请分析原因并尝试修复\n";
            file << "- 如果无法完成任务，请明确告知用户\n";
            file.close();
            Logger::instance().info("已创建默认 AGENTS.md: " + agents_md_path_);
        }
    }

    first_run_ = false;
}
