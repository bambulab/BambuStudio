// Verify the filament list row colour-name reverse-lookup pipeline
// against the live Studio (real cloud, no mock).
//
//   1. Snapshot every list row's tail state (swatch / name / filaCode / hex).
//   2. For each row, open the Add-Similar dialog. AddEditDialog primes the
//      candidate cache for that spool's fila_id and any same-(brand+material)
//      fallback fila_ids.  Walk the candidate panel and dump every visible
//      candidate hex / name / filaCode so we can see what's really in the
//      "official list" the user is asking us to look up against.
//   3. Close the dialog and re-snapshot the row tail.  Compare against (1)
//      to see whether (and why) the list row picked up a colorName/filaCode
//      after the cache warmed up.
//   4. Try the cross-fila path: open the AMS tab + Manual tab in the dialog
//      so that more fila_ids enter the cache, close, snapshot again.
//   5. Emit a focused diagnosis per row:
//        - "matched on own fila_id"        — best case
//        - "matched cross-fila single-hex" — needs resolver extension
//        - "no candidate in any fila_id"   — real official list gap
//      so we know what fix (if any) the data warrants.
//
// Output: .evidence/list-color-name/summary.json

import { chromium } from '@playwright/test'
import fs from 'node:fs/promises'
import path from 'node:path'

const CDP = 'http://127.0.0.1:9222'
const ROOT = path.resolve('.evidence/list-color-name')
await fs.rm(ROOT, { recursive: true, force: true })
await fs.mkdir(ROOT, { recursive: true })

const browser = await chromium.connectOverCDP(CDP, { timeout: 10_000 })
const ctx = browser.contexts()[0]
const page = ctx.pages().find((p) => p.url().includes('localhost:13628') && p.url().includes('#/filament'))
if (!page) { console.error('no filament page'); process.exit(1) }
console.log('using:', page.url())
await page.bringToFront()
await page.waitForTimeout(800)

const summary = { startedAt: new Date().toISOString(), rows: [], defects: [] }

async function snapshotRows() {
  return page.evaluate(() => {
    const rows = Array.from(document.querySelectorAll('tr[data-testid^="filament-row-"]'))
    return rows.map((row) => {
      const tid = row.getAttribute('data-testid')
      const colorCell = row.querySelector('[data-testid="filament-row-color"]')
      return {
        tid,
        colorCode: colorCell?.getAttribute('data-color-code') ?? null,
        colorType: row.getAttribute('data-color-type'),
        name: row.querySelector('[data-testid="filament-row-name"]')?.textContent?.trim() ?? null,
        rowMaterial: row.querySelector('[data-testid="filament-row-material"]')?.textContent?.trim() ?? null,
        rowBrand: row.querySelector('[data-testid="filament-row-brand"]')?.textContent?.trim() ?? null,
        swatchBg: row.querySelector('[data-testid="filament-row-color-swatch"]')?.getAttribute('style') ?? null,
        colorName: row.querySelector('[data-testid="filament-row-color-name"]')?.textContent?.trim() ?? null,
        filaCode: row.querySelector('[data-testid="filament-row-fila-code"]')?.textContent?.trim() ?? null,
        hexLabel: row.querySelector('[data-testid="filament-row-color-hex"]')?.textContent?.trim() ?? null,
      }
    })
  })
}

async function dumpCandidatePanel() {
  return page.evaluate(() => {
    const items = Array.from(document.querySelectorAll('[data-testid^="color-candidate-"]'))
    return items.map((el) => {
      const titleAttr = el.getAttribute('title') || ''
      return {
        tid: el.getAttribute('data-testid'),
        colorCode: el.getAttribute('data-color-code') ?? null,
        colorType: el.getAttribute('data-color-type') ?? null,
        // AddEditDialog renders `data-colors` (comma-joined hex list); some
        // older probes were looking for `data-hexes` which never existed.
        hexes: el.getAttribute('data-colors') ?? null,
        name: el.getAttribute('data-color-name') ?? null,
        selected: el.getAttribute('data-selected') ?? null,
        title: titleAttr,
        text: el.textContent?.trim() ?? '',
      }
    })
  })
}

async function dumpTabState() {
  return page.evaluate(() => {
    const out = {}
    for (const tid of ['dialog-tab-ams', 'dialog-tab-manual']) {
      const el = document.querySelector(`[data-testid="${tid}"]`)
      out[tid] = el ? { active: el.getAttribute('data-active'), text: el.textContent?.trim() } : null
    }
    // Also dump brand / material so we know what selector value the dialog
    // landed on after prefill.
    const brand = document.querySelector('[data-testid="filament-brand"]')
    const material = document.querySelector('[data-testid="filament-material"]')
    out.brand = brand ? /** @type {HTMLSelectElement} */ (brand).value : null
    out.material = material ? /** @type {HTMLSelectElement} */ (material).value : null
    return out
  })
}

async function ensureDialogClosed() {
  await page.evaluate(() => {
    document.querySelectorAll('[data-testid="dialog-cancel"], [data-testid="detail-dialog-close"]')
      .forEach((b) => /** @type {HTMLElement} */ (b).click())
  }).catch(() => undefined)
  await page.waitForTimeout(150)
  await page.keyboard.press('Escape').catch(() => undefined)
  await page.waitForTimeout(150)
}

console.log('\n=== STEP 1: baseline rows (cache may be cold or warm from prior session) ===')
const baseline = await snapshotRows()
for (const r of baseline) console.log(' ', JSON.stringify(r))

const perRow = []
for (let i = 0; i < baseline.length; i++) {
  const row = baseline[i]
  console.log(`\n--- ROW ${i + 1}/${baseline.length}: ${row.tid} (${row.name} ${row.colorCode}) ---`)
  await ensureDialogClosed()
  // Open Add-Similar via the row button
  const rowSel = `tr[data-testid="${row.tid}"]`
  const opened = await page.locator(`${rowSel} [data-testid="filament-row-add-similar"]`).click({ force: true, timeout: 4_000 }).then(() => true).catch(() => false)
  if (!opened) { perRow.push({ row, error: 'add-similar click failed' }); continue }
  const visible = await page.getByTestId('add-dialog').waitFor({ state: 'visible', timeout: 5_000 }).then(() => true).catch(() => false)
  if (!visible) { perRow.push({ row, error: 'add dialog did not appear' }); await ensureDialogClosed(); continue }
  await page.waitForTimeout(800)

  const tabsBefore = await dumpTabState()

  // Add-Similar opens with the Manual tab active. The candidate panel for
  // BBL filaments lives directly under the Manual tab once a brand+material
  // is selected (the dialog prefills those from the source spool), so the
  // initial dump should already contain the official PLA Silk catalogue.
  const candsManual = await dumpCandidatePanel()

  // Move to AMS tab to see if AMS-derived candidates surface anything else.
  if (await page.locator('[data-testid="dialog-tab-ams"]').count()) {
    await page.locator('[data-testid="dialog-tab-ams"]').click({ force: true }).catch(() => undefined)
    await page.waitForTimeout(400)
  }
  const candsAms = await dumpCandidatePanel()

  // Cycle through other materials on the Manual tab to populate cross-fila
  // candidates in the cache. We do this via the brand + material selectors.
  if (await page.locator('[data-testid="dialog-tab-manual"]').count()) {
    await page.locator('[data-testid="dialog-tab-manual"]').click({ force: true }).catch(() => undefined)
    await page.waitForTimeout(300)
  }

  const otherMaterialDumps = []
  const matSel = page.locator('[data-testid="filament-material"]')
  if (await matSel.count()) {
    const opts = await matSel.evaluate((sel) => Array.from(/** @type {HTMLSelectElement} */ (sel).options).map((o) => o.value))
    const originalMat = await matSel.inputValue()
    for (const v of opts) {
      if (!v || v === originalMat) continue
      await matSel.selectOption(v).catch(() => undefined)
      await page.waitForTimeout(450)
      const cs = await dumpCandidatePanel()
      const singles = cs
        .filter((c) => (c.hexes || '').split(',').filter(Boolean).length === 1)
        .map((c) => ({ name: c.name, code: c.colorCode, hex: (c.hexes || '').split(',').find(Boolean) || null }))
      otherMaterialDumps.push({ material: v, count: cs.length, singles })
    }
    if (originalMat) await matSel.selectOption(originalMat).catch(() => undefined)
    await page.waitForTimeout(300)
  }
  const candsManualAfterCycle = await dumpCandidatePanel()
  // Aggregate every candidate we observed across all materials into a
  // single global hex catalogue so we can compare the spool hex against
  // every BBL fila_id we touched, not just the spool's own.
  const globalSingles = []
  const seenCodes = new Set()
  function harvest(arr) {
    for (const c of arr) {
      if (!c.hexes) continue
      const hexes = c.hexes.split(',').map((s) => s.trim()).filter(Boolean)
      if (hexes.length !== 1) continue
      const key = `${c.colorCode}|${hexes[0]}`
      if (seenCodes.has(key)) continue
      seenCodes.add(key)
      globalSingles.push({ name: c.name, code: c.colorCode, hex: hexes[0] })
    }
  }
  harvest(candsManual); harvest(candsManualAfterCycle)
  for (const d of otherMaterialDumps) {
    if (d.singles) for (const s of d.singles) harvest([{ name: s.name, colorCode: s.code, hexes: s.hex || '' }])
  }

  const matchOwn = candsManual.find((c) => {
    const norm = (s) => (s || '').replace(/^#/, '').toUpperCase().slice(0, 6)
    if (!c.hexes) return false
    const list = c.hexes.split(',').map((h) => norm(h)).filter(Boolean)
    if (list.length !== 1) return false
    return list[0] === norm(row.colorCode)
  })

  const matchCross = globalSingles.find((c) => {
    const norm = (s) => (s || '').replace(/^#/, '').toUpperCase().slice(0, 6)
    return norm(c.hex) === norm(row.colorCode)
  })

  await ensureDialogClosed()
  await page.waitForTimeout(400)

  // Re-snapshot row to see whether priming the cache surfaced colorName
  const after = await snapshotRows()
  const afterRow = after.find((r) => r.tid === row.tid) ?? null

  perRow.push({
    row,
    afterRow,
    matchOwn: matchOwn ?? null,
    matchCross: matchCross ?? null,
    manualCandsCount: candsManual.length,
    amsCandsCount: candsAms.length,
    materialsCycled: otherMaterialDumps.length,
    globalSinglesCount: globalSingles.length,
    manualHexCatalog: candsManual
      .map((c) => ({ name: c.name, code: c.colorCode, hexes: (c.hexes || '').split(',').filter(Boolean) })),
    crossMaterials: otherMaterialDumps.map((d) => ({ material: d.material, count: d.count })),
    tabsBefore,
  })

  console.log(`  manual candidates (own fila_id): ${candsManual.length}`)
  console.log(`  ams candidates: ${candsAms.length}`)
  console.log(`  cross-material runs: ${otherMaterialDumps.length} (singles=${globalSingles.length})`)
  console.log(`  match-own-single-hex: ${matchOwn ? `${matchOwn.name} (${matchOwn.colorCode})` : 'NONE'}`)
  console.log(`  match-cross-single-hex: ${matchCross ? `${matchCross.name} (${matchCross.code})` : 'NONE'}`)
  console.log(`  row.colorName before/after: ${row.colorName} -> ${afterRow?.colorName}`)
  console.log(`  row.filaCode  before/after: ${row.filaCode} -> ${afterRow?.filaCode}`)
}

summary.rows = perRow
summary.finishedAt = new Date().toISOString()
await fs.writeFile(path.join(ROOT, 'summary.json'), JSON.stringify(summary, null, 2))

console.log('\n========================================')
console.log('  list color-name probe done')
console.log('========================================')
console.log(`  rows: ${perRow.length}`)
for (const r of perRow) {
  const tag = !r.afterRow?.colorName ? 'MISS' : 'OK'
  console.log(`    [${tag}] ${r.row.tid} (${r.row.colorCode}) own-match=${r.matchOwn?.name ?? 'NONE'} after.colorName=${r.afterRow?.colorName ?? 'null'}`)
}
await browser.close()
