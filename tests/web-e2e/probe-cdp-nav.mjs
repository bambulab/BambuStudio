// Force the localhost:13628 webview to navigate to #/filament so the
// React app mounts the filament page without the user clicking through
// Studio's Device menu.
import { chromium } from '@playwright/test'

const browser = await chromium.connectOverCDP('http://127.0.0.1:9222', { timeout: 10_000 })
const ctx = browser.contexts()[0]
let target = null
for (const p of ctx.pages()) {
  if (p.url().startsWith('http://localhost:13628/')) { target = p; break }
}
if (!target) {
  console.log('no localhost:13628 page found; pages:')
  for (const p of ctx.pages()) console.log(' ', p.url())
  process.exit(1)
}

console.log('before navigate:', target.url())
const dest = target.url().replace(/(#.*)?$/, '#/filament')
await target.goto(dest, { waitUntil: 'domcontentloaded', timeout: 15_000 })
await target.waitForTimeout(2_000)
const hasRoot = await target.evaluate(() => !!document.querySelector('[data-testid="filament-page-root"]')).catch(() => false)
const rowCount = await target.evaluate(() => document.querySelectorAll('tr[data-testid^="filament-row-"]').length).catch(() => 0)
const tabs = await target.evaluate(() => Array.from(document.querySelectorAll('[data-testid^="filament-tab-"], [role="tab"]')).map((el) => el.getAttribute('data-testid') || el.textContent?.trim() || '')).catch(() => [])
console.log('after navigate:', target.url())
console.log('hasFilamentRoot:', hasRoot)
console.log('rowCount:', rowCount)
console.log('tabs:', tabs)

await browser.close()
