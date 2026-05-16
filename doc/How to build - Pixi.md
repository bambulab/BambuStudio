# Building Bambu Studio with Pixi

[Pixi](https://pixi.sh) provides all dependencies from conda-forge,
replacing the legacy `BuildLinux.sh` + `deps/` ExternalProject flow.

Supports `linux-64` (glibc ≥ 2.34), `win-64`, and `osx-arm64`
(Apple Silicon, macOS 11+). `osx-64` is not wired yet.

## Install pixi

- Linux / macOS: `curl -fsSL https://pixi.sh/install.sh | bash`
- Windows: `iwr -useb https://pixi.sh/install.ps1 | iex`

Windows additionally requires Visual Studio 2019 or 2022 with the C++ workload.
macOS additionally requires the Xcode Command Line Tools
(`xcode-select --install`) — the conda-forge `clang_osx-arm64` wrapper
uses the system SDK and `lipo`.

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

All three platforms have split Debug / Release trees that coexist
(`build/debug/`, `build/release/`).

- `pixi run build` — Debug build (default, fast iteration)
- `pixi run build-release` — Release build
- `pixi run bambu-studio-release` — launch the Release binary
- `pixi run dist` — distributable for the host OS:
  - Linux: AppImage at `build/release/`
  - macOS: DMG at `build/release/`
  - Windows: MSI at `build/release/dist/`

### macOS `.app` and DMG

`pixi run package` builds `build/release/BambuStudio.app` — a
self-contained bundle with all conda-forge dylibs in
`Contents/Frameworks/` and the resources tree at
`Contents/Resources/`. Ad-hoc codesigned by default; set
`BAMBU_CODESIGN_IDENTITY="Developer ID Application: …"` to sign with a
real cert.

`pixi run dist` wraps that `.app` into `build/release/BambuStudio-<version>-arm64.dmg`
via dmgbuild, with an `Applications` symlink for drag-installation.

First launch from a downloaded DMG triggers the Gatekeeper
"unidentified developer" dialog. Right-click the `.app` in
Applications and choose **Open** to whitelist it once.

### Windows MSI

`pixi run dist` runs `cmake --install` to stage the Release tree under
`build/release/dist/payload/BambuStudio/`, copies every DLL from the
pixi env's `Library/bin/` into the payload (covers transitive deps
that static analysis misses, e.g. OpenCV's runtime `LoadLibrary`),
generates `_payload.wxs`, and wraps everything with WiX 5 into
`build/release/dist/BambuStudio-<version>-x64.msi` (~400 MB).

WiX itself is a `dotnet tool`; `dist.ps1` idempotently installs
`wix.exe` into the pixi env on first use. The MSI is unsigned (Windows
SmartScreen will warn on first run) and assumes the Visual C++ 2022
Redistributable is already on the target system.

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
