#pragma once

#include <string>
#include <optional>

namespace ClawAgent {

/**
 * @brief Skill 数据结构
 */
struct Skill {
    std::string name;           // skill 名称
    std::string description;    // 简短描述
    std::string content;        // 完整 markdown 内容
    std::string file_path;     // 文件路径
    bool is_workspace_skill;   // 是否为工作区级 skill
};

} // namespace ClawAgent
