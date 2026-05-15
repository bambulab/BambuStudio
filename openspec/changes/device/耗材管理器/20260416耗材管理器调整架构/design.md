## Context

### 现有实现

耗材管理器前端位于 `resources/web/fila_manager/`，由三个文件组成：
- `index.html`（~420 行）：DOM 骨架 + 内联 SVG 图标
- `index.css`（~1200 行）：手写 CSS，`:root` 变量硬编码暗色主题
- `index.js`（~1460 行）：全局变量 + 函数式渲染，jQuery 依赖

C++ 端 `wgtFilaManagerPanel` 使用自定义消息协议：
- JS → C++：`{type:"request", seq:N, command:"xxx", data:{...}}`
- C++ → JS：`{type:"response", seq:N, code:0, data:{...}}` 或 `{type:"push", command:"xxx", data:{...}}`
- bridge 通过 `window.__cppPush()` 注入，JS 通过 `chrome.webview.postMessage` / `window.wx.postMessage` 发送

### 目标规范

`openspec/rules/device/10-web-panels开发约束.md` 定义了标准化 Web Panel 架构：
- **技术栈**：React + Zustand 5 + Tailwind CSS 4 + TypeScript 6 + Vite 8
- **协议**：JSON-RPC 2.0（`{jsonrpc:"2.0", id, method, params}` / `{jsonrpc:"2.0", id, result/error}`）
- **构建**：IIFE 格式，产物到 `resources/web/panels/{panel-name}/`
- **通信**：统一 `bridge/client.ts`，禁止组件直接 `postMessage`

### 约束

- `wgtFilaManagerStore`（数据模型 + 持久化）和 `wgtFilaManagerSync`（AMS 同步）不变
- `FilamentSpool` 结构体不变
- 所有已有功能必须一一对应迁移，不增不减
- Node.js 构建步骤需在开发环境可选（不影响无 Node.js 的 C++ 编译）

## Goals / Non-Goals

**Goals:**
- 前端完全按 `10-web-panels开发约束.md` 规范重写，获得类型安全和组件化能力
- C++ Panel 协议层迁移到 JSON-RPC 2.0，与其他 Web Panel 统一
- 构建产物输出到标准路径 `resources/web/panels/fila-manager/`
- 支持 `dark:` / `light:` 双主题（Tailwind variant）
- 旧代码完整清理

**Non-Goals:**
- 不新增功能（统计页、扫描录入等仍为 V2 scope）
- 不修改 `wgtFilaManagerStore` / `wgtFilaManagerSync` 的接口和行为
- 不要求 CI 必须运行前端构建（构建产物可提交到 repo）
- 不迁移其他面板（本次仅 fila-manager）
- 不引入 SSR、路由库或其他超出规范的依赖

## Decisions

### 1. web-panels/ 初始化策略

**选择**：在仓库根目录新建 `web-panels/` 作为独立前端工程

**理由**：`10-web-panels开发约束.md` 规范明确定义了此目录结构。所有 Web Panel 共享同一个 `package.json` 和 Vite 配置，以 rollup multi-entry 支持多面板。fila-manager 是第一个面板，建立基础设施后续面板可直接复用。

**替代方案**：每个面板独立 `package.json` — 增加依赖管理复杂度，与规范不符。

### 2. bridge/client.ts 通信层设计

**选择**：实现共享的 `bridge/client.ts`，封装 JSON-RPC 2.0 请求/推送

```typescript
// request: JS → C++ (带 id，期望 response)
export function request<T>(method: string, params?: Record<string, unknown>): Promise<T>

// on: C++ → JS 推送监听 (无 id 的 notification)
export function on(method: string, handler: (params: Record<string, unknown>) => void): () => void
```

传输通道：
- JS → C++：`window.wx.postMessage(JSON.stringify(rpc))`（wxWebView 标准通道）
- C++ → JS：`WebView::RunScript("window.postMessage('" + json + "')")` — JS 端在 `window.addEventListener('message', ...)` 中路由

**理由**：规范要求所有通信走 bridge，禁止组件直接 postMessage。Promise-based API 比回调更易用。

### 3. Zustand Store 结构

**选择**：单一 `useStore` 管理全部状态

```typescript
interface FilaManagerState {
  // 数据
  spools: FilamentSpool[]
  presets: PresetOptions
  machines: MachineInfo[]
  amsData: AmsData | null
  initialized: boolean

  // UI 状态
  view: 'my' | 'archived' | 'stats'
  tab: 'all' | 'favorite' | 'ams'
  filters: FilterState
  sortKey: string
  sortAsc: boolean
  searchQuery: string
  selectedIds: Set<string>
  page: number
  pageSize: number
  grouped: boolean

  // 操作
  initFromDevice: (data: InitResponse) => void
  updateSpoolList: (spools: FilamentSpool[]) => void
  // ... CRUD 操作（乐观更新 + request）
}
```

**理由**：规范要求一个面板一个 store。耗材管理器状态虽复杂，但都围绕同一个数据集（spools），不需要拆分。

### 4. 组件拆分策略

**选择**：按功能区域拆分为 5 个核心组件

```
panels/fila-manager/
├── FilaManagerPanel.tsx      # 主面板：sidebar + content 两栏布局
├── components/
│   ├── Sidebar.tsx           # 左侧导航栏
│   ├── Toolbar.tsx           # 工具栏（标签页、筛选、搜索、操作按钮）
│   ├── SpoolTable.tsx        # 表格（表头排序、行渲染、批量选择、分组、分页）
│   ├── AddSpoolDialog.tsx    # 添加/编辑弹窗（手动+AMS 双模式、表单）
│   ├── ColorPalette.tsx      # 颜色预设面板
│   ├── FilterDropdown.tsx    # 筛选下拉菜单
│   ├── EmptyState.tsx        # 空状态占位
│   └── StatsView.tsx         # 统计视图（V1 占位框架）
```

**理由**：每个组件对应 Figma 中一个明确的视觉区域，职责清晰。SpoolTable 最复杂（~300 行 JS → ~200 行 TSX），其余组件较轻量。

### 5. C++ 端 JSON-RPC 2.0 适配

**选择**：改造 `OnWebMsg` 为 JSON-RPC dispatcher，保持 handler 注册模式

```cpp
void wgtFilaManagerPanel::OnWebMsg(wxWebViewEvent& evt) {
    auto j = json::parse(evt.GetString().ToStdString());
    if (j.value("jsonrpc", "") != "2.0") return;

    std::string method = j.value("method", "");
    auto params = j.value("params", json::object());

    if (j.contains("id")) {
        // Request → dispatch to handler → send_response or send_error
        int id = j["id"].get<int>();
        auto it = m_handlers.find(method);
        if (it != m_handlers.end()) it->second(id, params);
        else send_error(id, -32601, "Method not found: " + method);
    }
    // Notification (no id) — 目前 C++ 不需要处理 JS 发来的 notification
}

void wgtFilaManagerPanel::send_response(int id, const json& result) {
    SendMsg({{"jsonrpc","2.0"}, {"id",id}, {"result",result}});
}

void wgtFilaManagerPanel::send_error(int id, int code, const std::string& msg) {
    SendMsg({{"jsonrpc","2.0"}, {"id",id}, {"error",{{"code",code},{"message",msg}}}});
}

void wgtFilaManagerPanel::push_notification(const std::string& method, const json& params) {
    SendMsg({{"jsonrpc","2.0"}, {"method",method}, {"params",params}});
}
```

Method 映射表（旧 command → 新 method）：

| 旧 command | 新 method |
|-----------|-----------|
| `init` | `fila_manager.init` |
| `get_spool_list` | `fila_manager.get_spool_list` |
| `get_preset_options` | `fila_manager.get_preset_options` |
| `get_machine_list` | `fila_manager.get_machine_list` |
| `get_ams_data` | `fila_manager.get_ams_data` |
| `add_spool` | `fila_manager.add_spool` |
| `batch_add` | `fila_manager.batch_add` |
| `update_spool` | `fila_manager.update_spool` |
| `remove_spool` | `fila_manager.remove_spool` |
| `batch_remove` | `fila_manager.batch_remove` |
| `mark_empty` | `fila_manager.mark_empty` |
| `toggle_favorite` | `fila_manager.toggle_favorite` |
| `archive_spool` | `fila_manager.archive_spool` |

Push notification：

| 旧 push command | 新 notification method |
|----------------|----------------------|
| `spool_list` | `fila_manager.spool_list_updated` |
| `theme_changed` | `fila_manager.theme_changed` |

### 6. Bridge 注入方式

**选择**：C++ 在 `wxEVT_WEBVIEW_LOADED` 后注入 `window.postMessage` 路由

**理由**：规范定义 C++ → JS 通过 `RunScript("window.postMessage('...')")` 传递。前端在 `bridge/client.ts` 中 `window.addEventListener('message', ...)` 接收并路由。无需自定义 `__cppPush`，更标准化。

### 7. 构建产物管理

**选择**：构建产物提交到 repo（`resources/web/panels/fila-manager/`）

**理由**：与现有 `resources/web/` 下其他资源一致，C++ 编译不依赖 Node.js。开发者修改前端代码后手动 `npm run build` 并提交产物。

## Risks / Trade-offs

- **[风险] 首次建立 web-panels 基础设施** — 需要安装 Node.js 依赖、配置 Vite，有学习曲线。→ 缓解：本次建立模板后，后续面板只需复制结构。
- **[风险] React 与 wxWebView 兼容性** — 少数 API 在 WebKitGTK（Linux）上可能行为不同。→ 缓解：仅使用标准 DOM API；构建为 IIFE 避免 ES module 兼容问题。
- **[风险] 大量 UI 代码需一次性重写** — index.js ~1460 行 + index.css ~1200 行 + index.html ~420 行。→ 缓解：按组件逐个迁移，每个组件可独立验证。
- **[Trade-off] 构建产物提交到 repo** — 增加 repo 体积，diff 噪音。→ 收益：CI 和 C++ 编译零依赖 Node.js。后续可改为 CI 构建。
- **[Trade-off] 技术栈升级** — 团队需学习 React/Zustand/Tailwind。→ 收益：与其他面板统一，长期维护成本降低。
- **[风险] JSON-RPC 2.0 错误码不够细化** — 当前 `code: -1` 统一错误码。→ 缓解：V1 保持简单，后续按需引入标准 JSON-RPC 错误码。
