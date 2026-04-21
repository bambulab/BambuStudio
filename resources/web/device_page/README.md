# Device Page - Web UI for Device Management

Embedded in the desktop application via wxWebView, this React app provides device management, calibration, filament management, and other device-related features.

> **Note:** The WebView loads the **compiled output** (`dist/index.html`) — a static HTML file bundled by Vite with all JS/CSS inlined. The React/TypeScript source code in `src/` is **not** executed directly; it must be built first via `npx vite build`. During development, the Vite dev server can serve the source directly for hot-reload (see [Development Workflow](#development-workflow-hot-reload)).

## Tech Stack

- **React 19** + **TypeScript**
- **TanStack Router** (file-based routing, hash history)
- **Zustand** (state management, slice pattern)
- **Tailwind CSS v4**
- **Radix UI** (dialog, popover, toggle)
- **i18next** + **react-i18next** (internationalization)
- **Vite** (build tool)
- **pnpm** (package manager)

## Directory Structure

```
device_page/
├── locales/                 # Translation JSON files (17 languages)
├── public/img/              # Static assets (SVG icons)
├── dist/                    # Build output (loaded by C++ webview)
├── src/
│   ├── main.tsx             # App entry (router + StrictMode)
│   ├── i18n.tsx             # i18next configuration
│   ├── routes/              # File-based routes (TanStack Router)
│   │   ├── __root.tsx       # Root layout (nav bar, conditional by route)
│   │   ├── index.tsx        # / (home)
│   │   ├── calibration.tsx  # /calibration
│   │   ├── filament.tsx     # /filament
│   │   ├── webcalib.tsx     # /webcalib
│   │   └── ...
│   ├── features/            # Feature modules
│   │   ├── calibration/     # PA calibration management
│   │   ├── filament/        # Filament management
│   │   ├── webcalib/        # Web calibration
│   │   ├── ams/             # AMS control
│   │   ├── control/         # Machine control (nozzle, heatbed, etc.)
│   │   └── ...
│   ├── hooks/
│   │   └── Bridge.tsx       # JS <-> C++ communication bridge
│   ├── store/               # Zustand store slices
│   │   ├── AppStore.tsx     # Root store
│   │   ├── CalibrationSlice.tsx
│   │   └── ...
│   └── components/          # Shared UI components
├── vite.config.ts           # Vite config (file:// compat plugin, @locales alias)
├── i18next-parser.config.js # Translation key extraction config
├── tsconfig.app.json        # TypeScript config
└── package.json
```

## Development Environment Setup

### Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| **Node.js** | >= 18 | Required by Vite 6. Recommend LTS (20.x or 22.x) |
| **pnpm** | 10.12.1 | Pinned via `packageManager` in package.json. Install: `corepack enable && corepack prepare` |

### Install Dependencies

```bash
cd resources/web/device_page
pnpm install
```

## Quick Start

```bash
# Dev server (hot reload, accessible at http://localhost:5173)
pnpm dev

# Build for production (output to dist/)
pnpm build         # tsc + vite build
npx vite build     # skip tsc, vite only (faster, recommended during development)
```

## Loading Modes

The C++ side (`WebDevicePage.cpp`) loads the web app in two modes:

| Mode | URL | Use Case |
|------|-----|----------|
| **file://** (default) | `file://...resources/web/device_page/dist/index.html` | Production / normal use |
| **HTTP server** | `http://localhost:13628/index.html` | Development with hot-reload |

In **debug builds** (non-release), HTTP server mode is auto-enabled via:

```cpp
#if !BBL_RELEASE_TO_PUBLIC
#define DEVICE_USE_HTTP_SERVER
#endif
```

No manual changes needed — just build the C++ side in Debug configuration.

### Development Workflow (Hot Reload)

For the best development experience, use the C++ HTTP server + Vite dev server together:

1. **Build & launch the desktop app** (Debug configuration) — the embedded HTTP server starts automatically on `localhost:13628`, serving files from `dist/`
2. **Run the Vite dev server** in a terminal:
   ```bash
   cd resources/web/device_page
   pnpm dev
   ```
   Vite runs on `http://localhost:5173` with HMR (Hot Module Replacement).
3. **In the desktop app**, click "Load App" in the debug toolbar — this navigates the WebView to `http://localhost:13628/index.html`
4. For **real-time hot reload**, type `http://localhost:5173` in the debug toolbar URL box and click "Go!" — changes you make in `src/` are reflected instantly

The debug toolbar (visible only in debug builds) provides:

| Button | Action |
|--------|--------|
| **Load App** | Navigate to `http://localhost:13628/index.html` (C++ HTTP server) |
| **Reload** | Hard-reload the current page in WebView |
| **URL box + Go!** | Navigate to any URL (e.g., Vite dev server `http://localhost:5173`) |

### Production Build Workflow

For testing with `file://` protocol (matches production behavior):

```bash
npx vite build                    # Build to dist/
# Then launch the desktop app in Release mode — it loads dist/index.html via file://
```

## Multi-Tab Architecture

One WebView instance serves multiple main app tabs. Tab switching triggers hash route navigation:

```
MainFrame Tab Bar
  ├── Web Device     → WebDevicePage → #/calibration
  ├── Filament Manager → WebDevicePage → #/filament
  └── Calibration    → WebDevicePage → #/webcalib
```

All tabs share the same `WebDevicePage` (wxPanel). The C++ side calls `NavigateTo("/route")` on tab switch, which executes `window.location.hash = '#/route'` in the WebView.

## Adding a New Page

### 1. Create the route file

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

### 2. Create the feature module

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

### 3. Register C++ tab (if needed as a top-level tab)

In `MainFrame.cpp` (`init_tabpanel`):

```cpp
m_tabpanel->AddPage(m_web_device, _L("My Feature"), ...);
```

In the tab change handler, add routing:

```cpp
else if (sel == tpMyFeature)
    m_web_device->NavigateTo("/myfeature");
```

### 4. Hide navigation bar (if full-screen page)

In `src/routes/__root.tsx`, add the route to `hideNav`:

```tsx
const hideNav = pathname === '/filament' || pathname === '/webcalib' || pathname === '/myfeature';
```

### 5. Build and verify

```bash
npx vite build
```

## Feature Module Structure

Each page is backed by a **feature module** under `src/features/`. Taking `calibration` as a reference:

```
src/features/calibration/
├── CalibrationPage.tsx        # Page component (entry point, composes sub-components)
├── useCalibrationBridge.ts    # Bridge hook (all C++ communication for this feature)
├── types.ts                   # TypeScript types (request/response payloads, data models)
├── PAHistoryTable.tsx          # Sub-component: data table
├── PAEditModal.tsx             # Sub-component: create/edit dialog
└── PADeleteConfirm.tsx         # Sub-component: delete confirmation
```

### Step-by-step: Adding a sub-feature module

Using "Filament Manager" as an example:

#### 1. Define types (`types.ts`)

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

#### 2. Create store slice (`store/FilamentSlice.tsx`)

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

Then register in `AppStore.tsx`:

```tsx
import { createFilamentSlice } from './FilamentSlice';
import type { FilamentSlice } from './FilamentSlice';

export type RootState = ... & FilamentSlice;

// Inside create():
...createFilamentSlice(set, get, api),
```

#### 3. Create bridge hook (`useFilamentBridge.ts`)

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

#### 4. Build the page component (`FilamentPage.tsx`)

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

### Convention summary

| File | Role |
|------|------|
| `types.ts` | Data models, request/response payload types |
| `use[Feature]Bridge.ts` | All C++ communication (request + report event subscription) |
| `store/[Feature]Slice.tsx` | Zustand state & actions for this feature |
| `[Feature]Page.tsx` | Page entry component, composes sub-components |
| `[SubComponent].tsx` | UI sub-components (tables, modals, forms) |

## JS <-> C++ Communication

Communication uses `useDeviceBridge()` hook (`src/hooks/Bridge.tsx`).

### Frontend -> C++ (Request)

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

### C++ -> Frontend (Report/Push)

C++ sends state updates via `ReportMsg`, dispatched as `CustomEvent('cpp:device')` on `document`. Feature hooks subscribe to these events.

### Adding a new ViewModel (C++ side)

1. Create `src/slic3r/GUI/DeviceWeb/MyFeatureViewModel.hpp/cpp`
2. Implement `IViewModel` interface (`GetModule()`, `OnCommand()`, `ReportState()`)
3. Register in `WebDevicePage.cpp`:
   ```cpp
   m_device_web_mgr->Register(std::make_unique<MyFeatureViewModel>());
   ```
4. Add files to `CMakeLists.txt`

## Internationalization (i18n)

### Usage in components

```tsx
import { useTranslation } from 'react-i18next';

function MyComponent() {
  const { t } = useTranslation();
  return (
    <>
      <span>{t("Confirm")}</span>
      <span>{t("Loaded {{count}} items", { count: 5 })}</span>
      <span>{t("Save", { context: "file" })}</span>
    </>
  );
}
```

- **Key = English original text** (readable in code)
- **Interpolation**: `{{variable}}` in key and translations
- **Context**: `t("key", { context: "ctx" })` looks up `key_ctx` in translation files

### Translation files

Located in `locales/`, one JSON per language. 17 languages matching the desktop app:

```
en, zh_CN, ja_JP, it_IT, fr_FR, de_DE, hu_HU, es_ES,
sv_SE, cs_CZ, nl_NL, uk_UA, ru_RU, tr_TR, pt_BR, ko_KR, pl_PL
```

Format:

```json
{
  "Confirm": "确认",
  "Save_file": "保存文件",
  "Loaded {{count}} items": "已加载 {{count}} 个项目"
}
```

English file (`en.json`) also maintains full key-value mapping for cases where key text differs from display text.

### Extract new keys from code

```bash
pnpm run i18n
```

Scans all `src/**/*.{ts,tsx}` for `t()` calls, adds new keys to all 17 language files. Existing translations are preserved.

### Language detection

Priority: URL `?lang=zh_CN` > `localStorage["BambuWebLang"]` > fallback `en`

The C++ side appends `?lang=` to the URL when loading the page, consistent with other webview pages in the project.

### Alias configuration

Translation files use the `@locales` path alias. If the `locales/` directory moves, update these 3 files:

| File | Setting |
|------|---------|
| `vite.config.ts` | `resolve.alias['@locales']` |
| `tsconfig.app.json` | `compilerOptions.paths['@locales/*']` |
| `i18next-parser.config.js` | `LOCALES_DIR` constant |

## Vite Build Notes

The `fileProtocolCompat()` plugin in `vite.config.ts` ensures the build output works under `file://` protocol:

1. Strips `crossorigin` attributes (blocked by browser under file://)
2. Moves `<script>` from `<head>` to end of `<body>` (DOM must be ready for React)
3. Strips `type="module"` from scripts (IIFE bundle)
