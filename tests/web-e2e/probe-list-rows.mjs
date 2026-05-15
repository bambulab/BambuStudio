// One-shot list-row probe: dump every visible spool row's persisted
// colour fields + render attributes, plus a screenshot of the whole list
// view.  Catches mismatches between (color_code, colors, color_type)
// and the SpoolColorChip's actual fill — multicolor spools should render
// gradient/multi strips, single-hex spools should render as solid.
import { chromium } from '@playwright/test'
import fs from 'node:fs/promises'
import path from 'node:path'

const CDP = 'http://127.0.0.1:9222'
const OUT_DIR = path.resolve('.evidence/list-rows')
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
await page.getByTestId('filament-page-root').waitFor({ state: 'visible', timeout: 10_000 }).catch(() => undefined)

// Hard-close any leftover dialog from previous probes.
async function ensureDialogClosed() {
  const dialog = page.getByTestId('add-dialog')
  for (let i = 0; i < 5; i++) {
    if (!(await dialog.isVisible().catch(() => false))) return
    await page.getByTestId('dialog-cancel').first().click({ timeout: 2_000 }).catch(() => undefined)
    await dialog.waitFor({ state: 'detached', timeout: 2_000 }).catch(() => undefined)
  }
}
await ensureDialogClosed()

const rowDump = await page.evaluate(() => {
  const rows = Array.from(document.querySelectorAll('tr[data-testid^="filament-row-"]'))
  return rows.map((row) => {
    const colorCell = row.querySelector('[data-testid="filament-row-color"]')
    const nameCell = row.querySelector('[data-testid="filament-row-name"]')
    const svg = colorCell?.querySelector('svg')
    // Capture each shape's resolved fill so we can tell single-colour from
    // gradient (url(#…)) and from multicolor strips (multiple <rect>).
    const shapes = svg ? Array.from(svg.querySelectorAll('path,rect,ellipse')).map((s) => ({
      tag: s.tagName,
      fill: s.getAttribute('fill'),
    })) : []
    return {
      spoolId: row.getAttribute('data-spool-id'),
      colorType: row.getAttribute('data-color-type'),
      colorCode: colorCell?.getAttribute('data-color-code') ?? '',
      name: nameCell?.textContent?.trim() ?? '',
      svgFillSummary: shapes.slice(0, 6),
      hasGradient: !!svg?.querySelector('linearGradient'),
      multicolorStripCount: svg?.querySelectorAll('clipPath + g rect, g[clip-path] rect').length ?? 0,
    }
  })
})
console.log('[probe] rows:', JSON.stringify(rowDump, null, 2))
await fs.writeFile(path.join(OUT_DIR, 'rows.json'), JSON.stringify(rowDump, null, 2))

await page.screenshot({ path: path.join(OUT_DIR, 'list.png'), fullPage: false })
console.log('[probe] DONE — evidence in', OUT_DIR)
await browser.close()
