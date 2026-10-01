// Edit dialog round-trip probe.
//   - For each spool row in the list, click "detail" -> "edit"
//   - Dump the form's preview-bar (color name / fila code / hex list / swatch)
//   - Cancel without saving
// Catches: hex normalisation on edit (raw FFFFFFFF persisted in cloud spools);
// preview-bar correctness for spools whose persisted (color_code, colors,
// color_type) was sanitized vs not.
import { chromium } from '@playwright/test'
import fs from 'node:fs/promises'
import path from 'node:path'

const CDP = 'http://127.0.0.1:9222'
const OUT_DIR = path.resolve('.evidence/edit-dialog')
await fs.mkdir(OUT_DIR, { recursive: true })

const browser = await chromium.connectOverCDP(CDP)
const ctx = browser.contexts()[0]
const pages = ctx.pages()
const cands = pages.filter((p) => /index\.html/.test(p.url()) && /localhost:\d+/.test(p.url()))
const page = cands[cands.length - 1] || pages[pages.length - 1]
console.log('[probe] using:', page.url())
await page.bringToFront()
await page.evaluate(() => { if (!location.hash.includes('/filament')) location.hash = '#/filament' }).catch(() => undefined)
await page.waitForTimeout(200)
await page.getByTestId('filament-page-root').waitFor({ state: 'visible', timeout: 10_000 })

async function ensureDialogClosed() {
  for (const tid of ['add-dialog', 'detail-dialog']) {
    const d = page.getByTestId(tid)
    for (let i = 0; i < 5; i++) {
      if (!(await d.isVisible().catch(() => false))) break
      await page.getByTestId(tid === 'detail-dialog' ? 'detail-dialog-close' : 'dialog-cancel').first().click({ timeout: 1500 }).catch(() => undefined)
      await d.waitFor({ state: 'detached', timeout: 1500 }).catch(() => undefined)
    }
  }
}
await ensureDialogClosed()

// Discover all visible rows and their detail buttons.
const rows = await page.evaluate(() => {
  return Array.from(document.querySelectorAll('tr[data-testid^="filament-row-"]')).map((row, i) => ({
    index: i,
    testid: row.getAttribute('data-testid'),
    name: row.querySelector('[data-testid="filament-row-name"]')?.textContent?.trim() ?? '',
    colorCode: row.querySelector('[data-testid="filament-row-color"]')?.getAttribute('data-color-code') ?? '',
    colorType: row.getAttribute('data-color-type'),
  }))
})
console.log('[probe] rows:', JSON.stringify(rows, null, 2))

const dumps = []
for (const r of rows) {
  // Open detail dialog from this row.
  const detailBtn = page.locator(`[data-testid="${r.testid}"] [data-testid="filament-row-detail"]`)
  await detailBtn.click({ timeout: 5_000 }).catch((e) => console.log('[probe] detail click err', r.testid, e.message))
  const detail = page.getByTestId('detail-dialog')
  const detailOk = await detail.waitFor({ state: 'visible', timeout: 5_000 }).then(() => true).catch(() => false)
  if (!detailOk) { console.log('[probe] detail did not open for', r.testid); continue }
  // Click edit
  await page.getByTestId('detail-dialog-edit').click({ timeout: 3_000 }).catch(() => undefined)
  const edit = page.getByTestId('add-dialog') // edit dialog reuses AddEditDialog
  const editOk = await edit.waitFor({ state: 'visible', timeout: 5_000 }).then(() => true).catch(() => false)
  if (!editOk) { console.log('[probe] edit dialog did not open for', r.testid); await ensureDialogClosed(); continue }
  await page.waitForTimeout(800) // resolver
  const snap = await page.evaluate(() => ({
    colorName: document.querySelector('[data-testid="preview-color-name"]')?.textContent?.trim() ?? '',
    filaCode: document.querySelector('[data-testid="preview-fila-code"]')?.textContent?.trim() ?? '',
    hexList: document.querySelector('[data-testid="preview-hex-list"]')?.textContent?.trim() ?? '',
    swatchStyle: document.querySelector('[data-testid="preview-swatch"]')?.getAttribute('style') ?? '',
    candidatesSize: document.querySelectorAll('[data-testid^="color-candidate-"]').length,
  }))
  dumps.push({ row: r, edit: snap })
  console.log(r.testid, r.name, '|', JSON.stringify(snap))
  await ensureDialogClosed()
}
await fs.writeFile(path.join(OUT_DIR, 'edit-dumps.json'), JSON.stringify(dumps, null, 2))
console.log('[probe] DONE — evidence in', OUT_DIR)
await browser.close()
