/**
 * The filament manager list page (the entry surface).
 *
 * Usage:
 *
 *   const list = new FilamentListPage(page)
 *   await list.waitForReady()
 *   await list.search('PLA')
 *   const dialog = await list.openAddDialog()
 *
 * Keep this class shallow: anything that lives inside a dialog opened FROM
 * the list belongs in the dialog's POM, not here.
 */
import type { Locator, Page } from '@playwright/test'
import { BasePage } from './BasePage'
import { FilamentAddDialog } from './FilamentAddDialog'
import { FilamentEditDialog } from './FilamentEditDialog'

export class FilamentListPage extends BasePage {
  constructor(page: Page) {
    super(page)
  }

  protected get root(): Locator {
    return this.page.getByTestId('filament-page-root')
  }

  // ------------------------------------------------------------ readiness
  async waitForReady(): Promise<void> {
    await this.root.waitFor({ state: 'visible', timeout: 15_000 })
  }

  // ------------------------------------------------------------ toolbar
  get addButton(): Locator {
    return this.byTestId('filament-add')
  }

  get searchBox(): Locator {
    return this.byTestId('filament-search')
  }

  get groupButton(): Locator {
    return this.byTestId('filament-group-toggle')
  }

  get pushAllButton(): Locator {
    return this.byTestId('filament-push-all')
  }

  // ------------------------------------------------------------ tabs
  tabAll(): Locator {
    return this.byTestId('filament-tab-all')
  }
  tabAms(): Locator {
    return this.byTestId('filament-tab-ams')
  }

  /**
   * Returns the currently active tab id, derived from `data-active` on the
   * two tab buttons.  Defaults to 'all' when neither is marked active
   * (shouldn't happen but keeps the assertion path simple).
   */
  async currentTab(): Promise<'all' | 'ams'> {
    const amsActive = await this.tabAms().getAttribute('data-active').catch(() => 'false')
    return amsActive === 'true' ? 'ams' : 'all'
  }

  async switchTab(tab: 'all' | 'ams'): Promise<void> {
    const target = tab === 'all' ? this.tabAll() : this.tabAms()
    await target.click()
    await this.page.waitForFunction(
      ({ which }) => {
        const el = document.querySelector(`[data-testid="filament-tab-${which}"]`)
        return el?.getAttribute('data-active') === 'true'
      },
      { which: tab },
      { timeout: 3_000 },
    ).catch(() => undefined)
  }

  // ------------------------------------------------------------ group toggle
  async isGrouped(): Promise<boolean> {
    return (await this.groupButton.getAttribute('data-grouped')) === 'true'
  }

  async toggleGroup(): Promise<void> {
    const before = await this.isGrouped()
    await this.groupButton.click()
    await this.page.waitForFunction(
      (was) => {
        const el = document.querySelector('[data-testid="filament-group-toggle"]')
        return el?.getAttribute('data-grouped') !== String(was)
      },
      before,
      { timeout: 3_000 },
    ).catch(() => undefined)
  }

  async setGrouped(grouped: boolean): Promise<void> {
    if (await this.isGrouped() !== grouped) {
      await this.toggleGroup()
    }
  }

  async readGroupSummaries(): Promise<Array<{ key: string; count: number; totalWeight: number }>> {
    const groups = await this.root.getByTestId('filament-group-row').all()
    const out: Array<{ key: string; count: number; totalWeight: number }> = []
    for (const group of groups) {
      const key = (await group.getAttribute('data-group-key'))?.trim() ?? ''
      const count = Number(await group.getAttribute('data-group-count')) || 0
      const totalWeight = Number(await group.getAttribute('data-group-weight')) || 0
      out.push({ key, count, totalWeight })
    }
    return out
  }

  // ------------------------------------------------------------ rows
  /**
   * Locator that resolves to all rendered spool rows.
   *
   * IMPORTANT: scope to `<tr>` because the prefix `filament-row-` is also
   * used by per-row sub-elements (`filament-row-color`, `-name`, `-detail`,
   * `-add-similar`, `-delete`).  A naive `[data-testid^="filament-row-"]`
   * would over-match those buttons and break invariant assertions.  The
   * row itself is the only `<tr>` carrying that prefix.
   */
  get rows(): Locator {
    return this.root.locator('tr[data-testid^="filament-row-"]')
  }

  rowById(id: string): Locator {
    return this.byTestId(`filament-row-${id}`)
  }

  /** Reads visible rows into a lightweight snapshot for assertions. */
  async listRowSummaries(): Promise<Array<{ id: string; name: string; colorCode: string }>> {
    const rows = await this.rows.all()
    const out: Array<{ id: string; name: string; colorCode: string }> = []
    for (const row of rows) {
      const tid = await row.getAttribute('data-testid')
      const id = tid?.replace(/^filament-row-/, '') ?? ''
      const name = (await row.getByTestId('filament-row-name').textContent().catch(() => ''))?.trim() ?? ''
      const colorCode =
        (await row.getByTestId('filament-row-color').getAttribute('data-color-code').catch(() => ''))?.trim() ?? ''
      out.push({ id, name, colorCode })
    }
    return out
  }

  async search(text: string): Promise<void> {
    await this.searchBox.fill(text)
  }

  async clearSearch(): Promise<void> {
    await this.searchBox.fill('')
  }

  /** Counts only spool rows: groups have no `filament-row-<id>` testid. */
  async rowCount(): Promise<number> {
    return this.rows.count()
  }

  /**
   * Reads the currently rendered rows into a snapshot of all the attributes
   * the framework relies on.  This is what the row-invariants spec asserts
   * against — keeping it in one place means the contract is documented
   * exactly once.
   */
  async readRowAttributes(): Promise<
    Array<{ id: string; name: string; colorCode: string; colorType: string }>
  > {
    const rows = await this.rows.all()
    const out: Array<{ id: string; name: string; colorCode: string; colorType: string }> = []
    for (const row of rows) {
      const tid = await row.getAttribute('data-testid')
      const id = tid?.replace(/^filament-row-/, '') ?? ''
      const colorCode = (await row.getAttribute('data-color-code'))?.trim() ?? ''
      const colorType = (await row.getAttribute('data-color-type'))?.trim() ?? ''
      const name = (await row.getByTestId('filament-row-name').textContent().catch(() => ''))?.trim() ?? ''
      out.push({ id, name, colorCode, colorType })
    }
    return out
  }

  // ---------------------------------------------------------- row actions
  async openDetailFor(rowId: string): Promise<void> {
    await this.rowById(rowId).getByTestId('filament-row-detail').click()
    await this.page.getByTestId('detail-dialog').waitFor({ state: 'visible', timeout: 5_000 })
  }

  async openAddSimilarFor(rowId: string): Promise<FilamentAddDialog> {
    await this.rowById(rowId).getByTestId('filament-row-add-similar').click()
    const dialog = new FilamentAddDialog(this.page)
    await dialog.waitForReady()
    return dialog
  }

  /**
   * Click the row's delete trash icon.  The list does NOT delete on click —
   * a `confirm-dialog` appears that the spec is responsible for cancelling.
   * This contract keeps every spec on the safe side: real deletion only
   * happens when both the trash icon AND the confirm button are clicked.
   */
  async clickDeleteFor(rowId: string): Promise<void> {
    await this.rowById(rowId).getByTestId('filament-row-delete').click()
    await this.page.getByTestId('confirm-dialog').waitFor({ state: 'visible', timeout: 5_000 })
  }

  // ------------------------------------------------------------ navigation
  async openAddDialog(): Promise<FilamentAddDialog> {
    await this.addButton.click()
    const dialog = new FilamentAddDialog(this.page)
    await dialog.waitForReady()
    return dialog
  }

  /**
   * Opening the edit dialog goes through the detail surface today: list
   * row -> "Spool Detail" -> "Edit".  We expose a single helper so specs
   * do not have to know about the intermediate dialog.
   */
  async openEditDialogFor(rowId: string): Promise<FilamentEditDialog> {
    await this.rowById(rowId).getByTestId('filament-row-detail').click()
    await this.page.getByTestId('detail-dialog-edit').click()
    const dialog = new FilamentEditDialog(this.page)
    await dialog.waitForReady()
    return dialog
  }
}
