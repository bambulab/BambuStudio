# Windows installer integration

The updater expects an NSIS-compatible installer with this command line:

```text
installer.exe /UPDATE /D=<current installation directory>
```

NSIS requires `/D` to be the final argument, without quotes around its value.
The helper waits for Bambu Studio to exit, waits for the installer to finish,
then starts the installed `bambu-studio.exe` with the original data directory.
The installer must return zero only after a successful installation. Return a
nonzero exit code on cancellation or failure. The helper reports that failure
and discards the staged package; it does not reopen the existing app.

Before enabling a production Windows update channel:

- Integrate update mode into the official installer. Keep its existing product
  identity, uninstaller and registry ownership. Do not register a second product
  in the same directory or add another uninstaller that can remove that tree.
- Fix the destination to the existing installation during update mode, preserve
  user profiles, and remove obsolete application files without deleting user
  files. Copying over a directory alone leaves stale libraries behind.
- Preserve visible UAC and cancellation behavior. Use the production signing
  pipeline and add execution-time digest and signer checks to the helper.
- Test acceptance, cancellation, failure and relaunch from an unelevated desktop
  using paths with spaces and Unicode. Check installed-version migration and
  recovery after failure.

`tests/application_updater/fixtures/windows.nsi` installs only a disposable
fixture. It requires a `.bambu-updater-fixture` marker in the destination, runs
without elevation, and creates no uninstaller, shortcuts or registry entries.
It is not a production installer or a test of the official installer contract.
