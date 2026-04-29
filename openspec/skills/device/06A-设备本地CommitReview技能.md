---
name: device-local-commit-review
description: >-
  本地 commit 完成后、推送 Gerrit 前，对设备域本地 commit 做结构化 review。
  用于检查 commit message、提交范围、patch 内容和推送门禁。
---

# 设备本地 Commit Review 技能

在设备域本地提交完成后、推送 Gerrit 前，对待推送 commit 做一次结构化 review。目标是确认 commit 本身可评审、message 合规、变更范围正确，并拦截会导致 Gerrit / CI / reviewer 打回的问题。

## 适用场景

- 本地 commit 已完成，准备推送 Gerrit
- 用户要求“review 本地提交 / review 最新提交 / 推送前检查”
- `07-设备代码推送Gerrit技能.md` 执行前的强制门禁

## 前置条件

- 工作区干净：`git status --short` 无输出
- 本地存在待推送 commit：`git log --oneline @{upstream}..HEAD` 有输出
- 代码验证已按本次改动风险完成（构建 / 定向测试 / 人工验证）

## 步骤

### 1. 确定 review 范围

```powershell
git status --short --branch
git log --oneline @{upstream}..HEAD
git diff @{upstream}..HEAD --stat
```

若工作区不干净，**停止**，让用户先决定提交、丢弃或拆分未提交改动；不要在 review 流程里自动 stash / restore 用户改动。

### 2. 检查 commit message

逐个检查待推送 commit：

```powershell
git log --reverse --pretty=fuller @{upstream}..HEAD
```

必须符合 `.cursor/rules/commit-msg.mdc`：

- subject 格式：`FIX|NEW|ENH|ADD|TMP|REVERT: 简短描述`
- 必须包含 `JIRA: STUDIO-...` / `github pull request: ...` / `JIRA: none reason: <reason>`
- 必须包含 `自测结果: pass|fail`
- `Change-Id: I<40-hex>` 必须是最后一行
- 禁止 `Made-with: Cursor`、`Generated-by:`、`Co-authored-by: Cursor` 等 AI 署名
- `.cursor/`、`.claude/`、`.doc/` 等 AI/文档规则目录不得和源码变更混在同一个 commit

命中任一问题：结论为 **需修复后推送**。

### 3. 检查提交范围

检查是否混入无关文件：

```powershell
git diff --name-status @{upstream}..HEAD
```

重点拦截：

- IDE / 构建缓存 / `__pycache__` / `.vs` / 临时日志
- unrelated dist 或格式化改动
- 不同 Jira / 不同逻辑改动被混在一个不可评审 commit 且 message 未说明
- 代码修复 commit 混入 AI 文档 / 规则目录

### 4. 检查 patch 内容

```powershell
git diff @{upstream}..HEAD --check
git diff @{upstream}..HEAD
```

按变更类型检查：

- C++ / wxWidgets：UI 操作是否在 UI 线程；回调捕获生命周期是否安全；错误路径是否处理
- Web / device_page：是否重建实际加载的 `dist`；新增 helper 是否有定向测试；是否引入无意义 `any` / `console.log`
- 云端 / 网络：不打印 token、用户 ID、邮箱、序列号、Authorization；错误信息不泄露完整响应体
- 文档 / 规则：若只改文档，应使用独立 `[AI Docs]` / doc-only 类 commit

### 5. 输出结论

```markdown
## 设备本地 Commit Review 结果

**Review 范围**：<N> 个 commit，<M> 个文件
**总体评估**：[可推送 / 需修复后推送]

### Commit Message
- <符合 / 问题列表>

### 提交范围
- <符合 / 问题列表>

### Patch 内容
- P0: <无 / 列表>
- P1: <无 / 列表>
- P2: <无 / 列表>

### 验证状态
- <已执行验证和结果 / 缺口>

### 结论
- <可进入 device-change-review 和 device-gerrit-push / 需先修复>
```

## 推送门禁

只有当结论为 **可推送** 时，才允许继续执行：

1. `device-change-review`
2. `device-gerrit-push`

如果结论为 **需修复后推送**，必须先修复并重新运行本技能。

## 不做什么

- 不自动修改代码或 commit message；只输出 review 结果，除非用户明确要求修复
- 不自动 stash / restore 用户改动
- 不跳过 `.cursor/rules/commit-msg.mdc`
- 不把“只看 git status 干净”当作 review 通过
