# 耗材管理器

本目录收录耗材管理器（Filament Manager）相关的基线规格、OpenSpec 变更和实现归档，包括架构变更、云端接线、网络库改造、UI 打磨和功能扩展。

## 待定问题清单

- `待定问题清单.md` — 尚未形成规格的待讨论事项，按主题分章、按编号追加，供后续 `proposal` / `design` 引用。

## 手工自测

- `自测/` — 耗材管理器的功能自测资产（由 `device-self-test` 技能生成并手工维护）
  - `自测/index.html` — 可勾选的功能自测清单，按新用户旅程排列的 10 大功能组（F1~F10，60+ 条），支持本地 `file://` 打开、localStorage 持久化、JSON 导入导出
  - `自测/README.md` — 使用说明、功能组划分与清单演进约定
  - `自测/自动化E2E用例索引.md` — Playwright 自动化 E2E 用例索引，记录 `STUDIO-18340`、`STUDIO-18341` 等真实 `@printer` 与 mock `@webview-only` 用例的命令、前置条件和证据附件
  - 典型用途：联调阶段跑端到端验证；`20260417耗材管理器云端接入Web前端/tasks.md §10` 的手工核对入口

## 基线规格

物理归档于 `基线规格/` 子目录，原始路径为 `openspec/specs/`。

| 规格 | 一句话摘要 |
| --- | --- |
| `基线规格/filament-manager-panel/spec.md` | WebView 面板嵌入 MainFrame 顶级 Tab |
| `基线规格/filament-inventory-model/spec.md` | `FilamentSpool` 数据模型与本地 JSON 持久化 |
| `基线规格/filament-ams-sync/spec.md` | AMS 料槽状态同步到耗材库 |
| `基线规格/filament-manual-entry/spec.md` | 手动录入/编辑/删除/批量操作 |
| `基线规格/fila-table-view/spec.md` | 主页表格展示：导航、筛选、排序、批量操作 |
| `基线规格/fila-add-dialog/spec.md` | 添加/编辑耗材弹窗（手动 + AMS 双模式） |
| `基线规格/main-frame-navigation/spec.md` | MainFrame 顶级 Tab 注册「耗材管理」页 |

## 变更历史

按归档日期降序排列。

### 20260420耗材管理器云端接入改走网络库

**change**: `fila-cloud-network-agent` · 状态：active

把耗材管理器云端访问从 Studio 直连 `Http` 改为 `NetworkAgent -> bambu_network -> AccountManager` 标准链路，统一复用网络库中的鉴权、区域路由和请求封装；`networking` 侧对应实现已本地提交 `7e19457`。

- `proposal.md` — 为什么要把耗材云端改走网络库，以及与旧 CloudClient/CloudSync 文档的边界
- `design.md` — 技术设计（5 个接口、AccountManager 落点、Studio 适配层、异步保持方式、调用链）
- `tasks.md` — 已完成项与待验证项清单
- `specs/fila-cloud-network-agent/spec.md` — 网络库导出接口、Studio 接入约束与设备域归档要求

### 20260420耗材管理器开发版调试日志窗

**change**: `fila-debug-log-panel` · 状态：active

为耗材管理器页面增加一个仅开发版可见、固定贴底的调试日志窗，由 C++ 层统一控制开关并下发 `debug_enabled`，页面内可同时查看本地数据变化、C++↔Web 通信与云端 HTTP 请求/结果。

- `proposal.md` — 为什么单独抽成独立变更，以及它和云端接线 change 的边界
- `design.md` — 技术设计（C++ 总开关、统一 debug sink、固定贴底布局、结构化日志）
- `tasks.md` — 实施与验证清单（已按实际实现打勾）
- `specs/fila-debug-log-panel/spec.md` — 开发版底部调试日志窗的行为规格

### 20260417耗材管理器云端接入Web前端

**change**: `fila-cloud-integration` · 状态：active（§1~§9 已落地，§10 端到端验证待联调）

把已实现但孤立的 `wgtFilaManagerCloudClient` / `wgtFilaManagerCloudSync` 通过新建的 `wgtFilaManagerCloudDispatcher` 接入 `GUI_App` 生命周期、`FilaManagerVM` 新 submod、登录/登出事件、push 串行化与 React 前端 `useFilamentBridge` action，让 Filament tab 的增删改查真正打通云端 HTTP。

- `proposal.md` — 接线动机与范围（含承接 superseded 旧 change 的边界）
- `design.md` — 技术设计（raw ptr 所有权、`wgtFilaManagerCloudDispatcher`、统一 `build_sync_state()` payload、config 缓存 + 原样透传、错误分类）
- `tasks.md` — 实施任务（10 阶段）；§1~§9 已勾选并同步到实际落地形态，§10 端到端矩阵仍待跑
- `specs/fila-cloud-integration/spec.md` — C++ 接线层 ADDED requirements（所有权、dispatcher 专用接口、CRUD 联动、登录事件、`sync.state / pull_done / push_failed` Report、config 缓存策略）
- `specs/fila-cloud-web-actions/spec.md` — React ↔ Bridge 合约（sync/config Request/Response、`state / pull_done / push_failed / fetched` Report、`fetchCloudSyncStatus / triggerCloudPull / triggerCloudPushAll / fetchCloudFilamentConfig` Hook API、`cloudSync / cloudConfig / toasts` slice、`CloudBadge` 四态、AddEditDialog 合并）

### 20260417耗材管理器云端API对接

**change**: `fila-cloud-sync` · 状态：**superseded**（by `fila-cloud-integration`）

新增云端 API 客户端（`wgtFilaManagerCloudClient`）和同步编排器（`wgtFilaManagerCloudSync`），对接耗材管理 V2 五个 API（增删改查 + 配置），不修改现有代码。**C++ 源文件已实现并编译入 `libslic3r_gui`，但 CloudSync 从未被实例化，属于死代码；所有端到端验证 (task 10.x) 未执行**。接线工作迁移到 `fila-cloud-integration`。

- `STATUS.md` — superseded 说明与边界
- `proposal.md` — 云端 API 对接动机与范围
- `design.md` — 技术设计（CloudClient + CloudSync 双模块）
- `tasks.md` — 实施任务（10 阶段、30 任务，1~9 已实现，10 未验证）
- `specs/fila-cloud-api-client/spec.md` — 5 个 API 端点的客户端规格（仍有效，被新 change 引用）
- `specs/fila-cloud-sync/spec.md` — 云端⇄本地同步编排规格（仍有效，被新 change 引用）

原始 OpenSpec 路径保留为 `openspec/changes/fila-cloud-sync/`。

### 20260416耗材管理器子页面化

**change**: `fila-manager-dialog-pages` · 状态：active

将耗材管理器中的添加/编辑子弹窗从页内 overlay 改为独立页面视图，建立主列表页与子页面之间的进入、返回、参数传递和保存后刷新闭环。

- `proposal.md` — 页面化改造的动机与范围
- `design.md` — 面板内独立页面视图的设计决策
- `tasks.md` — 页面导航、表单迁移、入口对接与清理验证任务
- `specs/fila-dialog-pages/spec.md` — 主页面与独立子页面的导航规格
- `specs/filament-manager-panel/spec.md` — 面板内页面级切换与返回刷新规格
- `specs/fila-add-dialog/spec.md` — 添加/编辑流程页面化后的行为规格

原始 OpenSpec 路径保留为 `openspec/changes/fila-manager-dialog-pages/`。

### 20260416耗材管理器调整架构

**change**: `migrate-fila-manager-to-web-panels` · 状态：active

将耗材管理器前端从原始 HTML/CSS/JS 迁移到标准 Web Panel 架构（React + Zustand + Tailwind + TypeScript + Vite），通信协议从自定义格式迁移到 JSON-RPC 2.0。

- `proposal.md` — 迁移动机与范围
- `design.md` — 技术设计（7 个关键决策）
- `tasks.md` — 实施任务（10 阶段、60+ 任务）
- `specs/web-panels-scaffold/spec.md` — web-panels 基础设施规格
- `specs/fila-manager-react-ui/spec.md` — React UI 组件规格
- `specs/fila-manager-jsonrpc-bridge/spec.md` — JSON-RPC 2.0 通信规格

原始 OpenSpec 已删除，以本目录为唯一维护位置。

### 20260408列表UI打磨

**change**: `filament-list-ui-polish` · 状态：active

耗材列表视图 UI 打磨：料卷图标重设计、分组模式、底部分页器，与 Figma 交互稿对齐。

- `proposal.md` — 变更动机
- `design.md` — 技术方案
- `tasks.md` — 实施任务
- `specs/table-row-redesign/spec.md` — 耗材列行样式重设计
- `specs/table-grouping/spec.md` — 分组模式
- `specs/table-pagination/spec.md` — 底部分页器

### 20260408品牌类型下拉联动

**change**: `preset-driven-dropdowns` · 状态：active

添加耗材弹窗中品牌/类型下拉框由 `PresetBundle::filaments` 驱动联动。

- `proposal.md` — 变更动机
- `design.md` — 技术方案
- `tasks.md` — 实施任务
- `specs/preset-options-sync/spec.md` — C++ 侧 vendor/type 聚合与推送

### 20260408AMS设备列表请求

**change**: `ams-machine-list-request` · 状态：active

将打印机列表请求从 `get_ams_data` 中拆分为独立的 `get_machine_list`，优化设备选择器体验。

- `proposal.md` — 变更动机
- `design.md` — 技术方案
- `tasks.md` — 实施任务
- `specs/machine-list-request/spec.md` — `get_machine_list` 请求规格

### 20260408主题亮暗切换

**change**: `theme-dark-light` · 状态：active

耗材管理器 WebView 主题切换：CSS 变量方案、`[data-theme]` 属性、Studio 亮暗模式联动。

- `proposal.md` — 变更动机
- `design.md` — 技术方案
- `tasks.md` — 实施任务
- `specs/css-theme-switch/spec.md` — CSS 主题切换规格

### 20260407耗材管理器一期框架

**change**: `filament-manager-v1` · 状态：archived

一期完整框架搭建：C++ 三模块（Store、Panel、Sync）+ 前端 HTML/JS + AMS 同步。

- `proposal.md` — 需求动机
- `design.md` — 架构设计
- `tasks.md` — 实施任务

### 20260407耗材管理器UX对齐

**change**: `align-fila-manager-ux` · 状态：archived

前端 UI 对齐 Figma 交互稿：左侧导航、表格行样式、添加弹窗双模式。

- `proposal.md` — 变更动机
- `design.md` — 技术方案
- `tasks.md` — 实施任务
- `specs/fila-table-view/spec.md` — 表格视图增量规格
- `specs/fila-add-dialog/spec.md` — 添加弹窗增量规格
