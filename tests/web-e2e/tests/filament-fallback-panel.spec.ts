/**
 * @studio-17977 @webview-only
 *
 * Requirement 3 of STUDIO-17977: when the user picks a non-official brand
 * (no matching fila_id in FilamentColorCodeQuery), the candidate panel
 * MUST aggregate official colors of the same material_type from the
 * frontend-cached candidates instead of going empty.
 *
 * The presence of "Generic" as a brand and any material whose label
 * contains "PLA Basic" is assumed; if the developer's account has no such
 * combination the spec auto-skips with a clear message.
 */
import { test, expect } from '../src/fixtures/filament'

test.describe('STUDIO-17977 fallback candidate panel @studio-17977 @webview-only', () => {
  test('Generic + PLA Basic surfaces aggregated official candidates', async ({ filamentList }) => {
    const dialog = await filamentList.openAddDialog()
    await dialog.switchTab('manual')

    const brandOptions = await dialog.brandSelect.locator('option').allTextContents()
    if (!brandOptions.some((o) => o.includes('Generic'))) {
      test.skip(true, '"Generic" brand not present in this account; cannot exercise fallback path.')
    }
    await dialog.selectBrand('Generic')

    const materialOptions = await dialog.materialSelect.locator('option').allTextContents()
    const target = materialOptions.find((o) => /PLA\s*Basic/i.test(o))
    if (!target) {
      test.skip(true, 'No PLA Basic material under Generic; cannot exercise fallback path.')
    }
    if (!target) return
    await dialog.selectMaterial(target)

    // Fallback path: at least 2 candidates - any value is suspicious if it
    // is exactly the same as what the strict path returns for an empty
    // result.  This is intentionally loose so the spec stays green when
    // FilamentColorCodeQuery's data set evolves.
    const candidates = await dialog.listCandidates()
    expect(
      candidates.length,
      'fallback aggregate should expose at least 2 candidates for a known PLA material',
    ).toBeGreaterThanOrEqual(2)

    await dialog.cancel()
  })

  test('picking a fallback candidate populates the preview bar with multi-hex when applicable', async ({
    filamentList,
  }) => {
    const dialog = await filamentList.openAddDialog()
    await dialog.switchTab('manual')

    const brandOptions = await dialog.brandSelect.locator('option').allTextContents()
    if (!brandOptions.some((o) => o.includes('Generic'))) {
      test.skip(true, '"Generic" brand not present.')
    }
    await dialog.selectBrand('Generic')

    const materialOptions = await dialog.materialSelect.locator('option').allTextContents()
    const target = materialOptions.find((o) => /PLA\s*Basic/i.test(o))
    test.skip(!target, 'No PLA Basic material under Generic.')
    if (!target) return
    await dialog.selectMaterial(target)

    const candidates = await dialog.listCandidates()
    const multi = candidates.find((c) => c.colorType !== 2 && c.colors.length > 1)
    test.skip(
      !multi,
      'No gradient/multicolor candidate present in fallback panel; multi-hex preview cannot be exercised here.',
    )
    if (!multi) return

    await dialog.pickCandidate(multi.colorCode)
    const preview = await dialog.readPreview()
    expect(preview.hexes.length, 'gradient/multicolor preview hex list size').toBeGreaterThanOrEqual(2)
    expect(preview.filaCode, 'preview shows official BBL code').toBe(multi.colorCode)

    await dialog.cancel()
  })
})
