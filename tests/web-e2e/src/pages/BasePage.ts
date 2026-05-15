/**
 * Base class for every page object in this suite.
 *
 * Conventions enforced from here:
 *
 *   - Selectors are always `data-testid` based; never rely on text or
 *     CSS class names that change with i18n or design refactors.
 *
 *   - Public methods on POM subclasses describe USER intent (open dialog,
 *     pick candidate) rather than mechanical actions (click locator X).
 *     Specs read like prose, easier to grow into the long term.
 *
 *   - POMs do NOT assert.  They expose state (locator, scalar value) and
 *     the spec performs assertions through `expect`.  Mixing the two
 *     couples specs to UX details that should evolve.
 */
import type { Locator, Page } from '@playwright/test'

export abstract class BasePage {
  constructor(public readonly page: Page) {}

  /** A locator scoped to this page's outermost data-testid root. */
  protected abstract get root(): Locator

  /** Override per-page with whatever proves the surface is ready. */
  abstract waitForReady(): Promise<void>

  /** Helper for tests/POMs to grab a child by testid under this page. */
  byTestId(id: string): Locator {
    return this.root.getByTestId(id)
  }

  /** Convenience: capture a debugging screenshot to test-results/. */
  async snapshot(name: string): Promise<void> {
    await this.page.screenshot({
      path: `test-results/screenshots/${Date.now()}-${name}.png`,
      fullPage: true,
    })
  }
}
