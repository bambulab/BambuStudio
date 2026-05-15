# OpenSpec 规则

本文件定义设备域 `OpenSpec` 资料的统一处理口径，包括：

- 哪些 `OpenSpec` 资料可以进入 `openspec/specs/device/软件代码文档/`
- 设备 profile 在遇到 `OpenSpec` 相关任务时应按什么入口链路进入上下文
- 归档时需要补哪些信息与索引

## 入口约束

当任务属于以下任一情况时，设备 profile 必须按本规则进入，而不是只停留在通用索引层：

- 新建或修改设备相关 `OpenSpec` change
- 归档设备相关 `OpenSpec` 资料
- 阅读设备相关 `OpenSpec` 历史变更或基线规格
- 从设备问题继续下钻到 `OpenSpec` 设计和实现背景

推荐链路：

1. `/openspec/docs/device/readme食用指南.md`
2. `/openspec/docs/device/开发流程.md`
3. `/openspec/rules/device/03-openspec规则.md`
4. `/openspec/specs/device/README.md`
5. 需要按主题快速定位时，再看 `/openspec/specs/device/主题索引.md`

## 归档目标

设备相关 `OpenSpec` 在本目录中承担三类作用：

- 记录设计背景
- 记录实现取舍
- 提供代码阅读与变更追踪入口

因此它们不直接混入功能架构文档，而进入软件代码文档。

## 纳入标准

满足以下任一条件即可纳入：

- 直接涉及设备对象、设备列表、设备状态同步
- 直接涉及设备页面、设备交互、设备发送流程
- 直接涉及 AMS、料槽、打印机选择、设备协议
- 能明显补充设备代码入口、实现约束或设计决策

## 暂不纳入标准

以下情况默认不纳入设备主归档：

- 纯主题样式、配色、视觉模式切换
- 通用下拉框、表格样式、无设备语义的 UI 打磨
- 仅涉及耗材 profile 参数、但不构成设备主流程知识

## 状态标注

进入归档区时，建议使用以下状态：

- `active`
  - 正在推进或作为独立专题仍有参考价值
- `baseline`
  - 作为长期能力基线存在
- `archived`
  - 已完成归档，但仍保留历史设计价值

## 归档目录命名规则

OpenSpec change 归档到 `openspec/specs/device/软件代码文档/` 时，目录命名格式为：

```
YYYYMMDD + 改动内容描述
```

示例：

- `20260416耗材管理器调整架构`
- `20260407耗材管理器一期框架`
- `20260410AMS设备列表请求拆分`

规则：

- 日期为归档当天，格式 `YYYYMMDD`，紧跟改动内容，无分隔符
- 改动内容使用中文业务语义，简明扼要
- 同一主题的多次变更按日期排列在同一主题目录下（如 `软件代码文档/耗材管理器/`）

归档目录内部结构保持与 OpenSpec change 一致：

```
YYYYMMDD改动内容/
├── .openspec.yaml
├── proposal.md
├── design.md
├── tasks.md
└── specs/
    └── {capability-name}/
        └── spec.md
```

## 归档时同步更新的索引

每次归档后，按以下顺序更新：

1. 对应主题目录的 `README.md`（如 `软件代码文档/耗材管理器/README.md`）
2. `openspec/specs/device/来源映射表.md` - 新增来源与归档位置映射
3. `openspec/specs/device/主题索引.md` - 在对应问题域下补充条目
4. `openspec/specs/device/软件代码文档/README.md` - 如新增主题目录则补充入口

## 每个条目至少补充的信息

- 来源路径
- 主题一句话摘要
- 状态
- 影响范围
- 推荐阅读顺序

## 当前已纳入条目

### 耗材管理器主题（`openspec/specs/device/软件代码文档/耗材管理器/`）

基线规格（7 个，物理归档于 `耗材管理器/基线规格/`）：

- `openspec/specs/filament-manager-panel/spec.md`
- `openspec/specs/filament-inventory-model/spec.md`
- `openspec/specs/filament-ams-sync/spec.md`
- `openspec/specs/filament-manual-entry/spec.md`
- `openspec/specs/fila-table-view/spec.md`
- `openspec/specs/fila-add-dialog/spec.md`
- `openspec/specs/main-frame-navigation/spec.md`

变更记录（12 个，物理归档于 `耗材管理器/YYYYMMDD改动内容/`）：

- `../networking/...` + `src/slic3r/Utils/...` + `src/slic3r/GUI/fila_manager/wgtFilaManagerCloudClient...` → `20260420耗材管理器云端接入改走网络库/`
- `openspec/changes/fila-debug-log-panel/` → `20260420耗材管理器开发版调试日志窗/`
- `openspec/changes/fila-cloud-integration/` → `20260417耗材管理器云端接入Web前端/`
- `openspec/changes/fila-cloud-sync/` → `20260417耗材管理器云端API对接/`
- `openspec/changes/fila-manager-dialog-pages/` → `20260416耗材管理器子页面化/`
- ~~`openspec/changes/migrate-fila-manager-to-web-panels/`~~（已删除）→ `20260416耗材管理器调整架构/`
- `openspec/changes/filament-list-ui-polish/` → `20260408列表UI打磨/`
- `openspec/changes/preset-driven-dropdowns/` → `20260408品牌类型下拉联动/`
- `openspec/changes/ams-machine-list-request/` → `20260408AMS设备列表请求/`
- `openspec/changes/theme-dark-light/` → `20260408主题亮暗切换/`
- `openspec/changes/archive/2026-04-07-filament-manager-v1/` → `20260407耗材管理器一期框架/`
- `openspec/changes/archive/2026-04-07-align-fila-manager-ux/` → `20260407耗材管理器UX对齐/`

### 其他设备主题

- `openspec/changes/fix-ext-spool-line-with-selector/`

## 当前首批排除条目

- `openspec/changes/adjust-preheat-temperature/` - 偏耗材 profile 与打印策略

## 维护动作

新增设备相关 `OpenSpec` 后，按以下顺序维护：

1. 更新 `openspec/specs/device/来源映射表.md`
2. 如影响问题域，更新 `openspec/specs/device/主题索引.md`
3. 在 `openspec/specs/device/软件代码文档/README.md` 或对应专题入口中补说明
