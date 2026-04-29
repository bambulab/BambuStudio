# Commit msg 语言规则（设备域）

本规则定义设备域开发场景下 commit message 的语言选择，补充 `.cursor/rules/commit-msg.mdc`
的通用格式约束；通用格式规则（Change Type / jira / 自测结果 等）仍然适用。

## 触发条件：是否包含"开源代码"改动

"开源代码" = 会被推送到上游开源仓库（`github.com/bambulab/BambuStudio` 及对应 Gerrit
`refs/for/` 目标分支）的代码与资源，典型路径：

- `src/`、`xs/`、`resources/`、`deps/`、`tests/`
- 仓库根的 `CMakeLists.txt`、`build_*`、`package_*`、`BuildLinux.sh` 等构建脚本
- 其他会随版本发布出仓的文件

"非开源代码" = 仅本地开发 / 协作工具链，不会进入上游发布，典型路径：

- `.cursor/`、`.claude/`、`.jira/`
- `openspec/`（含 `openspec/ai-context/`）、`.specify/`、`.superpowers/`
- 其他明确的内部工具或草稿目录

注：上面这些目录都属于 `.cursor/rules/commit-msg.mdc` 「AI 相关目录提交隔离」管辖范围，
独立提交时必须带 `[AI Docs]` 标签。

## 规则

1. 只要 commit 的 diff 中**任一文件**命中"开源代码"路径，整条 commit message
   （subject / body / trailer）必须使用**全英文**。
2. 若 commit 只动"非开源代码"文件，可使用中文或英文，保持可读即可（设备域文档
   改动建议用中文，和 `openspec/docs/device/` 已有内容一致）。
3. 尽量不要把"开源代码"与"非开源代码"的改动混在同一 commit 里。
   必须混写时，按规则 1 使用全英文。
4. 规则 1 适用范围包含主题、正文、`jira:` / `reason:` / `自测结果:` 等 trailer
   的**字段值**；字段名本身保持仓库现有约定。
5. cherry-pick 到其它分支时保持源 commit 的语言不变，不要因目标分支调整语言。

## 常见场景速查

| 改动范围 | 示例 | 语言 |
| --- | --- | --- |
| 纯 `src/` 或 `resources/` | `filaments_blacklist.json`、设备模块 C++ 代码 | 英文 |
| 纯 `openspec/` 任意子目录 | 新增技能、更新 README、OpenSpec 归档、设备域 spec | 中文（推荐），需带 `[AI Docs]` |
| 纯 `.cursor/skills/device/` 桥接 | 新建 SKILL.md 桥接、更新门控逻辑 | 中文（推荐），需带 `[AI Docs]` |
| 混合（例如 src/ + openspec/） | 改 C++ 同时沉淀文档 | 必须拆成至少两条 commit（AI 隔离规则） |

## 与通用规则的关系

- 本规则优先级高于"保持原有语言"的默认描述，仅在设备域内生效。
- `.cursor/rules/commit-msg.mdc` 中的格式、必填字段、`reason` 枚举等仍然必须遵守，
  不因语言切换而放宽。
