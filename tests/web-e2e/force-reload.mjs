import { chromium } from '@playwright/test'
const browser = await chromium.connectOverCDP('http://127.0.0.1:9222')
const ctx = browser.contexts()[0]
const cands = ctx.pages().filter((p) => /index\.html/.test(p.url()) && /localhost:\d+/.test(p.url()))
const page = cands[cands.length - 1] || ctx.pages()[ctx.pages().length - 1]
console.log('reloading:', page.url())
await page.reload({ waitUntil: 'domcontentloaded', timeout: 15_000 })
await page.waitForTimeout(2_000)
const ok = await page.getByTestId('filament-page-root').isVisible({ timeout: 10_000 }).catch(() => false)
console.log('filament-page-root visible:', ok)
await browser.close()
