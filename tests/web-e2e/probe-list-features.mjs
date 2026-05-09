// Comprehensive probe of the filament list features (real Studio, no mock).
// Verifies:
//   F1  list metadata schema per row (testid, swatch, name, hex, action btns)
//   F2  data-attribute schema (data-color-code, data-color-type)
//   F3  colorName / filaCode reverse-lookup status — including which spools
//       have it and which spools fall back to hex-only
//   F4  filter / search / group toggle / tab switch all flip state cleanly
//   F5  detail / add-similar / delete entries open the right dialog
//   F6  candidate cache snapshot per spool, post-prefetch
//
// All operations go against a real, logged-in Studio session over CDP.
//
// Output: console table + .evidence/list-features/summary.json

import { chromium } from '@playwright/test'
import fs from 'node:fs/promises'
import path from 'node:path'

const CDP = 'http://127.0.0.1:9222'
const ROOT = path.resolve('.evidence/list-features')
await fs.rm(ROOT, { recursive: true, force: true })
await fs.mkdir(ROOT, { recursive: true })

const browser = await chromium.connectOverCDP(CDP, { timeout: 10_000 })
const ctx = browser.contexts()[0]
const page = ctx.pages().find((p) => p.url().includes('localhost:13628') && p.url().includes('#/filament'))
if (!page) {
  console.error('no filament page; run probe-cdp-nav.mjs first')
  process.exit(1)
}
console.log('using:', page.url())
await page.bringToFront()
await page.waitForTimeout(1_500) // let prefetch warm up

const summary = {
  startedAt: new Date().toISOString(),
  defects: [],
  sections: {},
}
const recordDefect = (id, detail) => { summary.defects.push({ id, detail }); console.log('[DEFECT]', id, detail) }

// ============================================================
// F1 + F2 + F3  list rows: metadata schema + colour reverse-lookup
// ============================================================
console.log('\n=== F1+F2+F3  list rows ===')
const dump = await page.evaluate(() => {
  const w = /** @type {any} */ (window)
  const store = w.__filamentStore
  const cache = store?.getState?.()?.filament?.candidatesByFilaId ?? {}
  const presets = store?.getState?.()?.filament?.presets ?? null
  const spoolList = store?.getState?.()?.filament?.spools ?? []

  function normalizeHexCss(v) {
    const raw = (v || '').trim()
    if (!raw) return ''
    const withHash = raw.startsWith('#') ? raw : `#${raw}`
    return withHash.slice(0, 7).toUpperCase()
  }
  function multisetEqIgnoringCase(a, b) {
    if (a.length !== b.length) return false
    const sa = a.map((h) => h.toUpperCase()).slice().sort().join(',')
    const sb = b.map((h) => h.toUpperCase()).slice().sort().join(',')
    return sa === sb
  }

  const rows = Array.from(document.querySelectorAll('tr[data-testid^="filament-row-"]'))
  const out = rows.map((row) => {
    const testid = row.getAttribute('data-testid')
    const colorCell = row.querySelector('[data-testid="filament-row-color"]')
    const colorCode = colorCell?.getAttribute('data-color-code') ?? null
    return {
      testid,
      spoolId: row.getAttribute('data-spool-id'),
      colorCode,
      colorType: row.getAttribute('data-color-type'),
      brand: row.getAttribute('data-brand'),
      materialType: row.getAttribute('data-material-type'),
      name: row.querySelector('[data-testid="filament-row-name"]')?.textContent?.trim() ?? null,
      swatchStyle: row.querySelector('[data-testid="filament-row-color-swatch"]')?.getAttribute('style') ?? null,
      colorName: row.querySelector('[data-testid="filament-row-color-name"]')?.textContent?.trim() ?? null,
      filaCode: row.querySelector('[data-testid="filament-row-fila-code"]')?.textContent?.trim() ?? null,
      hexLabel: row.querySelector('[data-testid="filament-row-color-hex"]')?.textContent?.trim() ?? null,
      hasDetailBtn: !!row.querySelector('[data-testid="filament-row-detail"]'),
      hasAddSimilarBtn: !!row.querySelector('[data-testid="filament-row-add-similar"]'),
      hasDeleteBtn: !!row.querySelector('[data-testid="filament-row-delete"]'),
    }
  })

  // Per-spool diagnostics: what could the resolver match against?
  const diagnostics = []
  for (const s of spoolList) {
    const spoolHexes = (Array.isArray(s.colors) && s.colors.length > 0)
      ? s.colors.map(normalizeHexCss).filter((h) => h.length === 7)
      : (normalizeHexCss(s.color_code) ? [normalizeHexCss(s.color_code)] : [])
    const ownCands = cache[s.setting_id] || []
    const ownCandsCount = ownCands.length
    const ownExactMatch = ownCands.find((c) => {
      const cHexes = (Array.isArray(c.colors) ? c.colors : []).map(normalizeHexCss).filter((h) => h.length === 7)
      return multisetEqIgnoringCase(cHexes, spoolHexes)
    })
    // Cross-fila scan: any single-hex candidate in any cached fila_id matching spool's single hex.
    const isSingle = spoolHexes.length === 1
    const crossSingleMatches = []
    if (isSingle) {
      for (const [filaId, list] of Object.entries(cache)) {
        if (filaId === s.setting_id) continue
        for (const c of (list || [])) {
          const cHexes = (Array.isArray(c.colors) ? c.colors : []).map(normalizeHexCss).filter((h) => h.length === 7)
          if (cHexes.length === 1 && cHexes[0].toUpperCase() === spoolHexes[0].toUpperCase()) {
            crossSingleMatches.push({ filaId, code: c.color_code, name: c.name, type: c.color_type })
          }
        }
      }
    }
    diagnostics.push({
      spoolId: s.spool_id,
      settingId: s.setting_id,
      brand: s.brand,
      materialType: s.material_type,
      spoolColors: s.colors,
      spoolColorCode: s.color_code,
      spoolHexesNormalized: spoolHexes,
      ownCandsCount,
      ownExactMatch: ownExactMatch ? { code: ownExactMatch.color_code, name: ownExactMatch.name, type: ownExactMatch.color_type } : null,
      crossSingleMatches,
      cachedFilaIds: Object.keys(cache),
      presetsVendorCount: presets?.vendors?.length ?? 0,
    })
  }

  return { rows: out, diagnostics }
})
console.log('rows:')
for (const r of dump.rows) {
  console.log(' ', JSON.stringify(r))
}
console.log('\ndiagnostics:')
for (const d of dump.diagnostics) {
  console.log(' ', JSON.stringify(d))
}
summary.sections.list = dump

// Sanity: every visible row must have schema.
for (const r of dump.rows) {
  if (!r.colorCode) recordDefect('F2-no-color-code', `${r.testid} missing data-color-code`)
  if (!r.swatchStyle || !/background:.*(rgb|#|linear-gradient)/i.test(r.swatchStyle))
    recordDefect('F1-empty-swatch', `${r.testid} swatch has no visible background`)
  if (!r.hexLabel) recordDefect('F1-no-hex-label', `${r.testid} hex label missing`)
  if (!r.hasDetailBtn || !r.hasAddSimilarBtn || !r.hasDeleteBtn)
    recordDefect('F1-missing-action-btns', `${r.testid} missing one of detail/add-similar/delete`)
}

// ============================================================
// F4  toolbar interactions
// ============================================================
console.log('\n=== F4  toolbar interactions ===')

// Group toggle
const groupBefore = await page.locator('[data-testid="filament-group-toggle"]').getAttribute('data-grouped')
await page.locator('[data-testid="filament-group-toggle"]').click({ force: true })
await page.waitForTimeout(150)
const groupAfter = await page.locator('[data-testid="filament-group-toggle"]').getAttribute('data-grouped')
console.log('group: before=', groupBefore, ' after=', groupAfter)
if (groupBefore === groupAfter) recordDefect('F4-group-toggle-no-flip', 'data-grouped did not change')
await page.locator('[data-testid="filament-group-toggle"]').click({ force: true })
await page.waitForTimeout(150)
const groupRestored = await page.locator('[data-testid="filament-group-toggle"]').getAttribute('data-grouped')
if (groupRestored !== groupBefore) recordDefect('F4-group-toggle-stuck', `did not restore: was=${groupBefore} now=${groupRestored}`)

// Tab switch
const tabAllBefore = await page.locator('[data-testid="filament-tab-all"]').getAttribute('data-active')
await page.locator('[data-testid="filament-tab-ams"]').click({ force: true })
await page.waitForTimeout(150)
const tabAmsActive = await page.locator('[data-testid="filament-tab-ams"]').getAttribute('data-active')
console.log('tab: ams.active=', tabAmsActive, ' all.was=', tabAllBefore)
if (tabAmsActive !== 'true') recordDefect('F4-tab-ams-not-active', `expected true got ${tabAmsActive}`)
await page.locator('[data-testid="filament-tab-all"]').click({ force: true })
await page.waitForTimeout(150)

// Search box (write + clear)
const searchBox = page.locator('[data-testid="filament-search"]')
await searchBox.click({ force: true })
await searchBox.fill('zzznoexist')
await page.waitForTimeout(200)
const rowsAfterSearch = await page.$$('tr[data-testid^="filament-row-"]')
const expectedZero = rowsAfterSearch.length === 0
console.log('search "zzznoexist": rows=', rowsAfterSearch.length, ' (expect 0)')
if (!expectedZero) recordDefect('F4-search-not-filtering', `search yielded ${rowsAfterSearch.length} rows for impossible substring`)
await searchBox.fill('')
await page.waitForTimeout(200)
const rowsAfterClear = await page.$$('tr[data-testid^="filament-row-"]')
console.log('search cleared: rows=', rowsAfterClear.length, ' (expect', dump.rows.length, ')')
if (rowsAfterClear.length !== dump.rows.length)
  recordDefect('F4-search-not-restoring', `clear left ${rowsAfterClear.length}, expected ${dump.rows.length}`)

summary.sections.toolbar = {
  groupBefore, groupAfter, groupRestored,
  tabAllBefore, tabAmsActive,
  searchFilteredZero: expectedZero,
  searchClearedRowCount: rowsAfterClear.length,
}

// ============================================================
// F5  per-row entry points (detail / add-similar / delete)
// ============================================================
console.log('\n=== F5  row entry points ===')
const rowEls = await page.$$('tr[data-testid^="filament-row-"]')
const entryDumps = []
for (const row of rowEls) {
  const tid = await row.getAttribute('data-testid')
  // Detail
  await row.$eval('[data-testid="filament-row-detail"]', (b) => b.click())
  const detailVisible = await page.getByTestId('detail-dialog').waitFor({ state: 'visible', timeout: 5_000 }).then(() => true).catch(() => false)
  const detailColorLabel = detailVisible
    ? await page.locator('[data-testid="detail-color-label"]').textContent().catch(() => null)
    : null
  if (!detailVisible) recordDefect('F5-detail-not-open', tid)
  await page.locator('[data-testid="detail-dialog-close"]').click({ force: true }).catch(() => undefined)
  await page.getByTestId('detail-dialog').waitFor({ state: 'detached', timeout: 3_000 }).catch(() => undefined)

  // Add-similar
  await row.$eval('[data-testid="filament-row-add-similar"]', (b) => b.click())
  const addVisible = await page.getByTestId('add-dialog').waitFor({ state: 'visible', timeout: 5_000 }).then(() => true).catch(() => false)
  if (!addVisible) recordDefect('F5-add-similar-not-open', tid)
  await page.locator('[data-testid="dialog-cancel"]').click({ force: true }).catch(() => undefined)
  await page.getByTestId('add-dialog').waitFor({ state: 'detached', timeout: 3_000 }).catch(() => undefined)

  // Delete (cancel — no mutation)
  await row.$eval('[data-testid="filament-row-delete"]', (b) => b.click())
  const confirmVisible = await page.getByTestId('confirm-dialog').waitFor({ state: 'visible', timeout: 5_000 }).then(() => true).catch(() => false)
  if (!confirmVisible) recordDefect('F5-delete-confirm-not-open', tid)
  await page.locator('[data-testid="confirm-dialog-cancel"]').click({ force: true }).catch(() => undefined)
  await page.getByTestId('confirm-dialog').waitFor({ state: 'detached', timeout: 3_000 }).catch(() => undefined)

  entryDumps.push({ tid, detailVisible, detailColorLabel, addVisible, confirmVisible })
  console.log(`  ${tid} detail=${detailVisible} colorLabel=${detailColorLabel} add=${addVisible} confirm=${confirmVisible}`)
}
summary.sections.entryPoints = entryDumps

// ============================================================
// finalise
// ============================================================
summary.finishedAt = new Date().toISOString()
summary.passed = summary.defects.length === 0
await fs.writeFile(path.join(ROOT, 'summary.json'), JSON.stringify(summary, null, 2))
console.log('\n========================================')
console.log('  list features probe done')
console.log('  defects:', summary.defects.length)
console.log('========================================')
await browser.close()
