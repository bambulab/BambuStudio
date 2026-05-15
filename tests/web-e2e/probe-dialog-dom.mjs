import { chromium } from '@playwright/test'

const browser = await chromium.connectOverCDP('http://127.0.0.1:9222')
const ctx = browser.contexts()[0]
const page = ctx.pages().find((p) => p.url().includes('localhost:13628') && p.url().includes('#/filament'))
console.log('using:', page.url())

const info = await page.evaluate(() => {
  const tids = ['add-dialog', 'edit-dialog', 'detail-dialog', 'confirm-dialog']
  const out = []
  for (const tid of tids) {
    document.querySelectorAll(`[data-testid="${tid}"]`).forEach((el) => {
      const ancestors = []
      let cur = el
      while (cur && cur !== document.body) {
        const cs = window.getComputedStyle(cur)
        ancestors.push({
          tag: cur.tagName,
          cls: cur.className,
          pos: cs.position,
          z: cs.zIndex,
        })
        cur = cur.parentElement
      }
      out.push({ tid, ancestors })
    })
  }
  return out
})
console.log(JSON.stringify(info, null, 2))

await browser.close()
