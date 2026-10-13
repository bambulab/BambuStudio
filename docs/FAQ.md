# Troubleshooting and FAQ

## Is this official Bambu Studio?

No. It is a community fork with the existing slicer and an integrated open networking replacement. The application UI still contains upstream branding.

## Where is the download button?

This release contains source, not a fork installer. Use the Build from source button in the README. The upstream Studio installer does not contain this fork's loader changes, and Open Bamboo Networking's standalone releases are plugin packages, not this application.

## Why did Download ZIP leave out networking source?

GitHub source archives do not expand git submodules. Use `git clone --recurse-submodules`, or run `git submodule update --init --recursive` in a git checkout.

## Can it run next to my installed Studio?

Yes, keep a separate executable directory and use the supplied Windows launcher for a separate profile. Do not copy this build over your installation. Windows camera COM registration is shared; the driver leaves automatic registration disabled. See [the camera setup note](BUILDING.md#run-with-a-separate-profile).

## The window never appears, or a DLL fails to load

Look in the application data directory's logs, confirm the dependency DLLs and resources are staged, and check transitive dependencies with `dumpbin /dependents`. The initial development build required an additional libiconv DLL and the Mesa fallback. A file existing on disk is not proof that Windows can load all its dependencies.

## The plugin builds, but Studio rejects it

Check the Studio version, the plugin's configured ABI, architecture, MSVC toolset, and CRT. Reconfigure with the helper after a Studio version change; use a fresh plugin build directory after changing generator or compiler. Both the version string and actual interface compatibility matter.

## Why doesn't a cloud-paired printer accept every command?

Some commands depend on Bambu Lab's per-installation signing credentials and printer firmware policy. Open source alone does not supply those credentials. Consult the pinned plugin's [two operating modes](https://github.com/ClusterM/open-bamboo-networking/blob/d8d8561cb5c5343963d0125e0eecefa0d644824d/README.md#two-ways-to-run). LAN-only or Developer Mode is the intended starting point, where supported by your printer.

## Does open networking make all of Studio offline?

No. The fork guards the proprietary plugin download/update paths. Other upstream online services, such as model browsing or account-related features, remain in the application. LAN behaviour and optional cloud use also depend on plugin configuration.

## Why no promise of 100% open-source Windows binaries?

The replacement networking code is open, but Windows builds still use Microsoft's platform components. The working local runtime also used prebuilt FFmpeg, Mesa, and a toolchain libiconv binary. [The provenance record](STATUS.md#build-provenance) lists the actual inputs.

## How should I update it?

Review upstream changes, update the plugin pin deliberately, and recheck version/ABI compatibility. Pulling a newer plugin branch without testing it defeats the reason for pinning. Keep `FULLY_OPEN_BUILD=ON` when configuring Studio.

## Where should I report problems?

Use [this fork's issue tracker](https://github.com/Nickx777/bambu-open-studio/issues/new/choose) for integration and build problems. Include the commit, OS, compiler, printer model/firmware when relevant, exact steps, and redacted logs. Once a problem is isolated to unchanged upstream code, link the corresponding upstream report.
