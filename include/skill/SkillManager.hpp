#pragma once

#include <string>
#include <vector>
#include <memory>
#include <set>
#include "skill/Skill.hpp"

namespace ClawAgent {

/**
 * @brief Skill 管理器 - 管理 skill 的加载和上下文注入
 *
 * 加载模式:
 * - startup: 启动时加载所有 skill，之后缓存
 * - dynamic: 运行时按需加载 skill
 *
 * 上下文注入:
 * - full_content_skills=["*"]: 所有 skill 完整内容注入
 * - full_content_skills=["s1","s2"]: 指定 skill 完整内容，其他仅元数据
 * - full_content_skills=[]: 所有 skill 仅元数据
 */
class SkillManager {
public:
    // 加载模式
    enum class LoadMode { Startup, Dynamic };

    SkillManager(const std::string& workspace_skills_dir,
                 const std::string& global_skills_dir,
                 LoadMode mode,
                 const std::vector<std::string>& full_content_skills);

    // 禁用拷贝
    SkillManager(const SkillManager&) = delete;
    SkillManager& operator=(const SkillManager&) = delete;

    // 加载所有 skill (startup 模式)
    void loadSkills();

    // 动态加载特定 skill (dynamic 模式)
    std::optional<Skill> loadSkill(const std::string& name);

    // 获取 skill 列表
    std::vector<Skill> getSkills() const;

    // 获取特定 skill
    std::optional<Skill> getSkill(const std::string& name) const;

    // 获取启用的 skill 内容 (用于上下文注入)
    std::string getSkillsContext() const;

    // 获取加载模式
    LoadMode getLoadMode() const { return load_mode_; }

private:
    // 扫描目录加载 skill (startup 模式内部用)
    void loadSkillsFromDirectory(const std::string& dir, bool is_workspace);

    // 扫描目录获取 skill 列表 (dynamic 模式内部用)
    void scanDirectorySkills(const std::string& dir,
                             std::vector<Skill>& result,
                             bool is_workspace) const;

    // 添加 skill 内容到上下文
    void addSkillsFromDirectory(const std::string& dir,
                                std::stringstream& ss,
                                bool is_workspace,
                                bool& has_any_skill) const;
    // 扫描目录加载 skill
    std::optional<Skill> loadSkillFromDir(const std::string& dir,
                                          const std::string& name,
                                          bool is_workspace_skill);

    // 解析 SKILL.md 文件
    std::optional<Skill> parseSkillFile(const std::string& file_path,
                                        bool is_workspace_skill) const;

    std::vector<Skill> skills_;
    std::string workspace_skills_dir_;
    std::string global_skills_dir_;
    LoadMode load_mode_;
    std::set<std::string> full_content_skills_;  // 需要完整内容的 skill 列表
};

} // namespace ClawAgent
