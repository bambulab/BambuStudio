# Modular / parallel architecture contract

This extends `SPEC.md`. It restructures the app so the 9 screens can be built **in parallel, each in
its own git worktree, with zero shared-file conflicts**, then assembled deterministically. Markup
stays verbatim from `design-source/Bambu Studio.dc.html` — modularization is only about *where the
code lives*, never about changing the design.

## The foundation (built once, sequentially, before any fan-out)

Files owned by the foundation (no screen agent may edit these):

- `runtime/mini-dc.js` — the runtime (from Run 1). Screen-agnostic; unchanged by modularization.
- `app/styles.css` — MD3 tokens (from Run 1).
- `app/searchfield.logic.js` + `<template data-component="SearchField">` — the shared component.
- `index.html` — contains:
  - `<template data-shell>`: the root `<div data-theme … data-density … style="…{{ accentOverride }}">`
    with the **title bar**, **tab bar**, the **screen container**, the **overlays** (appearance
    popover, snackbars, version-history drawer), and the **dialogs** (Export / Send / Add-filament).
    Inside the screen container, one placeholder comment per screen, in the design's order:
    `<!--SCREEN:home-->`, `<!--SCREEN:prepare-->`, `<!--SCREEN:preview-->`, `<!--SCREEN:device-->`,
    `<!--SCREEN:multi-->`, `<!--SCREEN:project-->`, `<!--SCREEN:calibration-->`, `<!--SCREEN:filament-->`,
    `<!--SCREEN:settings-->`.
  - Boot code (`app/boot.js` or inline) that: reads each `<template data-screen="ID">`, replaces the
    matching `<!--SCREEN:ID-->` placeholder in the shell template with that screen template's
    innerHTML, producing the final `main` template string; registers `SearchField` and `main`;
    mounts `main` into `#app` with initial props `{theme,density,accent,view}` (+ `?view=/theme=/
    density=/accent=` query overrides). A screen whose template is absent leaves an empty placeholder
    (app still boots) — this is what lets a worktree run with only its own screen present.
- `app/main.logic.js` — the `Main extends DCLogic` class holding **all shared state** (the design's
  full `this.state`) and **all shared/global methods** (hexToHsl, accentVars, go, commit, notify,
  dismissSnack, seg, render_tabs, the `menus`, theme/density/accent handlers, snackbars, version
  history, and the Export/Send/Add-filament dialog logic — exMatch, toggleExportItem,
  toggleSelectAllExport, doExport, render_filRows, etc., since dialogs use shared state). Its
  `renderVals()` returns `Object.assign({}, this.commonVals(), ...SCREENS.map(s => s.vals.call(this)))`
  where `commonVals()` returns everything not screen-specific (theme, density, accentOverride, tabs,
  menus, showControls, snacks, history, dialog flags, filaments, projectName, branch, …).
- `app/screens/registry.js` — defines the registry the screens plug into:
  ```js
  window.SCREENS = [];
  window.registerScreen = function(def){
    // def = { id, mixin:{...methods}, vals:function(){ return {...} } }
    Object.assign(Main.prototype, def.mixin);   // attach render_* helpers to the class
    window.SCREENS.push(def);
  };
  ```
  Load order in index.html: registry.js → all screen `*.logic.js` → main.logic.js (which reads
  `window.SCREENS`). (Or main.logic first defining `Main`, then registry, then screens — pick one
  order and document it; the foundation decides.)

## A screen module (built in parallel — each agent owns exactly these two files)

For screen `ID` (e.g. `prepare`), the agent creates ONLY:

1. `app/screens/ID.template.html` — a `<template data-screen="ID">` whose contents are the **verbatim
   inner markup** of that screen's `<sc-if value="{{ isID }}"> … </sc-if>` block from
   `design-source/Bambu Studio.dc.html` (copy exactly; keep the `sc-if` wrapper so the screen shows
   only when active). Do not restyle or summarize.

2. `app/screens/ID.logic.js` — registers the screen:
   ```js
   registerScreen({
     id: 'ID',
     mixin: { /* that screen's render_* helpers, verbatim from the design class */ },
     vals: function(){ return { isID: this.state.view==='ID', /* that screen's keys */ }; }
   });
   ```
   Move ONLY that screen's helpers + its slice of `renderVals()` here. Screen key sets are disjoint
   (home→recent; prepare→gizmos/sceneTools/processTabs/params/manipRows; preview→gcode*/prevOpts/
   layer…; device→temps/speedModes/lamp…; multi→devices; project→projectCats/projectFiles;
   calibration→caliCards; filament→filamentRows(+openExport); settings→prefs+accentSwatches), so no
   agent writes a key another owns. Shared keys stay in the foundation's `commonVals()`.

## Screen → design-source line ranges (start markers; copy each `sc-if` block through its close)

- home L312, prepare L100, preview L349, device L414, multi L501, project L523, calibration L565,
  filament L584, settings L613. (Dialogs L754/795/811 + overlays live in the foundation.)

## Parallel build via worktrees

1. Foundation is committed on `md3-redesign` (call this commit F).
2. For each screen: `git worktree add ../bs-wt-ID md3-wt-ID` (branch `md3-wt-ID` off F).
3. A Workflow spawns one agent per screen; each `cd`s into its worktree, runs Codex to author its two
   files per this contract, verifies its screen renders (boot the app with `?view=ID`), commits on
   `md3-wt-ID`.
4. Assembly (main tree): merge each `md3-wt-ID` (all touch disjoint files) → `md3-redesign`; remove
   worktrees; boot assembler stitches; full-app QA; commit + push.
