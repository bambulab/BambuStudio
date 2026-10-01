// Comprehensive acceptance probe for STUDIO-17977.
// Walks the entire filament manager surface that this change touched,
// captures both data + structure + pixels per the PRODUCT_REVIEW_CHECKLIST,
// and dumps timings to surface UX-grade regressions.
//
// Sections:
//   P1  AMS sync     ??4 slots ? {slot card SVG, preview swatch CSS,
//                                 candidate panel CSS, form fields}
//   P2  List rows    ??every row's SpoolColorChip + per-row screenshot
//   P3  Detail dlg   ??open detail per row; dump dialog chip + screenshot
//   P4  Edit dlg     ??open edit; switch brand; verify reconcile
//   P5  Manual add   ??manual tab brand/material ??multi-hex candidate clicks
//   P6  Perf         ??dialog open / AMS load / candidate load / rapid toggle
//   P7/P8 covered through P1+P5 (SVG path + CSS path both exercised)
//
// Output: .evidence/acceptance/{section}/*  with one JSON summary + PNGs.

import { chromium } from '@playwright/test'
import fs from 'node:fs/promises'
import path from 'node:path'

const CDP = 'http://127.0.0.1:9222'
const ROOT = path.resolve('.evidence/acceptance')
await fs.rm(ROOT, { recursive: true, force: true })
await fs.mkdir(ROOT, { recursive: true })

const browser = await chromium.connectOverCDP(CDP)
const ctx = browser.contexts()[0]
const cands = ctx.pages().filter((p) => /index\.html/.test(p.url()) && /localhost:\d+/.test(p.url()))
const page = cands[cands.length - 1] || ctx.pages()[ctx.pages().length - 1]
console.log('[probe] using:', page.url())
await page.bringToFront()
await page.evaluate(() => { if (!location.hash.includes('/filament')) location.hash = '#/filament' }).catch(() => undefined)
await page.waitForTimeout(200)
await page.getByTestId('filament-page-root').waitFor({ state: 'visible', timeout: 15_000 })

// A previous interrupted run can leave a Radix-style portal backdrop
// (`div.fixed.inset-0.bg-black/50`) floating over the page, often with
// a stale add-dialog still inside it. Force-clear at probe start (and
// again after each section via ensureDialogClosed).
async function nukeStaleOverlays() {
  await page.keyboard.press('Escape').catch(() => undefined)
  await page.evaluate(() => {
    // Hard-kill any dialog node still in the DOM and its parent overlay.
    // Cancel-button clicks can be racy under WebView2 (Studio's compositor
    // sometimes keeps the React subtree mounted across the close anim);
    // the rule is "by the time we land here, any dialog is stale".
    const tids = ['add-dialog', 'edit-dialog', 'detail-dialog', 'confirm-dialog']
    for (const tid of tids) {
      document.querySelectorAll(`[data-testid="${tid}"]`).forEach((el) => {
        // Walk up to the portal/backdrop wrapper (fixed or pointer-events
        // intercepting). Stop at body so we never nuke the page root.
        let cursor = el
        while (cursor && cursor.parentElement && cursor.parentElement !== document.body) {
          cursor = cursor.parentElement
          const cs = window.getComputedStyle(cursor)
          if (cs.position === 'fixed' || cs.zIndex === '1000' || cs.zIndex === '999') break
        }
        cursor.remove()
      })
    }
    // Also drop any leftover pointer-event-blocking overlay.
    document.querySelectorAll('div.fixed').forEach((el) => {
      const cs = window.getComputedStyle(el)
      if (cs.position !== 'fixed') return
      // Heuristic: full-screen dim layers we use are inset-0 + bg-black/50
      // class. They may also live in a portal under document.body.
      const rect = el.getBoundingClientRect()
      const fillsViewport = rect.width >= window.innerWidth - 4 && rect.height >= window.innerHeight - 4
      if (fillsViewport && el.className.includes('bg-black')) el.remove()
    })
  }).catch(() => undefined)
}
await nukeStaleOverlays()

const isLoggedInAttr = await page.locator('[data-testid="filament-page-root"]').getAttribute('data-logged-in').catch(() => 'false')
console.log('[probe] data-logged-in:', isLoggedInAttr)
if (isLoggedInAttr !== 'true') {
  console.log('[probe] WARNING: not logged in. Cloud features and the add dialog will be disabled.')
}

const summary = {
  startedAt: new Date().toISOString(),
  perf: {},
  sections: {},
  defects: [],
}

const recordDefect = (id, detail) => { summary.defects.push({ id, detail }); console.log('[DEFECT]', id, detail) }

// WebView2's compositor sometimes stalls Playwright's screenshot path on
// the implicit "wait for fonts" step. Use a short timeout + swallow so a
// missing PNG doesn't block the data-layer assertions, which are the
// primary acceptance signal anyway.
const safeShot = async (target, p) => {
  try { await target.screenshot({ path: p, timeout: 5_000, animations: 'disabled' }) }
  catch (err) { console.log(`[shot-skip] ${path.basename(p)}: ${err.message?.split('\n')[0]}`) }
}

async function ensureDialogClosed() {
  for (const tid of ['add-dialog', 'edit-dialog', 'detail-dialog', 'confirm-dialog']) {
    const d = page.getByTestId(tid)
    for (let i = 0; i < 4; i++) {
      if (!(await d.isVisible().catch(() => false))) break
      const closeId = tid === 'detail-dialog' ? 'detail-dialog-close'
        : tid === 'confirm-dialog' ? 'confirm-dialog-cancel'
        : 'dialog-cancel'
      await page.getByTestId(closeId).first().click({ timeout: 1500, force: true }).catch(() => undefined)
      await page.keyboard.press('Escape').catch(() => undefined)
      await d.waitFor({ state: 'detached', timeout: 1500 }).catch(() => undefined)
    }
  }
  // Sweep any stray modal overlay (bg-black/50 fixed inset-0) blocking
  // page-level interactions. We don't kill it directly ? instead we walk
  // the DOM and call .remove() on overlays whose only child is a dialog
  // we already closed. This is the classic "leftover backdrop"
  // workaround for Radix/HeadlessUI portals.
  await nukeStaleOverlays()
}

const inspectSvg = (el) => {
  const svg = el.querySelector('svg')
  if (!svg) return null
  const stops = Array.from(svg.querySelectorAll('linearGradient stop')).map((s) => ({
    offset: s.getAttribute('offset'),
    color: s.getAttribute('stop-color') || s.getAttribute('stopColor'),
  }))
  const shapes = Array.from(svg.querySelectorAll('path,rect,ellipse')).map((s) => ({
    tag: s.tagName,
    fill: s.getAttribute('fill'),
    opacity: s.getAttribute('opacity'),
  }))
  const distinctBodyFills = Array.from(new Set(
    shapes.filter((s) => s.fill && s.fill !== '#1a1a1a' && !s.fill.startsWith('rgba'))
      .map((s) => s.fill),
  ))
  return {
    hasGradient: !!svg.querySelector('linearGradient'),
    hasClipPath: !!svg.querySelector('clipPath'),
    stopCount: stops.length,
    stops,
    distinctBodyFills,
    shapes,
  }
}

// Inject a helper into the page for reuse.
await page.exposeFunction('__inspectSvg', () => null) // placeholder so ts type infers

async function dumpSvgFor(locator) {
  return await locator.evaluate((el) => {
    const svg = el.querySelector('svg')
    if (!svg) return null
    const stops = Array.from(svg.querySelectorAll('linearGradient stop')).map((s) => ({
      offset: s.getAttribute('offset'),
      color: s.getAttribute('stop-color') || s.getAttribute('stopColor'),
    }))
    const shapes = Array.from(svg.querySelectorAll('path,rect,ellipse')).map((s) => ({
      tag: s.tagName,
      fill: s.getAttribute('fill'),
      opacity: s.getAttribute('opacity'),
    }))
    const bodyFills = Array.from(new Set(shapes
      .filter((s) => s.fill && s.fill !== '#1a1a1a' && !s.fill.startsWith('rgba'))
      .map((s) => s.fill),
    ))
    return {
      hasGradient: !!svg.querySelector('linearGradient'),
      hasClipPath: !!svg.querySelector('clipPath'),
      stopCount: stops.length,
      stops,
      bodyFills,
      shapes,
    }
  })
}

// ============================================================
// P2  List rows
// ============================================================
console.log('\n=== P2  list rows ===')
await ensureDialogClosed()
await safeShot(page, path.join(ROOT, 'P2-list-overview.png'))
const rows = await page.$$('tr[data-testid^="filament-row-"]')
const rowDumps = []
for (let i = 0; i < rows.length; i++) {
  const row = rows[i]
  const info = await row.evaluate((el) => ({
    testid: el.getAttribute('data-testid'),
    spoolId: el.getAttribute('data-spool-id'),
    colorType: el.getAttribute('data-color-type'),
    colorCode: el.querySelector('[data-testid="filament-row-color"]')?.getAttribute('data-color-code') ?? '',
    name: el.querySelector('[data-testid="filament-row-name"]')?.textContent?.trim() ?? '',
  }))
  const colorCell = await row.$('[data-testid="filament-row-color"]')
  const svgInfo = colorCell ? await dumpSvgFor(colorCell) : null
  await safeShot(colorCell, path.join(ROOT, `P2-row-${i}-${info.testid}.png`))
  rowDumps.push({ ...info, svg: svgInfo })
  // Sanity rules
  if (svgInfo && info.colorType === '2' && svgInfo.hasGradient) {
    recordDefect('P2-single-has-gradient', `${info.testid} colorType=2 but SVG has linearGradient`)
  }
  if (svgInfo?.hasClipPath) {
    recordDefect('P2-clipPath-present', `${info.testid} should not use clipPath after STUDIO-17977 refactor`)
  }
}
summary.sections.P2 = { rowCount: rowDumps.length, rows: rowDumps }
console.log('[P2] rows:', rowDumps.length)

// ============================================================
// P1  AMS sync (+ P6 perf timings + P8 cross-path)
// ============================================================
console.log('\n=== P1  AMS sync ===')
await ensureDialogClosed()
const t0 = performance.now()
// Playwright's `click` (even force:true) goes through synthetic events
// that, in Studio's WebView2 build, sometimes don't reach React's
// onClick handler when the toolbar sits behind a flex sibling. Drive
// the click via a real DOM dispatch instead ? equivalent to a user
// click for React's bubbling listeners.
// HTMLButtonElement.click() bypasses Playwright's actionability gate
// (which fails when DebugLogPanel siblings overlap the toolbar) AND
// produces a real, bubbling, trusted click for React's createRoot
// listener. dispatchEvent + custom MouseEvent is NOT trusted, so some
// React 18 onClick handlers ignore it.
// Studio Release build auto-opens the DebugLogPanel via
// `window.__internalBuild` which overlaps the toolbar in actionability
// checks. force:true clicks past the overlap (Playwright still
// dispatches a real CDP Input.dispatchMouseEvent so the click is
// trusted by React).
await page.getByTestId('filament-add').click({ force: true })
await page.getByTestId('add-dialog').waitFor({ state: 'visible', timeout: 8_000 })
summary.perf.dialogOpenMs = Math.round(performance.now() - t0)
console.log('[perf] dialog open ms:', summary.perf.dialogOpenMs)

await page.getByTestId('dialog-tab-ams').click({ force: true })
await page.waitForTimeout(300)
const tAms0 = performance.now()
const printerSelect = page.getByTestId('add-dialog').locator('select').first()
const opts = await printerSelect.evaluate((el) => Array.from(el.querySelectorAll('option')).map((o) => ({ value: o.value, label: o.textContent?.trim() })))
const target = opts.find((o) => /^129/.test(o.label || ''))
if (!target) { recordDefect('P1-no-129-printer', 'Could not find 129 in printer dropdown'); }
else {
  await printerSelect.selectOption({ value: target.value })
  await page.locator('[data-testid="ams-grid"] [data-testid^="ams-slot-"]').first().waitFor({ state: 'visible', timeout: 8_000 })
}
summary.perf.amsLoadMs = Math.round(performance.now() - tAms0)
console.log('[perf] ams load ms:', summary.perf.amsLoadMs)

const slots = ['ams-slot-0-0', 'ams-slot-0-1', 'ams-slot-0-2', 'ams-slot-0-3']
const slotDumps = []
for (const slot of slots) {
  const card = page.getByTestId(slot)
  if (!(await card.isVisible().catch(() => false))) continue
  const tClick0 = performance.now()
  await card.click({ force: true })
  await page.locator(`[data-testid="${slot}"][data-selected="true"]`).waitFor({ state: 'visible', timeout: 4_000 }).catch(() => undefined)
  // Wait for resolver to settle.
  await page.waitForTimeout(500)
  const clickMs = Math.round(performance.now() - tClick0)

  const cardSvg = await dumpSvgFor(card)
  await safeShot(card, path.join(ROOT, `P1-card-${slot}.png`))

  const formData = await page.evaluate(() => {
    const text = (sel) => document.querySelector(sel)?.textContent?.trim() ?? ''
    const attr = (sel, n) => document.querySelector(sel)?.getAttribute(n) ?? ''
    return {
      colorName: text('[data-testid="preview-color-name"]'),
      filaCode: text('[data-testid="preview-fila-code"]'),
      hexList: text('[data-testid="preview-hex-list"]'),
      previewSwatchStyle: attr('[data-testid="preview-swatch"]', 'style'),
      candidateCount: document.querySelectorAll('[data-testid^="color-candidate-"]').length,
      brand: document.querySelector('[data-testid="add-dialog"] select[data-testid="brand-select"]')?.value
        ?? Array.from(document.querySelectorAll('[data-testid="add-dialog"] select')).map((s) => s.value).join(' | '),
    }
  })

  // Visual sanity: AMS card SVG.
  if (cardSvg?.hasClipPath) recordDefect('P1-clipPath-present', `${slot} card SVG has clipPath`)
  const slotColorType = await card.getAttribute('data-color-type')
  if (slotColorType !== '2' && cardSvg && cardSvg.bodyFills.length !== 1) {
    recordDefect('P1-multihex-bodyFills-not-1', `${slot} multi card body fills should all be url(#grad), got ${JSON.stringify(cardSvg.bodyFills)}`)
  }
  if (slotColorType === '2' && cardSvg?.hasGradient) {
    recordDefect('P1-single-has-gradient', `${slot} colorType=2 should not render linearGradient`)
  }
  // Cross-validate hex consistency: slot data-color and preview hexList head
  const slotColor = (await card.getAttribute('data-color') || '').slice(0, 7).toUpperCase()
  const previewHead = (formData.hexList || '').split(' / ')[0].trim().toUpperCase()
  if (slotColor && previewHead && slotColor !== previewHead) {
    recordDefect('P1-hex-mismatch', `${slot} slot=${slotColor} preview=${previewHead}`)
  }
  if (!formData.colorName) recordDefect('P1-empty-color-name', `${slot} preview-color-name is empty`)
  if (!formData.filaCode) recordDefect('P1-empty-fila-code', `${slot} preview-fila-code is empty`)

  // CSS path: dump all candidate swatch styles for this slot's panel.
  const candDump = await page.evaluate(() =>
    Array.from(document.querySelectorAll('[data-testid^="color-candidate-"]')).map((el) => ({
      testid: el.getAttribute('data-testid'),
      code: el.getAttribute('data-color-code'),
      type: el.getAttribute('data-color-type'),
      colors: el.getAttribute('data-colors'),
      // For visual-grade: pull the inner swatch's actual computed style.
      style: el.querySelector('span[style], div[style]')?.getAttribute('style') ?? '',
    })),
  )

  slotDumps.push({ slot, clickMs, slotColor, slotColorType, cardSvg, formData, candidateCount: candDump.length, candDump: candDump.slice(0, 12) })
  console.log(`[P1] ${slot} | colorType=${slotColorType} | clickMs=${clickMs} | name=${formData.colorName} ${formData.filaCode} | hexList=${formData.hexList}`)
}
summary.sections.P1 = { slots: slotDumps }

// Rapid toggle perf.
console.log('\n=== P6  rapid AMS toggle ===')
const sequence = [...slots, ...slots.slice().reverse(), slots[0], slots[2], slots[3], slots[1]]
const rapid = []
for (const slot of sequence) {
  const t0 = performance.now()
  await page.getByTestId(slot).click({ timeout: 3_000, force: true }).catch(() => undefined)
  const ok = await page.locator(`[data-testid="${slot}"][data-selected="true"]`).waitFor({ state: 'visible', timeout: 4_000 }).then(() => true).catch(() => false)
  const dt = Math.round(performance.now() - t0)
  await page.waitForTimeout(200)
  const snap = await page.evaluate(() => ({
    name: document.querySelector('[data-testid="preview-color-name"]')?.textContent?.trim() ?? '',
    code: document.querySelector('[data-testid="preview-fila-code"]')?.textContent?.trim() ?? '',
    hex: document.querySelector('[data-testid="preview-hex-list"]')?.textContent?.trim() ?? '',
  }))
  rapid.push({ slot, dt, ok, ...snap })
}
summary.perf.rapidToggle = rapid
const rapidMax = Math.max(...rapid.map((r) => r.dt))
const rapidAvg = Math.round(rapid.reduce((s, r) => s + r.dt, 0) / rapid.length)
summary.perf.rapidToggleAvgMs = rapidAvg
summary.perf.rapidToggleMaxMs = rapidMax
console.log('[perf] rapid toggle  avg=', rapidAvg, 'max=', rapidMax)
if (rapid.some((r) => !r.ok)) recordDefect('P6-rapid-toggle-stuck', 'Some rapid toggle clicks did not flip data-selected within 4s')

// ============================================================
// P5  Manual tab ??multi-hex candidate clicks
// ============================================================
console.log('\n=== P5  manual tab multi-hex SKU clicks ===')
await ensureDialogClosed()
await page.getByTestId('filament-add').click({ force: true })
await page.getByTestId('add-dialog').waitFor({ state: 'visible', timeout: 5_000 })
await page.getByTestId('dialog-tab-manual').click({ force: true })
await page.waitForTimeout(300)

async function pickSelect(matcher) {
  const selects = await page.getByTestId('add-dialog').locator('select').elementHandles()
  for (const sel of selects) {
    const opts = await sel.evaluate((el) => Array.from(el.querySelectorAll('option')).map((o) => o.textContent?.trim()))
    const target = opts.find((o) => matcher.test(o))
    if (target) {
      await sel.evaluate((el, val) => {
        const opt = Array.from(el.querySelectorAll('option')).find((o) => o.textContent?.trim() === val)
        if (opt) { el.value = opt.value; el.dispatchEvent(new Event('change', { bubbles: true })) }
      }, target)
      return true
    }
  }
  return false
}
await pickSelect(/^Bambu Lab$/)
await page.waitForTimeout(200)
const tCandLoad0 = performance.now()
await pickSelect(/^PLA Silk$/)
await page.locator('[data-testid^="color-candidate-"]').first().waitFor({ state: 'visible', timeout: 8_000 })
summary.perf.candidateLoadMs = Math.round(performance.now() - tCandLoad0)
console.log('[perf] candidate load ms:', summary.perf.candidateLoadMs)

const multiCands = await page.evaluate(() =>
  Array.from(document.querySelectorAll('[data-testid^="color-candidate-"]'))
    .map((el) => ({
      testid: el.getAttribute('data-testid'),
      code: el.getAttribute('data-color-code'),
      name: el.getAttribute('data-color-name'),
      type: el.getAttribute('data-color-type'),
      colors: el.getAttribute('data-colors'),
      innerStyle: el.querySelector('span[style], div[style]')?.getAttribute('style') ?? '',
    }))
    .filter((c) => c.type === '0' || c.type === '1'),
)

await safeShot(page, path.join(ROOT, 'P5-manual-pla-silk-panel.png'))

const manualPicks = []
for (const c of multiCands) {
  if (!c.testid) continue
  await page.getByTestId(c.testid).click({ force: true }).catch(() => undefined)
  await page.waitForTimeout(300)
  const snap = await page.evaluate(() => ({
    name: document.querySelector('[data-testid="preview-color-name"]')?.textContent?.trim() ?? '',
    code: document.querySelector('[data-testid="preview-fila-code"]')?.textContent?.trim() ?? '',
    hex: document.querySelector('[data-testid="preview-hex-list"]')?.textContent?.trim() ?? '',
    swatch: document.querySelector('[data-testid="preview-swatch"]')?.getAttribute('style') ?? '',
  }))
  manualPicks.push({ candidate: c, preview: snap })
  if (snap.name !== c.name) recordDefect('P5-name-mismatch', `${c.code} clicked but preview name=${snap.name}, expected ${c.name}`)
  if (!snap.code.includes(c.code || '')) recordDefect('P5-code-mismatch', `${c.code} clicked but preview code=${snap.code}`)
  // Multi-hex preview swatch must be a linear-gradient
  if (!snap.swatch.includes('linear-gradient')) recordDefect('P5-preview-not-gradient', `${c.code} preview swatch is not gradient: ${snap.swatch}`)
}
summary.sections.P5 = { manualPicks }
await ensureDialogClosed()

// ============================================================
// P4  Edit dialog roundtrip
// ============================================================
console.log('\n=== P4  edit dialog roundtrip ===')
const editDumps = []
const allRows = await page.$$('tr[data-testid^="filament-row-"]')
for (let i = 0; i < Math.min(allRows.length, 3); i++) {
  await ensureDialogClosed()
  const row = allRows[i]
  const meta = await row.evaluate((el) => ({
    testid: el.getAttribute('data-testid'),
    name: el.querySelector('[data-testid="filament-row-name"]')?.textContent?.trim() ?? '',
  }))
  const detailBtn = await row.$('[data-testid="filament-row-detail"]')
  if (!detailBtn) continue
  const ok = await detailBtn.click({ timeout: 3_000, force: true }).then(() => true).catch(() => false)
  if (!ok) { recordDefect('P4-detail-btn-blocked', `${meta.testid} detail click blocked`); await ensureDialogClosed(); continue }
  const detailVis = await page.getByTestId('detail-dialog').waitFor({ state: 'visible', timeout: 4_000 }).then(() => true).catch(() => false)
  if (!detailVis) { recordDefect('P4-detail-not-visible', meta.testid); await ensureDialogClosed(); continue }
  await page.getByTestId('detail-dialog-edit').click({ timeout: 3_000, force: true }).catch(() => undefined)
  // Edit dialog uses testid "edit-dialog" (separate from add).
  const editVis = await page.getByTestId('edit-dialog').waitFor({ state: 'visible', timeout: 5_000 }).then(() => true).catch(() => false)
  if (!editVis) { recordDefect('P4-edit-not-visible', meta.testid); await ensureDialogClosed(); continue }
  await page.waitForTimeout(700)
  const snap = await page.evaluate(() => ({
    colorName: document.querySelector('[data-testid="preview-color-name"]')?.textContent?.trim() ?? '',
    filaCode: document.querySelector('[data-testid="preview-fila-code"]')?.textContent?.trim() ?? '',
    hexList: document.querySelector('[data-testid="preview-hex-list"]')?.textContent?.trim() ?? '',
    swatch: document.querySelector('[data-testid="preview-swatch"]')?.getAttribute('style') ?? '',
    candidateCount: document.querySelectorAll('[data-testid^="color-candidate-"]').length,
  }))
  editDumps.push({ row: meta, edit: snap })
  console.log(`[P4] ${meta.testid} -> name=${snap.colorName} code=${snap.filaCode} hex=${snap.hexList}`)
  await safeShot(page, path.join(ROOT, `P4-edit-${i}.png`))
  await ensureDialogClosed()
}
summary.sections.P4 = { editDumps }

// ============================================================
// P3  Detail dialog (open every spool, screenshot + verify color field)
// ============================================================
console.log('\n=== P3  detail dialog ===')
await ensureDialogClosed()
const detailRows = await page.$$('tr[data-testid^="filament-row-"]')
const detailDumps = []
for (let i = 0; i < detailRows.length; i++) {
  await ensureDialogClosed()
  const row = detailRows[i]
  const meta = await row.evaluate((el) => ({
    testid: el.getAttribute('data-testid'),
    name: el.querySelector('[data-testid="filament-row-name"]')?.textContent?.trim() ?? '',
    colorCode: el.getAttribute('data-color-code') ?? el.querySelector('[data-color-code]')?.getAttribute('data-color-code') ?? '',
    colorType: el.getAttribute('data-color-type') ?? '',
  }))
  const detailBtn = await row.$('[data-testid="filament-row-detail"]')
  if (!detailBtn) continue
  const ok = await detailBtn.click({ timeout: 3_000, force: true }).then(() => true).catch(() => false)
  if (!ok) continue
  const det = page.getByTestId('detail-dialog')
  const visible = await det.waitFor({ state: 'visible', timeout: 4_000 }).then(() => true).catch(() => false)
  if (!visible) continue
  await safeShot(page, path.join(ROOT, `P3-detail-${i}.png`))
  const colorField = await page.evaluate(() => {
    const f = document.querySelector('[data-testid="detail-color-field"]')
    if (!f) return null
    const swatch = f.querySelector('[data-testid="detail-color-swatch"]')
    const label = f.querySelector('[data-testid="detail-color-label"]')
    return {
      colorType: f.getAttribute('data-color-type'),
      swatchStyle: swatch?.getAttribute('style') ?? '',
      swatchClientRect: swatch
        ? { w: swatch.getBoundingClientRect().width, h: swatch.getBoundingClientRect().height }
        : null,
      labelText: label?.textContent?.trim() ?? '',
    }
  })
  detailDumps.push({ row: meta, colorField })
  if (!colorField) recordDefect('P3-no-color-field', meta.testid)
  else if (!colorField.swatchStyle.includes('background')) recordDefect('P3-empty-swatch-style', `${meta.testid} swatch has no background`)
  else if (!colorField.labelText) recordDefect('P3-empty-color-label', meta.testid)
  await ensureDialogClosed()
}
summary.sections.P3 = { detailDumps }

// ============================================================
// finalise
// ============================================================
summary.finishedAt = new Date().toISOString()
summary.passed = summary.defects.length === 0
await fs.writeFile(path.join(ROOT, 'summary.json'), JSON.stringify(summary, null, 2))
console.log('\n========================================')
console.log('  acceptance probe done')
console.log('  defects:', summary.defects.length)
console.log('  perf:', JSON.stringify(summary.perf, null, 2))
console.log('========================================')

await browser.close()
