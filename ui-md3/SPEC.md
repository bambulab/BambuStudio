# Bambu Studio MD3 — Standalone App Implementation Spec

**Owner of this spec:** the "UI rewriter" (design + architecture + review).
**Implementer:** Codex (`gpt-5.6-sol`, ultra effort).

## Goal

Rewrite the **entire Bambu Studio application UI** as a self-contained, dependency-free
web application that faithfully realizes the Material Design 3 (Material You) design in
`ui-md3/design-source/Bambu Studio.dc.html`. The design is the single source of truth.

The design was authored against a proprietary React-based runtime (`design-source/support.js`)
using a small templating dialect (`{{ }}` bindings, `<sc-for>`, `<sc-if>`, `<dc-import>`,
`style-hover`, `onClick`/`onInput`). **We do NOT ship `support.js` or React.** Instead we
reimplement a tiny vanilla-JS runtime (`mini-dc.js`) that supports exactly this dialect, so the
design markup and its logic class port over almost verbatim — maximizing fidelity.

The result must run by simply opening `ui-md3/index.html` in a browser (`file://`) **and** via a
static server. No build step, no npm, no network except optional Google Fonts.

## Hard constraints

- **Do not edit anything in `ui-md3/design-source/`** — it is the read-only reference.
- No external JS/CSS dependencies. Vanilla ES2020+ only. Google Fonts `<link>`s are allowed
  (Roboto, Roboto Mono, Material Symbols Outlined); the app must still work if they fail
  (system-font fallback; icon glyphs may show as text — acceptable).
- Runs on `file://` (so **no `fetch()` of local files** — inline templates as `<template>`
  elements or JS strings; load runtime/logic via `<script src>`; CSS via `<link>` or `<style>`).
- Keep the code readable and close to the design's own structure so fidelity review is easy.

## File layout (create under `ui-md3/`)

```
ui-md3/
  index.html              boots the app: fonts, styles, <template>s, runtime, logic, mount #app
  runtime/
    mini-dc.js            the vanilla DC runtime (parser + keyed VDOM patch + components)
  app/
    styles.css           MD3 token system + base resets (extracted from the design <helmet>)
    main.template.html    (optional) if you prefer external template text, inline into index.html
    main.logic.js        ported main Component (DCLogic subclass) — all render_* + renderVals
    searchfield.logic.js  ported SearchField Component
  design-source/          READ-ONLY reference (already present) — do not modify
  SPEC.md                 this file
  README.md               you write this at the end
```

You may inline templates directly in `index.html` inside `<template data-component="main">` and
`<template data-component="SearchField">` elements (recommended, keeps `file://` working). Logic
classes and the runtime load via `<script src>`.

## The runtime contract (`mini-dc.js`) — implement EXACTLY this dialect

The templates use these constructs. `hint-*` attributes (`hint-placeholder-count`,
`hint-placeholder-val`, `hint-size`) are design-tool hints — **ignore them at runtime** (strip).

1. **Interpolation `{{ expr }}`** appears in:
   - text nodes: `<span>{{ layer }} / {{ maxLayer }}</span>`
   - attribute values, possibly mid-string: `style="... color:{{ t.fg }}; ...{{ accentOverride }}"`,
     `title="{{ g.label }}"`, `value="{{ value }}"`, `placeholder="{{ placeholder }}"`.
   - `expr` is a whitespace-trimmed key path resolved against the current **props object**
     (the return value of `renderVals()`), plus the loop variable(s) in scope. Support dotted
     paths: `t.fg`, `p.knobX`, `n.icon`, `h.diff`. No arbitrary JS expressions are needed — only
     identifier / dotted-path lookups and boolean literals like `{{ true }}` (treat `true`/`false`
     as literals). If a key is missing, render empty string.
   - When an interpolated value is a **function** and the attribute is an event attribute
     (`onClick`, `onInput`), bind it as a listener (see 5). When a value is a function elsewhere,
     render empty string.

2. **`<sc-for list="{{ arr }}" as="x">…</sc-for>`** — for each item in `arr` (array from props),
   render the inner content with `x` bound to the item (so `{{ x }}`, `{{ x.prop }}` resolve to it).
   Nested `sc-for` must work (inner scope shadows/extends outer). Preserve DOM across renders with
   a stable key per item (index is acceptable; prefer `item.id`/`item.hash`/`item.name` if present)
   so inputs/children inside a row aren't torn down needlessly.

3. **`<sc-if value="{{ bool }}">…</sc-if>`** — render inner content only when `bool` is truthy.
   Nested inside `sc-for` (e.g. `p.isSwitch`) and at top level (dialogs, popovers) must work.

4. **`<dc-import name="SearchField" placeholder="…" on-query="{{ fn }}">`** — instantiate the named
   component as a **child** with props from the element's attributes:
   - static attrs → string props (`placeholder="Search objects"` → `props.placeholder`).
   - `{{ }}` attrs → resolved value props.
   - **kebab-case → camelCase**: `on-query` → `props.onQuery` (a function).
   - The child renders its own template with its own logic/state.
   - **CRITICAL:** a `<dc-import>` child MUST persist its own internal state across parent
     re-renders (keyed by its position/path in the template). Typing in a SearchField must not
     reset the field, lose focus, or lose caret when the parent re-renders (e.g. the Export dialog
     search calls `onQuery` → parent `setState` → parent re-render — the field must keep its text,
     focus, and caret).

5. **Events**: `onClick="{{ fn }}"` → `addEventListener('click', fn)`;
   `onInput="{{ fn }}"` → `addEventListener('input', fn)`. `fn` comes from props (a bound closure
   from `renderVals()`), receives the DOM event. Rebinding across renders must not duplicate
   listeners (patch in place / replace handler).

6. **`style-hover="css-declarations"`** — while the element is hovered, merge these extra inline CSS
   declarations on top of its base `style`; remove them on mouseleave. This is the MD3 hover state
   layer. Must survive re-renders.

7. **Icons**: `<span data-icon style="…">icon_name</span>` — the text content is a Material Symbols
   Outlined ligature name (e.g. `deployed_code`, `expand_more`). `font-variation-settings:'FILL' 1`
   toggles filled icons. The `[data-icon]` CSS (font-family etc.) lives in `styles.css`.

### Rendering strategy (required behavior, your implementation choice)

Use a **keyed virtual-DOM patch** (parse each template once into a VNode tree; on render, evaluate
to a concrete tree against current props + loop scope; diff against the previous tree and patch the
real DOM in place). Patching in place — rather than rebuilding — is what preserves input focus/caret
and child-component state. Child `dc-import` instances are created once per keyed slot, mounted, and
on later parent renders only receive updated props (re-rendering themselves only if needed).

### Component base class (`DCLogic`)

Provide a base class `DCLogic` (name it exactly this — the ported logic does `extends DCLogic`) with:
- `constructor(props)` → sets `this.props = props || {}`. Subclass may set `this.state`.
- `this.state` — plain object (subclass sets it as a field or in its constructor).
- `setState(patch)` — accepts an **object** (shallow-merge) OR an **updater function**
  `st => ({...})` (called with current state, returns partial). After merge, schedule a re-render
  (batch multiple synchronous setStates into one render via microtask is fine).
- `this.props` — read the passed props.
- `renderVals()` — subclass returns the flat props map consumed by `{{ }}`. The runtime calls it
  each render.
- Optional lifecycle: `componentDidUpdate(prevProps)` — call after props change from the parent (for
  the main component this syncs theme/density/accent/view; in standalone these rarely change, so a
  correct no-op-when-unchanged implementation is enough).
- Mounting API, e.g. `mountComponent(name, hostEl, props)` and a registry
  `DC.register(name, TemplateSource, ComponentClass)` so `index.html` can register `main` and
  `SearchField` and mount `main` into `#app`.

The design's logic uses `Math.random()`, `Date.now()`, `setTimeout` — all fine in the browser.

## The design system (`styles.css`)

Extract verbatim from the `<helmet><style>` block of `design-source/Bambu Studio.dc.html`:
- `:root,[data-theme="light"]` and `[data-theme="dark"]` custom-property blocks (all `--md-*` tokens).
- `[data-density="comfortable"]` and `[data-density="compact"]` (`--gap --pad --row --fs --rail
  --sidebar --radius --radius-s`).
- base resets (`*{box-sizing}`, `html,body`, `button`, `a`, `[data-icon]`, scrollbar styling),
  and keyframes (`spin`, `mdfade`, `scrimin`, `pulse`).
- The root app container reads `data-theme` / `data-density` and applies `accentOverride`
  (a semicolon-terminated CSS string from `accentVars(seed, theme)`), exactly as the design's root
  `<div data-theme="{{ theme }}" data-density="{{ density }}" style="…{{ accentOverride }}">` does.

## Fidelity & acceptance

The app must reproduce, pixel-close, every part of `Bambu Studio.dc.html`:
- Title bar (logo, File/Edit/View/Objects/Help menus, git-branch chip, project chip, palette
  button, window controls), tab bar (Home/Prepare/Preview/Device/Multi-device/Project/Calibration/
  Filament/Settings with active indicator).
- All 9 screens with their full content (Prepare: gizmo rail, scene toolbar, 3D viewport mock, axis
  triad, view controls, plate bar, right sidebar with printer/filament/process/objects/manipulation;
  Preview: viewport, layer slider, moves slider, color scheme, legend, options, statistics; Device,
  Multi-device, Project, Calibration, Filament manager, Settings).
- Dialogs: Export filaments, Send-to-print, Add-filament. Appearance popover. Snackbars. Version-
  history drawer (with expandable commits + diff + restore).
- All interactivity from the logic: tab switching, gizmo/tool selection (auto-commit to history),
  process tabs, support toggle, theme/density/accent switching (accent regenerates the tonal ramp),
  layer slider, preview options/scheme, export dialog filtering + select-all + count, snackbar
  notifications with actions, history expand/restore.

**Acceptance tests (Run-1 framework demo must prove all of these):**
1. `{{ }}` interpolation in text and mid-attribute works; missing keys render empty.
2. `sc-for` (incl. nested) and `sc-if` (incl. inside `sc-for`) render correctly and update on state.
3. `dc-import` mounts a child, passes props (incl. `on-query`→`onQuery`), and the child keeps its
   own state across parent re-renders.
4. Typing in a text input keeps focus + caret across re-renders triggered by that input.
5. `style-hover` applies/removes on hover.
6. `onClick`/`onInput` fire with the DOM event; no duplicate listeners after many renders.

Default boot state (match the design's props defaults): `theme:'dark'`, `density:'comfortable'`,
`accent:'#22c55e'`, `view:'prepare'`. Expose a way to set the initial `view` (e.g. `?view=home`)
for easy screen-by-screen QA.
