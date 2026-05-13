/**
 * @filament-baseline @webview-only
 *
 * The group toggle in the filament list toolbar: clicking it must flip
 * `data-grouped`.  Clicking again must restore it.  This is a basic
 * stateful UI control regression — a previous refactor regressed it
 * twice during STUDIO-18126, so a 2-line spec is cheap insurance.
 */
import { mkdir } from 'node:fs/promises'
import { test, expect } from '../src/fixtures/filament'

const GROUP_FIXTURE_SPOOLS = [
  {
    spool_id: 'e2e-group-pla-blue',
    brand: 'Bambu Lab',
    material_type: 'PLA',
    series: 'PLA Matte',
    color_code: '#61D3F7',
    color_name: 'Sky Blue',
    diameter: 1.75,
    initial_weight: 1000,
    net_weight: 380,
    spool_weight: 0,
    remain_percent: 38,
    status: 'active',
    favorite: false,
    entry_method: 'manual',
  },
  {
    spool_id: 'e2e-group-pla-magenta',
    brand: 'Bambu Lab',
    material_type: 'PLA',
    series: 'PLA Matte',
    color_code: '#CC00CC',
    color_name: 'Magenta',
    diameter: 1.75,
    initial_weight: 1000,
    net_weight: 1000,
    spool_weight: 0,
    remain_percent: 100,
    status: 'active',
    favorite: false,
    entry_method: 'manual',
  },
  {
    spool_id: 'e2e-group-pla-metal',
    brand: 'Bambu Lab',
    material_type: 'PLA',
    series: 'PLA Metal',
    color_code: '#1D7C6A',
    color_name: 'Metal Green',
    diameter: 1.75,
    initial_weight: 1000,
    net_weight: 1000,
    spool_weight: 0,
    remain_percent: 100,
    status: 'active',
    favorite: false,
    entry_method: 'manual',
  },
]

async function seedGroupFixture(page: import('@playwright/test').Page): Promise<boolean> {
  return page.evaluate((spools) => {
    const store = (window as unknown as {
      __filamentStore?: {
        getState?: () => {
          filament: {
            setCloudSync: (value: unknown) => void
            setSpools: (value: Array<Record<string, unknown>>) => void
          }
        }
      }
    }).__filamentStore
    if (typeof store?.getState !== 'function') return false
    const filament = store.getState().filament
    filament.setCloudSync({
      logged_in: true,
      is_syncing: false,
      is_pulling: false,
      last_synced_at: '',
      last_error: { code: 0, message: '' },
    })
    filament.setSpools(spools)
    return true
  }, GROUP_FIXTURE_SPOOLS)
}

test.describe('filament group toggle @filament-baseline @webview-only', () => {
  test('clicking group toggle flips data-grouped and back', async ({ filamentList }) => {
    const initial = await filamentList.isGrouped()

    await filamentList.toggleGroup()
    expect(await filamentList.isGrouped()).toBe(!initial)

    await filamentList.toggleGroup()
    expect(await filamentList.isGrouped()).toBe(initial)
  })

  test('same filament type with different colors renders as one group @filament-group-by-type', async ({
    page,
    filamentList,
  }) => {
    test.skip(!(await seedGroupFixture(page)), 'This regression spec needs the dev/test __filamentStore hook.')

    await filamentList.clearSearch()
    await filamentList.setGrouped(false)

    const rows = await filamentList.listRowSummaries()
    const targetName = 'Bambu Lab PLA Matte'
    const target = rows.filter((row) => row.name === targetName)
    const targetColors = new Set(target.map((item) => item.colorCode).filter(Boolean))

    expect(target, 'fixture should render two same-type rows before grouping').toHaveLength(2)
    expect(targetColors, 'fixture rows should differ only by color').toEqual(new Set(['#61D3F7', '#CC00CC']))

    await filamentList.search(targetName)
    await filamentList.page.waitForTimeout(150)
    await filamentList.setGrouped(true)

    const groups = await filamentList.readGroupSummaries()
    const matchingGroups = groups.filter((group) => group.key === targetName)

    expect(matchingGroups, `group key "${targetName}" should not be split by color`).toHaveLength(1)
    expect(matchingGroups[0]!.count, 'group count should include same-type rows with different colors').toBe(2)
    expect(matchingGroups[0]!.totalWeight, 'group total should sum visible remain weights').toBe(1380)
    for (const color of targetColors) {
      expect(matchingGroups[0]!.key, `group key must not include color "${color}"`).not.toContain(color)
    }
  })

  test('real WebView grouped headers do not expose color names or hex codes @filament-group-real', async ({
    page,
    filamentList,
  }, testInfo) => {
    await filamentList.clearSearch()
    await filamentList.setGrouped(true)
    await page.waitForTimeout(150)

    const rows = await filamentList.listRowSummaries()
    const groups = await filamentList.readGroupSummaries()
    test.skip(rows.length === 0, 'No visible real inventory rows to verify.')
    test.skip(groups.length === 0, 'No visible real grouping headers to verify.')

    await mkdir('reports/screenshots', { recursive: true })
    const screenshotPath = 'reports/screenshots/filament-grouping-real-webview.png'
    await page.screenshot({ path: screenshotPath, fullPage: true })
    await testInfo.attach('filament-grouping-real-webview', {
      path: screenshotPath,
      contentType: 'image/png',
    })

    const rowColors = rows
      .map((row) => row.colorCode.trim().toUpperCase())
      .filter((color) => /^#[0-9A-F]{6}$/.test(color))

    for (const group of groups) {
      expect(group.key, `group "${group.key}" must not contain a hex color`).not.toMatch(/#[0-9A-Fa-f]{6}/)
      for (const color of rowColors) {
        expect(group.key.toUpperCase(), `group "${group.key}" must not contain row color ${color}`).not.toContain(color)
      }
    }
  })

  test('add one real spool then screenshot before and after grouping @filament-group-real-add @destructive', async ({
    page,
    filamentList,
  }, testInfo) => {
    const beforeRows = await filamentList.listRowSummaries()

    const dialog = await filamentList.openAddDialog()
    await dialog.switchTab('manual')

    const brands = await dialog.listBrands()
    const brand = brands.includes('Bambu Lab') ? 'Bambu Lab' : brands[0]
    test.skip(!brand, 'No brand options available; cannot add a real spool.')
    await dialog.selectBrand(brand!)

    const materials = await dialog.listMaterials()
    const material = materials[0]
    test.skip(!material, `No material options available for brand "${brand}".`)
    await dialog.selectMaterial(material!)

    const [candidate] = await dialog.listCandidates()
    test.skip(!candidate, `No color candidates available for "${brand} ${material}".`)
    await dialog.pickCandidate(candidate!.colorCode)
    await dialog.confirm()

    await page.waitForFunction(
      (count) => document.querySelectorAll('tr[data-testid^="filament-row-"]').length > count,
      beforeRows.length,
      { timeout: 10_000 },
    ).catch(() => undefined)

    const afterRows = await filamentList.listRowSummaries()
    expect(afterRows.length, 'adding one real spool should increase visible row count').toBeGreaterThan(beforeRows.length)

    const added = afterRows.find((row) =>
      !beforeRows.some((before) => before.id === row.id)
    )
    expect(added, 'newly added spool row should be visible').toBeTruthy()

    await mkdir('reports/screenshots', { recursive: true })

    await filamentList.clearSearch()
    await filamentList.setGrouped(false)
    await page.waitForTimeout(150)
    const beforeGroupingPath = 'reports/screenshots/filament-added-before-grouping-real-webview.png'
    await page.screenshot({ path: beforeGroupingPath, fullPage: true })
    await testInfo.attach('filament-added-before-grouping-real-webview', {
      path: beforeGroupingPath,
      contentType: 'image/png',
    })

    await filamentList.setGrouped(true)
    await page.waitForTimeout(150)
    const afterGroupingPath = 'reports/screenshots/filament-added-after-grouping-real-webview.png'
    await page.screenshot({ path: afterGroupingPath, fullPage: true })
    await testInfo.attach('filament-added-after-grouping-real-webview', {
      path: afterGroupingPath,
      contentType: 'image/png',
    })

    const groups = await filamentList.readGroupSummaries()
    expect(groups.length, 'grouped view should render group headers after adding one spool').toBeGreaterThan(0)
    for (const group of groups) {
      expect(group.key, `group "${group.key}" must not contain a hex color`).not.toMatch(/#[0-9A-Fa-f]{6}/)
    }
  })
})
