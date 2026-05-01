#include "skill/SkillManager.hpp"
#include "utils/Logger.hpp"

#include <sstream>
#include <fstream>
#include <sys/stat.h>
#include <dirent.h>
#include <cstring>
#include <unistd.h>

using namespace ClawAgent;

SkillManager::SkillManager(const std::string& workspace_skills_dir,
                           const std::string& global_skills_dir,
                           LoadMode mode,
                           bool inject_all,
                           const std::vector<std::string>& enabled)
    : workspace_skills_dir_(workspace_skills_dir)
    , global_skills_dir_(global_skills_dir)
    , load_mode_(mode)
    , enabled_skills_(enabled.begin(), enabled.end())
    , inject_all_(inject_all) {
}

void SkillManager::loadSkills() {
    if (load_mode_ != LoadMode::Startup) {
        return;
    }

    skills_.clear();

    // 扫描工作区 skill 目录
    loadSkillsFromDirectory(workspace_skills_dir_, true);

    // 扫描全局 skill 目录
    loadSkillsFromDirectory(global_skills_dir_, false);

    Logger::instance().info("SkillManager 加载了 " + std::to_string(skills_.size()) + " 个 skills");
}

void SkillManager::loadSkillsFromDirectory(const std::string& dir, bool is_workspace) {
    DIR* dp = opendir(dir.c_str());
    if (!dp) {
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dp)) != nullptr) {
        if (entry->d_type != DT_DIR) {
            continue;
        }

        std::string name(entry->d_name);
        if (name == "." || name == "..") {
            continue;
        }

        std::string skill_dir = dir + "/" + name;
        std::string skill_file = skill_dir + "/SKILL.md";

        if (access(skill_file.c_str(), F_OK) == 0) {
            auto skill = parseSkillFile(skill_file, is_workspace);
            if (skill) {
                skills_.push_back(*skill);
            }
        }
    }

    closedir(dp);
}

std::optional<Skill> SkillManager::loadSkill(const std::string& name) {
    if (load_mode_ == LoadMode::Startup) {
        // startup 模式：返回已加载的 skill
        return getSkill(name);
    }

    // dynamic 模式：按需加载
    // 先在工作区查找
    auto skill = loadSkillFromDir(workspace_skills_dir_, name, true);
    if (skill) {
        return skill;
    }

    // 再在全局目录查找
    return loadSkillFromDir(global_skills_dir_, name, false);
}

std::optional<Skill> SkillManager::loadSkillFromDir(const std::string& dir,
                                                   const std::string& name,
                                                   bool is_workspace_skill) {
    std::string skill_dir = dir + "/" + name;
    std::string skill_file = skill_dir + "/SKILL.md";

    if (access(skill_file.c_str(), F_OK) != 0) {
        return std::nullopt;
    }

    return parseSkillFile(skill_file, is_workspace_skill);
}

std::optional<Skill> SkillManager::parseSkillFile(const std::string& file_path,
                                                   bool is_workspace_skill) const {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // 简单解析: 尝试从开头解析 name 和 description
    // 格式: ---
    // name: xxx
    // description: xxx
    // ---
    Skill skill;
    skill.file_path = file_path;
    skill.is_workspace_skill = is_workspace_skill;
    skill.content = content;

    // 从文件路径提取 name
    size_t last_slash = file_path.find_last_of('/');
    size_t second_last_slash = file_path.find_last_of('/', last_slash - 1);
    if (last_slash != std::string::npos && second_last_slash != std::string::npos) {
        skill.name = file_path.substr(second_last_slash + 1,
                                      last_slash - second_last_slash - 1);
    }

    // 尝试解析 frontmatter
    std::istringstream iss(content);
    std::string line;
    bool in_frontmatter = false;
    bool frontmatter_done = false;

    while (std::getline(iss, line)) {
        if (!frontmatter_done && line.substr(0, 3) == "---") {
            if (!in_frontmatter) {
                in_frontmatter = true;
            } else {
                frontmatter_done = true;
            }
            continue;
        }

        if (in_frontmatter && !frontmatter_done) {
            if (line.substr(0, 12) == "description:") {
                skill.description = line.substr(12);
                while (skill.description.size() > 0 &&
                       (skill.description[0] == ' ' || skill.description[0] == '\r')) {
                    skill.description = skill.description.substr(1);
                }
            } else if (line.substr(0, 5) == "name:") {
                skill.name = line.substr(5);
                while (skill.name.size() > 0 &&
                       (skill.name[0] == ' ' || skill.name[0] == '\r')) {
                    skill.name = skill.name.substr(1);
                }
            }
        }

        // 找到第一个 # 开头的行作为标题
        if (frontmatter_done && line.substr(0, 1) == "#") {
            break;
        }
    }

    if (skill.description.empty()) {
        skill.description = skill.name;
    }

    return skill;
}

std::vector<Skill> SkillManager::getSkills() const {
    if (load_mode_ == LoadMode::Startup) {
        return skills_;
    }

    // dynamic 模式：需要扫描目录
    std::vector<Skill> result;

    // 工作区 skills
    scanDirectorySkills(workspace_skills_dir_, result, true);

    // 全局 skills
    scanDirectorySkills(global_skills_dir_, result, false);

    return result;
}

void SkillManager::scanDirectorySkills(const std::string& dir,
                                       std::vector<Skill>& result,
                                       bool is_workspace) const {
    DIR* dp = opendir(dir.c_str());
    if (!dp) {
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dp)) != nullptr) {
        if (entry->d_type != DT_DIR) {
            continue;
        }

        std::string name(entry->d_name);
        if (name == "." || name == "..") {
            continue;
        }

        std::string skill_file = dir + "/" + name + "/SKILL.md";
        if (access(skill_file.c_str(), F_OK) == 0) {
            auto skill = parseSkillFile(skill_file, is_workspace);
            if (skill) {
                result.push_back(*skill);
            }
        }
    }

    closedir(dp);
}

std::optional<Skill> SkillManager::getSkill(const std::string& name) const {
    for (const auto& skill : skills_) {
        if (skill.name == name) {
            return skill;
        }
    }
    return std::nullopt;
}

std::string SkillManager::getSkillsContext() const {
    std::stringstream ss;

    bool has_any_skill = false;

    // 工作区 skills
    addSkillsFromDirectory(workspace_skills_dir_, ss, true, has_any_skill);

    // 全局 skills
    addSkillsFromDirectory(global_skills_dir_, ss, false, has_any_skill);

    if (!has_any_skill) {
        return "";
    }

    return ss.str();
}

void SkillManager::addSkillsFromDirectory(const std::string& dir,
                                         std::stringstream& ss,
                                         bool is_workspace,
                                         bool& has_any_skill) const {
    DIR* dp = opendir(dir.c_str());
    if (!dp) {
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dp)) != nullptr) {
        if (entry->d_type != DT_DIR) {
            continue;
        }

        std::string name(entry->d_name);
        if (name == "." || name == "..") {
            continue;
        }

        std::string skill_file = dir + "/" + name + "/SKILL.md";
        if (access(skill_file.c_str(), F_OK) != 0) {
            continue;
        }

        // 检查是否需要注入
        if (!inject_all_ && !enabled_skills_.empty()) {
            if (enabled_skills_.find(name) == enabled_skills_.end()) {
                continue;
            }
        }

        // 读取内容
        std::ifstream file(skill_file);
        if (!file.is_open()) {
            continue;
        }

        auto skill = parseSkillFile(skill_file, is_workspace);
        if (!skill) {
            continue;
        }

        ss << "## " << skill->name << "\n";
        ss << "描述: " << skill->description << "\n";
        ss << "---\n";
        ss << skill->content << "\n\n";

        has_any_skill = true;
    }

    closedir(dp);
}
