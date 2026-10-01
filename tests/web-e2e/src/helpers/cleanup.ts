/**
 * Per-test cleanup helpers.
 *
 * Specs that mutate persistent state (add/edit/remove spools, push to cloud)
 * MUST register a cleanup callback through `registerCleanup()` and the
 * filament fixture will run them in reverse order on teardown.  This keeps
 * the developer's spool library identical before and after a test run -
 * no ad-hoc janitor scripts.
 */

export type CleanupFn = () => Promise<void> | void

interface CleanupState {
  fns: CleanupFn[]
}

const state: CleanupState = { fns: [] }

export function registerCleanup(fn: CleanupFn): void {
  state.fns.push(fn)
}

/**
 * Runs every registered cleanup in LIFO order.  Errors are collected and
 * re-thrown together so a failing cleanup does not mask later ones.
 */
export async function runCleanups(): Promise<void> {
  const errors: unknown[] = []
  while (state.fns.length > 0) {
    const fn = state.fns.pop()!
    try {
      await fn()
    } catch (err) {
      errors.push(err)
    }
  }
  if (errors.length > 0) {
    throw new AggregateError(errors as Error[], `Cleanup ran with ${errors.length} failure(s)`)
  }
}

/** Test helper: clear the cleanup queue without running anything (for fixture init). */
export function resetCleanups(): void {
  state.fns.length = 0
}
