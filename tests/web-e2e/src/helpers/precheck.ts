/**
 * Pre-flight checks the framework runs after attaching to the filament
 * manager but before the first spec executes.  Each check returns either
 * pass / fail / unknown and a human-readable message; specs can opt out
 * of running by checking the precheck result with `test.skip(...)`.
 *
 * The checks intentionally make few assumptions about the data state -
 * we only fail-fast on signals that imply the entire suite cannot run.
 */
import type { Page } from '@playwright/test'

export type CheckStatus = 'pass' | 'fail' | 'unknown'

export interface CheckResult {
  name: string
  status: CheckStatus
  detail: string
}

export interface PrecheckSummary {
  results: CheckResult[]
  fatalCount: number
  /** Aggregate flags consumed by spec-level test.skip() guards. */
  flags: {
    loggedIn: boolean
    onFilamentPage: boolean
    cloudReachable: boolean
    printerOnline: boolean
  }
}

/**
 * Helper: prefer data-testid selectors so checks survive i18n.  The few
 * legacy fallbacks live in this file only; specs should always use POMs.
 */
async function exists(page: Page, testId: string): Promise<boolean> {
  return page.getByTestId(testId).first().isVisible().catch(() => false)
}

async function checkOnFilamentPage(page: Page): Promise<CheckResult> {
  const ok = await exists(page, 'filament-page-root')
  return {
    name: 'on-filament-page',
    status: ok ? 'pass' : 'fail',
    detail: ok
      ? 'Filament manager root visible.'
      : 'data-testid="filament-page-root" not found.  Make sure you navigated to ' +
        'Device -> Filament Manager and the page finished loading.',
  }
}

async function checkLoggedIn(page: Page): Promise<CheckResult> {
  // The list root only mounts after auth gating; a separate signed-in marker
  // gives us a more precise signal.  Treat it as informative rather than
  // fatal so anonymous-mode flows can still run @webview-only specs.
  const signedIn = await exists(page, 'auth-signed-in')
  if (signedIn) return { name: 'logged-in', status: 'pass', detail: 'Signed-in marker visible.' }
  const signedOut = await exists(page, 'auth-signed-out')
  if (signedOut) {
    return {
      name: 'logged-in',
      status: 'fail',
      detail: 'Signed-out banner visible.  Cloud and printer specs will be skipped.',
    }
  }
  return {
    name: 'logged-in',
    status: 'unknown',
    detail: 'No auth marker found; assuming logged-in for @webview-only runs.',
  }
}

async function checkCloudReachable(page: Page): Promise<CheckResult> {
  const ok = await exists(page, 'cloud-status-online')
  if (ok) return { name: 'cloud-reachable', status: 'pass', detail: 'Cloud sync badge online.' }
  const offline = await exists(page, 'cloud-status-offline')
  if (offline) {
    return {
      name: 'cloud-reachable',
      status: 'fail',
      detail: 'Cloud sync badge offline.  @cloud specs will be skipped.',
    }
  }
  return { name: 'cloud-reachable', status: 'unknown', detail: 'Cloud status badge not detected.' }
}

async function checkPrinterOnline(page: Page): Promise<CheckResult> {
  const ok = await exists(page, 'printer-online-marker')
  if (ok) return { name: 'printer-online', status: 'pass', detail: 'At least one printer online.' }
  return {
    name: 'printer-online',
    status: 'unknown',
    detail: 'No data-testid="printer-online-marker" detected; @printer specs will be skipped.',
  }
}

export async function runPrecheck(page: Page): Promise<PrecheckSummary> {
  const results: CheckResult[] = []
  const filaPage = await checkOnFilamentPage(page)
  results.push(filaPage)
  const login = await checkLoggedIn(page)
  results.push(login)
  const cloud = await checkCloudReachable(page)
  results.push(cloud)
  const printer = await checkPrinterOnline(page)
  results.push(printer)

  const fatalCount = results.filter((r) => r.status === 'fail' && r.name === 'on-filament-page').length
  return {
    results,
    fatalCount,
    flags: {
      loggedIn: login.status === 'pass',
      onFilamentPage: filaPage.status === 'pass',
      cloudReachable: cloud.status === 'pass',
      printerOnline: printer.status === 'pass',
    },
  }
}
