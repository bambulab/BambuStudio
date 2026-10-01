/**
 * Page object for the Spool Detail dialog (DetailDialog.tsx).
 *
 * The dialog is read-only on its own — every mutating action (Edit, Prev,
 * Next) launches a separate surface.  Specs are expected to use this POM
 * to assert that the static fields render and to close the dialog cleanly
 * with `close()`.  We deliberately do NOT expose the Edit button here so
 * a spec that wants to traverse to the edit dialog goes through
 * `FilamentListPage.openEditDialogFor()` — that path is the contract.
 */
import type { Locator, Page } from '@playwright/test'
import { BasePage } from './BasePage'

export class FilamentDetailDialog extends BasePage {
  constructor(page: Page) {
    super(page)
  }

  protected get root(): Locator {
    return this.page.getByTestId('detail-dialog')
  }

  async waitForReady(): Promise<void> {
    await this.root.waitFor({ state: 'visible', timeout: 5_000 })
  }

  async getSpoolId(): Promise<string> {
    return (await this.root.getAttribute('data-spool-id')) ?? ''
  }

  /** The Edit button is exposed as a locator for assertions only. */
  get editButton(): Locator {
    return this.byTestId('detail-dialog-edit')
  }

  get closeButton(): Locator {
    return this.byTestId('detail-dialog-close')
  }

  async close(): Promise<void> {
    await this.closeButton.click()
    await this.root.waitFor({ state: 'detached', timeout: 5_000 }).catch(() => undefined)
  }
}
