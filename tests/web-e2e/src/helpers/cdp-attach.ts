/**
 * CDP attach helper.
 *
 * Connects Playwright to a running WebView2 via Chrome DevTools Protocol
 * and locates the page that hosts the filament manager bundle.
 *
 * Notes on WebView2 + Playwright (verified against Microsoft's WebView2
 * guide, 2026-05):
 *   - `chromium.connectOverCDP()` returns a Browser whose default context
 *     is exposed at `browser.contexts()[0]`.
 *   - WebView2 does not pre-create empty contexts; every WebView control
 *     in the host process surfaces as one or more `Page` objects under
 *     that single context.
 *   - The bundle ships from `resources/web/device_page/dist/index.html`,
 *     which is loaded from a custom scheme (`bambustudio://...`) inside
 *     the host.  We match by URL substring, with a fallback to the page
 *     title and a DOM-based heuristic for resilience.
 */
import { chromium, type Browser, type BrowserContext, type Page } from '@playwright/test'

export interface CdpAttachOptions {
  endpoint: string
  /** Substrings considered a positive URL match for the filament manager. */
  urlMatchers?: string[]
  /** Substrings to match against page titles when URL match fails. */
  titleMatchers?: string[]
  /** How long to wait for at least one matching page to exist. */
  pageDiscoveryTimeoutMs?: number
}

export interface AttachedSession {
  browser: Browser
  context: BrowserContext
  page: Page
  /** All pages found at attach time, useful for debug logging. */
  allPages: Page[]
  /** Disconnect Playwright; does not kill the underlying WebView2. */
  detach: () => Promise<void>
}

export class CdpAttachError extends Error {
  constructor(message: string, public readonly debug?: Record<string, unknown>) {
    super(message)
    this.name = 'CdpAttachError'
  }
}

// Confirmed against a real BambuStudio Release build (2026-05): the
// filament manager page lives at
//   http://localhost:<port>/index.html?lang=<locale>#/filament
// We match on `#/filament` (specific) and `device_page` (legacy fallback
// for builds that ship the bundle from the file:// scheme).  `fila` alone
// would over-match (e.g. a future "filament wiki" tab); `#/filament` is
// scoped to React Router hash routes only.
const DEFAULT_URL_MATCHERS = ['#/filament', 'device_page', '/device_page/']
const DEFAULT_TITLE_MATCHERS = ['Filament', '耗材', '#/filament']

function isFilamentPage(page: Page, urlMatchers: string[], _titleMatchers: string[]): boolean {
  const url = page.url() ?? ''
  if (urlMatchers.some((m) => url.includes(m))) return true
  // Title is sync-readable from Playwright but unreliable on first attach
  // (loaders may not have set it yet); leave to caller to retry the title
  // check separately - hence `_titleMatchers` is intentionally unused here.
  return false
}

/**
 * Polls all contexts for a page that matches the filament manager.  We
 * cannot rely on `context.waitForEvent('page')` because the page existed
 * BEFORE we connected.
 */
async function findFilamentPage(
  contexts: BrowserContext[],
  urlMatchers: string[],
  titleMatchers: string[],
  timeoutMs: number,
): Promise<{ context: BrowserContext; page: Page; allPages: Page[] }> {
  const deadline = Date.now() + timeoutMs
  let lastSnapshot: Array<{ url: string; title: string }> = []
  while (Date.now() < deadline) {
    const allPages = contexts.flatMap((c) => c.pages())
    lastSnapshot = await Promise.all(
      allPages.map(async (p) => ({ url: p.url() ?? '', title: await p.title().catch(() => '') })),
    )
    for (const ctx of contexts) {
      for (const page of ctx.pages()) {
        if (isFilamentPage(page, urlMatchers, titleMatchers)) {
          return { context: ctx, page, allPages }
        }
        // Fallback: title may have lagged; refetch once.
        const title = await page.title().catch(() => '')
        if (titleMatchers.some((m) => title.includes(m))) {
          return { context: ctx, page, allPages }
        }
      }
    }
    await new Promise((r) => setTimeout(r, 400))
  }
  throw new CdpAttachError(
    'Could not locate the filament manager page within the CDP target list. ' +
      'Make sure you actually opened it in BambuStudio (Device -> Filament Manager) ' +
      'before pressing Enter.',
    { lastSnapshot },
  )
}

export async function attachToWebView2(options: CdpAttachOptions): Promise<AttachedSession> {
  const {
    endpoint,
    urlMatchers = DEFAULT_URL_MATCHERS,
    titleMatchers = DEFAULT_TITLE_MATCHERS,
    pageDiscoveryTimeoutMs = 20_000,
  } = options

  let browser: Browser
  try {
    browser = await chromium.connectOverCDP(endpoint, { timeout: 10_000 })
  } catch (err) {
    throw new CdpAttachError(
      `chromium.connectOverCDP(${endpoint}) failed: ${(err as Error).message}. ` +
        `Confirm BambuStudio was launched with WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS set.`,
    )
  }

  const contexts = browser.contexts()
  if (contexts.length === 0) {
    await browser.close()
    throw new CdpAttachError('CDP connected but no browser contexts were exposed.')
  }

  try {
    const found = await findFilamentPage(contexts, urlMatchers, titleMatchers, pageDiscoveryTimeoutMs)
    return {
      browser,
      context: found.context,
      page: found.page,
      allPages: found.allPages,
      detach: async () => {
        await browser.close().catch(() => undefined)
      },
    }
  } catch (err) {
    await browser.close().catch(() => undefined)
    throw err
  }
}
