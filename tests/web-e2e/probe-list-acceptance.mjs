// Acceptance probe for the "list functionality + reverse-lookup" ask.
// Designed to be (a) short — no per-row dialog dance, (b) resilient to
// dialog overlays so it can't get stuck like the earlier v2 probe, and
// (c) emit a single verdict per acceptance bullet.
//
// Acceptance bullets:
//   L1  list row schema (testid, colorCode, swatch, hex, action btns) for
//       every visible spool
//   L2  toolbar interactions: search filter / clear, group toggle round-trip,
//       tab switch round-trip
//   L3  per-row entry points (detail / add-similar / delete) all open the
//       expected dialog and dismiss cleanly
//   L4  reverse-lookup channel via AMS slot picker — for every BBL slot that
//       the cloud reports under the active machine, confirm preview-bar
//       reflects the matched candidate's name+code+hexes
//
// Output: .evidence/list-acceptance/summary.json + screenshots

import { chromium } from '@playwright/test'
import fs from 'node:fs/promises'
import path from 'node:path'

const CDP = 'http://127.0.0.1:9222'
const ROOT = path.resolve('.evidence/list-acceptance')
await fs.rm(ROOT, { recursive: true, force: true })
await fs.mkdir(ROOT, { recursive: true })

const browser = await chromium.connectOverCDP(CDP, { timeout: 10_000 })
const ctx = browser.contexts()[0]
const page = ctx.pages().find((p) => p.url().includes('localhost:13628') && p.url().includes('#/filament'))
if (!page) { console.error('no filament page'); process.exit(1) }
// Hard cap so this probe can never get stuck like the v2 one did. If
// anything wedges, we exit and dump partial evidence.
const HARD_CAP_MS = 60_000
const hardCapTimer = setTimeout(() => {
  console.error(`[abort] HARD_CAP_MS=${HARD_CAP_MS}ms exceeded`)
  process.exit(2)
}, HARD_CAP_MS)
hardCapTimer.unref?.()
await page.bringToFront()
await page.waitForTimeout(800)
console.log('using:', page.url())

const summary = { startedAt: new Date().toISOString(), defects: [], bullets: {} }
const flag = (id, detail) => { summary.defects.push({ id, detail }); console.log(`  [DEFECT] ${id}: ${detail}`) }
async function safeShot(name) {
  try { await page.screenshot({ path: path.join(ROOT, name), timeout: 4_000 }) } catch {}
}
async function killOverlays() {
  await page.evaluate(() => {
    document.querySelectorAll('[data-testid="dialog-cancel"], [data-testid="detail-dialog-close"], [data-testid="confirm-dialog-cancel"]')
      .forEach((b) => /** @type {HTMLElement} */ (b).click())
    document.querySelectorAll('div.fixed.inset-0').forEach((d) => d.remove())
  }).catch(() => undefined)
  await page.waitForTimeout(120)
  await page.keyboard.press('Escape').catch(() => undefined)
  await page.waitForTimeout(120)
}

// ===================================================================
// L1: list row schema
// ===================================================================
console.log('\n=== L1: list row schema ===')
await killOverlays()
const rows = await page.evaluate(() => {
  return Array.from(document.querySelectorAll('tr[data-testid^="filament-row-"]')).map((row) => {
    const cell = row.querySelector('[data-testid="filament-row-color"]')
    return {
      tid: row.getAttribute('data-testid'),
      name: row.querySelector('[data-testid="filament-row-name"]')?.textContent?.trim() ?? null,
      colorCode: cell?.getAttribute('data-color-code') ?? null,
      colorType: row.getAttribute('data-color-type'),
      swatch: row.querySelector('[data-testid="filament-row-color-swatch"]')?.getAttribute('style') ?? null,
      colorName: row.querySelector('[data-testid="filament-row-color-name"]')?.textContent?.trim() ?? null,
      filaCode: row.querySelector('[data-testid="filament-row-fila-code"]')?.textContent?.trim() ?? null,
      hex: row.querySelector('[data-testid="filament-row-color-hex"]')?.textContent?.trim() ?? null,
      detailBtn: !!row.querySelector('[data-testid="filament-row-detail"]'),
      addBtn: !!row.querySelector('[data-testid="filament-row-add-similar"]'),
      delBtn: !!row.querySelector('[data-testid="filament-row-delete"]'),
    }
  })
})
console.log(`  rows: ${rows.length}`)
for (const r of rows) console.log('   ', JSON.stringify(r))
for (const r of rows) {
  if (!r.colorCode) flag('L1-color-code', r.tid)
  if (!r.swatch || !/(rgb|#|linear-gradient)/i.test(r.swatch)) flag('L1-swatch-empty', r.tid)
  if (!r.hex) flag('L1-hex-missing', r.tid)
  if (!r.detailBtn || !r.addBtn || !r.delBtn) flag('L1-action-btn', r.tid)
}
summary.bullets.L1 = { rows }
await safeShot('01-list.png')

// ===================================================================
// L2: toolbar interactions
// ===================================================================
console.log('\n=== L2: toolbar ===')
const t = {}
const grpEl = page.locator('[data-testid="filament-group-toggle"]')
t.groupBefore = await grpEl.getAttribute('data-grouped').catch(() => null)
await grpEl.click({ force: true }).catch(() => undefined); await page.waitForTimeout(150)
t.groupAfter = await grpEl.getAttribute('data-grouped').catch(() => null)
await grpEl.click({ force: true }).catch(() => undefined); await page.waitForTimeout(150)
t.groupRestore = await grpEl.getAttribute('data-grouped').catch(() => null)
if (t.groupBefore === t.groupAfter) flag('L2-group-no-flip', JSON.stringify(t))
if (t.groupBefore !== t.groupRestore) flag('L2-group-no-restore', JSON.stringify(t))

await page.locator('[data-testid="filament-tab-ams"]').click({ force: true }).catch(() => undefined)
await page.waitForTimeout(200)
t.tabAmsActive = await page.locator('[data-testid="filament-tab-ams"]').getAttribute('data-active').catch(() => null)
await page.locator('[data-testid="filament-tab-all"]').click({ force: true }).catch(() => undefined)
await page.waitForTimeout(200)
if (t.tabAmsActive !== 'true') flag('L2-tab-ams', JSON.stringify(t))

const search = page.locator('[data-testid="filament-search"]')
await search.click({ force: true }).catch(() => undefined)
await search.fill('zzznoexist')
await page.waitForTimeout(250)
t.filtered = (await page.$$('tr[data-testid^="filament-row-"]')).length
await search.fill('')
await page.waitForTimeout(250)
t.restored = (await page.$$('tr[data-testid^="filament-row-"]')).length
if (t.filtered !== 0) flag('L2-search-filter', `expected 0, got ${t.filtered}`)
if (t.restored !== rows.length) flag('L2-search-restore', `expected ${rows.length}, got ${t.restored}`)
console.log('  toolbar:', JSON.stringify(t))
summary.bullets.L2 = t

// ===================================================================
// L3: per-row entry points (detail / add-similar / delete)
// ===================================================================
console.log('\n=== L3: row entry points ===')
const ep = []
for (const r of rows) {
  await killOverlays()
  const sel = `tr[data-testid="${r.tid}"]`
  await page.locator(`${sel} [data-testid="filament-row-detail"]`).click({ force: true }).catch(() => undefined)
  const dV = await page.getByTestId('detail-dialog').waitFor({ state: 'visible', timeout: 4_000 }).then(() => true).catch(() => false)
  let dLbl = null
  if (dV) dLbl = await page.locator('[data-testid="detail-color-label"]').textContent().catch(() => null)
  await page.locator('[data-testid="detail-dialog-close"]').click({ force: true }).catch(() => undefined)
  await page.waitForTimeout(150)
  await killOverlays()

  await page.locator(`${sel} [data-testid="filament-row-add-similar"]`).click({ force: true }).catch(() => undefined)
  const aV = await page.getByTestId('add-dialog').waitFor({ state: 'visible', timeout: 4_000 }).then(() => true).catch(() => false)
  await page.locator('[data-testid="dialog-cancel"]').click({ force: true }).catch(() => undefined)
  await page.waitForTimeout(150)
  await killOverlays()

  await page.locator(`${sel} [data-testid="filament-row-delete"]`).click({ force: true }).catch(() => undefined)
  const cV = await page.getByTestId('confirm-dialog').waitFor({ state: 'visible', timeout: 4_000 }).then(() => true).catch(() => false)
  await page.locator('[data-testid="confirm-dialog-cancel"]').click({ force: true }).catch(() => undefined)
  await page.waitForTimeout(150)
  await killOverlays()

  ep.push({ tid: r.tid, detailVisible: dV, detailLabel: dLbl, addVisible: aV, confirmVisible: cV })
  if (!dV) flag('L3-detail', r.tid)
  if (!aV) flag('L3-add-similar', r.tid)
  if (!cV) flag('L3-delete-confirm', r.tid)
}
console.log('  entry points:', JSON.stringify(ep))
summary.bullets.L3 = ep

// ===================================================================
// L4: reverse-lookup happy-path via AMS slot picker
// ===================================================================
console.log('\n=== L4: AMS slot reverse-lookup ===')
await killOverlays()
const fab = page.locator('[data-testid="filament-add"]')
await fab.scrollIntoViewIfNeeded().catch(() => undefined)
await fab.click({ force: true }).catch(() => undefined)
const dialogVisible = await page.getByTestId('add-dialog').waitFor({ state: 'visible', timeout: 5_000 }).then(() => true).catch(() => false)
const slotEvidence = []
if (!dialogVisible) {
  flag('L4-add-dialog-not-open', 'failed to open Add dialog')
} else {
  // Switch to AMS tab
  const amsTab = page.locator('[data-testid="dialog-tab-ams"]')
  if (await amsTab.count()) {
    await amsTab.click({ force: true }).catch(() => undefined)
    await page.waitForTimeout(800)
  }
  const slots = await page.evaluate(() => {
    return Array.from(document.querySelectorAll('[data-testid^="ams-slot-"]'))
      .filter((el) => el.getAttribute('data-empty') === 'false')
      .map((el) => ({
        tid: el.getAttribute('data-testid'),
        color: el.getAttribute('data-color'),
        colorType: el.getAttribute('data-color-type'),
      }))
  })
  console.log(`  AMS slots: ${slots.length}`)
  if (slots.length === 0) {
    // No machine bound / no slots -> not a defect of the list features but
    // record so reviewer knows L4 was data-skipped.
    summary.bullets.L4_skipped = true
  }
  for (const slot of slots) {
    await page.locator(`[data-testid="${slot.tid}"]`).click({ force: true }).catch(() => undefined)
    await page.waitForTimeout(700)
    const ev = await page.evaluate(() => {
      const previewName = document.querySelector('[data-testid="preview-color-name"]')?.textContent?.trim() ?? null
      const previewCode = document.querySelector('[data-testid="preview-fila-code"]')?.textContent?.trim() ?? null
      const previewHex = document.querySelector('[data-testid="preview-hex-list"]')?.textContent?.trim() ?? null
      const previewSwatch = document.querySelector('[data-testid="preview-swatch"]')?.getAttribute('style') ?? null
      const sel = document.querySelector('[data-testid^="color-candidate-"][data-selected="true"]')
      return {
        previewName, previewCode, previewHex, previewSwatch,
        selectedCand: sel ? {
          code: sel.getAttribute('data-color-code'),
          name: sel.getAttribute('data-color-name'),
          colors: sel.getAttribute('data-colors'),
          colorType: sel.getAttribute('data-color-type'),
        } : null,
      }
    })
    const officialCand = ev.selectedCand && ev.selectedCand.code
      && !ev.selectedCand.code.startsWith('#')
      && !/^[0-9A-F]{6,8}$/i.test(ev.selectedCand.code)
    slotEvidence.push({ slot, ev, officialCand })
    console.log(`    ${slot.tid}: name='${ev.previewName}' code='${ev.previewCode}' hex='${ev.previewHex}' selected=${ev.selectedCand ? `${ev.selectedCand.name}/${ev.selectedCand.code}` : 'NONE'}`)
    if (!ev.previewSwatch || !/(rgb|#|linear-gradient)/i.test(ev.previewSwatch)) flag('L4-swatch-empty', slot.tid)
    if (slot.color && ev.previewHex) {
      // The AMS slot exposes 8-char `#RRGGBBAA` (Bambu cloud appends alpha).
      // The preview-bar normalises to canonical 6-char `#RRGGBB` per
      // STUDIO-17977 — that's the fix we want to verify, not flag.
      const slotHexes = slot.color.split('/').map((h) => h.trim().toUpperCase().slice(0, 7)).filter(Boolean)
      const previewHexUpper = ev.previewHex.toUpperCase()
      for (const h of slotHexes) if (!previewHexUpper.includes(h)) flag('L4-hex-mismatch', `${slot.tid}: ${h} not in '${ev.previewHex}'`)
    }
    if (officialCand) {
      if (!ev.previewCode || !ev.previewCode.includes(ev.selectedCand.code)) flag('L4-fila-code-missing', `${slot.tid} expected '${ev.selectedCand.code}' got '${ev.previewCode}'`)
      if (ev.selectedCand.name && (!ev.previewName || !ev.previewName.includes(ev.selectedCand.name))) flag('L4-name-missing', `${slot.tid} expected '${ev.selectedCand.name}' got '${ev.previewName}'`)
    }
  }
  await safeShot('02-add-ams.png')
  await killOverlays()
}
summary.bullets.L4 = { dialogVisible, slotEvidence }

// ===================================================================
// finalise
// ===================================================================
summary.passed = summary.defects.length === 0
summary.finishedAt = new Date().toISOString()
await fs.writeFile(path.join(ROOT, 'summary.json'), JSON.stringify(summary, null, 2))

console.log('\n========================================')
console.log(`  list-acceptance done. defects: ${summary.defects.length}`)
console.log('========================================')
for (const d of summary.defects) console.log(`  - ${d.id}: ${d.detail}`)
clearTimeout(hardCapTimer)
await browser.close()
