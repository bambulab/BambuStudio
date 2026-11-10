# Linux AppImage updater

See [application-updater.md](application-updater.md) for the shared updater,
all platform behaviors, release packaging, tests, and CI templates.

Linux-specific details: a writable, user-owned x86-64 type-2 AppImage is required.
The updater verifies ELF architecture, locks the installed image, downloads to
a private directory beside it, retains `<AppImage>.previous`, and atomically
renames the verified image into place. The running process continues on its
old inode. Restart waits for exit, clears AppImage environment variables, and preserves the current data directory.

The optional `appimage_download_smoke` target streams a real HTTPS release into
a disposable fixture; it never launches the downloaded image. To roll back,
close the app, rename the current AppImage aside, and rename `.previous` to the
original path. Keep the backup executable.
