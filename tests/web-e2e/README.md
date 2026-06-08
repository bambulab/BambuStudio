# BambuStudio Filament Manager · Web E2E

End-to-end tests for the BambuStudio filament manager **webview**, driven
by Playwright over the Chrome DevTools Protocol exposed by WebView2.

> Scope: only the embedded React app under
> `resources/web/device_page/src/features/filament/`.  This suite does
> **not** drive Win32 menus or the BambuStudio main window — the
> developer is expected to log in and open the filament manager
> themselves before each run.

---

## TL;DR — running a suite

```powershell
# from the repo root
cd tests\web-e2e
pnpm install
pnpm exec playwright install chromium    # one-off
copy .env.example .env.local              # then edit if needed

# interactive launcher (recommended)
pnpm e2e
```

Power users can skip the launcher and call Playwright directly:

```powershell
# 1. start BambuStudio yourself with CDP enabled
$env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = "--remote-debugging-port=9222 --remote-allow-origins=*"
& "..\..\build_release\src\Release\bambu-studio.exe"

# 2. log in + open Device → Filament Manager

# 3. attach + run from another terminal in tests/web-e2e
pnpm e2e:run --grep @smoke
```

---

## P0 — verify the WebView2 CDP path before anything else

The whole framework hinges on BambuStudio honouring the
`WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS` environment variable.  The
`scripts/check-cdp.ps1` helper proves this end-to-end:

```powershell
cd tests\web-e2e
pnpm e2e:check-cdp
```

Outcomes:

| exit | meaning | next step |
| ---- | ------- | --------- |
| 0    | `[PASS]` filament page detected on CDP | proceed with `pnpm e2e` |
| 1    | CDP works but filament page heuristic missed it | confirm you opened the manager; the URL pattern in `src/helpers/cdp-attach.ts` may need tweaking |
| 2    | bambu-studio.exe not found | set `STUDIO_E2E_BIN` in `.env.local` |
| 3    | port already in use | pick a free `STUDIO_E2E_CDP_PORT` |
| 4    | port silent | BambuStudio overrides the env var; needs a 1-line C++ patch in the webview creator (see `docs/cdp-fallback.md` if/when added) |
| 5    | port open but `/json/version` failed | rare — capture the response and file an issue |

---

## Layout

```
tests/web-e2e/
├── package.json           pnpm workspace, scripts entry point
├── playwright.config.ts   suite, project, retry, reporter config
├── tsconfig.json
├── .env.example           shared defaults; copy to .env.local
├── scripts/
│   └── check-cdp.ps1      P0 verification (no Playwright dependency)
├── src/
│   ├── cli/
│   │   └── launch.ts      `pnpm e2e` entry — interactive 4-step flow
│   ├── fixtures/
│   │   ├── studio.ts      worker-scoped CDP attach + page override
│   │   └── filament.ts    POM injection on top of studio.ts
│   ├── helpers/
│   │   ├── studio-launcher.ts    spawns bambu-studio.exe with env var
│   │   ├── cdp-attach.ts         connectOverCDP + finds filament page
│   │   ├── precheck.ts           non-fatal preflight signals
│   │   └── cleanup.ts            LIFO cleanup queue per test
│   └── pages/
│       ├── BasePage.ts
│       ├── FilamentListPage.ts
│       ├── FilamentAddDialog.ts
│       └── FilamentEditDialog.ts
├── tests/                 spec files; one per business surface
│   ├── smoke.spec.ts
│   ├── filament-color-query.spec.ts
│   ├── filament-fallback-panel.spec.ts
│   └── filament-brand-switch.spec.ts
└── reports/               (gitignored) HTML report + trace files
```

---

## Tagging convention

Specs and `test.describe` blocks include tags in their titles so a
single repo can host suites that need different runtime conditions:

| tag             | meaning                                         | grep example |
| --------------- | ----------------------------------------------- | ------------ |
| `@smoke`        | < 2 min sanity check                            | `--grep @smoke` |
| `@webview-only` | runs against the webview alone, no cloud/printer needed | `--project=webview-only` |
| `@cloud`        | requires cloud reachability (login + sync)      | `--project=cloud` |
| `@printer`      | requires an online printer + AMS                | `--project=printer` |
| `@studio-17977` | scoped to the multicolor / gradient feature     | `--grep @studio-17977` |
| `@destructive`  | mutates persistent cloud state                  | use carefully |

Always attach `@webview-only` (or a stricter tag) so the launcher's
"webview-only" project filter sees the spec.

---

## Adding a new spec — checklist

1. **Title carries every applicable tag**, e.g.
   `test('multicolor adds correctly @studio-17977 @webview-only', ...)`.
2. **Use the filament fixture** (`import { test, expect } from '../src/fixtures/filament'`).
3. **Drive the UI through POMs**, not raw locators.  If the POM is
   missing a method, extend the POM rather than reaching into the page.
4. **Add `data-testid`** to any new DOM your spec touches; never depend
   on text content.
5. **Mutating state?**  Use `registerCleanup()` to remove the row, push
   a revert, or otherwise return the account to its pre-test state.
6. **Run only the new spec** via `pnpm e2e:run --grep your-tag` while
   iterating; only widen scope after green.
7. **Land the data-testid changes alongside the spec** so the next
   developer never opens a PR with a hanging selector.

---

## Troubleshooting

| symptom | diagnosis |
| ------- | --------- |
| `chromium.connectOverCDP failed` | BambuStudio not running, or port blocked / already taken |
| `Could not locate the filament manager page` | you forgot to navigate to Device → Filament Manager before pressing Enter |
| `data-testid="filament-page-root" not found` | dist bundle is stale; run `pnpm build` in `resources/web/device_page` and restart Studio |
| every spec marked skipped | check the precheck banner in the run output — typically not signed in |
| spec passes locally then flakes | CDP attaches once per worker; never call `browser.close()` in a test |

---

## Long-term maintenance contract

- The framework is **opt-in**: no automatic invocation in CI, no PR gate.
- New surface in the manager → new POM + new specs.  Both go in the
  same PR; we do not ship "test selectors only" PRs.
- Dev-only mock bridge (`resources/web/device_page/src/dev/mockBridge.ts`)
  is **not** used by this suite — it targets the real C++ webview.
- `playwright.config.ts` keeps `workers: 1` and `fullyParallel: false`
  on purpose: a single live WebView2 cannot fan out.
