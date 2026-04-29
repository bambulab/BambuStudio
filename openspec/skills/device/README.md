# 设备域技能索引

`openspec/skills/device/` 用来说明如何利用工具和 `openspec/docs/device/` 中的资料高效工作。

## 技能列表

按开发生命周期排序：

### 前置判别（无条件激活）

| 文件 | name | 用途 |
| --- | --- | --- |
| `00-设备域识别技能.md` | `device-domain-detect` | 判定一个任务 / 改动 / commit / issue 是否属于设备小组业务面，输出"是 / 否 / 跨边界 + 置信度 + 触发信号 + 当前开关状态"。**自身不受 profile 开关限制**，但**只识别不启用**——识别结果不会自动调用其他设备域技能或加载 profile，是否启用仍由 `deviceDevelopProfile` / `STUDIO_AI_DEVICE_ENTRY` / 用户明确要求决定 |

### 开发前

| 文件 | name | 用途 |
| --- | --- | --- |
| `01-设备上下文进入技能.md` | `device-context-entry` | 根据开关和任务类型进入设备上下文 |
| `02-设备知识检索技能.md` | `device-knowledge-search` | 通过索引快速定位设备资料 |
| `03-设备问题分析技能.md` | `device-problem-analysis` | 从问题域追溯到设计和源码 |

### 开发后

| 文件 | name | 用途 |
| --- | --- | --- |
| `04-文档沉淀技能.md` | `device-doc-archiving` | 开发产出沉淀回设备目录 |
| `05-设备代码本地提交技能.md` | `device-local-commit` | 分类提交功能代码和文档到本地仓库 |
| `06-设备改动Review技能.md` | `device-change-review` | 推送前自查：敏感信息、代码质量、规范 |
| `07-设备代码推送Gerrit技能.md` | `device-gerrit-push` | 推送 commit 到 Gerrit Code Review |

### 收尾

| 文件 | name | 用途 |
| --- | --- | --- |
| `08-设备开发完成技能.md` | `device-dev-completion` | 善后：沉淀 → 提交 → 回顾 → 改进 |

### 手动触发（不跟随 profile 自动激活）

| 文件 | name | 用途 |
| --- | --- | --- |
| `09-设备研发本地编译技能.md` | `device-local-build` | Windows Release 本地编译（最小验证 / 完整构建），仅用户手动调用时触发，激活时先告知用户 |
| `10-设备研发本地调试技能.md` | `device-local-debug` | 先按 `device-local-build` 做完整构建，再默认打开 Visual Studio（可切 VSCode）启动调试；仅用户手动调用时触发，激活时先告知用户 |
| `11-设备联调自测技能.md` | `device-self-test` | 把改动 spec 目录里的"改动预期 / 功能预期"抽成可交互本地 HTML 自测页（写回该 spec 目录），供开发者勾选状态与备注、导出 Markdown 报告；仅生成页面，不触发构建/调试/提交，仅用户手动调用时触发 |

### 套件管理（手动触发）

| 文件 | name | 用途 |
| --- | --- | --- |
| `50-设备域开发套件移植技能.md` | `device-kit-migrate` | 把当前项目的设备域 AI 开发资产（技能 / 规则 / OpenSpec / profile）打包成 `workspace/` + `globals/` 双层 toolkit，铺到新项目或新机器；执行前向用户**七问**：目标项目名 / 是否带用户名下资产 / 是否带项目通用规则与跨项目技能 / 是否带设备 specs / 是否对 STUDIO 标记脱敏 / 输出形态 / 安装模式；不改源项目；仅手动调用，激活前必须告知 |

## 推荐阅读顺序

0. 归属判别 → `00`（任务归属不明 / 准备应用设备域规则前先用它；只识别，不会自动启用 profile）
1. 进入设备上下文 → `01`（**真正启用** profile 的动作；需开关或用户授权）
2. 查找资料 → `02`
3. 问题排查 → `03`
4. 补知识或归档 → `04`
5. 提交代码 → `05`
6. 推送前自查 → `06`
7. 推送 Gerrit → `07`
8. 周期收尾 → `08`（编排 04-07 并增加流程反馈）

## 格式规范

每个技能文件遵循 Cursor SKILL.md 格式：

- YAML frontmatter：`name`（kebab-case，≤64 字符）+ `description`（≤1024 字符，第三人称，含 WHAT 和 WHEN）
- Markdown body < 500 行
- 结构：目标 → 适用场景 → 步骤 → 不做什么
