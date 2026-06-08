/**
 * @filament-baseline @webview-only
 *
 * Non-mutating regression for the filament library search box.
 *
 * Contract:
 *   - typing a substring of an existing row's display name reduces (or
 *     keeps equal) the visible row count, and every remaining row's
 *     display name still contains the substring (case-insensitive)
 *   - clearing the search box restores the full row count
 *
 * The test auto-skips on libraries with fewer than 2 rows because there
 * is nothing meaningful to filter against.
 */
import { test, expect } from '../src/fixtures/filament'

test.describe('filament list search @filament-baseline @webview-only', () => {
  test('typing a substring filters rows; clearing restores them', async ({ filamentList }) => {
    const before = await filamentList.readRowAttributes()
    test.skip(before.length < 2, `Library has ${before.length} rows; need ≥2 to test filtering.`)

    // Pick the longest single token from the first row's name to maximise
    // the chance of a non-trivial filter (very short substrings tend to
    // match every row).
    const firstName = before[0]!.name.split(/\s+/).find((w) => w.length >= 3) ?? before[0]!.name.slice(0, 3)
    const probe = firstName.toLowerCase()

    await filamentList.search(probe)
    await filamentList.page.waitForTimeout(150) // React debounce safety; search is sync but allow render commit.

    const after = await filamentList.readRowAttributes()
    expect(after.length, `filtering by "${probe}" should not blow up the list`).toBeGreaterThan(0)
    expect(after.length, 'filtered list should be ≤ original').toBeLessThanOrEqual(before.length)

    // Every surviving row's name must contain the probe (case-insensitive)
    // — that is the contract the search promises.
    for (const row of after) {
      expect(
        row.name.toLowerCase().includes(probe),
        `row "${row.name}" should match search probe "${probe}"`,
      ).toBe(true)
    }

    await filamentList.clearSearch()
    await filamentList.page.waitForTimeout(150)

    const restored = await filamentList.readRowAttributes()
    expect(restored.length, 'clearing search should restore the full list').toBe(before.length)
  })
})
