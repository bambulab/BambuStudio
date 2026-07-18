# Bambu Studio — Material Design 3

**Live:** https://codingmachineedge.github.io/BambuStudio/ · **App:** https://codingmachineedge.github.io/BambuStudio/app/

A self-contained web implementation of a **Material Design 3 (Material You) concept redesign** of the
Bambu Studio slicer UI. Every screen of the real app's information architecture, re-skinned in pure
MD3 — no invented features. Runs in any browser with **zero dependencies and no build step**.

> Independent, open-source **concept** redesign built on a fork of the open-source Bambu Studio
> project. Not affiliated with, authorized by, or endorsed by Bambu Lab.

## Run it

- **Simplest:** open [`index.html`](index.html) directly in a browser (`file://`).
- **Static server:** serve this folder and open `/index.html`.
- **Landing page:** [`landing.html`](landing.html) — the marketing/entry page (Pages site root).
- **Desktop app:** see [`desktop/`](desktop/) — an Electron wrapper packaged into a Windows
  installer by the [`windows-installer`](../.github/workflows/windows-installer.yml) workflow.

Query overrides for QA: `index.html?view=preview&theme=light&density=compact&accent=%237c5cff`
(`view` ∈ home·prepare·preview·device·multi·project·calibration·filament·settings).

## What's inside

| Screen | Highlights |
|---|---|
| **Home** | Welcome hero, quick actions, recent projects |
| **Prepare** | 3D editor — gizmo rail, scene toolbar, plate bar, printer/filament/process/objects/manipulation sidebar |
| **Preview** | G-code viewer — color schemes, legend, layer & move sliders, statistics |
| **Device** | Live camera, temperatures, fans, axis control, AMS, calibration |
| **Multi-device** | All printers at a glance |
| **Project** | Project files (pictures, BOM, guides, notes) |
| **Calibration** | Flow dynamics, flow rate, max volumetric speed, temp tower, retraction, Z fine-tuning |
| **Filament** | Preset manager with filter / select / bulk-export |
| **Settings** | Appearance, general prefs, presets, network, version control, about |

Plus the Export / Send-to-print / Add-filament dialogs, an appearance popover, snackbars, and a
version-history drawer. **Material You** is first-class: light/dark themes, a comfortable/compact
density toggle, and an accent **seed color** that regenerates the full HSL tonal ramp live.

## Architecture

```
ui-md3/
  index.html            app shell + inlined <template>s + boot
  runtime/mini-dc.js    tiny dependency-free runtime for the design's template dialect
  app/
    styles.css          MD3 token system (light/dark, density, accent) — verbatim from the design
    main.logic.js       Main class: shared state + shared/dialog methods + commonVals + renderVals
    boot.js             stitches screen templates into the shell, registers + mounts the app
    searchfield.logic.js  shared regex-capable search component
    screens/
      registry.js       registerScreen(): mixes each screen's methods into Main, collects vals
      <id>.template.html  one <template data-screen> per screen (verbatim MD3 markup)
      <id>.logic.js       that screen's render helpers + vals slice
      CONTRACT.md         the foundation↔screen contract
  design-source/        read-only original Claude Design (the source of truth) + SPEC.md, ARCH-PARALLEL.md
  desktop/              Electron shell for the Windows installer
  landing.html          marketing / entry page
```

**`mini-dc.js`** reimplements the design's template dialect (`{{ }}` interpolation, `<sc-for>`,
`<sc-if>`, `<dc-import>`, `style-hover`, `onClick`/`onInput`) as a keyed virtual-DOM patcher that
preserves input focus/caret and child-component state across re-renders — so the design's markup and
logic port over almost verbatim, maximizing fidelity. See [`SPEC.md`](SPEC.md) and
[`ARCH-PARALLEL.md`](ARCH-PARALLEL.md).

## How it was built

The [design](design-source/) was imported from a Claude Design project, then implemented as real code
by **orchestrating the Codex CLI** (`gpt-5.6-sol`): the runtime and foundation first, then the nine
screens built **in parallel — each by its own Codex agent in a dedicated git worktree** — and finally
assembled and QA'd in the browser.
