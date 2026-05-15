# 软件代码文档

本区收敛设备相关的代码阅读线索、协议入口、实现决策和 `OpenSpec` 变更。

与功能架构文档相比，这里更强调：

- 代码入口在哪里
- 协议和请求如何组织
- 某次设备相关改动为什么这么做
- 代码阅读应从哪条链路进入

## 主要来源

### 1. 设备相关 `OpenSpec`

首批纳入条目：

- `openspec/changes/ams-machine-list-request/`
- `openspec/changes/fix-ext-spool-line-with-selector/`
- `openspec/specs/filament-ams-sync/spec.md`
- `openspec/changes/archive/2026-04-07-align-fila-manager-ux/`
- `openspec/changes/archive/2026-04-07-filament-manager-v1/`

### 2. 耗材管理器

- `软件代码文档/耗材管理器/` — 耗材管理器专题目录（基线规格 + 全部变更）

### 2a. 主题与配色（跨切面参考）

- `软件代码文档/主题与配色/` — Studio C++ / WebView 两套主题系统的实现线索与亮暗配色对照表
  - `README.md` — 入口、适用范围、关键源码入口
  - `主题实现参考.md` — `dark_mode()` / `StateColor` / `UpdateDarkUI` + WebView UserAgent / `theme_changed` 推送链路
  - `配色对照表.md` — `GUI_App` 命名色、`StateColor::gDarkColors` 36 条、老 CSS 变量、新 Tailwind `@theme` token 三张对照表

### 2b. DPI 适配（跨切面参考）

- `软件代码文档/DPI适配/` — Studio 在高 DPI / 跨屏拖动 / 运行时 DPI 变化下的适配机制
  - `README.md` — 入口、适用范围、关键源码入口
  - `DPI实现参考.md` — `DPI_DEFAULT` / `DPIAware<P>` / `em_unit` / `FromDIP` / `ScalableBitmap` + `MainFrame::on_dpi_changed` 全链路 + WebView 透传策略
  - `DPI接入清单.md` — 新写 Dialog / Panel / 控件 / WebView 页面时的接入步骤、代码审查自检清单、常见问题速查

基线规格（7 个，物理归档于 `耗材管理器/基线规格/`）：

- `filament-manager-panel` — WebView 面板
- `filament-inventory-model` — 数据模型
- `filament-ams-sync` — AMS 同步
- `filament-manual-entry` — 手动录入
- `fila-table-view` — 表格视图
- `fila-add-dialog` — 添加弹窗
- `main-frame-navigation` — MainFrame 导航

变更记录（12 个）：

- `20260420耗材管理器云端接入改走网络库/` — 耗材云端访问改走 `NetworkAgent -> bambu_network -> AccountManager`
- `20260420耗材管理器开发版调试日志窗/` — 开发版底部调试日志窗（`data / bridge / http`）
- `20260417耗材管理器云端接入Web前端/` — CloudSync 所有权、VM 接入、登录事件、前端 sync/config action
- `20260417耗材管理器云端API对接/` — 云端 API 客户端与同步编排（5 端点 + pull/push）
- `20260416耗材管理器子页面化/` — 添加/编辑子弹窗改为独立页面视图
- `20260416耗材管理器调整架构/` — 前端迁移到 Web Panel 架构（React + JSON-RPC 2.0）
- `20260408列表UI打磨/` — 列表 UI 打磨（分组、分页、行样式）
- `20260408品牌类型下拉联动/` — 品牌/类型下拉由 PresetBundle 驱动
- `20260408AMS设备列表请求/` — get_machine_list 拆分
- `20260408主题亮暗切换/` — WebView CSS 主题切换
- `20260407耗材管理器一期框架/` — 一期 C++ 三模块 + 前端
- `20260407耗材管理器UX对齐/` — UI 对齐 Figma 交互稿

### 3. 代码阅读类资料

当前与代码阅读强相关的设备文档仍主要位于：

- `docs/device-management/05-附录/01-关键文件索引.md`
- `docs/device-management/05-附录/06-关键源码入口索引-精确版.md`
- `docs/device-management/05-附录/07-设备管理源码阅读路线图.md`
- `docs/device-management/05-附录/08-关键调用链表.md`

### 4. 外部协作源码

- `NetworkAgent` 对接的 `bambu_network` / `AccountManager` 源码不应默认假设始终在当前工作区内
- 相关实现结论优先看 `耗材管理器/20260420耗材管理器云端接入改走网络库/`
- 当任务必须继续下钻到网络库源码，而当前工作区不可读或缺失时，应先让用户提供网络库仓库链接或源码路径，再继续分析

## 阅读建议

### 先理解需求和变更背景

建议顺序：

1. `ams-machine-list-request`
2. `filament-ams-sync`
3. `fix-ext-spool-line-with-selector`
4. archive 历史方案

### 再追源码

建议配合：

- `openspec/specs/device/主题索引.md`
- `openspec/specs/device/来源映射表.md`
- `docs/device-management/05-附录/` 下的索引类文档
- 需要网络库实现细节时，再按索引提示向用户索要 networking 仓库链接

## 当前边界

- 这里收录实现视角的设备资料
- 纯功能分层和页面结构仍优先放在 `openspec/specs/device/功能架构文档/`
- `OpenSpec` 不强制搬运原始文件，先保留原始路径并在这里建立设备视角入口
- 设备域 `OpenSpec` 任务应先遵循 `../../.rules/03-openspec规则.md` 的入口链路和归档口径

详细规则见：`../../.rules/03-openspec规则.md`
