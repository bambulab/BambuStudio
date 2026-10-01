/**
 * @filament-baseline @webview-only
 *
 * Tab switching between "All" and "AMS" updates the active tab marker
 * and filters down to entry_method=='ams_sync' rows.  We never assert
 * an exact row count for the AMS tab (depends on the developer's
 * library) but we DO assert it is ≤ All tab's row count and that the
 * tab marker flips.
 */
import { test, expect } from '../src/fixtures/filament'

test.describe('filament tab switch @filament-baseline @webview-only', () => {
  test('All <-> AMS tab toggles data-active and never grows row count', async ({ filamentList }) => {
    await filamentList.switchTab('all')
    expect(await filamentList.currentTab()).toBe('all')
    const allCount = await filamentList.rowCount()

    await filamentList.switchTab('ams')
    expect(await filamentList.currentTab()).toBe('ams')
    const amsCount = await filamentList.rowCount()
    expect(amsCount, 'AMS-only rows must be a subset of All').toBeLessThanOrEqual(allCount)

    // Restore to 'all' so subsequent specs see the default surface.
    await filamentList.switchTab('all')
    expect(await filamentList.currentTab()).toBe('all')
  })
})
