/**
 * @filament-baseline @webview-only
 *
 * Schema-style invariants over rendered spool rows.  Catches DOM
 * regressions that downstream POMs depend on — every row MUST surface:
 *   - data-color-code (non-empty hex such as "#3F8BFF" OR a comma-list
 *     for legacy multicolor rows; we only assert non-empty here)
 *   - data-color-type ∈ {0, 1, 2}
 *   - filament-row-name with non-empty text
 *
 * This spec is the cheapest tripwire against an accidental breaking
 * change to SpoolTable / formatSpoolDisplayName / FilamentColor.
 */
import { test, expect } from '../src/fixtures/filament'

test.describe('row attribute invariants @filament-baseline @webview-only', () => {
  test('every visible spool row carries the metadata POMs depend on', async ({ filamentList }) => {
    const rows = await filamentList.readRowAttributes()
    test.skip(rows.length === 0, 'No rows to assert against.')

    for (const row of rows) {
      expect(row.colorCode, `row ${row.id} data-color-code must be non-empty`).not.toBe('')
      expect(['0', '1', '2'], `row ${row.id} data-color-type must be 0|1|2`).toContain(row.colorType)
      expect(row.name, `row ${row.id} display name must be non-empty`).not.toBe('')
    }
  })
})
