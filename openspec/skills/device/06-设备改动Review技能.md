---
name: device-change-review
description: >-
  推送 Gerrit 前的自查技能。检查用户敏感信息（P0）、C++ 代码质量、前端质量、
  commit 规范和文档一致性。用于本地 commit 完成后、推送前。
---

# 设备改动 Review 技能

推送 Gerrit 之前，对本次改动做一轮自查，降低 review 打回率。

## 适用场景

- 本地 commit 完成，推送前
- 用户要求对当前改动做 self-review
- `08-设备开发完成技能` 阶段 3 之后、`07-设备代码推送Gerrit技能` 之前

## 步骤

### 1. 确定范围

```powershell
git log --oneline origin/HEAD..HEAD
git diff origin/HEAD..HEAD --stat
```

### 2. 用户敏感信息（P0）

**最高优先级，命中必须修复后才能推送。**

#### 日志

检查 `BOOST_LOG_TRIVIAL`、`printf`、`std::cout`、`console.log`：

- 禁止打印完整 token / 用户 ID / 邮箱 / 手机号 / 序列号 / 密码 / API key
- 禁止打印完整 HTTP Authorization header 值
- 允许：token 前 6 位 + `...`、`token empty: true/false`

#### 网络请求

- Authorization header 仅 HTTPS
- 请求 body 不含非必要用户标识
- 错误回调不透传完整 response body 到 UI

#### 本地存储

- 不存储明文 token
- 缓存路径不含用户真实姓名或邮箱

#### 前端

- `console.log` 不输出 token / 用户 ID
- bridge 消息中用户数据已最小化
- store 不缓存认证 token

### 3. C++ 代码质量

#### 内存与资源安全

- 裸指针有明确所有权和释放路径
- 回调 lambda 捕获的 `this`，执行时对象是否存活
- `CallAfter` 中捕获的指针/引用生命周期覆盖回调时机

#### 线程安全

- HTTP 回调通过 `CallAfter` 回 UI 线程再操作 UI 数据
- 共享状态读写在同一线程或有同步机制
- 事件处理无长耗时操作阻塞 UI

#### 错误处理

- `on_error` 回调非空 lambda
- JSON 解析有 `try-catch`
- 网络不可达 / 超时 / 非 200 状态码有合理处理
- 错误日志有足够上下文（路径、状态码、摘要）

#### API 调用规范

- 认证接口调用前检查 `is_user_login()`
- URL 通过 `wxGetApp()` 获取，非硬编码
- HTTP method 与 API 设计一致

### 4. 前端代码质量

仅当变更包含 `web-panels/` 时执行。

- 遵循 `10-web-panels开发约束.md` 红线
- store 乐观更新有 `.catch` 兜底
- TypeScript 无 `any` 类型
- 无 `export default`

### 5. Commit Message 规范

- Change Type 正确
- jira / reason 字段存在且合理
- 自测结果字段存在
- 描述与实际改动一致

### 6. 文档一致性

仅当变更包含 `openspec/docs/device/` 时执行。

- 归档目录命名 `YYYYMMDD改动内容`
- `来源映射表.md` 已更新
- `主题索引.md` 已更新
- 对应主题 `README.md` 已更新

## 输出格式

```markdown
## 设备改动 Review 结果

**Review 范围**：X 个 commit，Y 个文件，Z 行变更
**总体评估**：[可推送 / 需修复后推送]

### 敏感信息检查
- （通过 / 发现 N 个问题）

### 代码质量
- P0：（无 / 列表）
- P1：（无 / 列表）
- P2：（无 / 列表）

### Commit Message
- （符合规范 / 需修改）

### 文档一致性
- （通过 / 需补充）

### 需修复项（推送前）
1. ...

### 建议改进项（不阻塞推送）
1. ...
```

## 严重级别

| 级别 | 描述 | 处置 |
| --- | --- | --- |
| P0 | 敏感信息泄露、崩溃、内存损坏 | **阻塞推送** |
| P1 | 线程安全、逻辑错误、缺少错误处理 | 推送前应修复 |
| P2 | 代码异味、可维护性、命名 | 建议修复，不阻塞 |
| P3 | 风格、可选优化 | 可选 |

## 不做什么

- 默认只输出 review 结果，不修改代码
- 不替代 Gerrit 正式 Code Review，本技能是推送前自查
- P0 问题报告并等待用户确认，不自动修复
