/**
 * @filament-baseline @webview-only
 *
 * The Add dialog has two top-level tabs (manual / ams).  This spec
 * verifies the tab switch is reflected in `data-active` on both tab
 * elements and that tab content is conditionally rendered:
 *   - manual tab shows brand/material selects
 *   - ams tab shows the ams-grid container (or, if no AMS exists in
 *     the developer's account, the grid is at least mounted)
 *
 * No data is created — the dialog is cancelled at the end.
 */
import { test, expect } from '../src/fixtures/filament'

test.describe('Add dialog tab switching @filament-baseline @webview-only', () => {
  test('manual <-> ams tab toggles data-active and surfaces tab content', async ({
    filamentList,
    filamentAdd,
  }) => {
    const dialog = await filamentList.openAddDialog()

    await dialog.switchTab('manual')
    expect(await filamentAdd.currentTab()).toBe('manual')
    await expect(dialog.brandSelect).toBeVisible()

    await dialog.switchTab('ams')
    expect(await filamentAdd.currentTab()).toBe('ams')
    // ams-grid may show an empty state when no AMS data is present in
    // the account; we only require it to mount within a reasonable
    // budget — the grid container itself is the contract.
    await expect(dialog.amsGrid).toBeAttached({ timeout: 5_000 })

    await dialog.switchTab('manual')
    expect(await filamentAdd.currentTab()).toBe('manual')

    await dialog.cancel()
  })
})
