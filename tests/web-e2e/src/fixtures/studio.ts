/**
 * Worker-scoped fixtures that own the CDP attachment to the live
 * BambuStudio WebView2.  Built around two principles:
 *
 *   1. One attach per Playwright worker.  Reconnecting once per test would
 *      multiply runtime by an order of magnitude and is unnecessary - the
 *      BambuStudio process is single-instance and stable.
 *
 *   2. Override Playwright's built-in `page` fixture so existing code that
 *      depends on `page` keeps working.  Test-scoped fixtures (filament,
 *      precheck) compose on top of this.
 */
import { test as base, type Browser, type BrowserContext, type Page } from '@playwright/test'
import { attachToWebView2 } from '../helpers/cdp-attach'
import { runPrecheck, type PrecheckSummary } from '../helpers/precheck'
import { runCleanups, resetCleanups } from '../helpers/cleanup'

/**
 * Probe whether the bundle currently loaded in the WebView2 carries the
 * minimum testid surface this framework depends on.  When a developer
 * launched Studio against an older dist (no `filament-tab-all`,
 * `confirm-dialog-cancel`, `detail-dialog-close`...) we trigger one
 * page.reload(): the in-process HTTP server (`DeviceHttpServer.cpp`)
 * always serves the latest dist so reloading is the cheapest way to
 * sync.  Skipped when all probe testids resolve in <1.5s.
 *
 * Probes are URL-anchor preserved: the post-reload URL still starts
 * with `index.html?lang=...#/filament` so attachment stays valid.
 */
const PROBE_TESTIDS = [
  'filament-page-root',
  'filament-tab-all',
  'filament-add',
] as const

async function ensureFreshBundle(page: Page): Promise<void> {
  const probeOk = await Promise.all(
    PROBE_TESTIDS.map((id) => page.getByTestId(id).first().isVisible({ timeout: 1500 }).catch(() => false)),
  )
  if (probeOk.every(Boolean)) return

  console.log('[studio fixture] missing probe testids -> reloading WebView2 page once to pick up fresh dist...')
  await page.reload({ waitUntil: 'domcontentloaded', timeout: 15_000 }).catch(() => undefined)
  await page.getByTestId('filament-page-root').waitFor({ state: 'visible', timeout: 10_000 }).catch(() => undefined)
}

/**
 * Best-effort sweep that closes any modal / popover left over from a
 * previous test.  AddEditDialog and DetailDialog do not bind ESC, so we
 * have to find their footer Cancel buttons by testid and click them.
 * The custom color picker (inside AddEditDialog) DOES respond to outside
 * clicks so it disappears for free once the dialog closes.
 *
 * Errors are swallowed - this is housekeeping, never an assertion.
 */
async function resetUiOverlays(page: Page): Promise<void> {
  // First: any open color picker tray nested inside a dialog.  Clicking
  // outside dismisses it.
  await page.mouse.click(2, 2).catch(() => undefined)
  await page.waitForTimeout(50)

  // Loop: poll for any dialog (add / edit / confirm), click its cancel,
  // and re-check.  Bound the loop so a stuck overlay never hangs.
  for (let i = 0; i < 4; i++) {
    const cancelables = [
      page.getByTestId('dialog-cancel'),
      page.getByTestId('confirm-dialog-cancel'),
      page.getByRole('dialog').getByRole('button', { name: /cancel|取消/i }),
    ]
    let clicked = false
    for (const target of cancelables) {
      const visible = await target.first().isVisible().catch(() => false)
      if (visible) {
        await target.first().click({ timeout: 1500 }).catch(() => undefined)
        clicked = true
        break
      }
    }
    if (!clicked) break
    await page.waitForTimeout(120)
  }

  await page
    .getByTestId('filament-page-root')
    .waitFor({ state: 'visible', timeout: 3_000 })
    .catch(() => undefined)
}

export interface StudioWorkerFixtures {
  /** Active CDP browser handle, valid for the entire worker. */
  studioBrowser: Browser
  studioContext: BrowserContext
  /** The filament manager Page object discovered at attach time. */
  studioPage: Page
}

export interface StudioTestFixtures {
  /** Result of pre-flight checks; specs gate themselves on its flags. */
  precheck: PrecheckSummary
}

/**
 * Resolves the CDP endpoint URL.  Reads in priority order:
 *   1. STUDIO_E2E_CDP_ENDPOINT (full URL)
 *   2. STUDIO_E2E_CDP_PORT env var
 *   3. fallback to 9222
 *
 * We deliberately avoid plumbing this through Playwright's `use.cdpEndpoint`
 * because worker-scoped fixtures cannot consume test-scoped options - the
 * env var is the single source of truth.
 */
function resolveCdpEndpoint(): string {
  if (process.env.STUDIO_E2E_CDP_ENDPOINT) return process.env.STUDIO_E2E_CDP_ENDPOINT
  const port = process.env.STUDIO_E2E_CDP_PORT ?? '9222'
  return `http://127.0.0.1:${port}`
}

export const test = base.extend<StudioTestFixtures, StudioWorkerFixtures>({
  studioBrowser: [
    async ({}, use) => {
      const endpoint = resolveCdpEndpoint()
      const session = await attachToWebView2({ endpoint })
      // Stash the session on the browser handle so dependent fixtures can
      // reach context/page without a second attach round-trip.
      ;(session.browser as Browser & { __session?: typeof session }).__session = session
      await use(session.browser)
      await session.detach()
    },
    { scope: 'worker' },
  ],

  studioContext: [
    async ({ studioBrowser }, use) => {
      const session = (studioBrowser as Browser & { __session?: { context: BrowserContext } })
        .__session
      if (!session) throw new Error('studioContext: session was not stashed on browser handle.')
      await use(session.context)
    },
    { scope: 'worker' },
  ],

  studioPage: [
    async ({ studioBrowser }, use) => {
      const session = (studioBrowser as Browser & { __session?: { page: Page } }).__session
      if (!session) throw new Error('studioPage: session was not stashed on browser handle.')
      await ensureFreshBundle(session.page)
      await use(session.page)
    },
    { scope: 'worker' },
  ],

  // Override Playwright's built-in `page`.  Specs that take `{ page }` will
  // automatically receive the attached WebView2 page.
  //
  // `studioPage` is worker-scoped, so a previous test's failure can leave a
  // modal dialog or color picker popover open that would otherwise occlude
  // the next test's clicks.  Sandwich every test between two reset passes
  // so a fresh test always sees the bare list page:
  //   1. Pre-use:  press ESC up to 3 times, then click outside any sticky
  //                overlay we know about.
  //   2. Cleanups: registered side-effects run in LIFO.
  //   3. Post-use: same ESC sweep so we hand the page back clean even if
  //                an assertion failed mid-flow.
  page: async ({ studioPage }, use) => {
    resetCleanups()
    await resetUiOverlays(studioPage)
    await use(studioPage)
    try {
      await runCleanups()
    } finally {
      await resetUiOverlays(studioPage)
    }
  },

  // Test-scoped precheck.  Cheap (~200 ms) and the result is meaningful per
  // test because the developer might switch tabs between specs.
  precheck: async ({ page }, use) => {
    const summary = await runPrecheck(page)
    await use(summary)
  },
})

export { expect } from '@playwright/test'
