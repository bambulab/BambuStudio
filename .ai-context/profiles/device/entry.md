# Device Develop Profile Entry

下列任一条件成立即启用本 profile：

1. `/.ai-context/switch.json` 或 `/openspec/ai-context/switch.json` 中：

   ```json
   {
     "deviceDevelopProfile": true
   }
   ```

2. 环境变量 `STUDIO_AI_DEVICE_ENTRY` 设置为 `true` / `1` / `on`（不区分大小写）。

3. 用户在当前会话里明确要求进入设备域。

任意一路打开即启用，不要求三路同时满足。

## 启用后的工作方式

在本仓库中处理设备相关开发、分析、排查或文档整理任务时，应将 `/openspec/docs/device/` 视为优先上下文来源。

执行原则：

1. 不直接全量扫描 `/openspec/docs/device/`
2. 先按入口和索引建立上下文
3. 遇到设备域 `OpenSpec` 任务时，必须先进入 `/openspec/rules/device/03-openspec规则.md`
4. 再根据任务类型进入规则、技能或专题文档
5. 回答、实现和分析优先与 `/openspec/docs/device/` 既有约束保持一致

## 首次加载入口

优先阅读：

1. `/openspec/docs/device/readme食用指南.md`
2. `/openspec/docs/device/开发流程.md`
3. 若任务涉及设备域 `OpenSpec` change/spec/proposal/design/tasks/归档，先读 `/openspec/rules/device/03-openspec规则.md`
4. `/openspec/specs/device/README.md`

若需要快速定位主题，再看：

5. `/openspec/specs/device/主题索引.md`

## 按任务类型继续下钻

- 涉及目录约束、迁移、归档、结构调整：
  - 进入 `rules/README.md`
- 涉及设备域 `OpenSpec` 变更、归档、规格或基线：
  - 先进入 `/openspec/rules/device/03-openspec规则.md`，再回到 `/openspec/specs/device/`
- 涉及设备问题分析、资料检索、经验沉淀：
  - 进入 `skills/README.md`
- 涉及设备功能、代码入口、历史方案、OpenSpec 变更：
  - 进入 `mappings/README.md`，再回到 `/openspec/specs/device/`

## 边界

- 本 profile 只定义“如何进入设备上下文”
- `/openspec/docs/device/` 才是设备知识与约束正文
- 若当前任务明确与设备域无关，则不应强行加载设备文档链路
