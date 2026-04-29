# AI Context Profiles

本目录用于承载“可选启用”的 AI 上下文配置。

目标：

- 让特定业务组通过一个开关快速启用专属上下文
- 默认不影响未启用该 profile 的同学
- 将工具适配层与业务知识源解耦

当前约定：

- 配置文件开关：`switch.json` 中的 `deviceDevelopProfile`
  - 默认值 `false`，启用值 `true`
- 环境变量开关：`STUDIO_AI_DEVICE_ENTRY`
  - 设置为 `true` / `1` / `on`（不区分大小写）即启用，适用于 CI / 临时会话
- 显式要求：用户在自然语言里明确要求进入设备域

任一路开启即视为启用 device profile。

使用方式：

1. 在 `/.ai-context/switch.json` 或 `/openspec/ai-context/switch.json` 中将 `deviceDevelopProfile` 从 `false` 改为 `true`，或在环境中设置 `STUDIO_AI_DEVICE_ENTRY=true`
2. 各 AI 工具适配层读取上述开关，并在启用时加载 `profiles/device/entry.md`

注意：

- `/openspec/ai-context/` 只负责"开关 + 统一入口"
- 设备知识正文分布在 `/openspec/` 下的 `docs/device/`、`rules/device/`、`skills/device/`、`specs/device/`
- 工具适配层不应复制业务规则正文，只应引用本目录入口
