# DeviceWebHtml

Native HTML/CSS/JS implementation of the AMS / Filament / Hotend device page,
running **side by side** with the React build in `../device_page/`.

Gated behind the app config key `enable_device_web_html`. With the key unset the
entry button is never created and release paths are unchanged.

## Why

- `file://` + WKWebView masks every script error as `"Script error."`, so macOS
  failures in the React bundle are undiagnosable from the log.
- The React bundle is 999 KB JS + 155 KB CSS for all 13 routes; this page alone
  loads roughly 50 KB of JS plus the CSS it already had.
- Provides a second implementation to compare against, which is the only way to
  get a behavioural baseline while the repo has no visual regression tests.

## Layout

```
DeviceWebHtml/
  tools/
    gen-locales.mjs        # locales/*.json      -> web/locales/*.js
    gen-tokens.mjs         # dist/assets/index.css -> web/css/tokens.css
    gen-inline-assets.mjs  # overlay svgs        -> web/js/inline-assets.js
    contract-test.mjs      # host-contract self test, no browser or Studio build
    compare-pages.mjs      # React vs HTML visual diff (see Verification)
    fixtures/mockState.mjs # golden ViewModel shared by the comparison
  web/                     # shipped verbatim to resources/web/device_page_html/
    ams_hotend.html        # single entry, hash-routed over 5 device_page routes
    css/
      tokens.css           # generated: --ams-* / --color-fm-* + preflight
      *.css                # copied from the React feature, unmodified
    assets/                # copied from the React feature
    locales/               # generated, do not hand-edit
    js/
      dom.js               # el() + keyed patchList(), no virtual DOM
      bridge.js            # transport, request/response pairing, trace ring buffer
      health.js            # boot/ready handshake
      layout.js            # content_size reporting
      theme.js             # UserAgent + runtime theme_changed
      i18n.js              # t() with i18next-compatible interpolation
      defaults.js          # structural defaults for normalizeState
      store.js             # business protocol + normalisation + de-duplication
      router.js            # hash routing
      inline-assets.js     # generated: svg overlays as data URIs
      components/          # ported 1:1 from the React components/ tree
        spool.js           # SpoolGraphic, SpoolDisc, IndexPill, CardText, ...
        nozzle.js          # Nozzle (5 branches), FeedLine, colour visibility
        hotend.js          # DryerChamber, HotendCard, progress panels, menus
      pages/               # one module per route
      app.js               # bootstrap
```

Three of the generators read the React **build output** rather than its source,
because the source form is not consumable by a browser:

| Generator | Why the compiled artefact is the only source |
| --- | --- |
| `gen-locales.mjs` | `fetch`/XHR against `file://` is blocked, so JSON cannot be loaded at runtime |
| `gen-tokens.mjs` | `styles.css` declares tokens with Tailwind 4's `@theme { }`, which no browser understands |
| `gen-inline-assets.mjs` | SVG overlays composited inside another `<svg>` must be data URIs to rasterise identically |

Re-run all three after rebuilding `device_page`.

## Host contracts

All four are implemented and covered by `tools/contract-test.mjs`. Any change
here must be mirrored against the C++ side and the React implementation.

| Contract | Source of truth | Consequence of getting it wrong |
| --- | --- | --- |
| Transport (`chrome.webview` / `webkit.messageHandlers.wx` / iframe `app://`, `cpp:device` events) | `device_page/src/hooks/Bridge.tsx`, `DeviceWebBridge.cpp::InitBridge()` | Nothing works |
| `boot` / `ready` handshake | `Bridge.tsx` `marksHealthReady()`, `DeviceWebBridge.cpp:231` | `WebViewWatcher` reloads once, then shows the diagnostic fallback panel |
| `device_host/layout/content_size` | `ams-filament-hotend/hostContentSize.ts` | Dialog is sized wrong |
| Theme (UA parse + `init`/`theme_changed` report) | `hooks/useDeviceWebTheme.ts`, `WebView.cpp:918/951` | White flash, wrong colour scheme |

`ready` is only released by a **successful** (`error_code === 0`) business
`init` or `state`/`get` response, and only once per page instance. Sending it
earlier would defeat the watchdog; never sending it tears the page down.

`DeviceWebHealth.cpp` injects its own `error` listener and publishes
`window.__bambuWebViewHealth`. **Do not overwrite `window.onerror`** or the
js_error channel goes dark.

## Constraints that shaped the code

- **No ES modules.** Blocked by CORS under `file://` in WKWebView. Everything is
  a classic script attaching to the global `BBL` namespace.
- **No `fetch`/XHR for local files.** Same reason. Locales ship as generated
  `.js` files injected via `<script>`; see `tools/gen-locales.mjs`.
- **`localStorage` access must be guarded.** Touching it on a `file://` document
  throws `SecurityError` in WKWebView. `i18n.js` wraps every access.
- **No wholesale `innerHTML` rebuilds.** The host re-pushes a full ViewModel
  several times per second. `store.js` de-duplicates via `JSON.stringify`, and
  `dom.patchList()` reconciles slot lists by `ams_id + slot_id` (`display_index`
  alone collides across AMS units).

## Known divergences from the React build

1. **Empty defaults instead of the demo dataset.** `device_page/mockData.ts`
   supplies a nine-slot demo that `normalizeState()` also uses as the fallback
   for omitted fields. `defaults.js` is an empty skeleton instead, so a genuinely
   empty device state renders empty rather than fabricated. Intentional: this
   front-end exists to make failures visible.
2. **`/device_page/ams_bridge_debug` is not ported.** Debug-only route.
3. **Assets are copied, not shared.** The React `dist/` hashes and renames its
   assets, so referencing into it would be fragile.
4. **`ext-ams-route` is structural.** `AmsExtOperationPage.tsx` draws the tray →
   nozzle route with computed SVG geometry; this build emits the same class names
   and data attributes but leaves the precise path to CSS. Verify visually before
   relying on it.
5. **`+4` is one text node, not two.** React's `{'+'}{bindCount}` produces two
   adjacent text nodes in the AMS bind badge. Rendered output is identical.

## Verification

### Host contracts

```bash
node src/slic3r/GUI/DeviceWeb/DeviceWebHtml/tools/contract-test.mjs
```

Runs the four host contracts, the business protocol envelopes, state
normalisation, de-duplication and request timeout against a stubbed host - no
browser and no Studio build required.

### Visual parity against the React build

```bash
node src/slic3r/GUI/DeviceWeb/DeviceWebHtml/tools/compare-pages.mjs
```

Serves both documents from one origin, installs a fake WebView2 host that answers
both pages from `fixtures/mockState.mjs`, then for every variant reports the SVG
count, the per-class and per-`data-testid` census diff, and a pixel delta.
Screenshots and a red-highlighted diff image land in `tools/out/`.

Current result:

```
[filament] svg 31/31 | class missing 0 extra 0 mismatch 0 | testid missing 0 | pixels 0 (0.000%)
[hotend]   svg 10/10 | class missing 0 extra 0 mismatch 0 | testid missing 0 | pixels 81 (0.003%)
```

The 81 pixels on the hotend tab are a transition still settling on one temperature
readout; the DOM of all nine hotend cards is byte-identical.

Add a variant to the `VARIANTS` array to cover a new state. The batch modes and
the four sub-routes are **not covered yet** - they need fixtures that put the page
into those modes.

### On a real printer

Enable `enable_device_web_html`, open both dialogs against the same machine, and
walk the state matrix (empty / single AMS / multi AMS / external, the four page
modes, in-progress states, `unknown`/`error`/`reading` slots, two themes, two
languages).

## Build

CMake target `device_page_html_build` copies `web/` to
`resources/web/device_page_html/`. Plain directory copy - no Node, pnpm or Vite,
so it bypasses the toolchain bootstrap in `../CMakeLists.txt` entirely.
`resources/` is installed wholesale on all three platforms, so no packaging
changes are needed.
