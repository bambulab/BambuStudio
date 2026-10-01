// One-shot manual probe: drive WebView2 over CDP, walk every AMS slot,
// dump the form's colour state + screenshot per slot.  Output goes to
// `.evidence/ams-slot-switch/` so we can diff slot transitions visually.
//
// Usage:  node probe-ams-slot-switch.mjs
import { chromium } from '@playwright/test'
import fs from 'node:fs/promises'
import path from 'node:path'

const CDP = 'http://127.0.0.1:9222'
const OUT_DIR = path.resolve('.evidence/ams-slot-switch')
await fs.mkdir(OUT_DIR, { recursive: true })

const browser = await chromium.connectOverCDP(CDP)
const ctx = browser.contexts()[0]
const pages = ctx.pages()
console.log('[probe] pages:', pages.map((p) => p.url()))
// device_page bundle is served at localhost:<port>/index.html (hash router).
// Match the device_page WebView2 by URL substring; pick the latest.
const candidates = pages.filter((p) => /index\.html/.test(p.url()) && /localhost:\d+/.test(p.url()))
const page = candidates[candidates.length - 1] || pages[pages.length - 1]
console.log('[probe] using:', page.url())

await page.bringToFront()
await page.waitForLoadState('domcontentloaded').catch(() => undefined)

// Force the hash router to /filament before reload, so the dist landing
// page is the filament feature even if the user navigated elsewhere.
await page.evaluate(() => {
  if (!location.hash || !location.hash.includes('/filament')) {
    location.hash = '#/filament'
  }
}).catch(() => undefined)

// Reload once so any stale dist is replaced — we just rebuilt.
await page.reload({ waitUntil: 'domcontentloaded', timeout: 15_000 }).catch(() => undefined)
await page.evaluate(() => { if (!location.hash.includes('/filament')) location.hash = '#/filament' }).catch(() => undefined)
await page.waitForTimeout(400)
await page.getByTestId('filament-page-root').waitFor({ state: 'visible', timeout: 15_000 })

// Defensive: hard-close any open add-dialog (stale state from previous
// probe runs or manual exploration).  reload() alone is not enough — the
// zustand store and WebView2 can survive a refresh under CDP and leave
// the dialog re-opened pointing at whichever slot was last selected.
async function ensureDialogClosed() {
  const dialog = page.getByTestId('add-dialog')
  for (let i = 0; i < 5; i++) {
    if (!(await dialog.isVisible().catch(() => false))) return
    const cancel = page.getByTestId('dialog-cancel').first()
    await cancel.click({ timeout: 2_000 }).catch(() => undefined)
    await dialog.waitFor({ state: 'detached', timeout: 2_000 }).catch(() => undefined)
  }
}
await ensureDialogClosed()

// 1) Open Add dialog -> AMS tab.
await page.getByTestId('filament-add').click({ timeout: 5_000 }).catch((e) => console.log('[probe] add click err:', e.message))
const dialog = page.getByTestId('add-dialog')
const dialogVisible = await dialog.isVisible().catch(() => false)
console.log('[probe] add-dialog visible after click:', dialogVisible)
if (!dialogVisible) {
  await page.screenshot({ path: path.join(OUT_DIR, '_no-dialog.png'), fullPage: false })
}
await dialog.waitFor({ state: 'visible', timeout: 10_000 }).catch(() => undefined)
await page.getByTestId('dialog-tab-ams').click({ timeout: 5_000 }).catch((e) => console.log('[probe] ams tab click err:', e.message))
await page.waitForTimeout(800)

// Pick the "129" machine — that's where the multicolor A3 spool lives.
// The select has no testid; it's the first <select> inside the AMS tab.
const printerSelect = dialog.locator('select').first()
const allOptions = await printerSelect.evaluate((el) =>
  Array.from(el.querySelectorAll('option')).map((o) => ({ value: o.value, label: o.textContent?.trim() })),
)
console.log('[probe] printer options:', allOptions)
const target = allOptions.find((o) => /^129/.test(o.label || ''))
if (target) {
  console.log('[probe] selecting printer:', target)
  await printerSelect.selectOption({ value: target.value })
  await page.waitForTimeout(1500) // wait AMS snapshot for new device
} else {
  console.log('[probe] WARN: no printer label starts with "129", staying on default')
}

// Probe AMS shell state — error / loading banner / unit count.
const amsState = await page.evaluate(() => ({
  amsGrid: !!document.querySelector('[data-testid="ams-grid"]'),
  amsLoading: document.body.textContent?.includes('Loading') || document.body.textContent?.includes('加载'),
  amsError: document.querySelector('.text-fm-warning')?.textContent?.trim() ?? '',
  units: Array.from(document.querySelectorAll('[data-testid^="ams-unit-"]')).map((u) => ({
    testid: u.getAttribute('data-testid'),
    active: u.getAttribute('data-active'),
  })),
  raw: document.querySelector('[data-testid="add-dialog"]')?.textContent?.slice(0, 400) ?? '',
}))
console.log('[probe] amsState:', JSON.stringify(amsState, null, 2))
await page.screenshot({ path: path.join(OUT_DIR, '_ams-tab.png'), fullPage: false })

// If a unit exists but isn't active, click the first one to populate slots.
if (amsState.units.length && !amsState.units.some((u) => u.active === 'true')) {
  await page.getByTestId(amsState.units[0].testid).click()
  await page.waitForTimeout(400)
}

// 2) Discover every non-empty slot in the currently selected AMS unit.
const slotInfo = await page.evaluate(() => {
  const out = []
  const slots = Array.from(document.querySelectorAll('[data-testid^="ams-slot-"]'))
  for (const el of slots) {
    out.push({
      testid: el.getAttribute('data-testid'),
      empty: el.getAttribute('data-empty'),
      color: el.getAttribute('data-color'),
      colorType: el.getAttribute('data-color-type'),
    })
  }
  return out
})
console.log('[probe] slots:', JSON.stringify(slotInfo, null, 2))
await fs.writeFile(path.join(OUT_DIR, 'slots.json'), JSON.stringify(slotInfo, null, 2))

const dumps = []
for (const s of slotInfo) {
  if (s.empty === 'true') continue
  console.log(`[probe] click ${s.testid}`)
  await page.getByTestId(s.testid).click()
  // Wait until the slot card actually flips to data-selected="true" — the
  // first probe run showed cases where snapshot ran before React applied
  // selectAmsSlot, leaving the form bound to whichever slot the previous
  // session left selected.
  await page
    .locator(`[data-testid="${s.testid}"][data-selected="true"]`)
    .waitFor({ state: 'visible', timeout: 5_000 })
    .catch(() => undefined)
  // Wait the resolver useEffect that re-binds colour name / fila code.
  await page.waitForTimeout(800)

  const snapshot = await page.evaluate((slotTestId) => {
    const text = (sel) => document.querySelector(sel)?.textContent?.trim() ?? ''
    const attr = (sel, name) => document.querySelector(sel)?.getAttribute(name) ?? ''
    const swatchStyle = attr('[data-testid="preview-swatch"]', 'style')
    return {
      slot: slotTestId,
      slotData: {
        color: attr(`[data-testid="${slotTestId}"]`, 'data-color'),
        colorType: attr(`[data-testid="${slotTestId}"]`, 'data-color-type'),
        selected: attr(`[data-testid="${slotTestId}"]`, 'data-selected'),
      },
      preview: {
        colorName: text('[data-testid="preview-color-name"]'),
        filaCode: text('[data-testid="preview-fila-code"]'),
        hexList: text('[data-testid="preview-hex-list"]'),
        swatchStyle,
      },
      candidatePanel: {
        size: document.querySelectorAll('[data-testid^="color-candidate-"]').length,
        all: Array.from(
          document.querySelectorAll('[data-testid^="color-candidate-"]'),
        ).map((el) => ({
          code: el.getAttribute('data-color-code'),
          name: el.getAttribute('data-color-name'),
          colors: el.getAttribute('data-colors'),
          type: el.getAttribute('data-color-type'),
        })),
      },
    }
  }, s.testid)

  dumps.push(snapshot)
  console.log(JSON.stringify(snapshot, null, 2))

  const safeName = s.testid.replace(/[^a-z0-9-]/gi, '_')
  await page.screenshot({
    path: path.join(OUT_DIR, `${safeName}.png`),
    fullPage: false,
  })
}

await fs.writeFile(path.join(OUT_DIR, 'dumps.json'), JSON.stringify(dumps, null, 2))
console.log('[probe] DONE — evidence in', OUT_DIR)

await browser.close()
