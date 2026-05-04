# Workspace 和 Skill 开发计划

## 背景调研

### OpenClaw 架构参考

#### 1. Home 目录结构 (`~/.openclaw/`)

```
~/.openclaw/
├── openclaw.json          # 主配置文件
├── agents/
│   ├── main/
│   │   ├── agent/        # agent 特定配置 (auth, models)
│   │   └── sessions/     # 会话历史
│   └── work0/
├── workspace/            # 默认工作区
│   ├── AGENTS.md        # agent 操作指令
│   ├── SOUL.md          # persona/角色定义
│   ├── USER.md          # 用户信息
│   ├── IDENTITY.md      # agent 身份
│   ├── TOOLS.md         # 工具说明
│   ├── HEARTBEAT.md     # 心跳检查清单
│   ├── memory/          # 每日记忆
│   │   └── YYYY-MM-DD.md
│   └── skills/          # 工作区级 skill
├── skills/              # 托管 skill (全局)
├── extensions/          # 插件扩展
├── credentials/         # 认证信息
├── logs/
└── ...
```

#### 2. Workspace 定义

- **位置**: `~/.openclaw/workspace` (可配置)
- **作用**: agent 的 home 目录，唯一的工作目录，用于文件工具和上下文
- **与 `~/.openclaw/` 的区别**: workspace 存文件/记忆，`~/.openclaw/` 存配置/认证/会话

#### 3. Skill 系统

Skill 是**可复用的流程文档**，不是事实/偏好记录。

**存储位置**:
- 工作区级: `<workspace>/skills/<skill-name>/SKILL.md`
- 托管级: `~/.openclaw/skills/<skill-name>/SKILL.md`

**SKILL.md 格式**:
```markdown
---
name: skill-name
description: 简短描述
---

# Skill Title

## Workflow

- 步骤1
- 步骤2
```

**Skill Workshop**: 自动捕获机制，将用户纠正或成功流程转为 skill

---

## ClawAgent 目标架构

### 1. Home 目录

**优先级**: `config.json` 配置 > `CLAWAGENT_HOME` 环境变量 > 用户 home 目录

**目录结构** (`~/.clawagent/` 或自定义):

```
~/.clawagent/              # 或 $CLAWAGENT_HOME
├── config.json            # 主配置文件 (可选，与项目 config.json 分开)
├── workspace/             # 工作区
│   ├── AGENTS.md         # agent 行为规范
│   ├── SOUL.md           # persona 定义
│   ├── memory/          # 记忆存储
│   │   └── YYYY-MM-DD.md
│   └── skills/          # 工作区 skill
├── skills/              # 全局/托管 skill
├── sessions/            # 会话历史
├── credentials/         # 认证信息 (API keys 等)
└── logs/                # 日志
```

### 2. Config.json 新增配置项

```json
{
    "clawagent": {
        "home": "~/.clawagent"                // 可选，默认 ~/.clawagent
    },
    "skills": {
        "load_mode": "startup",            // startup: 启动时加载, dynamic: 运行时动态加载
        "full_content_skills": ["*"]       // ["*"]: 所有 skill 完整内容注入
                                           // ["skill1","skill2"]: 指定 skill 完整内容，其他仅元数据
                                           // []: 所有 skill 仅元数据
    }
}
```

### 3. Skill 上下文注入规则

**核心原则：目录下所有 skill 都会注入（至少元数据）**

| full_content_skills | 结果 |
|---------------------|------|
| `["*"]` | 所有 skill 完整内容注入 |
| `["skill1", "skill2"]` | skill1、skill2 完整内容 + 其他所有 skill 仅元数据 |
| `[]` | 所有 skill 仅元数据注入 |

**注入内容格式：**
- 元数据：skill name + description（每个 skill 都会有）
- 完整内容：Workflow + When to Use 等完整 SKILL.md 内容

```markdown
=== 可用 Skills ===

## skill-name
描述: 示例 skill
---
# Skill Title

## Workflow
- 步骤 1
- 步骤 2

## When to Use
适用于...
```

**说明：**
- `full_content_skills` 控制哪些 skill 获得完整内容
- `["*"]` 是便捷写法，等价于所有 skill 完整内容
- 非 `["*"]` 时，每个 skill 都会注入元数据，只有列表中的 skill 额外获得完整内容

### 3. 项目内置 Skills

项目目录下的 `skills/` 目录包含内置 skill，可作为参考或直接使用：

```
skills/
└── example-skill/         # 示例 skill
    └── SKILL.md
```

用户可以将 `skills/` 目录复制到 `~/.clawagent/skills/` 使用。

### 4. Skill 定义

**SKILL.md 格式** (参考 openclaw):

```markdown
---
name: example-skill
description: 示例 skill
---

# Example Skill

## Workflow

- 步骤 1
- 步骤 2

## When to Use

适用于...
```

**Skill 加载机制**:
1. 工作区 skill: `<workspace>/skills/<skill-name>/SKILL.md` (最高优先级)
2. 全局 skill: `<home>/skills/<skill-name>/SKILL.md`
3. 内置 skill: 代码中内置的默认 skill

---

## 实现方案

### Phase 1: Workspace 支持

#### 1.1 Home 目录解析模块

新增 `WorkspaceManager` 类:

```cpp
class WorkspaceManager {
public:
    // 获取 clawagent home 目录
    static std::string getHome();
    // 获取 workspace 目录
    static std::string getWorkspace();
    // 获取 skill 目录
    static std::string getSkillsDir();
    // 获取全局 skill 目录
    static std::string getGlobalSkillsDir();
    // 获取会话目录
    static std::string getSessionsDir();

    // 获取 AGENTS.md 路径
    static std::string getAgentsMdPath();
    // 读取 AGENTS.md (动态读取，支持热更新)
    static std::string readAgentsMd();

private:
    // 优先级: config > env > default
    static std::string resolveHome();
};
```

#### 1.2 修改 ConfigManager

新增配置项:
- `clawagent.home` - home 目录路径
- `clawagent.workspace` - workspace 目录路径 (默认 `${home}/workspace`)
- `clawagent.skills_dir` - 全局 skill 目录 (默认 `${home}/skills`)
- `skills.load_mode` - "startup" 或 "dynamic"
- `skills.inject_all` - true/false
- `skills.enabled` - 启用的 skill 列表

#### 1.3 修改 AgentRuntime

- `buildSystemPrompt()` 从 workspace 的 `AGENTS.md` **动态读取** (每次调用时读取)
- `getDynamicContext()` 显示 workspace 路径
- SkillManager 集成

#### 1.4 初始化逻辑

启动时:
1. 解析 home 目录
2. 创建必要目录结构
3. 如果 AGENTS.md 不存在，创建默认版本
4. 根据 `load_mode` 初始化 SkillManager

### Phase 2: Skill 系统

#### 2.1 Skill 数据结构

```cpp
struct Skill {
    std::string name;
    std::string description;
    std::string content;       // 完整 markdown 内容
    std::string file_path;     // 文件路径
    bool is_workspace_skill;   // 是否为工作区级 skill
};

class SkillManager {
public:
    // 加载模式: startup 或 dynamic
    enum class LoadMode { Startup, Dynamic };

    SkillManager(const std::string& workspace_skills_dir,
                 const std::string& global_skills_dir,
                 LoadMode mode);

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

    // 重新加载 (用于 AGENTS.md 动态更新)
    void reloadAgentsMd();

private:
    std::vector<Skill> skills_;
    std::string workspace_skills_dir_;
    std::string global_skills_dir_;
    LoadMode load_mode_;
    std::set<std::string> full_content_skills_;  // 需要完整内容的 skill 列表 ["*"] 表示全部
};
```

#### 2.2 Skill 加载模式

| 模式 | 说明 |
|------|------|
| `startup` | 启动时加载所有 skill，之后缓存 |
| `dynamic` | 运行时按需加载 skill |

#### 2.3 Skill 上下文格式

```markdown
=== 可用 Skills ===

## skill-name
描述: 示例 skill
---
<skill content>
```

**上下文注入规则**:
- `full_content_skills=["*"]`: 所有 skill 完整内容注入
- `full_content_skills=["skill1"]`: skill1 完整内容，其他仅元数据
- `full_content_skills=[]`: 所有 skill 仅元数据

### Phase 3: 集成测试

- 测试 home 目录解析优先级
- 测试 workspace 目录创建
- 测试 skill 加载和上下文注入

---

## 影响的文件

### 新增

| 文件 | 说明 |
|------|------|
| `include/workspace/WorkspaceManager.hpp` | Home 目录管理 |
| `src/workspace/WorkspaceManager.cpp` | 实现 |
| `include/skill/SkillManager.hpp` | Skill 管理 |
| `include/skill/Skill.hpp` | Skill 数据结构 |
| `src/skill/SkillManager.cpp` | 实现 |

### 修改

| 文件 | 修改内容 |
|------|----------|
| `include/config/ConfigManager.hpp` | 新增 `clawagent.*` 和 `skills.*` 配置项 |
| `src/config/ConfigManager.cpp` | 解析新配置项 |
| `include/agent/AgentRuntime.hpp` | 新增 `skill_manager_` 和 `workspace_manager_` 成员 |
| `src/agent/AgentRuntime.cpp` | 使用 WorkspaceManager 和 SkillManager |
| `src/ClawAgent.cpp` | 初始化 WorkspaceManager 和 SkillManager |

### 配置变更

`config.json` 新增配置示例:

```json
{
    "clawagent": {
        "home": "~/.clawagent",
        "workspace": "${home}/workspace"
    },
    "skills": {
        "load_mode": "startup",
        "full_content_skills": ["*"]
    }
}
```

---

## 待确定事项

~~1. **Skill 是否需要运行时动态加载?**~~ - 已确定: 可配置 (startup/dynamic)
~~2. **Skill 的引用方式?**~~ - 已确定: 可配置 (inject_all/enabled)

3. **是否需要 Skill Workshop (自动捕获)?**
   - 这是一个较大的功能
   - 初期可以先不做

---

## 开发顺序

1. `WorkspaceManager` 实现 (Home 目录解析、AGENTS.md 动态读取)
2. `ConfigManager` 配置项添加
3. `AgentRuntime` 集成 workspace (AGENTS.md 动态加载)
4. `SkillManager` 实现 (startup/dynamic 模式支持)
5. Skill 上下文注入 (inject_all / enabled 配置支持)
6. 单元测试

---

## 参考文档

- OpenClaw Agent Workspace: `docs/concepts/agent-workspace.md`
- OpenClaw Skill Workshop: `docs/plugins/skill-workshop.md`
- OpenClaw Plugin Manifest: `docs/plugins/manifest.md`
