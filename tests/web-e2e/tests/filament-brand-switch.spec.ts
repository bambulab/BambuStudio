/**
 * @studio-17977 @webview-only
 *
 * Requirement 4 of STUDIO-17977: when brand or material changes mid-session,
 * the form's selected color MUST snap to a candidate that is valid for the
 * new (brand, material) combination - never leave a stale color from the
 * previous selection.  This regressed twice during development; pinning it
 * down with an automated check.
 *
 * Strategy:
 *   1. select Brand A + Material A, pick a candidate, capture color triple
 *   2. switch Material to a different one under the same brand
 *   3. assert the new preview bar is consistent with the new candidate
 *      list, i.e. preview.colorCode is found in the new candidates.
 */
import { test, expect } from '../src/fixtures/filament'

test.describe('STUDIO-17977 brand/material switch alignment @studio-17977 @webview-only', () => {
  test('switching material rebases the color to a valid candidate', async ({ filamentList }) => {
    const dialog = await filamentList.openAddDialog()
    await dialog.switchTab('manual')

    // Pick a brand that has at least 2 different materials in the account.
    await dialog.selectBrand('Bambu Lab')
    const materials = await dialog.materialSelect.locator('option').allTextContents()
    const realMaterials = materials.filter(
      (m) => m && !/^Select/i.test(m) && !/Brand First/i.test(m),
    )
    if (realMaterials.length < 2) {
      test.skip(true, 'Bambu Lab has fewer than 2 materials in this account; nothing to switch between.')
    }

    const [m1, m2] = realMaterials as [string, string]
    await dialog.selectMaterial(m1)

    const c1 = await dialog.listCandidates()
    test.skip(c1.length === 0, `Brand+${m1} produced no candidates.`)
    if (c1.length === 0) return
    await dialog.pickCandidate(c1[0]!.colorCode)
    const preview1 = await dialog.readPreview()
    // STUDIO-17977: candidates whose `color_code` is a raw hex (i.e. came
    // from the user-owned cloud library, not the BBL official catalogue)
    // are intentionally NOT shown in the preview's `preview-fila-code`
    // slot — that slot is reserved for real BBL fila codes. A hex-only
    // candidate surfaces only via `preview-hex-list`. So branch on the
    // shape of c1[0].colorCode:
    //   - hex-shaped (#RRGGBB or #RRGGBBAA): expect hex-list to contain it
    //   - BBL code (digits / mixed): expect filaCode to equal it
    const code = c1[0]!.colorCode
    const isHexCode = /^#?[0-9a-f]{6,8}$/i.test(code)
    if (isHexCode) {
      const want = code.startsWith('#') ? code.toUpperCase() : `#${code.toUpperCase()}`
      expect(preview1.hexes.map((h) => h.toUpperCase())).toContain(want)
    } else {
      expect(preview1.filaCode).toBe(code)
    }

    await dialog.selectMaterial(m2)

    // After switching material, the candidate panel changes.  The form
    // must align: preview color must come from the NEW candidate set.
    const c2 = await dialog.listCandidates()
    test.skip(c2.length === 0, `Brand+${m2} produced no candidates; cannot verify alignment.`)
    if (c2.length === 0) return

    const preview2 = await dialog.readPreview()
    const stillStale = preview2.filaCode === preview1.filaCode && !c2.some((c) => c.colorCode === preview1.filaCode)
    expect(stillStale, 'preview must not retain a code that is invalid for the new material').toBe(false)

    if (preview2.filaCode) {
      expect(
        c2.some((c) => c.colorCode === preview2.filaCode),
        'preview code should be present in the new candidate list',
      ).toBe(true)
    }

    await dialog.cancel()
  })
})
