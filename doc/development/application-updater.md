# In-app updates

The optional updater downloads and verifies updates inside Bambu Studio on
Windows, macOS, and Linux. It is disabled by default: an empty
`BAMBUSTUDIO_UPDATE_REPOSITORY` preserves the existing update check and browser
download flow. Release maintainers must configure a trusted GitHub repository
and compatible packages before enabling it. The build and test scripts do not
publish releases or select a personal fork as the update source.

Users must manually install the first build that enables the updater. Subsequent
updates come from the configured repository. Packages in that channel must also
contain the updater and preserve its configuration.

When enabled, the existing startup check and **Check for updates** open the
release dialog with **Install update**. Downloads stream to disk on a worker thread with
progress and cancellation. Stable releases are the default; the existing beta
preference includes prereleases. Before use, every package must match the exact
configured asset name and repository URL, its expected byte count, and the
SHA-256 digest provided by GitHub over HTTPS. Failed or cancelled downloads are
deleted. This digest relies on the repository and GitHub account; it is not an
independently signed release manifest.

## Platform behavior

| Platform | Exact release asset | Installation |
| --- | --- | --- |
| Linux x86-64 | `BambuStudio_linux-x86_64.AppImage` | Atomic replacement of a writable, user-owned type-2 AppImage; `.previous` backup retained. |
| Windows x86-64 | `BambuStudio_windows-x86_64.exe` | Verified NSIS installer runs after the app exits, with the current installation directory fixed for the update. UAC and installer UI remain visible. |
| Windows ARM64 / ARM64EC | `BambuStudio_windows-arm64.exe` | Same installer flow; publish the ARM64EC build when the x64 network plugin is required. |
| macOS Intel / Apple silicon | `BambuStudio_macos-universal.dmg` | Mount read-only, verify the app signature and local CPU architecture, copy beside the current app, then replace after exit. Retain `.previous`; restore it if replacement fails. |

**Install now** uses the normal close path, including project save prompts.
Cancelling a save keeps the app open. Deferred web-project saves keep the update
request until the save completes. Rechecking with an update ready offers the
installation/restart again without downloading it twice. Save your project
before restarting; the updater does not reopen unsaved work automatically.
All three platforms relaunch with the current data directory, preserving a
custom `--datadir` profile.

On Linux the replacement is installed immediately and **Restart now** is
optional. Other platforms install only after choosing to close and install.
Choosing later keeps the verified download for the current session; a normal
exit without installation discards it. Windows PowerShell 5.1 runs the local
helper without loading profiles. The Windows installer may request administrator
credentials. Cancelling that installer leaves the app closed; launch it again
normally. Installer errors are shown by the helper.

macOS requires a writable app and parent directory, owned by the current user;
move an app from a read-only disk image or protected system directory into a
writable Applications folder first. The helper detects a renamed/replaced
installation and serializes replacement with a lock directory. It rotates an
existing regular backup, rejects symlink paths, and preserves the immediate
previous version. A failed swap keeps the staged download for diagnosis. A
crash or power loss during the two directory renames can require manually
renaming `.previous` back. Full macOS replacement is not atomic across both
renames.

Linux Flatpak and non-AppImage installations retain the upstream behavior.
Linux ARM and Windows 32-bit do not enable this updater by default.

## Build and package releases

Increment the four-component `SLIC3R_VERSION` in `version.inc` for every release.
Use the matching tag, e.g. `v02.08.04.58`, and upload the exact assets above.
GitHub's asset `digest` must be `sha256:<64 lowercase hex digits>`; absent digests
fail closed. Drafts and tags without four numeric version components are ignored.
The updater compares numeric versions across the latest 100 releases.

Build with the upstream platform dependencies and scripts. For Linux:

```sh
sudo apt-get install -y libgtk-3-dev
sudo ./BuildLinux.sh -ur
./BuildLinux.sh -dsir
cp build/BambuStudio_ubu64.AppImage build/BambuStudio_linux-x86_64.AppImage
```

For Windows, build the appropriate x64 or ARM64EC CMake configuration and its
`install` target. Integrate the helper's `/UPDATE /D=<installation-directory>`
contract into the official installer before enabling the Windows channel. See
[Windows installer integration](../../packaging/updates/windows-installer.md)
for exit codes, installation ownership, stale files, and signing requirements.
There is no production Windows installer in this change.

Build/package one ARM64 variant per release; ARM64 and ARM64EC share the ARM64
asset channel and must not contain incompatible binaries.

For macOS, `./BuildMac.sh -s -x -a universal -t 10.15 -1` builds a universal bundle
once the upstream dependencies exist. Sign the finished bundle, including its
nested executables/libraries, with your release identity, then run:

```sh
packaging/updates/macos-dmg.sh build/BambuStudio/BambuStudio.app "$PWD/BambuStudio_macos-universal.dmg"
```

The provided DMG script is also a reference packaging path; it does not
replace the release signing or notarization pipeline.

The DMG script requires a valid signature and both architectures. For local testing,
a correctly applied ad-hoc signature can pass verification; distribution should
use Developer ID signing and notarization/stapling to avoid Gatekeeper friction.
The updater verifies signature integrity; it does not pin a developer team ID.
It trusts the bytes selected by the configured GitHub repository.

Enable the updater explicitly with these CMake cache entries:

```
-DBAMBUSTUDIO_UPDATE_REPOSITORY=owner/repository
-DBAMBUSTUDIO_UPDATE_ASSET=<exact-platform-asset>
```

An empty repository disables the feature. For an upstream release, the repository
must be `bambulab/BambuStudio`; a fork must use its own trusted repository. The
asset names in the table are the reference packaging contract, not a claim that
the existing official release assets follow it. Before enabling a channel,
configure its exact asset name, publish compatible packages with GitHub SHA-256
digests, and verify the complete update from an installed previous build.

## Tests and CI

The portable tests build on all three platforms with CMake 3.16+, C++17, libcurl,
and OpenSSL. Windows can use the vcpkg toolchain for those dependencies:

```sh
cmake -S tests/application_updater -B build/application-updater-tests
cmake --build build/application-updater-tests --config Debug
ctest --test-dir build/application-updater-tests -C Debug --output-on-failure
```

`tests/application_updater/windows_helper_tests.ps1` parses the actual helper
source and runs eight scenarios with mocked process/installer commands and a
headless alert: normal install, already-exited parent, cancelled installer, and
parent timeout, each with and without a custom profile. It also checks literal
paths, the fixed installation directory,
cleanup, and restart ordering. It runs with PowerShell 7 on Linux or Windows;
Windows PowerShell 5.1 has also been exercised in the native VM; real UAC on an unelevated desktop still requires verification.

The Linux suite additionally tests atomic replacement, inode preservation,
backup rotation, concurrent updates, unsafe paths, restart waiting/environment,
and the macOS helper's actual directory transaction with substituted macOS
launch/notification tools, including rollback and changed-installation detection:

```sh
cmake -S tests/appimage_updater -B build/updater-tests
cmake --build build/updater-tests -j2
ctest --test-dir build/updater-tests --output-on-failure
```

`doc/development/application-updater-workflow.yml` is an inert cross-platform
CI template. Copy it to `.github/workflows/application-updater.yml` and adapt its
branch triggers to activate it. The existing Linux full AppImage build template
remains at `appimage-updater-workflow.yml`. Neither publishes releases. The
AppImage template produces an artifact with the updater disabled unless the
build is explicitly configured with a trusted update repository.

Before distributing, test the complete GUI update on each native OS: successful
installation and launch, download cancellation, offline/rate-limited checks,
permission failures, unsaved projects and cancelled/deferred saves. On Windows,
test UAC acceptance/cancellation and Unicode/spaced installation paths. On macOS,
test mounted-image rejection, Intel/Apple silicon, signature failure, backup
rotation, and rollback. Portable tests and Linux shell transaction tests do not
replace native GUI/installer testing.

## Validation and rollout

Component, native installer-fixture and application startup results are recorded
in the upstream [validation report](https://github.com/bambulab/BambuStudio/pull/12789#issuecomment-6431342786).
The Windows NSIS fixture lives under
`tests/application_updater/fixtures`; it requires a marker in a disposable test
directory and never creates an uninstaller, shortcuts, or registry entries. It
runs without elevation and does not test UAC or the official installer.

Before enabling production releases, resolve these items:

- Agree on stable release asset names and integrate the official Windows
  installer. The reference asset names do not match current official releases.
- Recheck the Windows package at execution time and verify its expected signer.
  Require the expected developer Team ID on macOS. A GitHub digest alone trusts
  the configured repository, and the current macOS check verifies integrity only.
- Define recovery after installer cancellation/failure and helper timeouts.
  The current helper may leave the app closed; Windows discards the package.
- Preserve required legacy update policies when enabling the new check. The new
  path bypasses forced-update and cloud-download policies and skips only an exact
  skipped version.
- Enforce supported platforms after reading CMake cache overrides, and prevent a
  second Linux instance from overwriting the rollback copy after another instance
  has replaced the AppImage.
- Bound macOS preparation commands and make their cancellation interruptible.
  Test slow downloads, explicit Linux CA setup, and malformed release metadata.
- Cover the GUI close/veto and deferred-save paths, helper arguments, reentrant
  checks, and failure recovery in automated tests. Activate the CI templates.
- Complete native Windows/macOS GUI update/save tests, ordinary unelevated UAC,
  signing/notarization/Gatekeeper, Windows ARM64, Intel Mac execution, older macOS
  compatibility, and hardware graphics tests. Fixture tests and app startup do
  not establish production release compatibility.
