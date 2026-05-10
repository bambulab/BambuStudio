# Building Bambu Studio with Pixi

[Pixi](https://pixi.sh) provides all dependencies from conda-forge,
replacing the legacy `BuildLinux.sh` + `deps/` ExternalProject flow.

Supports `linux-64` (glibc ≥ 2.34) and `win-64` (Visual Studio 2019 or
2022 required). `osx-*` is planned.

## Install pixi

- Linux / macOS: `curl -fsSL https://pixi.sh/install.sh | bash`
- Windows: `iwr -useb https://pixi.sh/install.ps1 | iex`

Windows additionally requires Visual Studio 2019 or 2022 with the C++ workload.

## Build

```
pixi install                  # conda-forge deps
pixi run bootstrap            # libnoise + wxWidgets (once)
pixi run build                # bambu-studio
pixi run bambu-studio         # launch (args pass through)
```

Override parallelism: `CMAKE_BUILD_PARALLEL_LEVEL=4 pixi run build`.
Default is `floor(free_mem_GB / 2.5)` to fit Boost.Spirit / CGAL TUs.

### Build variants

Linux has split Debug / Release trees that coexist (`build/debug/`,
`build/release/`); `pixi run build` defaults to Debug for fast iteration.
Windows is single-configuration Release for now.

| | Linux | Windows |
|---|---|---|
| Debug | `pixi run build` (default) | not yet |
| Release | `pixi run build-release` | `pixi run build` |
| AppImage | `pixi run appimage` | -- |
| Launch Release binary | `pixi run bambu-studio-release` | `pixi run bambu-studio` |

## Network plugin (cloud / printer features)

Sign in via the GUI to auto-download. Manual install:

```bash
mkdir -p ~/.config/BambuStudio/plugins
url=$(curl -fsS -H "X-BBL-OS-Type: linux" \
    "https://api.bambulab.com/v1/iot-service/api/slicer/resource?slicer/plugins/cloud=$(awk -F\" '/SLIC3R_VERSION/{print $2}' version.inc | head -c 9).00" \
    | grep -oE 'https://[^"]+linux[^"]+\.zip')
curl -L "$url" | bsdtar -xf - -C ~/.config/BambuStudio/plugins/
```

Slicing works without it; only cloud / device / store need it.

## Clean

```bash
pixi clean      # remove .pixi/envs (rebuild via install + bootstrap)
rm -rf build    # remove CMake tree (rebuild via pixi run build)
```
