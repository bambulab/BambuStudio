## Why

耗材管理器当前前端使用原始 HTML/CSS/JS（`resources/web/fila_manager/`），自定义 `{type, seq, command}` 通信协议，与团队已确立的 Web Panel 开发规范（`openspec/rules/device/10-web-panels开发约束.md`）不一致。该规范要求所有 WebView 面板统一使用 React + Zustand + Tailwind CSS + TypeScript + Vite 技术栈，IIFE 构建输出，JSON-RPC 2.0 通信协议，以及标准化的项目结构。在后续面板功能持续扩展之前完成迁移，可以避免两套模式并存带来的维护成本，同时获得类型安全、组件化、主题适配（dark:/light:）等工程收益。

## What Changes

### 前端架构迁移
- 新建 `web-panels/` 目录（若尚未存在），安装 React/React DOM/Zustand/Tailwind/Vite 依赖
- 在 `web-panels/src/panels/fila-manager/` 下按规范创建面板：`main.tsx`、`types.ts`、`store.ts`、`style.css`、`FilaManagerPanel.tsx` 及 `components/` 子组件
- 将现有 `index.js` 的业务逻辑（列表渲染、筛选/排序/分页、弹窗表单、AMS 模式）用 React 函数组件 + Zustand store 重写
- 所有样式改用 Tailwind 原子类，支持 `dark:` variant 双主题
- 构建产物输出到 `resources/web/panels/fila-manager/`（IIFE 格式）

### C++↔JS 通信协议迁移
- 将自定义 `{type:"request", seq, command, data}` / `{type:"response", seq, code, data}` / `{type:"push", command, data}` 协议改为 **JSON-RPC 2.0**
  - JS → C++：`{ jsonrpc:"2.0", id:N, method:"fila_manager.xxx", params:{} }`
  - C++ → JS 响应：`{ jsonrpc:"2.0", id:N, result:{} }` 或 `{ jsonrpc:"2.0", id:N, error:{code, message} }`
  - C++ → JS 推送：`{ jsonrpc:"2.0", method:"fila_manager.xxx", params:{} }`（无 id）
- 前端统一通过共享 `bridge/client.ts` 的 `request()` 和 `on()` 与 C++ 交互

### C++ Panel 适配
- `wgtFilaManagerPanel` 加载路径从 `resources/web/fila_manager/index.html` 改为 `resources/web/panels/fila-manager/index.html`
- `OnWebMsg` 解析逻辑从自定义协议改为 JSON-RPC 2.0 dispatch
- `SendMsg` / `push_to_web` 输出格式改为 JSON-RPC 2.0
- handler 注册保持不变，仅 method name 加 `fila_manager.` 前缀
- bridge 注入从 `__cppPush` 改为标准 `window.postMessage`

### 旧代码清理
- 删除 `resources/web/fila_manager/`（index.html/css/js + test 文件）
- 更新 CMakeLists 或构建脚本确保新产物路径被打包

## Capabilities

### New Capabilities
- `web-panels-scaffold`: `web-panels/` 项目初始化（package.json、tsconfig、vite.config、bridge/client.ts），为耗材管理器及后续面板提供共享基础设施
- `fila-manager-react-ui`: 耗材管理器前端的 React + TypeScript 重写，包含表格视图、添加/编辑弹窗、AMS 模式、筛选排序分页等全部 UI 功能
- `fila-manager-jsonrpc-bridge`: C++ Panel 和前端的 JSON-RPC 2.0 通信协议适配

### Modified Capabilities
- `filament-manager-panel`: WebView 加载路径和消息解析逻辑变更（file path + protocol format）

## Impact

- **新增目录**: `web-panels/` — 完整的前端工程（~15 个源文件）
- **新增构建产物**: `resources/web/panels/fila-manager/` — IIFE 格式 HTML+JS+CSS
- **修改 C++**: `wgtFilaManagerPanel.h/cpp` — 加载路径、协议解析、bridge 注入
- **删除**: `resources/web/fila_manager/` — 旧前端代码（index.html/css/js, test.html, screenshot_test.py）
- **依赖**: 新增 Node.js 构建步骤（`npm install && npm run build`），需在 CI 和开发文档中说明
- **无数据变更**: `wgtFilaManagerStore` / `wgtFilaManagerSync` / `FilamentSpool` 数据模型不变
- **无功能变更**: 所有已有功能一一对应迁移，不增不减
