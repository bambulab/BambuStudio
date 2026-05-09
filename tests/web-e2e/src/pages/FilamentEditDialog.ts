/**
 * Edit-mode wrapper around AddEditDialog.  Inherits the full surface of
 * `FilamentAddDialog` and overrides the testid root so callers do not
 * have to think about which mode they are in: a spec writes
 *
 *     const edit = await list.openEditDialogFor('sp-2')
 *     await edit.selectBrand('Bambu Lab')
 *
 * Internally edit / add are the same React component but rendered with
 * `data-testid="edit-dialog"` when `initSpool` is set.
 */
import type { Locator, Page } from '@playwright/test'
import { FilamentAddDialog } from './FilamentAddDialog'

export class FilamentEditDialog extends FilamentAddDialog {
  constructor(page: Page) {
    super(page)
  }

  protected override get root(): Locator {
    return this.page.getByTestId('edit-dialog')
  }
}
