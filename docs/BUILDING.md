# Building and running

This is a source preview. Start with a separate checkout and keep `FULLY_OPEN_BUILD=ON`. The Studio build and the open networking plugin are separate build steps; compiling the slicer alone is not enough.

## Windows x64

### Prerequisites

- Git and CMake on `PATH`. The local successful build used Visual Studio 2022 Build Tools with the x64 C++ tools and Windows SDK.
- Visual Studio's Desktop development with C++ components. Use compatible MSVC toolsets and the dynamic CRT for both Studio and the plugin: C++ objects cross the DLL boundary.
- A bootstrapped [vcpkg checkout](https://github.com/microsoft/vcpkg) and `VCPKG_ROOT` pointing to it. The plugin uses its manifest to resolve dependencies.
- Perl on `PATH` for the dependency build's OpenSSL configuration. If using Strawberry Perl, be careful with its MinGW libraries; see troubleshooting below.
- The other requirements in the [upstream Windows compile guide](https://github.com/bambulab/BambuStudio/wiki/Windows-Compile-Guide), including a working `pkg-config` where required.

A dependency build is substantial. Allow time, disk space, and network access for third-party source downloads. The first successful local build needed extra environment-specific fixes, so the commands below are entry points rather than a clean-machine reproducibility claim.

### Build

```powershell
git clone --recurse-submodules https://github.com/Nickx777/bambu-open-studio.git
cd bambu-open-studio
$env:VCPKG_ROOT = 'C:\dev\vcpkg'
.\build-fully-open.ps1 -BuildStudio
```

The default configuration is `RelWithDebInfo`, matching the upstream Windows script. The driver runs the Studio build, derives the plugin version from `version.inc`, configures the plugin, builds it, and copies its DLLs beside the matching executable. Studio build output goes to `build_win.out.log`.

If Studio is already built:

```powershell
.\build-fully-open.ps1 -Configuration RelWithDebInfo
```

Plugin configuration defaults to the compiler selected by CMake. To choose it explicitly, configure the submodule before running the driver, using the same Studio version and toolset:

```powershell
$studioVersion = (Select-String -Path version.inc -Pattern 'SLIC3R_VERSION\s+"([^"]+)"').Matches[0].Groups[1].Value
Push-Location open-bamboo-networking
.\configure.ps1 -ClientType bambu_studio -WithVersion $studioVersion `
  -Generator 'Visual Studio 17 2022' -BuildType RelWithDebInfo `
  -Prefix "$PWD\build\staging" -PatchConf:$false -RegisterDShowFilter:$false
Pop-Location
.\build-fully-open.ps1
```

Use a fresh plugin build directory when changing the compiler, CRT, architecture, or ABI version. The driver reconfigures an existing tree with the current Studio version; it does not repair incompatible generator caches.

### Stage a development runtime

The plugin driver does not package all Studio dependencies. If needed, stage the dependency DLLs and resources into the executable directory:

```powershell
$exeDir = Join-Path $PWD 'build\src\RelWithDebInfo'
$depsPrefix = Join-Path $PWD 'deps\build\out_deps\usr\local'
Copy-Item "$depsPrefix\bin\*.dll" $exeDir
if (Test-Path "$depsPrefix\bin\occt") {
    Copy-Item "$depsPrefix\bin\occt\*.dll" $exeDir
}
if (-not (Test-Path "$exeDir\resources")) {
    Copy-Item .\resources "$exeDir\resources" -Recurse
}
```

The first local build also needed `libiconv-2__.dll` due to toolchain library discovery, and `mesa/opengl32.dll` for the OpenGL fallback. Neither is included here. Prefer correcting the dependency/toolchain selection and building the required runtime from its source. Do not assume a DLL from another installation has the right version or architecture. Record where every redistributed runtime came from.

### Run with a separate profile

```powershell
.\scripts\start-open-studio.ps1
# Custom locations are supported:
.\scripts\start-open-studio.ps1 -Executable 'D:\build\bambu-studio.exe' `
  -DataDirectory 'D:\profiles\bambu-open'
```

The launcher sets `--no-single-instance` and `--datadir`, redirects `APPDATA` and `LOCALAPPDATA` for the child process, and restores the calling shell's environment afterward. Its default profile is `.bambu-open/profile/`. You can inspect the intended launch with `-WhatIf`.

**Camera registration:** the build driver disables automatic DirectShow registration. On Windows, camera playback may require registering the open `BambuSource.dll`; that is a shared Windows integration and can affect which library the installed Studio uses. Treat camera registration as an explicit local setup step, following the plugin's [building guide](https://github.com/ClusterM/open-bamboo-networking/blob/d8d8561cb5c5343963d0125e0eecefa0d644824d/BUILDING.md). A separate settings profile does not isolate shared Windows COM registration.

## Linux and macOS

These fork integrations have not been boot-verified in this project. Follow the upstream [Linux](https://github.com/bambulab/BambuStudio/wiki/Linux-Compile-Guide) or [macOS](https://github.com/bambulab/BambuStudio/wiki/Mac-Compile-Guide) guide and the pinned plugin's [dependency guide](https://github.com/ClusterM/open-bamboo-networking/blob/d8d8561cb5c5343963d0125e0eecefa0d644824d/BUILDING.md).

Build Studio with `FULLY_OPEN_BUILD=ON`, then:

```sh
git submodule update --init --recursive
sh ./build-fully-open.sh
```

The helper looks for a `bambu-studio` executable under `build/`. It copies plugin libraries beside that executable. It does not assemble an AppImage or a signed macOS `.app`; macOS bundle paths, library lookup, and codesigning need separate validation. Builds that put their executable elsewhere need manual staging or a helper adjustment.

## Troubleshooting the toolchain

| Symptom | What to check |
| --- | --- |
| CMake complains about a different generator | Use a separate build directory for each generator; do not mix Ninja and Visual Studio caches. |
| Link errors involving GMP/MPFR and `__imp___gmpz_*` | CMake may have found Strawberry's MinGW `.a` files. Point `GMP_LIBRARY_RELEASE`, `GMP_LIBRARY_DEBUG`, and `MPFR_LIBRARIES` at the MSVC libraries under the deps prefix. |
| `pkg-config` fails to execute | A Perl script shim may be shadowing the real executable. Set `PKG_CONFIG_EXECUTABLE` to a working `pkg-config.exe`. |
| Dependency CMake policy errors | Check the dependency's supported CMake version. The local build used a compatibility minimum of 3.5 where required. |
| Excessive Debug dependency work | A separate deps configure with `-DDEP_DEBUG=OFF` can avoid the extra Debug dependency build when targeting RelWithDebInfo. |
| `BambuStudio.dll` fails to load, error 126 | Inspect transitive DLL imports with `dumpbin /dependents`; a missing dependency can cause this even when `BambuStudio.dll` exists. |
| OpenGL probe fails before the UI appears | Check your graphics driver and the upstream Mesa fallback packaging. |

More answers are in [the FAQ](FAQ.md). For a report, include the exact commit, toolset, configuration, failing command, and a redacted log.
