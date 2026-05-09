# Building Bambu Studio on Linux with Pixi

This is the Linux build path that uses [Pixi](https://pixi.sh) (a
conda-forge-backed package manager) to provide all dependencies. It
avoids `apt`/`dnf`-style system package juggling and the heavy
`deps/` ExternalProject path, and works the same on any modern x86_64
Linux distro.

The end result is a `build/src/bambu-studio` binary launched via
`pixi run bambu-studio`. Verified on Ubuntu 26.04 host; should work on
any distro with glibc ≥ 2.34 (Ubuntu 22.04+, Fedora 38+, Debian 12+).

## TL;DR

```bash
curl -fsSL https://pixi.sh/install.sh | bash    # pixi (one-time, per machine)

pixi install                                    # ~30 s — conda-forge deps
pixi run bootstrap                              # ~30–40 min — libnoise + wxWidgets
pixi run build                                  # ~10–15 min — BambuStudio
pixi run bambu-studio                           # launch
```

## Prerequisites

* **Pixi** ≥ 0.63: `curl -fsSL https://pixi.sh/install.sh | bash`
* **Disk**: ≥ 15 GB free (pixi env + build artefacts).
* **RAM**: ≥ 10 GB recommended (CGAL / Boost.Spirit TUs are template-heavy).
  Build parallelism is auto-tuned to `floor(free_mem_GB / 2.5)` matching
  `BuildLinux.sh`. Override with `CMAKE_BUILD_PARALLEL_LEVEL=N`.
* **bubblewrap** (`bwrap`) on the host for the WebKit2GTK sandbox; usually
  preinstalled on modern desktop distros.

## Step by step

### 1. Provision the conda-forge environment

```bash
pixi install
```

Pulls Boost, TBB, CGAL, OCCT, OpenCV, FFmpeg, GTK 3, WebKit2GTK 4.1,
and ~30 other libraries from conda-forge into `.pixi/envs/default/`.
Reproducibility is anchored by `pixi.lock`.

### 2. Build the bambulab forks

```bash
pixi run bootstrap
```

Two libraries aren't on conda-forge and have to be compiled from
source via `deps/`:

| Library | Why | Approx. build time |
|---|---|---|
| **`libnoise`** (bambulab fork) | Custom version not upstream | < 1 min |
| **`wxWidgets`** (bambulab fork) | Bambu-specific patches, GTK3, no DLL | 25–40 min |

Both end up installed into the active pixi env. Re-running the task
short-circuits if they're already present.

### 3. Compile Bambu Studio

```bash
pixi run build
```

Internally:

```
cmake --preset linux-pixi
cmake --build --preset linux-pixi --parallel <auto>
```

Configure flags are baked into `CMakePresets.json` (`linux-pixi` preset):
`SLIC3R_GUI=ON`, `SLIC3R_GTK=3`, `BBL_RELEASE_TO_PUBLIC=1`,
`CMAKE_PREFIX_PATH=$CONDA_PREFIX`, plus EGL hints.

### 4. Run

```bash
pixi run bambu-studio
pixi run bambu-studio path/to/file.3mf      # args pass through
```

The `bambu-studio` task wraps the binary with the env workarounds the
pixi build needs:

| Var | Why |
|---|---|
| `LIBGL_ALWAYS_SOFTWARE=1` | conda libGLX dispatching to a host nvidia driver crashes `MakeCurrent`. Software GL sidesteps it; remove if your driver+conda combo is known good. |
| `XDG_DATA_DIRS` / `GTK_THEME=Adwaita` | The conda libgtk-3 only ships Default/Emacs themes; point at host `/usr/share/themes/` so `gtk_settings_get_default()` returns non-NULL. |
| `SSL_CERT_FILE` / `CURL_CA_BUNDLE` | conda's libcurl bakes its `OPENSSLDIR` at build time; outside `pixi run` it can't find any CA bundle, breaking auth handshakes. |

Each is set with `${VAR:-default}` so user-exported overrides win.

### 5. (First run) Network plugin

The cloud / printer / MakerWorld features depend on a closed-source
plugin (`libbambu_networking.so` + friends) that Bambu Lab distributes
out-of-band. Bambu Studio normally fetches it after sign-in; if that
fails you can install it manually:

```bash
mkdir -p ~/.config/BambuStudio/plugins

# URL is returned by the API only when the X-BBL-OS-Type: linux header
# is sent — the public website resource list shows Windows zips only.
# Replace the version segment if your build differs.
url=$(curl -fsS \
    -H "X-BBL-OS-Type: linux" \
    "https://api.bambulab.com/v1/iot-service/api/slicer/resource?slicer/plugins/cloud=$(awk -F\" '/SLIC3R_VERSION/{print $2}' version.inc | head -c 9).00" \
    | grep -oE 'https://[^"]+linux[^"]+\.zip')

curl -L -o /tmp/bambu-plugin.zip "$url"
unzip -o /tmp/bambu-plugin.zip -d ~/.config/BambuStudio/plugins/
```

Slicing works without the plugin; only cloud / device / store features need it.

## Cleaning up

```bash
pixi clean         # remove .pixi/envs (rebuild via `pixi install` + bootstrap)
rm -rf build       # remove the CMake build tree (rebuild via `pixi run build`)
```

## Layout

```
pixi.toml                      # conda-forge dependency manifest + tasks
pixi.lock                      # exact versions for reproducibility
CMakePresets.json              # linux-pixi configure preset
scripts/pixi/
├── _jobs.sh                   # shared helper: floor(free_mem_GB / 2.5)
├── build.sh                   # pixi run build → cmake configure + build
├── setup-libnoise.sh          # pixi run setup-libnoise
├── setup-wxwidgets.sh         # pixi run setup-wxwidgets
└── run.sh                     # pixi run bambu-studio (env workarounds + launch)
deps/                          # ExternalProject defs (most disabled with pixi;
                               # only libnoise + wxWidgets actually run)
build/                         # CMake build dir (generated)
.pixi/envs/default/            # conda-forge env (generated)
```

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Bootstrap aborts with `Failed to clone src/zlib` | Transient git submodule clone issue under load | `rm -rf build/deps-build-wx && pixi run setup-wxwidgets` |
| Build OOM-killed (`cc1plus` killed) on -j20+ | CGAL / Boost.Spirit TUs spike to ~2.5 GB each | Lower parallelism: `CMAKE_BUILD_PARALLEL_LEVEL=4 pixi run build` |
| Crash in `_gtk_settings_get_screen` at startup | Stale build before `Label::initSysFont` was moved to `OnInit` | Pull latest, rebuild |
| MakerWorld page shows "Sorry, you have been blocked" | App routed to `bambulab.net` dev/qa/pre host; needs `BBL_RELEASE_TO_PUBLIC=1` | Already set in `CMakePresets.json`; rebuild if you customised |
| First launch shows "Failed to Get Network Plugin" | Plugin auto-download not yet completed (or blocked) | Sign in via the GUI, or install manually (see step 5) |
| Login UI accepts password but doesn't proceed | conda's libcurl can't find CA bundle | The `bambu-studio` task sets `SSL_CERT_FILE` automatically; if you launch the binary directly export it yourself |

## Why Pixi vs the legacy `BuildLinux.sh` flow?

The historical `BuildLinux.sh` / `linux.d/{debian,fedora}` path needs
30+ system packages installed via the distro package manager, builds
all 28 third-party dependencies via `deps/` ExternalProject (hours), and
gives different results on Ubuntu vs Fedora vs Arch. Pixi replaces both
the package install step and ~20 of those ExternalProject builds with
prebuilt conda-forge binaries pinned in `pixi.lock` — same machine, same
output, every time.

The only forks that still go through `deps/` are the two that aren't on
conda-forge: bambulab's `libnoise` and `wxWidgets`. They live behind
`pixi run setup-libnoise` / `pixi run setup-wxwidgets` and only run once.
