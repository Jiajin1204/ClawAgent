#pragma once

#include <string>
#include <memory>

namespace ClawAgent {

/**
 * @brief Workspace 管理器 - 管理 ClawAgent 的 home 和 workspace 目录
 *
 * 目录结构:
 * - home: ~/.clawagent (或配置的路径)
 * - workspace: ${home}/workspace
 * - skills: ${home}/skills
 * - sessions: ${home}/sessions
 */
class WorkspaceManager {
public:
    // 获取单例实例
    static WorkspaceManager& instance();

    // 禁用拷贝
    WorkspaceManager(const WorkspaceManager&) = delete;
    WorkspaceManager& operator=(const WorkspaceManager&) = delete;

    /**
     * @brief 初始化 (由 ClawAgentCore 在启动时调用)
     * @param config_home 用户配置的 home 目录 (可为空)
     */
    void initialize(const std::string& config_home = "");

    // 获取 clawagent home 目录
    std::string getHome() const { return home_; }

    // 获取 workspace 目录
    std::string getWorkspace() const { return workspace_; }

    // 获取 skill 目录 (全局)
    std::string getGlobalSkillsDir() const { return global_skills_dir_; }

    // 获取会话目录
    std::string getSessionsDir() const { return sessions_dir_; }

    // 获取 AGENTS.md 路径
    std::string getAgentsMdPath() const { return agents_md_path_; }

    // 获取 memory 目录
    std::string getMemoryDir() const { return memory_dir_; }

    // 读取 AGENTS.md (动态读取，支持热更新)
    std::string readAgentsMd() const;

    // 检查是否是首次运行 (需要创建目录结构)
    bool isFirstRun() const { return first_run_; }

    // 创建必要的目录结构
    void createDirectories();

    // 展开路径中的 ~ 和环境变量
    static std::string expandPath(const std::string& path);

private:
    WorkspaceManager() = default;

    // 解析 home 目录
    void resolveHome(const std::string& config_home);

    // 解析 workspace 目录
    void resolveWorkspace(const std::string& config_workspace);

    // 创建单个目录
    bool createDirectory(const std::string& path);

    std::string home_;
    std::string workspace_;
    std::string global_skills_dir_;
    std::string sessions_dir_;
    std::string agents_md_path_;
    std::string memory_dir_;
    bool first_run_ = true;
    bool initialized_ = false;
};

} // namespace ClawAgent
