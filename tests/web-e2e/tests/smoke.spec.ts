/**
 * @smoke @webview-only
 *
 * The minimum sanity check for an attached BambuStudio session: the
 * filament manager loads, exposes its root testid, and a few baseline
 * UI affordances are visible.  Every other spec depends on this passing.
 */
import { test, expect } from '../src/fixtures/filament'

test.describe('smoke @smoke @webview-only', () => {
  test('filament manager surface is reachable', async ({ filamentList, precheck }) => {
    expect(precheck.flags.onFilamentPage, 'precheck.onFilamentPage').toBe(true)
    await expect(filamentList.addButton).toBeVisible()
    await expect(filamentList.searchBox).toBeVisible()
    await expect(filamentList.groupButton).toBeVisible()
  })

  test('opening + closing the add dialog is non-destructive', async ({ filamentList }) => {
    const dialog = await filamentList.openAddDialog()
    await expect(dialog.brandSelect).toBeVisible()
    await expect(dialog.materialSelect).toBeVisible()
    await dialog.cancel()
    await expect(filamentList.addButton).toBeVisible()
  })
})
