// Walk every CDP page, print URL/title and probe DOM for filament-page-root.
import { chromium } from '@playwright/test'

const browser = await chromium.connectOverCDP('http://127.0.0.1:9222', { timeout: 10_000 })
const ctxs = browser.contexts()
console.log(`contexts: ${ctxs.length}`)
for (const ctx of ctxs) {
  for (const p of ctx.pages()) {
    const url = p.url()
    const title = await p.title().catch(() => '')
    let hasFilament = false
    let bodyChunk = ''
    try {
      hasFilament = await p.evaluate(() => !!document.querySelector('[data-testid="filament-page-root"]'))
      bodyChunk = await p.evaluate(() => (document.body?.textContent || '').trim().slice(0, 100))
    } catch (err) {
      bodyChunk = `(eval err: ${err.message})`
    }
    console.log(`---\n  url:   ${url}\n  title: ${title}\n  hasFilamentRoot: ${hasFilament}\n  body[0..100]: ${bodyChunk}`)
  }
}
await browser.close()
