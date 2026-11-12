# Status and build provenance

This is a source preview of a working local Windows integration. The record below comes from the original development session and its build and startup logs. Publication adds documentation, a separate-profile launcher, and safer plugin build-driver defaults; it does not constitute a new full application rebuild.

## Recorded local verification — October 12, 2026

| Check | Recorded result | Limit |
| --- | --- | --- |
| Windows x64 Studio compile and link | Completed, application version `02.08.04.57`. | One development environment; not a clean-machine build. |
| Open plugin compile | Completed at `d8d8561cb5c5343963d0125e0eecefa0d644824d`. | Interim upstream source revision. |
| Export audit | 141 replacement exports; all 113 symbols in the audit's Studio lookup snapshot were present. | Symbol presence does not establish ABI or behavioural correctness. |
| GUI startup | Application log reached GUI mode. | No end-to-end slicing or physical print was recorded. |
| Fork loader guards | Logged disabled profile plugin copying and loading from the executable directory. | Startup evidence only. |
| Open plugin startup | Logged the open plugin at ABI `02.08.04.57`. | No connected printer test was recorded. |
| Discovery listener | Logged SSDP listening on port 2021. | A listening socket does not prove discovery of a printer. |
| Separate application | Stock Studio and the development executable were observed running from different paths. | Shared Windows registrations still require care. |

The useful startup markers were:

```text
copy_network_if_available is disabled in fully-open build
fully-open build: loading network plugin from <application directory>
Loaded Open Bamboo Networking plugin, interim build, commit #d8d8561c, ABI 02.08.04.57
ssdp: listening on 0.0.0.0:2021
```

These are excerpts from historical local evidence, not a CI run. Raw logs, profiles, backups, and the conversation transcript are not part of this repository.

## Build provenance

| Component | Source or input |
| --- | --- |
| Studio | Bambu Studio source at base commit `47a13ee`, plus the fork integration patches. |
| Networking and camera implementation | Open Bamboo Networking source at the pinned commit above. |
| Most Studio dependencies | Upstream dependency superbuild, using open-source dependency releases. |
| FFmpeg on Windows | Upstream `bambulab/ffmpeg_prebuilts` package, not compiled in the local dependency build. |
| Mesa OpenGL fallback | A binary copied from the installed Studio for the first local boot; not shipped here or rebuilt during that session. |
| libiconv runtime | `libiconv-2__.dll` from the local Strawberry toolchain, required by the development build's dependency selection. |
| Compiler, SDK, CRT | Microsoft Visual Studio 2022 Build Tools, Windows SDK, and Microsoft runtime components. |
| WebViews | Microsoft WebView2, a proprietary platform runtime. |

The phrase “fully open” in the build option refers to replacing the closed Bambu networking/camera packages. It should not be read as a reproducible, entirely source-built Windows distribution or a claim that every platform runtime is open source.

## Still needs verification

- Fresh recursive clone → dependencies → Studio → open plugin → runtime staging → launch on a clean Windows machine.
- A real model import, slice, and G-code export.
- LAN discovery and telemetry against named printer/firmware combinations.
- File upload, print start, pause/resume, cancellation, and camera playback.
- Optional cloud sign-in/preset syncing and the plugin's cloud-command limitations.
- Linux and macOS builds, loader locations, bundle packaging, and signing.
- Fork-specific installers and dependency source/license notices.

## Repository checks

For publication, both PowerShell helpers parsed successfully, shell syntax passed, documentation links resolved, and the SVG banner parsed. Mocked-process checks confirmed that a failed plugin build stops staging, the launcher quotes a profile path with spaces, and the calling shell's environment is restored after both successful and failed launches. No new full Studio build or printer test was performed.

The README's helper-check badge records those local checks; it is not live CI. An optional [GitHub Actions setup file](ci-source-checks.example.yml) can be copied to `.github/workflows/source-checks.yml` by a maintainer with workflow-write permission. It checks script syntax only.

The inherited upstream build and package-publishing workflows are preserved but disabled in this fork. They do not stage the fork's open plugin and should not be used to produce its releases without adaptation.
