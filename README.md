# OpenStudio

**OpenStudio** is a community fork of [Bambu Studio](https://github.com/bambulab/BambuStudio) that stays continuously synced with official Bambu Lab releases, while adding extra features, fixes, and quality-of-life improvements on top.

- 🔄 **Kept up to date automatically** — a scheduled job checks for new official BambuStudio releases and merges them into this fork within hours, so you're never far behind upstream.
- ✨ **Extra features** — this fork accumulates community-contributed fixes and enhancements that haven't (yet, or ever) landed in the official app.
- 📦 **Every build is published** — both official-parity releases and in-development builds are available on the [Releases page](../../releases), so you can always see (and download) exactly what's changed.
- 🧩 **Can be installed alongside official Bambu Studio** — this fork uses its own app name, macOS bundle identifier, and settings folder, so it won't overwrite or conflict with an existing official Bambu Studio installation.

> This project is not affiliated with or endorsed by Bambu Lab. "Bambu Studio" is a product of Bambu Lab; see [Trademarks & branding](#trademarks--branding) below for details on why this fork is named and identified differently.

## Downloads

Prebuilt Windows, macOS, and Linux builds are available on the [Releases page](../../releases):
- **Full releases** — tagged to match an official BambuStudio version (e.g. `v02.08.02.61`), created once that version's build succeeds here.
- **Prerelease/dev builds** — tagged `v<version>-build.<run>` (from `master`) or `v<version>-dev.<run>` (from `dev`), for testing in-progress changes before they're folded into a full release.

### Running the macOS app

The macOS build is **ad-hoc signed**, not notarized by Apple (notarization requires a paid Apple Developer ID this project doesn't have). Because of this, Gatekeeper will warn that the app "cannot be verified" or "may contain malware" the first time you try to open it. To run it anyway:

1. Open the `.dmg` and drag **OpenStudio** to `/Applications`.
2. Try to open it once (macOS will block it), then go to **System Settings > Privacy & Security > Open Anyway**.
3. Alternatively, run this once in Terminal after installing: `xattr -cr /Applications/OpenStudio.app`

## What is Bambu Studio?

Bambu Studio (the upstream project this fork is based on) is a cutting-edge, feature-rich slicing software. It contains project-based workflows, systematically optimized slicing algorithms, and an easy-to-use graphic interface, bringing users an incredibly smooth printing experience.

Bambu Studio is based on [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research, which is in turn based on [Slic3r](https://github.com/Slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community.

See the official [wiki](https://github.com/bambulab/BambuStudio/wiki) and [documentation directory](https://github.com/bambulab/BambuStudio/tree/master/doc) for general usage information (most of it still applies here, since this fork tracks upstream closely).

### Main features

Key features are:
- Basic slicing features & GCode viewer
- Multiple plates management
- Remote control & monitoring
- Auto-arrange objects
- Auto-orient objects
- Hybrid/Tree/Normal support types, customized support
- Multi-material printing and rich painting tools
- Multi-platform (Win/Mac/Linux) support
- Global/Object/Part level slicing parameters

Other major features are:
- Advanced cooling logic controlling fan speed and dynamic print speed
- Auto brim according to mechanical analysis
- Support for arc paths (G2/G3)
- Support for the STEP format
- Assembly & explosion view
- Flushing transition-filament into infill/object during filament change

## How this fork is maintained

- `master` — mirrors the latest official BambuStudio release, plus this fork's accumulated extra changes. Every push here triggers a full build and, if it succeeds, an automated release.
- `dev` — used for prerelease/testing builds of in-progress features before they're merged into `master`. Every push here triggers a build and a prerelease.
- A scheduled workflow checks for new official BambuStudio releases every 6 hours, merges them into `master` automatically, and republishes a matching full release once the build succeeds.

## Contributing

Contributions are very welcome! Whether it's a bug fix, a new feature, or an improvement to the build/release pipeline:

1. Fork this repository and create a branch off `dev` (preferred) or `master`.
2. Make your changes and open a pull request describing what changed and why.
3. Once merged, your change will be included in the next automated prerelease/release build.

If you're not sure whether something belongs upstream (in official BambuStudio) or here, feel free to open an issue to discuss first.

## How to compile

The following platforms are currently supported for compiling:
- Windows 64-bit — [Compile Guide](https://github.com/bambulab/BambuStudio/wiki/Windows-Compile-Guide)
- macOS 64-bit — [Compile Guide](https://github.com/bambulab/BambuStudio/wiki/Mac-Compile-Guide)
- Linux — [Compile Guide](https://github.com/bambulab/BambuStudio/wiki/Linux-Compile-Guide)

These guides are written for upstream BambuStudio but apply equally here, since this fork uses the same build scripts (`BuildWin.bat`, `BuildMac.sh`, `BuildLinux.sh`) with only branding/packaging differences layered on top in CI.

## Report an issue

Please use this repository's [issue tracker](../../issues) for anything specific to this fork (build/release pipeline, features unique to this fork, etc). For bugs also present in official Bambu Studio, consider also checking the [upstream tracker](https://github.com/bambulab/BambuStudio/issues).

## Trademarks & branding

"Bambu Studio" and its logo are trademarks/branding of Bambu Lab. This fork is renamed to **OpenStudio**, and uses its own macOS bundle identifier and app icon assets, specifically to:
- avoid implying official endorsement or affiliation with Bambu Lab, and
- allow it to be installed side-by-side with the official Bambu Studio app without file/settings conflicts.

The underlying source code remains licensed under AGPLv3 (see below); only the name/branding differs.

## License

OpenStudio, like Bambu Studio, is licensed under the GNU Affero General Public License, version 3. It is based on PrusaSlicer by Prusa Research.

PrusaSlicer is licensed under the GNU Affero General Public License, version 3. PrusaSlicer is owned by Prusa Research. PrusaSlicer is originally based on Slic3r by Alessandro Ranellucci.

Slic3r is licensed under the GNU Affero General Public License, version 3. Slic3r was created by Alessandro Ranellucci with the help of many other contributors.

The GNU Affero General Public License, version 3 ensures that if you use any part of this software in any way (even behind a web server), your software must be released under the same license.

The Bambu networking plugin is based on non-free libraries. It is optional and provides extended networking functionalities for users. By default, after installing without the networking plugin, you can still initiate printing via SD card after slicing is complete.
