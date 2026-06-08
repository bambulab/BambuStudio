// Verify the row tail injected for STUDIO-17977 缺陷 3 is still present
// in production: data-testids filament-row-color-swatch / -color-name /
// -fila-code / -color-hex.
import { chromium } from '@playwright/test'

const browser = await chromium.connectOverCDP('http://127.0.0.1:9222')
const ctx = browser.contexts()[0]
const page = ctx.pages().find((p) => p.url().includes('localhost:13628') && p.url().includes('#/filament'))
console.log('using:', page.url())
await page.waitForTimeout(800) // let prefetch warm cache

const rows = await page.$$('tr[data-testid^="filament-row-"]')
console.log('rows:', rows.length)
for (let i = 0; i < rows.length; i++) {
  const dump = await rows[i].evaluate((el) => ({
    testid: el.getAttribute('data-testid'),
    name: el.querySelector('[data-testid="filament-row-name"]')?.textContent?.trim() ?? null,
    swatchStyle: el.querySelector('[data-testid="filament-row-color-swatch"]')?.getAttribute('style') ?? null,
    colorName: el.querySelector('[data-testid="filament-row-color-name"]')?.textContent?.trim() ?? null,
    filaCode: el.querySelector('[data-testid="filament-row-fila-code"]')?.textContent?.trim() ?? null,
    hex: el.querySelector('[data-testid="filament-row-color-hex"]')?.textContent?.trim() ?? null,
    detailBtn: !!el.querySelector('[data-testid="filament-row-detail"]'),
    addSimilar: !!el.querySelector('[data-testid="filament-row-add-similar"]'),
    deleteBtn: !!el.querySelector('[data-testid="filament-row-delete"]'),
  }))
  console.log(JSON.stringify(dump))
}
await browser.close()
