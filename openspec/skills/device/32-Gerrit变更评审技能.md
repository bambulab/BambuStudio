---
name: device-gerrit-review
description: >-
  对一个 Gerrit change 做完整评审并把意见发回 Gerrit。支持 inline 行评 + patchset 总评 +
  可选 Code-Review 打分。用于用户给出 Gerrit URL 并要求"评审"、"回评"、"发 review"、
  "把意见发到 Gerrit"时。默认不打分、默认发送前先让用户确认。
---

# Gerrit 变更评审技能

完成一轮 Gerrit change 的评审，并把结构化意见发回 Gerrit。

## 适用场景

- 用户给出 Gerrit URL 或 change 编号，要求帮忙评审
- 已经完成本地评审，需要把意见发回 Gerrit
- 需要对 change 打 `Code-Review ±1/±2`（仅用户明确要求时）

## 前置条件

- 已按 `30-Gerrit凭据配置技能.md` 配好 `GERRIT_USERNAME` / `GERRIT_PAT`
- 会用 `31-Gerrit变更查询技能.md` 抓 `current_revision` 和 diff

## 核心原则

1. **先确认，后发送**：除非用户明确说"直接发"，必须先给出草稿由用户过一遍
2. **能精确挂行走 inline，定位不稳就走 patchset**：不为了有 inline 而硬挂到近似行
3. **不默认打分**：未经明确要求，不附加 `Code-Review` label
4. **每条意见分编号和级别**：`R1/R2/...` + `P0/P1/P2`
5. **inline 默认 `unresolved: true`**；inline 意见不在 patchset message 里再抄一遍
6. **Windows 下用"临时 JSON + Python 脚本"发送**，不内联长命令

## 级别定义

| 级别 | 含义 |
| --- | --- |
| P0 | 必修，阻塞合入（逻辑错误、回归风险、安全问题） |
| P1 | 最好修，明显改善正确性 / 可维护性 |
| P2 | 可修可不修，风格或表达层优化 |

## 步骤

### 1. 解析 URL，抓元数据

按 `31-Gerrit变更查询技能.md` 的 Step 1、Step 3 拿到：

- `current_revision`（commit hash）
- `status` / `labels` / `branch`
- 文件列表

> 后续 POST 请求必须使用 revision hash，不要写 `current`。

### 2. 抓 diff 做评审

优先拉整包 patch 看全局，再对重点文件用 `31-` Step 5 拉单文件 diff 精确定位行号。

评审覆盖面：

| 维度 | 重点 |
| --- | --- |
| 正确性 | 逻辑、空指针、边界、错误处理 |
| 并发与资源 | UI 线程安全、锁、资源泄漏 |
| 安全 | 注入、硬编码密钥、输入校验 |
| 性能 | 热路径拷贝、N+1、阻塞调用 |
| 可维护性 | 命名、职责、死代码、测试覆盖 |
| 架构 | 分层、耦合、API 兼容 |

设备域补充关注点（来自 `openspec/rules/device/`）：

- UI 线程 vs 设备线程的数据访问边界
- 协议层版本兼容（不要破坏旧机型）
- 涉及耗材/打印参数的默认值改动必须有注释

### 3. 整理评审问题表

维护一张内部表，每条至少包含：

| id | severity | file | line | delivery | summary | suggestion |
| --- | --- | --- | --- | --- | --- | --- |
| R1 | P1 | `src/…/foo.cpp` | 42 | inline | 复制了主流程初始化 | 提取公共函数 |
| R2 | P2 | `patchset` | — | patchset | helper 命名不贴合行为 | 改名 |

`delivery`：

- `inline`：能精确挂到 revision 侧新增/修改行
- `patchset`：挂不稳 / 属于总体问题 / 涉及多文件

### 4. 校验 inline 行号

对每个要发 inline 的文件，先从 diff 里确认目标行号在 **revision 侧**（新增或修改行），不要挂到删除行或上下文行。

### 5. 准备发送前草稿

给用户看：

```markdown
## Gerrit 回评草稿

- Change: <N>
- Patchset: <current revision short hash>
- Labels: <none 或 Code-Review ±N>
- Patchset message:
  - <总体结论>
  - <patchset 级问题，若有>

| 编号 | 级别 | 投递 | 位置 | 问题 | 建议 |
|------|------|------|------|------|------|
| R1 | P1 | inline | `src/…/foo.cpp:42` | … | … |
| R2 | P2 | patchset | `patchset` | … | … |
```

等待用户明确"直接发"后再执行 step 6。

### 6. 落盘 payload + 脚本，发送

**`review_payload.json`**（示例）：

```json
{
  "message": "整体方向对，但存在 1 条 patchset 级问题需处理。",
  "comments": {
    "src/slic3r/GUI/foo.cpp": [
      {
        "line": 42,
        "unresolved": true,
        "message": "R1 [P1] 这里复制了主流程初始化，建议抽公共函数，只保留分支差异。"
      }
    ]
  }
}
```

只有用户明确要求打分才加 `"labels": {"Code-Review": -1}`。

**`post_review.py`**：

```python
import base64, json, os, pathlib, sys, urllib.request
host, change, revision, payload_path = sys.argv[1], sys.argv[2], sys.argv[3], pathlib.Path(sys.argv[4])
payload = payload_path.read_text(encoding="utf-8")
json.loads(payload)
req = urllib.request.Request(f"{host}/a/changes/{change}/revisions/{revision}/review",
                             data=payload.encode("utf-8"), method="POST")
auth = (os.environ["GERRIT_USERNAME"] + ":" + os.environ["GERRIT_PAT"]).encode()
req.add_header("Authorization", "Basic " + base64.b64encode(auth).decode())
req.add_header("Content-Type", "application/json; charset=utf-8")
resp = urllib.request.urlopen(req).read().decode("utf-8")
print(resp[4:] if resp.startswith(")]}'") else resp)
```

执行：

```powershell
python post_review.py "https://gerrit.bambooolab.com" "<N>" "<revision_hash>" "review_payload.json"
```

### 7. 复核 + 回报

- 返回 `{}` 只代表 API 接受，**必须再调一次 `/messages` 或 `/comments` 确认**评论确实落在目标 change/patchset/文件/行
- 确认 inline 的 `unresolved: true` 生效
- 回报给用户：发到了哪个 change / patchset、inline 条数、label 值

### 8. 清理

发送完成后删除临时文件：

```powershell
Remove-Item review_payload.json, post_review.py
```

不要把 `review_payload.json` / `post_review.py` 提交到 git。

## 错误处理

| 现象 | 处理 |
| --- | --- |
| 401 Unauthorized | 检查 `GERRIT_USERNAME` / `GERRIT_PAT`；回 `30-Gerrit凭据配置技能.md` |
| 403 Forbidden | 去掉 `labels` 重试；账号可能没有打分权限 |
| 404 Not Found | 核对 change / revision hash；不要用 `current` 作为 URL 段 |
| 400 Bad Request | 检查 `comments` 的文件路径和 `line`（必须是 revision 侧新增/修改行） |
| 409 Conflict | revision 过期，重新抓 `current_revision` 后重发 |

纪律：错误先读 response body，再决定重试方向。业务错误改 payload；传输错误切到"临时文件 + Python"方案，不在 PowerShell 里试 3 种引号写法。

## 不做什么

- 不在未确认情况下直接发送（除非用户明确"直接发"）
- 不自动打 `Code-Review` 分数
- 不把 inline 意见原文重复写进 patchset message
- 不在 URL 里写 `current` 或 patchset 数字，使用 revision hash
- 不把 PAT / 凭据写入仓库文件
- 不在 PowerShell 里用 here-string + `python -c` + 长 JSON 混拼
