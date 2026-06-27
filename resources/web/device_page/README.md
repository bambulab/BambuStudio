# device_page runtime bundle

BambuStudio loads the embedded Web UI from **`dist/`** under this directory
(`file://…/web/device_page/dist/index.html` or the Debug HTTP server on port 13628).

## Source vs output

| Path | Role |
|------|------|
| `src/slic3r/GUI/DeviceWeb/device_page/` | React/TypeScript source, `package.json`, Vite config |
| `resources/web/device_page/dist/` | **Runtime bundle** (generated, gitignored) |

Do not commit `index.html` / `assets/` next to this README. Those were legacy copies and are removed.

## When `dist/` is rebuilt

CMake target `device_page_build` runs automatically before **`libslic3r_gui`**, **`BambuStudio`**, or **`BambuStudio_app_gui`** when any tracked frontend input changes (sources, locales, `public/`, lockfile, Vite/TS configs).

Manual rebuild:

```bash
cmake --build <build_dir> --target device_page_build --config Release
```

Or from the source tree:

```bash
cd src/slic3r/GUI/DeviceWeb/device_page
pnpm install
pnpm build
# then run device_page_build to copy into resources/web/device_page/dist
```

See also `src/slic3r/GUI/DeviceWeb/device_page/README.md`.
