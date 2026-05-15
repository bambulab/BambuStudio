// Manual tab multicolor candidate picker probe.
//   1. open Add dialog -> manual tab
//   2. set brand=Bambu Lab, material=PLA Silk (which has 7 multi-hex SKUs)
//   3. click each multi-hex candidate, dump form preview state
//   4. verify preview swatchStyle is gradient/strips and resolver hits.
//
// Then: AMS-tab regression — switch A1->A2->A3->A4->A1 to confirm rapid
// toggling never reverts to candidates[0] (the bug we just fixed).
import { chromium } from '@playwright/test'
import fs from 'node:fs/promises'
import path from 'node:path'

const CDP = 'http://127.0.0.1:9222'
const OUT_DIR = path.resolve('.evidence/manual-multicolor')
await fs.mkdir(OUT_DIR, { recursive: true })

const browser = await chromium.connectOverCDP(CDP)
const ctx = browser.contexts()[0]
const pages = ctx.pages()
const candidates = pages.filter((p) => /index\.html/.test(p.url()) && /localhost:\d+/.test(p.url()))
const page = candidates[candidates.length - 1] || pages[pages.length - 1]
console.log('[probe] using:', page.url())

await page.bringToFront()
await page.evaluate(() => { if (!location.hash.includes('/filament')) location.hash = '#/filament' }).catch(() => undefined)
await page.waitForTimeout(300)
await page.getByTestId('filament-page-root').waitFor({ state: 'visible', timeout: 10_000 })

// Hard-close any open add-dialog
async function ensureDialogClosed() {
  const dialog = page.getByTestId('add-dialog')
  for (let i = 0; i < 5; i++) {
    if (!(await dialog.isVisible().catch(() => false))) return
    await page.getByTestId('dialog-cancel').first().click({ timeout: 2_000 }).catch(() => undefined)
    await dialog.waitFor({ state: 'detached', timeout: 2_000 }).catch(() => undefined)
  }
}
await ensureDialogClosed()

await page.getByTestId('filament-add').click({ timeout: 5_000 })
const dialog = page.getByTestId('add-dialog')
await dialog.waitFor({ state: 'visible', timeout: 10_000 })

// Manual tab is the default but click anyway to be sure.
await page.getByTestId('dialog-tab-manual').click({ timeout: 5_000 })
await page.waitForTimeout(300)

// Set brand=Bambu Lab + material=PLA Silk via the brand/material <select>s.
async function pickSelect(labelMatcher) {
  // Find a <select> whose options contain a label matching `labelMatcher`.
  const selects = await dialog.locator('select').elementHandles()
  for (const sel of selects) {
    const opts = await sel.evaluate((el) =>
      Array.from(el.querySelectorAll('option')).map((o) => o.textContent?.trim()),
    )
    if (opts.some((o) => labelMatcher.test(o))) {
      const target = opts.find((o) => labelMatcher.test(o))
      console.log('[probe] picking', target, 'in select with options', opts.slice(0, 6))
      await sel.evaluate((el, val) => {
        const opt = Array.from(el.querySelectorAll('option')).find((o) => o.textContent?.trim() === val)
        if (opt) {
          el.value = opt.value
          el.dispatchEvent(new Event('change', { bubbles: true }))
        }
      }, target)
      return true
    }
  }
  console.log('[probe] WARN: no select had option matching', labelMatcher)
  return false
}

await pickSelect(/^Bambu Lab$/)
await page.waitForTimeout(300)
await pickSelect(/^PLA Silk$/)
await page.waitForTimeout(800) // candidates load

// Discover all multi-hex candidates (color_type 0 or 1).
const multiCands = await page.evaluate(() => {
  const els = Array.from(document.querySelectorAll('[data-testid^="color-candidate-"]'))
  return els
    .map((el) => ({
      testid: el.getAttribute('data-testid'),
      code: el.getAttribute('data-color-code'),
      name: el.getAttribute('data-color-name'),
      colors: el.getAttribute('data-colors'),
      type: el.getAttribute('data-color-type'),
    }))
    .filter((c) => c.type === '0' || c.type === '1')
})
console.log('[probe] found', multiCands.length, 'multi-hex candidates')

const dumps = []
for (const c of multiCands) {
  if (!c.testid) continue
  await page.getByTestId(c.testid).click().catch((e) => console.log('[probe] click err', c.testid, e.message))
  await page.waitForTimeout(500)
  const snap = await page.evaluate(() => {
    const text = (sel) => document.querySelector(sel)?.textContent?.trim() ?? ''
    const attr = (sel, name) => document.querySelector(sel)?.getAttribute(name) ?? ''
    return {
      colorName: text('[data-testid="preview-color-name"]'),
      filaCode: text('[data-testid="preview-fila-code"]'),
      hexList: text('[data-testid="preview-hex-list"]'),
      swatchStyle: attr('[data-testid="preview-swatch"]', 'style'),
    }
  })
  dumps.push({ candidate: c, preview: snap })
  console.log(c.code, c.name, '->', JSON.stringify(snap))
}

await fs.writeFile(path.join(OUT_DIR, 'manual-multicolor.json'), JSON.stringify(dumps, null, 2))

// Close manual dialog and run rapid AMS toggle regression.
await ensureDialogClosed()
await page.getByTestId('filament-add').click()
await page.getByTestId('add-dialog').waitFor({ state: 'visible', timeout: 5_000 })
await page.getByTestId('dialog-tab-ams').click()
await page.waitForTimeout(400)

const printerSelect = page.getByTestId('add-dialog').locator('select').first()
const opts = await printerSelect.evaluate((el) =>
  Array.from(el.querySelectorAll('option')).map((o) => ({ value: o.value, label: o.textContent?.trim() })),
)
const target = opts.find((o) => /^129/.test(o.label || ''))
if (target) {
  await printerSelect.selectOption({ value: target.value })
  await page.waitForTimeout(1500)
}

const slots = ['ams-slot-0-0', 'ams-slot-0-1', 'ams-slot-0-2', 'ams-slot-0-3']
const sequence = [...slots, ...slots.slice().reverse(), slots[0], slots[2], slots[3], slots[1]]
const rapid = []
for (const slot of sequence) {
  await page.getByTestId(slot).click({ timeout: 3_000 }).catch(() => undefined)
  await page.locator(`[data-testid="${slot}"][data-selected="true"]`).waitFor({ state: 'visible', timeout: 4_000 }).catch(() => undefined)
  await page.waitForTimeout(450)
  const snap = await page.evaluate(() => ({
    colorName: document.querySelector('[data-testid="preview-color-name"]')?.textContent?.trim() ?? '',
    filaCode: document.querySelector('[data-testid="preview-fila-code"]')?.textContent?.trim() ?? '',
    hexList: document.querySelector('[data-testid="preview-hex-list"]')?.textContent?.trim() ?? '',
  }))
  rapid.push({ slot, ...snap })
}
await fs.writeFile(path.join(OUT_DIR, 'rapid-ams-toggle.json'), JSON.stringify(rapid, null, 2))
console.log('[probe] rapid toggle results:')
rapid.forEach((r) => console.log(' ', r.slot, '|', r.hexList, '|', r.colorName, r.filaCode))

await ensureDialogClosed()
console.log('[probe] DONE — evidence in', OUT_DIR)
await browser.close()
