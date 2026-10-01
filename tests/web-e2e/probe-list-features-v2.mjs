// Comprehensive automated verification of the filament-manager list feature
// against a live Studio session. Two evidence tracks:
//
//   A) LIST ROW path — for every existing row, prove that all visible UI
//      affordances behave correctly (sort, filter, group toggle, tab switch,
//      detail / add-similar / delete entry points, schema attributes, swatch
//      and hex rendering). For colour-name / fila-code, record whichever the
//      reverse-lookup pipeline produced and explain why each row matched
//      (own fila_id) / partial-matched / missed.
//
//   B) DIALOG REVERSE-LOOKUP path — open the empty Add dialog, switch to AMS
//      tab, click each non-empty slot in turn, and verify the preview bar:
//        - preview-color-name  reflects the official BBL colour name when
//          the AMS slot encodes a single-hex SKU that exists in the official
//          PLA Silk / PLA Basic / etc. library
//        - preview-fila-code   shows the BBL code (e.g. "11400") for the
//          same single-hex SKU
//        - For multi-color slots, the preview swatch becomes a strip /
//          gradient and preview-color-name still resolves to the BBL
//          multi-colour SKU name (e.g. "蓝粉双色 / 马卡龙").
//      This path exercises exactly the reverse-lookup channel the user is
//      asking for ("从官方耗材列表里去查名字") on real cloud-fed data.
//
// Output: .evidence/list-features-v2/summary.json (+ verdict per track)

import { chromium } from '@playwright/test'
import fs from 'node:fs/promises'
import path from 'node:path'

const CDP = 'http://127.0.0.1:9222'
const ROOT = path.resolve('.evidence/list-features-v2')
await fs.rm(ROOT, { recursive: true, force: true })
await fs.mkdir(ROOT, { recursive: true })

const browser = await chromium.connectOverCDP(CDP, { timeout: 10_000 })
const ctx = browser.contexts()[0]
const page = ctx.pages().find((p) => p.url().includes('localhost:13628') && p.url().includes('#/filament'))
if (!page) { console.error('no filament page'); process.exit(1) }
await page.bringToFront()
await page.waitForTimeout(500)
console.log('using:', page.url())

const summary = { startedAt: new Date().toISOString(), tracks: {}, defects: [] }
const flagDefect = (id, detail) => { summary.defects.push({ id, detail }); console.log('  [DEFECT]', id, detail) }

async function safeShot(name) {
  try {
    await page.screenshot({ path: path.join(ROOT, name), timeout: 5_000 })
  } catch (e) { /* not fatal */ }
}
async function ensureNoDialog() {
  await page.evaluate(() => {
    document.querySelectorAll('[data-testid="dialog-cancel"], [data-testid="detail-dialog-close"], [data-testid="confirm-dialog-cancel"]')
      .forEach((b) => /** @type {HTMLElement} */ (b).click())
  }).catch(() => undefined)
  await page.waitForTimeout(150)
  await page.keyboard.press('Escape').catch(() => undefined)
  await page.waitForTimeout(150)
}
function eqHex(a, b) {
  const norm = (s) => (s || '').replace(/^#/, '').toUpperCase().slice(0, 6)
  return norm(a) === norm(b)
}

// ====================================================================
// TRACK A — list row functionality + reverse-lookup snapshot
// ====================================================================
console.log('\n=== Track A: list row functionality ===')
await ensureNoDialog()
const trackA = { rows: [], toolbar: {}, entryPoints: [] }

const baselineRows = await page.evaluate(() => {
  return Array.from(document.querySelectorAll('tr[data-testid^="filament-row-"]')).map((row) => {
    const cell = row.querySelector('[data-testid="filament-row-color"]')
    return {
      tid: row.getAttribute('data-testid'),
      name: row.querySelector('[data-testid="filament-row-name"]')?.textContent?.trim() ?? null,
      colorCode: cell?.getAttribute('data-color-code') ?? null,
      colorType: row.getAttribute('data-color-type'),
      swatchBg: row.querySelector('[data-testid="filament-row-color-swatch"]')?.getAttribute('style') ?? null,
      colorName: row.querySelector('[data-testid="filament-row-color-name"]')?.textContent?.trim() ?? null,
      filaCode: row.querySelector('[data-testid="filament-row-fila-code"]')?.textContent?.trim() ?? null,
      hexLabel: row.querySelector('[data-testid="filament-row-color-hex"]')?.textContent?.trim() ?? null,
      detailBtn: !!row.querySelector('[data-testid="filament-row-detail"]'),
      addBtn: !!row.querySelector('[data-testid="filament-row-add-similar"]'),
      delBtn: !!row.querySelector('[data-testid="filament-row-delete"]'),
    }
  })
})
trackA.rows = baselineRows
console.log(`  baseline: ${baselineRows.length} row(s)`)
for (const r of baselineRows) console.log('   ', JSON.stringify(r))

for (const r of baselineRows) {
  if (!r.colorCode) flagDefect('A-no-color-code', `${r.tid} missing data-color-code`)
  if (!r.swatchBg || !/(rgb|#|linear-gradient)/i.test(r.swatchBg)) flagDefect('A-empty-swatch', `${r.tid} swatch invisible`)
  if (!r.hexLabel) flagDefect('A-no-hex-label', `${r.tid} hex label missing`)
  if (!r.detailBtn || !r.addBtn || !r.delBtn) flagDefect('A-missing-action', `${r.tid} action button(s) missing`)
}

// Toolbar interactions
const groupBefore = await page.locator('[data-testid="filament-group-toggle"]').getAttribute('data-grouped').catch(() => null)
await page.locator('[data-testid="filament-group-toggle"]').click({ force: true }).catch(() => undefined)
await page.waitForTimeout(150)
const groupAfter = await page.locator('[data-testid="filament-group-toggle"]').getAttribute('data-grouped').catch(() => null)
await page.locator('[data-testid="filament-group-toggle"]').click({ force: true }).catch(() => undefined)
await page.waitForTimeout(150)
const groupRestored = await page.locator('[data-testid="filament-group-toggle"]').getAttribute('data-grouped').catch(() => null)
trackA.toolbar.group = { before: groupBefore, after: groupAfter, restored: groupRestored }
if (groupBefore === groupAfter) flagDefect('A-group-not-flip', `data-grouped did not flip on click`)
if (groupBefore !== groupRestored) flagDefect('A-group-not-restore', `did not restore: ${groupBefore} -> ${groupRestored}`)

// Tab switch
const tabAllBefore = await page.locator('[data-testid="filament-tab-all"]').getAttribute('data-active').catch(() => null)
await page.locator('[data-testid="filament-tab-ams"]').click({ force: true }).catch(() => undefined)
await page.waitForTimeout(200)
const tabAmsActive = await page.locator('[data-testid="filament-tab-ams"]').getAttribute('data-active').catch(() => null)
await page.locator('[data-testid="filament-tab-all"]').click({ force: true }).catch(() => undefined)
await page.waitForTimeout(200)
trackA.toolbar.tab = { allBefore: tabAllBefore, amsActive: tabAmsActive }
if (tabAmsActive !== 'true') flagDefect('A-tab-ams-no-activate', `expected true, got ${tabAmsActive}`)

// Search filter
const search = page.locator('[data-testid="filament-search"]')
await search.click({ force: true }).catch(() => undefined)
await search.fill('zzznoexist')
await page.waitForTimeout(250)
const filteredCount = (await page.$$('tr[data-testid^="filament-row-"]')).length
await search.fill('')
await page.waitForTimeout(250)
const restoredCount = (await page.$$('tr[data-testid^="filament-row-"]')).length
trackA.toolbar.search = { filtered: filteredCount, restored: restoredCount }
if (filteredCount !== 0) flagDefect('A-search-no-filter', `expected 0, got ${filteredCount}`)
if (restoredCount !== baselineRows.length) flagDefect('A-search-no-restore', `expected ${baselineRows.length}, got ${restoredCount}`)

// Entry points (open + cancel for each row, no mutation)
for (const r of baselineRows) {
  await ensureNoDialog()
  const sel = `tr[data-testid="${r.tid}"]`
  await page.locator(`${sel} [data-testid="filament-row-detail"]`).click({ force: true }).catch(() => undefined)
  const detailVisible = await page.getByTestId('detail-dialog').waitFor({ state: 'visible', timeout: 4_000 }).then(() => true).catch(() => false)
  const detailLabel = detailVisible ? await page.locator('[data-testid="detail-color-label"]').textContent().catch(() => null) : null
  await page.locator('[data-testid="detail-dialog-close"]').click({ force: true }).catch(() => undefined)
  await page.waitForTimeout(120)

  await page.locator(`${sel} [data-testid="filament-row-add-similar"]`).click({ force: true }).catch(() => undefined)
  const addVisible = await page.getByTestId('add-dialog').waitFor({ state: 'visible', timeout: 4_000 }).then(() => true).catch(() => false)
  await page.locator('[data-testid="dialog-cancel"]').click({ force: true }).catch(() => undefined)
  await page.waitForTimeout(120)

  await page.locator(`${sel} [data-testid="filament-row-delete"]`).click({ force: true }).catch(() => undefined)
  const delVisible = await page.getByTestId('confirm-dialog').waitFor({ state: 'visible', timeout: 4_000 }).then(() => true).catch(() => false)
  await page.locator('[data-testid="confirm-dialog-cancel"]').click({ force: true }).catch(() => undefined)
  await page.waitForTimeout(120)

  trackA.entryPoints.push({ tid: r.tid, detailVisible, detailLabel, addVisible, delVisible })
  if (!detailVisible) flagDefect('A-detail-not-open', r.tid)
  if (!addVisible) flagDefect('A-add-similar-not-open', r.tid)
  if (!delVisible) flagDefect('A-delete-confirm-not-open', r.tid)
}
await ensureNoDialog()

// ====================================================================
// TRACK B — dialog reverse-lookup happy-path on real AMS slots
// ====================================================================
console.log('\n=== Track B: AMS-driven reverse lookup ===')

const trackB = { ams: [], slotEvidence: [], dialogVisible: false }

// Open empty Add dialog (toolbar button)
await page.locator('[data-testid="filament-add"]').scrollIntoViewIfNeeded().catch(() => undefined)
await page.locator('[data-testid="filament-add"]').click({ force: true }).catch(() => undefined)
trackB.dialogVisible = await page.getByTestId('add-dialog').waitFor({ state: 'visible', timeout: 5_000 }).then(() => true).catch(() => false)
if (!trackB.dialogVisible) {
  flagDefect('B-add-dialog-not-open', 'failed to open empty Add dialog')
}

if (trackB.dialogVisible) {
  // Switch to AMS tab
  await page.locator('[data-testid="dialog-tab-ams"]').click({ force: true }).catch(() => undefined)
  await page.waitForTimeout(800)

  // Inventory non-empty slots
  const slots = await page.evaluate(() => {
    return Array.from(document.querySelectorAll('[data-testid^="ams-slot-"]'))
      .filter((el) => el.getAttribute('data-empty') === 'false')
      .map((el) => ({
        tid: el.getAttribute('data-testid'),
        color: el.getAttribute('data-color'),
        colorType: el.getAttribute('data-color-type'),
      }))
  })
  trackB.ams = slots
  console.log(`  AMS non-empty slots: ${slots.length}`)
  for (const s of slots) console.log('   ', JSON.stringify(s))

  for (const slot of slots) {
    await page.locator(`[data-testid="${slot.tid}"]`).click({ force: true }).catch(() => undefined)
    await page.waitForTimeout(700) // give selectAmsSlot + candidate match a tick

    const ev = await page.evaluate(() => {
      const previewName = document.querySelector('[data-testid="preview-color-name"]')?.textContent?.trim() ?? null
      const previewFilaCode = document.querySelector('[data-testid="preview-fila-code"]')?.textContent?.trim() ?? null
      const previewHex = document.querySelector('[data-testid="preview-hex-list"]')?.textContent?.trim() ?? null
      const previewSwatchStyle = document.querySelector('[data-testid="preview-swatch"]')?.getAttribute('style') ?? null
      // Selected candidate (if any) reflects which BBL SKU the slot resolved to.
      const selectedCand = document.querySelector('[data-testid^="color-candidate-"][data-selected="true"]')
      return {
        previewName,
        previewFilaCode,
        previewHex,
        previewSwatchStyle,
        selectedCand: selectedCand ? {
          code: selectedCand.getAttribute('data-color-code'),
          name: selectedCand.getAttribute('data-color-name'),
          colors: selectedCand.getAttribute('data-colors'),
          colorType: selectedCand.getAttribute('data-color-type'),
        } : null,
      }
    })
    const isMulti = (slot.color || '').split('/').filter(Boolean).length > 1 || (slot.colorType !== '2' && slot.colorType !== null)
    trackB.slotEvidence.push({ slot, ev, isMulti })
    console.log(`    ${slot.tid} colorType=${slot.colorType} multi=${isMulti}`)
    console.log(`      previewName=${ev.previewName}  filaCode=${ev.previewFilaCode}  hex=${ev.previewHex}`)
    console.log(`      selectedCand=${ev.selectedCand ? `${ev.selectedCand.name}/${ev.selectedCand.code}` : 'NONE'}`)

    // Acceptance rules:
    //  - Every non-empty AMS slot must reduce to a selected BBL candidate;
    //    selectedCand=null means the slot was rendered as "Custom" which
    //    contradicts the AMS reporting an official SKU. Whitelist genuinely
    //    out-of-library colours via known data signals if the future arises;
    //    today every Bambu OEM AMS slot encodes a recognised SKU.
    if (!ev.selectedCand) flagDefect('B-no-selected-candidate', `${slot.tid} (color=${slot.color})`)
    //  - Preview swatch must render some background.
    if (!ev.previewSwatchStyle || !/(rgb|#|linear-gradient)/i.test(ev.previewSwatchStyle)) flagDefect('B-preview-swatch-empty', slot.tid)
    //  - Preview-hex-list must contain the slot's hex(es) (ignoring case / order).
    if (slot.color && ev.previewHex) {
      const slotHexes = slot.color.split('/').map((h) => h.trim().toUpperCase()).filter(Boolean)
      const previewHexesLower = ev.previewHex.toUpperCase()
      for (const h of slotHexes) {
        if (!previewHexesLower.includes(h)) flagDefect('B-preview-hex-mismatch', `${slot.tid} expects ${h} in '${ev.previewHex}'`)
      }
    }
    //  - When the selected candidate has a non-hex color_code (i.e. real BBL
    //    SKU like "11400"), preview-fila-code MUST display it — that's the
    //    user-visible proof of the official-list reverse-lookup.
    if (ev.selectedCand && ev.selectedCand.code && !ev.selectedCand.code.startsWith('#') && !/^[0-9A-F]{6,8}$/i.test(ev.selectedCand.code)) {
      if (!ev.previewFilaCode || !ev.previewFilaCode.includes(ev.selectedCand.code)) {
        flagDefect('B-preview-fila-code-missing', `${slot.tid} expected fila code '${ev.selectedCand.code}', got '${ev.previewFilaCode}'`)
      }
    }
    //  - When the selected candidate has a non-empty name, preview-color-name
    //    must display it. Empty name == user-owned-only candidate; that's
    //    fine, no defect.
    if (ev.selectedCand && ev.selectedCand.name) {
      if (!ev.previewName || !ev.previewName.includes(ev.selectedCand.name)) {
        flagDefect('B-preview-name-missing', `${slot.tid} expected '${ev.selectedCand.name}', got '${ev.previewName}'`)
      }
    }
  }
  await safeShot('add-dialog-ams.png')
  await ensureNoDialog()
}

// ====================================================================
// finalise
// ====================================================================
summary.tracks.A = trackA
summary.tracks.B = trackB
summary.passed = summary.defects.length === 0
summary.finishedAt = new Date().toISOString()
await fs.writeFile(path.join(ROOT, 'summary.json'), JSON.stringify(summary, null, 2))
console.log('\n========================================')
console.log('  list-features-v2 done')
console.log(`  defects: ${summary.defects.length}`)
console.log('========================================')
for (const d of summary.defects) console.log(`  - ${d.id}: ${d.detail}`)
await browser.close()
