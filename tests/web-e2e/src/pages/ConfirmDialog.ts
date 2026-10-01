/**
 * Page object for the shared ConfirmDialog (ConfirmDialog.tsx) used by
 * delete / batch-delete flows.
 *
 * Tests in this suite must NEVER call `confirm()` on a destructive
 * dialog — that would mutate the developer's filament library.  The
 * helper is exposed for completeness but its name is deliberately
 * verbose to make accidental destructive use stand out in code review.
 */
import type { Locator, Page } from '@playwright/test'
import { BasePage } from './BasePage'

export class ConfirmDialog extends BasePage {
  constructor(page: Page) {
    super(page)
  }

  protected get root(): Locator {
    return this.page.getByTestId('confirm-dialog')
  }

  async waitForReady(): Promise<void> {
    await this.root.waitFor({ state: 'visible', timeout: 5_000 })
  }

  async title(): Promise<string> {
    return (await this.byTestId('confirm-dialog-title').textContent())?.trim() ?? ''
  }

  async isDanger(): Promise<boolean> {
    return (await this.root.getAttribute('data-danger')) === 'true'
  }

  get cancelButton(): Locator {
    return this.byTestId('confirm-dialog-cancel')
  }
  get destructiveConfirmButton(): Locator {
    return this.byTestId('confirm-dialog-confirm')
  }

  async cancel(): Promise<void> {
    await this.cancelButton.click()
    await this.root.waitFor({ state: 'detached', timeout: 5_000 }).catch(() => undefined)
  }

  /**
   * DESTRUCTIVE.  Reserved for specs that explicitly own the cleanup of
   * whatever they create within the same test.  Any spec calling this on
   * a delete confirm dialog must have just created the row that's being
   * confirmed away.
   */
  async confirmDestructive(): Promise<void> {
    await this.destructiveConfirmButton.click()
    await this.root.waitFor({ state: 'detached', timeout: 5_000 }).catch(() => undefined)
  }
}
