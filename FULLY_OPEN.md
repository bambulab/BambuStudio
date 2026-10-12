# Bambu Studio — fully-open build

This fork makes Bambu Studio build and run **100% from open source**:

- the closed-source `bambu_networking` plugin (and its `BambuSource` camera
  library) that Studio otherwise downloads from Bambu Lab's OTA service at
  first start is replaced by the open-source
  [open-bamboo-networking](open-bamboo-networking/) implementation, vendored
  here as a git submodule pinned to the `v2.2.0` release;
- every code path that downloads, installs or updates closed-source binary
  packages (network plugin, `cameratools`) is disabled at compile time;
- the plugin and camera library are **bundled next to the application
  executable** and loaded from there; plugin copies previously downloaded
  into the user data directory are ignored;
- the publisher signature check is skipped (the open build is not
  Authenticode-signed with Bambu Lab's certificate), and the default
  configuration marks the network feature as installed so no install/update
  prompts appear.

Everything else is upstream Bambu Studio (AGPL-3.0), unmodified.

## How it works — the patch set

| Change | File | Purpose |
| --- | --- | --- |
| `FULLY_OPEN_BUILD` CMake option (default ON) | `CMakeLists.txt` | compile-time switch; reconfigure with `-DFULLY_OPEN_BUILD=OFF` to restore stock upstream behavior |
| `download_plugin()` early-out | `src/slic3r/GUI/GUI_App.cpp` | no closed-source binary package is ever fetched |
| `install_plugin()` early-out | `src/slic3r/GUI/GUI_App.cpp` | no downloaded package is ever installed |
| `updating_bambu_networking()` early-out | `src/slic3r/GUI/GUI_App.cpp` | the "Download Bambu Network Plug-in" dialog never runs |
| `begin_network_plugin_download` ignored | `src/slic3r/GUI/GUI_App.cpp` | WebView-triggered plug-in download disabled |
| `sync_plugins()` early-out | `src/slic3r/Utils/PresetUpdater.cpp` | background OTA plugin sync disabled |
| bundled plugin directory + no cert check | `src/slic3r/Utils/NetworkAgent.cpp` | loads `bambu_networking` / `BambuSource` from the application directory only |
| `installed_networking=1`, `update_network_plugin=false` defaults | `src/libslic3r/AppConfig.cpp` | no install/update prompts; OTA never engaged |

## Building

Initialize the submodule first in any case:

```sh
git submodule update --init --recursive
```

### Windows

Prerequisites: the upstream Windows build prerequisites for the slicer
(Visual Studio 2019 with toolset v142 is what Bambu Studio is built with), and
for the plugin: the same MSVC toolset plus
[vcpkg](https://github.com/microsoft/vcpkg) (`VCPKG_ROOT` exported). The plugin
ABI passes `std::string` / `std::map` / `std::function` across the DLL
boundary, so the plugin must be built with an MSVC toolset compatible with the
Studio build.

```powershell
# build Studio (upstream) and the open plugin, then stage the plugin next to
# the built executable:
.\build-fully-open.ps1 -BuildStudio

# or, if Studio is already built:
.\build-fully-open.ps1
```

### Linux / macOS

Build Studio with the upstream instructions (`BuildLinux.sh` / `BuildMac.sh`),
then build and stage the plugin:

```sh
./build-fully-open.sh
```

## Version handshake

Studio accepts the plugin when the first 8 characters of the plugin version
(`MAJOR.MINOR.PATCH`) equal Studio's own build id (`SLIC3R_VERSION` in
`version.inc`). Both build drivers derive the plugin's `-WithVersion` from
`version.inc`, so a tree always produces matching artifacts. When you update to
a Studio release with a new ABI series, update the `open-bamboo-networking`
submodule to a release that supports it (their releases ship one prebuilt
series per Studio ABI, and building from source auto-matches).

### Build provenance (what compiles from source)

The `deps/` superbuild compiles Bambu Studio's dependencies from upstream
open-source releases (wxWidgets, Boost, OpenCASCADE/OCCT, OpenCV, OpenEXR,
OpenVDB, TBB, GMP/MPFR, Assimp, and more). Exceptions, called out for honesty:

- **FFmpeg (Windows)** is consumed as a prebuilt binary package
  (`bambulab/ffmpeg_prebuilts`) — upstream's Windows deps path does not build
  FFmpeg from source. It is a build of open-source FFmpeg; the Linux/macOS
  deps paths build it from source in-tree.
- **WebView2** is a closed Microsoft platform component (like the OS itself)
  required for Studio's WebViews; it cannot be open-sourced, only redistributed
  under Microsoft's terms.
- MSVC runtime redistributables are likewise Microsoft platform components.

Everything else in the build output is produced from source by this tree or by
the `open-bamboo-networking` submodule.

## What remains non-open (feature scope, not code)

- The bundled open plugin covers the full LAN workflow (discovery, telemetry,
  printing, FTPS and port-6000 file transfer, camera) and cloud sign-in with
  presets. Commands to a **cloud-paired** printer that require Bambu Lab's
  per-installation signing keys stay disabled unless the user supplies their
  own credentials — these are secrets, not source code, and cannot be
  open-sourced (see `open-bamboo-networking/README.md`, "Option B").
- "Go Live" streaming and HMS photo snapshots are proprietary-service features
  and are not implemented.
- `cameratools` helper binaries are not downloaded; the camera uses the
  bundled open `BambuSource` library.

## Licensing

- Bambu Studio: AGPL-3.0 (upstream).
- open-bamboo-networking: GNU Affero General Public License (see
  `open-bamboo-networking/LICENSE`). Both components are copyleft-compatible;
  the plugin is loaded through Studio's documented plugin interface.
