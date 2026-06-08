// Visual-grade probe: for every AMS slot card, dump the SpoolColorChip's
// full SVG outerHTML *and* take a focused screenshot of just the chip
// element so we can compare pixel output, not just attribute strings.
// Catches: shapes hidden behind overlays, clipPath misuse, gradient
// not applied, etc.
import { chromium } from '@playwright/test'
import fs from 'node:fs/promises'
import path from 'node:path'

const CDP = 'http://127.0.0.1:9222'
const OUT_DIR = path.resolve('.evidence/chip-visual')
await fs.mkdir(OUT_DIR, { recursive: true })

const browser = await chromium.connectOverCDP(CDP)
const ctx = browser.contexts()[0]
const pages = ctx.pages()
const cands = pages.filter((p) => /index\.html/.test(p.url()) && /localhost:\d+/.test(p.url()))
const page = cands[cands.length - 1] || pages[pages.length - 1]
await page.bringToFront()
await page.evaluate(() => { if (!location.hash.includes('/filament')) location.hash = '#/filament' }).catch(() => undefined)
await page.waitForTimeout(200)
await page.getByTestId('filament-page-root').waitFor({ state: 'visible', timeout: 10_000 })

async function ensureDialogClosed() {
  for (const tid of ['add-dialog', 'edit-dialog', 'detail-dialog']) {
    const d = page.getByTestId(tid)
    for (let i = 0; i < 4; i++) {
      if (!(await d.isVisible().catch(() => false))) break
      const btn = page.getByTestId(tid === 'detail-dialog' ? 'detail-dialog-close' : 'dialog-cancel').first()
      await btn.click({ timeout: 1500 }).catch(() => undefined)
      await d.waitFor({ state: 'detached', timeout: 1500 }).catch(() => undefined)
    }
  }
}
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
const verdicts = []
for (const slot of slots) {
  const card = page.getByTestId(slot)
  if (!(await card.isVisible().catch(() => false))) continue
  // Click the slot first so the form below also reflects it.
  await card.click().catch(() => undefined)
  await page.locator(`[data-testid="${slot}"][data-selected="true"]`).waitFor({ state: 'visible', timeout: 4_000 }).catch(() => undefined)
  await page.waitForTimeout(250)

  // Pull the SVG inside this card and inspect its structure.
  const svgInfo = await card.evaluate((el) => {
    const svg = el.querySelector('svg')
    if (!svg) return { found: false }
    const stops = Array.from(svg.querySelectorAll('linearGradient stop')).map((s) => ({
      offset: s.getAttribute('offset'),
      stopColor: s.getAttribute('stop-color') || s.getAttribute('stopColor'),
    }))
    const shapes = Array.from(svg.querySelectorAll('path,rect,ellipse')).map((s) => ({
      tag: s.tagName,
      fill: s.getAttribute('fill'),
      opacity: s.getAttribute('opacity'),
    }))
    return {
      found: true,
      hasGradient: !!svg.querySelector('linearGradient'),
      hasClipPath: !!svg.querySelector('clipPath'),
      stops,
      shapes,
      bbox: { width: svg.clientWidth, height: svg.clientHeight },
      outerHTML: svg.outerHTML.slice(0, 600),
    }
  })

  // Focused screenshot of just this slot card (visual diff anchor).
  const safe = slot.replace(/[^a-z0-9-]/gi, '_')
  await card.screenshot({ path: path.join(OUT_DIR, `${safe}.png`) }).catch(() => undefined)

  // Heuristic visual sanity: count how many distinct fills the SVG has —
  // a healthy gradient/multicolor chip uses `url(#…)` for the body
  // shapes; a single-colour chip uses one hex.  More than 2 distinct
  // *body* fills (excluding the eye/highlight colours) signals the old
  // overlay regression.
  const distinctBodyFills = new Set(
    (svgInfo.shapes || [])
      .filter((s) => s.fill && s.fill !== '#1a1a1a' && !s.fill.startsWith('rgba'))
      .map((s) => s.fill),
  )
  const verdict = {
    slot,
    hasGradient: svgInfo.hasGradient,
    hasClipPath: svgInfo.hasClipPath,
    distinctBodyFills: Array.from(distinctBodyFills),
    stops: svgInfo.stops,
  }
  verdicts.push(verdict)
}

await fs.writeFile(path.join(OUT_DIR, 'verdicts.json'), JSON.stringify(verdicts, null, 2))
for (const v of verdicts) {
  console.log(v.slot, '| gradient=' + v.hasGradient, '| clipPath=' + v.hasClipPath, '| bodyFills=' + JSON.stringify(v.distinctBodyFills), '| stops=' + v.stops.length)
}

await ensureDialogClosed()
console.log('[probe] DONE — evidence in', OUT_DIR)
await browser.close()
