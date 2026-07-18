# Bambu Studio MD3 — desktop shell

A thin [Electron](https://www.electronjs.org/) wrapper that packages the self-contained `ui-md3`
web app into a Windows installer (`.exe`, NSIS). Used by the
[`windows-installer.yml`](../../.github/workflows/windows-installer.yml) GitHub Actions workflow to
publish installers to GitHub Releases.

## Build locally (Windows)

```sh
cd ui-md3/desktop
npm install
npm run dist        # -> dist/BambuStudioMD3-Setup-<version>.exe
```

`npm run start` runs the app without packaging.

## How it works

- `prepare.js` copies `ui-md3/{index.html,landing.html,runtime,app}` into `./app/` (generated,
  git-ignored), preserving the layout so relative paths resolve.
- `main.js` opens a 1440×900 window and loads `app/index.html`.
- `electron-builder` produces an NSIS installer with a desktop + start-menu shortcut.

## Releases

Push a tag like `v0.1.0` (or run the workflow manually) and the workflow builds the installer on a
`windows-latest` runner and attaches it to the matching GitHub Release.

## Notes

- The installer is **unsigned** (no code-signing certificate), so Windows SmartScreen will warn on
  first run — expected for a concept build.
- Fonts/icons load from Google Fonts on first run; offline, the app falls back to system fonts.
