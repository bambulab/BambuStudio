# Building Bambu Studio with Pixi

[Pixi](https://pixi.sh) provides all dependencies from conda-forge,
replacing the legacy `BuildLinux.sh` + `deps/` ExternalProject flow.

Currently supports `linux-64` (glibc ≥ 2.34). `osx-*` / `win-64` are
planned — extend `pixi.toml`'s `platforms` and per-target dependencies.

## Quick start

```bash
curl -fsSL https://pixi.sh/install.sh | bash    # pixi, one-time

pixi install                  # conda-forge deps         (~30 s)
pixi run bootstrap            # libnoise + wxWidgets      (~30–40 min, once)
pixi run build                # bambu-studio              (~10–15 min)
pixi run bambu-studio         # launch (args pass through)
```

Override parallelism: `CMAKE_BUILD_PARALLEL_LEVEL=4 pixi run build`.
Default is `floor(free_mem_GB / 2.5)` to fit Boost.Spirit / CGAL TUs.

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
