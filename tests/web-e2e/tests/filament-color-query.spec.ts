/**
 * @studio-17977 @webview-only
 *
 * Verifies the candidate panel (FilamentColorCodeQuery driven) for an
 * official BBL filament:
 *   - the panel renders at least one candidate
 *   - candidates expose data-color-code / data-color-name attributes used
 *     by the framework's POMs
 *   - selecting one updates the preview bar with name + official BBL
 *     code + matching hex list
 *
 * The spec auto-discovers the first available brand/material pair so it
 * does not break when the user's library no longer contains a specific
 * product (e.g. "PLA Basic" vs "PLA Matte").  We only require that the
 * library has at least one official Bambu Lab material registered.
 */
import { test, expect } from '../src/fixtures/filament'

const OFFICIAL_BRAND = 'Bambu Lab'

test.describe('STUDIO-17977 official color query @studio-17977 @webview-only', () => {
  test('Bambu Lab + first available material surfaces a non-empty candidate list', async ({ filamentList }) => {
    const dialog = await filamentList.openAddDialog()
    await dialog.switchTab('manual')

    const brands = await dialog.listBrands()
    test.skip(!brands.includes(OFFICIAL_BRAND), `"${OFFICIAL_BRAND}" not found in brand list: ${brands.join(', ')}`)
    await dialog.selectBrand(OFFICIAL_BRAND)

    const materials = await dialog.listMaterials()
    test.skip(materials.length === 0, 'No materials registered for Bambu Lab in this library.')
    const firstMaterial = materials[0]!
    await dialog.selectMaterial(firstMaterial)

    const candidates = await dialog.listCandidates()
    expect(
      candidates.length,
      `official BBL palette should not be empty for "${firstMaterial}"`,
    ).toBeGreaterThan(0)

    for (const c of candidates) {
      expect(c.colorCode, 'data-color-code').not.toBe('')
      expect(c.colors.length, 'data-colors hex list').toBeGreaterThan(0)
    }

    await dialog.cancel()
  })

  test('picking a candidate fills name + official code + hex list in preview bar', async ({ filamentList }) => {
    const dialog = await filamentList.openAddDialog()
    await dialog.switchTab('manual')

    const brands = await dialog.listBrands()
    test.skip(!brands.includes(OFFICIAL_BRAND), `"${OFFICIAL_BRAND}" not found in brand list: ${brands.join(', ')}`)
    await dialog.selectBrand(OFFICIAL_BRAND)

    const materials = await dialog.listMaterials()
    test.skip(materials.length === 0, 'No materials registered for Bambu Lab in this library.')
    await dialog.selectMaterial(materials[0]!)

    const [first] = await dialog.listCandidates()
    test.skip(!first, 'No candidates returned by FilamentColorCodeQuery; cannot verify preview bar.')
    if (!first) return

    await dialog.pickCandidate(first.colorCode)
    const preview = await dialog.readPreview()

    expect(preview.name, 'preview color name').toBe(first.name)
    expect(preview.filaCode, 'official BBL code').toBe(first.colorCode)
    expect(preview.hexes.length).toBeGreaterThanOrEqual(1)
    if (first.colorType === 2) {
      expect(preview.hexes).toEqual([first.colors[0]?.toUpperCase() ?? ''])
    } else {
      expect(preview.hexes.length).toBeGreaterThanOrEqual(2)
    }

    await dialog.cancel()
  })
})
