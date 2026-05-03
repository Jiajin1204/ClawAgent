---
name: skill-creator
description: 当用户需要实现某个重复性任务时，创建可复用的 skill 给 agent 使用
---

# Skill Creator

当你需要创建一个新的 skill 时，按照以下步骤操作：

## 重要原则

**Skill 是给 agent 自己使用的操作指南，不是可执行脚本。**

Agent 会根据 SKILL.md 中的 Workflow 步骤来执行任务，就像用户指导 agent 一样。

## 步骤 1：确定 Skill 的用途

问自己：
- 这个任务是什么？
- 什么时候应该使用这个 skill？
- 任务可以分解成哪些步骤？

## 步骤 2：确定 ClawAgent Home 目录路径

**关键：ClawAgent 的 home 目录来自 config.json 配置，不是固定路径**

首先确定 home 目录：
1. 检查 `~/.clawagent` 是否存在
2. 或者查看当前用户 home 目录：`echo $HOME`
3. ClawAgent home 通常是 `${HOME}/.clawagent`

**注意：不同用户/设备的路径可能不同**

## 步骤 3：创建 SKILL.md 文件

**Skill 存放位置：`~/.clawagent/skills/<skill-name>/SKILL.md`**

格式必须是标准 Markdown：

```markdown
---
name: my-skill
description: 一句话描述 skill 用途
---

# My Skill

## Workflow

- 步骤 1：具体操作...
- 步骤 2：具体操作...
- 步骤 3：具体操作...

## When to Use

当用户需要...的时候使用此 skill。
```

## 步骤 4：创建目录和文件

使用 exec 工具创建目录，使用 write 工具创建文件：

```json
{
  "command": "mkdir -p ~/.clawagent/skills/my-skill"
}
```

```json
{
  "path": "~/.clawagent/skills/my-skill/SKILL.md",
  "text": "---\nname: my-skill\ndescription: ...\n---\n\n# My Skill\n\n## Workflow\n\n- ..."
}
```

**注意：使用 ~ 路径会由 shell 自动展开为用户 home 目录**

## 步骤 5：通知用户

告诉用户 skill 已创建，需要重启或在配置中启用才能生效。

## 示例：创建 Tavily 搜索 Skill

**用户需求**：创建搜索实时新闻的 skill

**正确的 SKILL.md 内容**：

```markdown
---
name: tavily-search
description: 使用 Tavily API 搜索实时新闻和最新信息
---

# Tavily Search

## Workflow

1. 构建 curl 命令调用 Tavily API：
   ```
   curl -X POST https://api.tavily.com/search \
     -H "Content-Type: application/json" \
     -H "Authorization: Bearer <API_KEY>" \
     -d '{"query": "<用户搜索词>", "search_depth": "advanced"}'
   ```

2. 使用 exec 工具执行 curl 命令

3. 解析返回的 JSON 结果（包含 results 数组）

4. 整理成结构化摘要返回给用户，包含：
   - 新闻标题
   - 来源 URL
   - 相关性分数
   - 内容摘要

## When to Use

当用户询问实时新闻、今日热点、最新资讯时使用此 skill。
```

**创建步骤示例：**

1. `mkdir -p ~/.clawagent/skills/tavily-search`
2. 创建 SKILL.md 文件

**注意**：
- **不需要创建脚本文件**，Workflow 就是 agent 的执行指南
- **不需要创建 skill.yaml**，只创建 SKILL.md
- **不需要创建 README.md**，SKILL.md 本身包含所有信息
- **使用 ~ 路径**，shell 会自动展开为正确的 home 目录

## 注意事项

- skill 名称只支持小写字母、数字、连字符（不用大写）
- description 应该简洁，一句话说明用途
- Workflow 应该清晰，每一步都有 agent 可执行的具体操作
- When to Use 说明触发此 skill 的场景
- **使用 ~ 路径让 shell 自动展开**
- **所有操作都是通过 read/write/exec 工具完成的**
