# 设备页 - Web UI 开发指南

本项目是嵌入在桌面应用 wxWebView 中的 React 应用，提供设备管理、校准、耗材管理等设备相关功能页面。

> **注意：** WebView 加载的是**编译后的产物**（`dist/index.html`）— 由 Vite 打包生成的静态 HTML 文件，JS/CSS 已内联。`src/` 下的 React/TypeScript 源码**不会**被直接执行，必须先通过 `npx vite build` 构建。开发阶段可以使用 Vite 开发服务器实现热更新（参见[开发工作流](#开发工作流热更新)）。

## 技术栈

- **React 19** + **TypeScript**
- **TanStack Router**（基于文件的路由，hash history）
- **Zustand**（状态管理，slice 模式）
- **Tailwind CSS v4**
- **Radix UI**（dialog、popover、toggle）
- **i18next** + **react-i18next**（国际化）
- **Vite**（构建工具）
- **pnpm**（包管理器）

## 目录结构

```
device_page/
├── locales/                 # 翻译文件（17 种语言）
├── public/img/              # 静态资源（SVG 图标）
├── dist/                    # 构建产物（C++ webview 加载此目录）
├── src/
│   ├── main.tsx             # 应用入口（路由 + StrictMode）
│   ├── i18n.tsx             # i18next 国际化配置
│   ├── routes/              # 基于文件的路由（TanStack Router）
│   │   ├── __root.tsx       # 根布局（导航栏，按路由条件显示/隐藏）
│   │   ├── index.tsx        # / （首页）
│   │   ├── calibration.tsx  # /calibration
│   │   ├── filament.tsx     # /filament
│   │   ├── webcalib.tsx     # /webcalib
│   │   └── ...
│   ├── features/            # 功能模块
│   │   ├── calibration/     # PA 校准管理
│   │   ├── filament/        # 耗材管理
│   │   ├── webcalib/        # Web 校准
│   │   ├── ams/             # AMS 控制
│   │   ├── control/         # 机器控制（喷嘴、热床等）
│   │   └── ...
│   ├── hooks/
│   │   └── Bridge.tsx       # JS <-> C++ 通信桥接
│   ├── store/               # Zustand 状态切片
│   │   ├── AppStore.tsx     # 根 store
│   │   ├── CalibrationSlice.tsx
│   │   └── ...
│   └── components/          # 共享 UI 组件
├── vite.config.ts           # Vite 配置（file:// 兼容插件、@locales 别名）
├── i18next-parser.config.js # 翻译 key 提取配置
├── tsconfig.app.json        # TypeScript 配置
└── package.json
```

## 开发环境配置

### 基础环境

| 工具 | 版本要求 | 说明 |
|------|----------|------|
| **Node.js** | >= 18 | Vite 6 要求。推荐使用 LTS 版本（20.x 或 22.x） |
| **pnpm** | 10.12.1 | 通过 package.json 的 `packageManager` 字段锁定。安装：`corepack enable && corepack prepare` |

### 安装依赖

```bash
cd resources/web/device_page
pnpm install
```

## 快速开始

```bash
# 启动开发服务器（热更新，访问 http://localhost:5173）
pnpm dev

# 构建（输出到 dist/）
pnpm build         # tsc + vite build（完整构建）
npx vite build     # 跳过 tsc，仅 vite 构建（更快，开发阶段推荐）
```

## 加载模式

C++ 侧（`WebDevicePage.cpp`）支持两种加载方式：

| 模式 | URL | 场景 |
|------|-----|------|
| **file://**（默认） | `file://...resources/web/device_page/dist/index.html` | 正式使用 |
| **HTTP 服务器** | `http://localhost:13628/index.html` | 开发调试（支持热更新） |

**Debug 构建**中，HTTP 服务器模式会自动启用：

```cpp
#if !BBL_RELEASE_TO_PUBLIC
#define DEVICE_USE_HTTP_SERVER
#endif
```

无需手动修改代码，使用 Debug 配置编译 C++ 即可。

### 开发工作流（热更新）

推荐同时使用 C++ HTTP 服务器 + Vite 开发服务器：

1. **以 Debug 配置编译并启动桌面应用** — 内置 HTTP 服务器自动在 `localhost:13628` 启动，提供 `dist/` 目录的文件服务
2. **在终端启动 Vite 开发服务器**：
   ```bash
   cd resources/web/device_page
   pnpm dev
   ```
   Vite 运行在 `http://localhost:5173`，支持 HMR（热模块替换）。
3. **在桌面应用中**，点击调试工具栏的 "Load App" 按钮 — WebView 将导航到 `http://localhost:13628/index.html`
4. 如需**实时热更新**，在调试工具栏的 URL 输入框中输入 `http://localhost:5173` 并点击 "Go!" — 修改 `src/` 下的代码后页面会即时刷新

调试工具栏（仅在 Debug 构建中可见）功能说明：

| 按钮 | 功能 |
|------|------|
| **Load App** | 导航到 `http://localhost:13628/index.html`（C++ HTTP 服务器） |
| **Reload** | 强制重新加载 WebView 当前页面 |
| **URL 输入框 + Go!** | 导航到任意 URL（如 Vite 开发服务器 `http://localhost:5173`） |

### 生产构建验证

测试 `file://` 协议加载（与正式环境一致）：

```bash
npx vite build                    # 构建到 dist/
# 然后以 Release 模式启动桌面应用 — 通过 file:// 协议加载 dist/index.html
```

## 多 Tab 共享架构

一个 WebView 实例服务多个主程序 Tab 页。Tab 切换时通过 hash 路由导航到对应页面：

```
主程序 Tab 栏
  ├── Web Device       → WebDevicePage → #/calibration
  ├── Filament Manager → WebDevicePage → #/filament
  └── Calibration      → WebDevicePage → #/webcalib
```

所有 Tab 共享同一个 `WebDevicePage`（wxPanel）。C++ 侧在 Tab 切换时调用 `NavigateTo("/route")`，实际执行 `window.location.hash = '#/route'`。切换是瞬时的，不会重新加载页面。

## 新增页面

### 1. 创建路由文件

```
src/routes/myfeature.tsx
```

```tsx
import { createFileRoute } from '@tanstack/react-router';
import { MyFeaturePage } from '../features/myfeature/MyFeaturePage';

export const Route = createFileRoute('/myfeature')({
  component: MyFeaturePage,
});
```

### 2. 创建功能模块

```
src/features/myfeature/MyFeaturePage.tsx
```

```tsx
import { useTranslation } from 'react-i18next';

export function MyFeaturePage() {
  const { t } = useTranslation();
  return <h1>{t("My Feature")}</h1>;
}
```

### 3. 注册 C++ Tab（如果需要作为顶层 Tab）

在 `MainFrame.hpp` 的 `TabPosition` 枚举中新增值，然后在 `MainFrame.cpp` 的 `init_tabpanel` 中注册：

```cpp
m_tabpanel->AddPage(m_web_device, _L("My Feature"), ...);
```

在 Tab 切换事件处理中添加路由：

```cpp
else if (sel == tpMyFeature)
    m_web_device->NavigateTo("/myfeature");
```

### 4. 隐藏导航栏（如果是全屏独立页面）

在 `src/routes/__root.tsx` 中将路由加入 `hideNav` 条件：

```tsx
const hideNav = pathname === '/filament' || pathname === '/webcalib' || pathname === '/myfeature';
```

### 5. 构建验证

```bash
npx vite build
```

## 功能模块内部结构

每个页面对应 `src/features/` 下的一个**功能模块**。以 `calibration` 为参考：

```
src/features/calibration/
├── CalibrationPage.tsx        # 页面组件（入口，组合子组件）
├── useCalibrationBridge.ts    # Bridge hook（该功能所有 C++ 通信）
├── types.ts                   # TypeScript 类型（请求/响应载荷、数据模型）
├── PAHistoryTable.tsx          # 子组件：数据表格
├── PAEditModal.tsx             # 子组件：新建/编辑弹窗
└── PADeleteConfirm.tsx         # 子组件：删除确认
```

### 创建子功能模块的完整步骤

以"耗材管理"为例：

#### 1. 定义类型（`types.ts`）

```ts
// src/features/filament/types.ts
export interface FilamentItem {
  id: string;
  name: string;
  material: string;
  color: string;
}

export interface BridgeResponseBody {
  module: string;
  resource: string;
  action: string;
  code: number;
  message: string;
  payload: Record<string, unknown>;
}
```

#### 2. 创建 Store Slice（`store/FilamentSlice.tsx`）

```tsx
import type { StateCreator } from 'zustand';
import type { RootState } from './AppStore';
import type { FilamentItem } from '../features/filament/types';

export interface FilamentSlice {
  filament: {
    items: FilamentItem[];
    isLoading: boolean;
    error: string | null;
    setItems: (items: FilamentItem[]) => void;
    setLoading: (v: boolean) => void;
    setError: (e: string | null) => void;
  };
}

export const createFilamentSlice: StateCreator<
  RootState, [['zustand/immer', never]], [], FilamentSlice
> = (set) => ({
  filament: {
    items: [],
    isLoading: false,
    error: null,
    setItems: (items) => set((s) => { s.filament.items = items; }),
    setLoading: (v) => set((s) => { s.filament.isLoading = v; }),
    setError: (e) => set((s) => { s.filament.error = e; }),
  },
});
```

然后在 `AppStore.tsx` 中注册：

```tsx
import { createFilamentSlice } from './FilamentSlice';
import type { FilamentSlice } from './FilamentSlice';

export type RootState = ... & FilamentSlice;

// 在 create() 内部：
...createFilamentSlice(set, get, api),
```

#### 3. 创建 Bridge Hook（`useFilamentBridge.ts`）

```ts
import { useCallback } from 'react';
import { useDeviceBridge } from '../../hooks/Bridge';
import useStore from '../../store/AppStore';
import type { BridgeResponseBody, FilamentItem } from './types';

function makeBody(resource: string, action: string, payload?: Record<string, unknown>) {
  return { module: 'filament', resource, action, payload: payload ?? {} };
}

export function useFilamentBridge() {
  const request = useDeviceBridge();
  const setItems = useStore((s) => s.filament.setItems);
  const setLoading = useStore((s) => s.filament.setLoading);
  const setError = useStore((s) => s.filament.setError);

  const fetchList = useCallback(async () => {
    setLoading(true);
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('filament_list', 'list')
    );
    setLoading(false);
    if (res.ok && res.value.code === 0) {
      setItems((res.value.payload?.items as FilamentItem[]) ?? []);
    } else {
      setError(res.ok ? res.value.message : res.error);
    }
  }, [request, setItems, setLoading, setError]);

  return { fetchList };
}
```

#### 4. 编写页面组件（`FilamentPage.tsx`）

```tsx
import { useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { useFilamentBridge } from './useFilamentBridge';
import useStore from '../../store/AppStore';

export function FilamentPage() {
  const { t } = useTranslation();
  const { fetchList } = useFilamentBridge();
  const items = useStore((s) => s.filament.items);
  const isLoading = useStore((s) => s.filament.isLoading);

  useEffect(() => { fetchList(); }, [fetchList]);

  return (
    <div className="p-4">
      <h1>{t("Filament Manager")}</h1>
      {isLoading ? <p>{t("Loading...")}</p> : (
        <ul>
          {items.map((f) => <li key={f.id}>{f.name}</li>)}
        </ul>
      )}
    </div>
  );
}
```

### 文件约定总结

| 文件 | 职责 |
|------|------|
| `types.ts` | 数据模型、请求/响应载荷类型定义 |
| `use[Feature]Bridge.ts` | 所有 C++ 通信（请求 + Report 事件订阅） |
| `store/[Feature]Slice.tsx` | 该功能的 Zustand 状态与 actions |
| `[Feature]Page.tsx` | 页面入口组件，组合子组件 |
| `[SubComponent].tsx` | UI 子组件（表格、弹窗、表单等） |

## JS <-> C++ 通信

通信使用 `useDeviceBridge()` hook（`src/hooks/Bridge.tsx`）。

### 前端 -> C++（请求）

```tsx
const request = useDeviceBridge();

const result = await request({
  module: "calibration",
  resource: "pa_history",
  action: "list",
  payload: { nozzle_diameter: 0.4 }
});

if (result.ok) {
  console.log(result.value);
}
```

### C++ -> 前端（推送）

C++ 通过 `ReportMsg` 发送状态更新，前端以 `CustomEvent('cpp:device')` 形式分发到 `document`。各功能模块的 hook 通过 `addEventListener` 订阅处理。

### 新增 ViewModel（C++ 侧）

1. 创建 `src/slic3r/GUI/DeviceWeb/MyFeatureViewModel.hpp/cpp`
2. 实现 `IViewModel` 接口（`GetModule()`、`OnCommand()`、`ReportState()`）
3. 在 `WebDevicePage.cpp` 中注册：
   ```cpp
   m_device_web_mgr->Register(std::make_unique<MyFeatureViewModel>());
   ```
4. 在 `CMakeLists.txt` 中添加新文件

## 国际化（i18n）

### 组件中使用

```tsx
import { useTranslation } from 'react-i18next';

function MyComponent() {
  const { t } = useTranslation();
  return (
    <>
      {/* 基本用法：key 就是英文原文 */}
      <span>{t("Confirm")}</span>

      {/* 带变量插值 */}
      <span>{t("Loaded {{count}} items", { count: 5 })}</span>

      {/* 带上下文（同一单词在不同场景翻译不同） */}
      <span>{t("Save", { context: "file" })}</span>
    </>
  );
}
```

- **Key = 英文原文**，代码可读性好
- **插值**：key 和翻译中用 `{{变量名}}` 占位
- **上下文**：`t("key", { context: "ctx" })` 查找翻译文件中的 `key_ctx`

### 翻译文件

位于 `locales/` 目录，每种语言一个 JSON 文件。支持 17 种语言（与桌面应用一致）：

```
en, zh_CN, ja_JP, it_IT, fr_FR, de_DE, hu_HU, es_ES,
sv_SE, cs_CZ, nl_NL, uk_UA, ru_RU, tr_TR, pt_BR, ko_KR, pl_PL
```

格式示例：

```json
{
  "Confirm": "确认",
  "Save_file": "保存文件",
  "Loaded {{count}} items": "已加载 {{count}} 个项目"
}
```

`en.json` 也保留完整的 key-value 映射，以应对 key 文案与实际英文显示不同的情况。

### 从代码中提取翻译 key

```bash
pnpm run i18n
```

扫描 `src/**/*.{ts,tsx}` 中所有 `t()` 调用，将新 key 自动添加到 17 个语言文件。已有的翻译不会被覆盖。

### 语言检测

优先级：URL 参数 `?lang=zh_CN` > `localStorage["BambuWebLang"]` > 回退 `en`

C++ 侧加载页面时自动附加 `?lang=` 参数，与项目中其他 webview 页面保持一致。

### 路径别名配置

翻译文件使用 `@locales` 路径别名。如需移动 `locales/` 目录，只需修改以下 3 处：

| 文件 | 配置项 |
|------|--------|
| `vite.config.ts` | `resolve.alias['@locales']` |
| `tsconfig.app.json` | `compilerOptions.paths['@locales/*']` |
| `i18next-parser.config.js` | `LOCALES_DIR` 常量 |

## Vite 构建说明

`vite.config.ts` 中的 `fileProtocolCompat()` 插件确保构建产物在 `file://` 协议下正常工作：

1. 移除 `crossorigin` 属性（file:// 下浏览器会阻止带此属性的资源加载）
2. 将 `<script>` 从 `<head>` 移到 `<body>` 末尾（确保 DOM 就绪后再执行 React）
3. 移除 `type="module"`（IIFE 格式的 bundle 不需要）
