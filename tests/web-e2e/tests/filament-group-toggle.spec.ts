/**
 * @filament-baseline @webview-only
 *
 * The group toggle in the filament list toolbar: clicking it must flip
 * `data-grouped`.  Clicking again must restore it.  This is a basic
 * stateful UI control regression — a previous refactor regressed it
 * twice during STUDIO-18126, so a 2-line spec is cheap insurance.
 */
import { test, expect } from '../src/fixtures/filament'

test.describe('filament group toggle @filament-baseline @webview-only', () => {
  test('clicking group toggle flips data-grouped and back', async ({ filamentList }) => {
    const initial = await filamentList.isGrouped()

    await filamentList.toggleGroup()
    expect(await filamentList.isGrouped()).toBe(!initial)

    await filamentList.toggleGroup()
    expect(await filamentList.isGrouped()).toBe(initial)
  })
})
