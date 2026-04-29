# Device Develop Profile

这是设备组的可选 AI 上下文 profile。

设计目标：

- 设备组同学只需要修改 `/.ai-context/switch.json` 中的 `deviceDevelopProfile`
- 未启用时，不向其他组同学注入设备上下文
- 启用后，统一从本 profile 入口进入，再回到 `/openspec/docs/device/` 读取正文

目录说明：

- `entry.md`
  - 设备 profile 的统一入口
- `load-order.md`
  - 默认加载顺序与任务分流
- `rules/`
  - 设备规则入口索引
- `skills/`
  - 设备技能入口索引
- `mappings/`
  - 主题和来源映射入口索引

真实知识源：

- `/openspec/docs/device/readme食用指南.md`
- `/openspec/docs/device/开发流程.md`
- `/openspec/rules/device/`
- `/openspec/skills/device/`
- `/openspec/specs/device/`
