# Screen module contract

This directory is the only area owned by the nine screen modules. Each module owns exactly two files:

- `app/screens/ID.template.html`
- `app/screens/ID.logic.js`

Screen modules must not edit the runtime, shell, shared logic, registry, boot code, shared SearchField, or styles.

## Load and assembly order

The authoritative classic-script order is:

1. `runtime/mini-dc.js`
2. `app/searchfield.logic.js`
3. `app/main.logic.js`
4. `app/screens/registry.js`
5. all assembled `app/screens/*.logic.js` files
6. `app/boot.js`

`main.logic.js` defines the global `Main` class, an empty `window.SCREENS` fallback, and a no-op registry hook. `registry.js` must load after it because registration mixes screen methods into `Main.prototype`; it resets `window.SCREENS` and installs the real `registerScreen`. Screen logic then registers against that implementation. `boot.js` runs last and must also work when no screens are registered.

Because the app runs from `file://`, the assembly step must inline every available screen template as a `<template data-screen="ID">` element in `index.html`; it must not fetch local template files. The assembly step also appends each available screen logic `<script>` after `registry.js` and before `boot.js`.

## Logic modules

Each `app/screens/ID.logic.js` must call:

```js
registerScreen({
  id: 'ID',
  mixin: { /* only the methods assigned below, copied verbatim */ },
  vals: function(){ return { /* only the keys assigned below */ }; }
});
```

Everything not assigned below comes from `Main.commonVals()`. A screen must not add shared keys or another screen's methods/keys.

| Screen | `mixin` methods | Exact keys returned by `vals` |
|---|---|---|
| `home` | `render_recent` | `isHome`, `recent` |
| `prepare` | `render_gizmos`, `render_scene`, `render_process_tabs`, `render_params`, `render_manip` | `isPrepare`, `gizmos`, `sceneTools`, `processTabs`, `params`, `manipRows` |
| `preview` | `render_legend`, `render_schemes`, `render_prevOpts` | `isPreview`, `gcodeLegend`, `gcodeSchemes`, `gcodeScheme`, `prevOpts`, `layer`, `maxLayer`, `onLayer`, `layerZ` |
| `device` | `render_temps`, `render_speedModes` | `isDevice`, `temps`, `speedModes`, `lamp`, `toggleLamp`, `lampTrack`, `lampTrackBorder`, `lampKnob`, `lampKnobX` |
| `multi` | `render_devices` | `isMulti`, `devices` |
| `project` | `render_projectCats`, `render_projectFiles` | `isProject`, `projectCats`, `projectFiles` |
| `calibration` | `render_cali` | `isCalibration`, `caliCards` |
| `filament` | none; use shared `render_filRows` / `filamentRows` | `isFilament` |
| `settings` | `render_prefs` | `isSettings`, `prefs` |

`isID` is true only when `this.state.view === 'ID'`. Methods and value expressions must be ported verbatim from `design-source/Bambu Studio.dc.html`, except for the module wrapper shown above.

## Template modules

Each `app/screens/ID.template.html` must contain one `<template data-screen="ID">`. Its contents are the complete, verbatim `<sc-if value="{{ isID }}"> … </sc-if>` block from `design-source/Bambu Studio.dc.html`, including the `sc-if` wrapper.

The source start lines are:

| Screen | Source start line |
|---|---:|
| `home` | 312 |
| `prepare` | 100 |
| `preview` | 349 |
| `device` | 414 |
| `multi` | 501 |
| `project` | 523 |
| `calibration` | 565 |
| `filament` | 584 |
| `settings` | 613 |

Do not restyle, summarize, reorder, or remove markup from a screen block. The shell assembler replaces the matching `<!--SCREEN:ID-->` marker with `template.innerHTML`; any marker without a corresponding template becomes empty.
