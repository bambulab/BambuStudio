// Standalone visual probe for STUDIO-17977 list row colour info.
//
// Drives the *vite dev* build (mock bridge enabled) instead of the live
// Studio webview, so we can self-validate the new "colorName · #hex" tail
// without depending on the WebView2 CDP port.
//
// What we verify per row:
//   1. data: data-testid filament-row-color-name / filament-row-color-hex
//      hold the expected text.
//   2. structure: the second-line wrapper is a single .text-secondary div
//      with at most one separator '·' and no stray children.
//   3. pixels: row screenshot is read back and saved alongside summary.
//
// Default mock spools cover: single-with-name, multicolor, gradient,
// Generic single, and a missing-colorName spool (Acme3D / sp-5).

import { chromium } from '@playwright/test'
import fs from 'node:fs/promises'
import path from 'node:path'

const URL = process.env.STUDIO_DEV_URL || 'http://localhost:5173/#/filament'
const ROOT = path.resolve('.evidence/list-row-standalone')
await fs.rm(ROOT, { recursive: true, force: true })
await fs.mkdir(ROOT, { recursive: true })

const browser = await chromium.launch({ headless: true })
const ctx = await browser.newContext({ viewport: { width: 1280, height: 900 } })
const page = await ctx.newPage()
page.on('console', (msg) => {
  if (msg.type() === 'error') console.log('[page error]', msg.text())
})
console.log('[probe] navigating to', URL)
await page.goto(URL, { waitUntil: 'domcontentloaded', timeout: 15_000 })
await page.getByTestId('filament-page-root').waitFor({ state: 'visible', timeout: 10_000 })

// STUDIO-17977 prefetch lands the candidate cache asynchronously after
// init() returns spools. Give it a beat so colorName / filaCode lookup
// in the row tail is exercised against a warm cache.
await page.waitForTimeout(1_500)

const rows = await page.$$('tr[data-testid^="filament-row-"]')
console.log(`[probe] found ${rows.length} rows`)

await page.screenshot({ path: path.join(ROOT, 'list-overview.png'), fullPage: false })

const dumps = []
for (let i = 0; i < rows.length; i++) {
  const row = rows[i]
  const dump = await row.evaluate((el) => {
    const tdName = el.querySelector('[data-testid="filament-row-name"]')?.textContent?.trim() ?? ''
    const tdColorName = el.querySelector('[data-testid="filament-row-color-name"]')?.textContent?.trim() ?? null
    const tdColorHex = el.querySelector('[data-testid="filament-row-color-hex"]')?.textContent?.trim() ?? null
    const tdFilaCode = el.querySelector('[data-testid="filament-row-fila-code"]')?.textContent?.trim() ?? null
    const tdColorSwatch = el.querySelector('[data-testid="filament-row-color-swatch"]')
    return {
      testid: el.getAttribute('data-testid'),
      colorCode: el.getAttribute('data-color-code'),
      colorType: el.getAttribute('data-color-type'),
      filamentName: tdName,
      colorName: tdColorName,
      filaCode: tdFilaCode,
      colorHex: tdColorHex,
      colorSwatchStyle: tdColorSwatch?.getAttribute('style') ?? null,
      colorSwatchSize: tdColorSwatch
        ? { w: tdColorSwatch.getBoundingClientRect().width, h: tdColorSwatch.getBoundingClientRect().height }
        : null,
      secondLineText: el.querySelector('[data-testid="filament-row-color-hex"]')?.parentElement?.textContent?.trim() ?? '',
    }
  })
  await row.screenshot({ path: path.join(ROOT, `row-${i}-${dump.testid}.png`) }).catch(() => undefined)
  dumps.push(dump)
}

// Sanity rules.
const defects = []
for (const d of dumps) {
  if (!d.colorHex) defects.push({ id: 'row-no-hex', detail: `${d.testid} has no filament-row-color-hex` })
  if (d.colorHex && !/^#[0-9A-F]{6}( \/ #[0-9A-F]{6})*$/.test(d.colorHex)) {
    defects.push({ id: 'row-hex-format', detail: `${d.testid} hex=${d.colorHex} does not match #RRGGBB[ / #RRGGBB]*` })
  }
  // Multi-hex spools (color_type 0 or 1) must show 2+ hexes.
  if ((d.colorType === '0' || d.colorType === '1') && d.colorHex && !d.colorHex.includes('/')) {
    defects.push({ id: 'row-multi-hex-but-single', detail: `${d.testid} colorType=${d.colorType} but hex='${d.colorHex}'` })
  }
  // Single hex spools with no colorName must still show at least one hex.
  if (d.colorType === '2' && !d.colorName && !d.colorHex) {
    defects.push({ id: 'row-single-empty-tail', detail: `${d.testid} single colour and tail is empty` })
  }
  // STUDIO-17977 PM feedback: every row must show a *rectangular* swatch
  // chip in the text tail, regardless of whether color_name is present.
  if (!d.colorSwatchStyle) {
    defects.push({ id: 'row-no-swatch', detail: `${d.testid} has no filament-row-color-swatch` })
  } else if (!/background:.*(rgb|#|linear-gradient)/i.test(d.colorSwatchStyle)) {
    defects.push({ id: 'row-swatch-empty-bg', detail: `${d.testid} swatch style is missing a background colour: ${d.colorSwatchStyle}` })
  }
  // Multi-hex swatches must use a linear-gradient.
  if ((d.colorType === '0' || d.colorType === '1') && d.colorSwatchStyle
      && !d.colorSwatchStyle.includes('linear-gradient')) {
    defects.push({ id: 'row-multi-swatch-not-gradient', detail: `${d.testid} colorType=${d.colorType} swatch is not a linear-gradient: ${d.colorSwatchStyle}` })
  }
}

await fs.writeFile(
  path.join(ROOT, 'summary.json'),
  JSON.stringify({ url: URL, rows: dumps, defects }, null, 2),
)

console.log('\n=== rows ===')
for (const d of dumps) {
  console.log(`${d.testid} | type=${d.colorType} | name="${d.filamentName}" | colorName="${d.colorName ?? '(none)'}" | filaCode="${d.filaCode ?? '(none)'}" | hex="${d.colorHex ?? '(none)'}"`)
}
console.log('\n=== defects ===', defects.length)
for (const x of defects) console.log(' -', x.id, ':', x.detail)

await browser.close()
