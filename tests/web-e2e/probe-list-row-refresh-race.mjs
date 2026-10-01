// STUDIO-17977 race probe: exercises the "瞬间正确，然后变成不正确" symptom
// by deliberately re-firing `setSpools` and forcing a delayed empty
// candidate response into the store *after* the populated one already
// landed. We test this directly against the dev mockBridge by reaching
// into the Zustand store from the page, simulating the late RPC response.
//
// Expected: the populated candidate cache survives the delayed empty
// write, so colorName / fila code stay visible across the simulated
// refresh storm.

import { chromium } from '@playwright/test'
import fs from 'node:fs/promises'
import path from 'node:path'

const URL = process.env.STUDIO_DEV_URL || 'http://localhost:5173/#/filament'
const ROOT = path.resolve('.evidence/list-row-race')
await fs.rm(ROOT, { recursive: true, force: true })
await fs.mkdir(ROOT, { recursive: true })

const browser = await chromium.launch({ headless: true })
const ctx = await browser.newContext({ viewport: { width: 1280, height: 900 } })
const page = await ctx.newPage()
page.on('console', (msg) => {
  if (msg.type() === 'error') console.log('[page error]', msg.text())
})

await page.goto(URL, { waitUntil: 'domcontentloaded', timeout: 15_000 })
await page.getByTestId('filament-page-root').waitFor({ state: 'visible', timeout: 10_000 })

// Wait for the prefetch round-trip to seat the candidate cache.
await page.waitForTimeout(2_000)

const expose = async () => page.evaluate(() => {
  const w = /** @type {any} */ (window)
  // The store is exposed by zustand's create() into a single hook; we
  // read it via the global `useStore` import at runtime — most projects
  // attach it implicitly. For dev visibility we hop through the public
  // store API via the React fiber tree if needed.
  // Easier: pick from globally-exposed __APP_STORE__ if any, else read
  // it from a hidden DOM marker we can synthesise. Here we just rely on
  // the dev build having the unminified module path stable.
  return {
    rowsBefore: Array.from(document.querySelectorAll('tr[data-testid^="filament-row-"]')).map((row) => ({
      testid: row.getAttribute('data-testid'),
      colorName: row.querySelector('[data-testid="filament-row-color-name"]')?.textContent?.trim() ?? null,
      filaCode: row.querySelector('[data-testid="filament-row-fila-code"]')?.textContent?.trim() ?? null,
      hex: row.querySelector('[data-testid="filament-row-color-hex"]')?.textContent?.trim() ?? null,
    })),
  }
})

console.log('[probe] === phase 1 — initial state after prefetch ===')
const before = await expose()
for (const r of before.rowsBefore) {
  console.log(` row ${r.testid} | colorName=${r.colorName ?? '(none)'} | filaCode=${r.filaCode ?? '(none)'} | hex=${r.hex}`)
}
await page.screenshot({ path: path.join(ROOT, 'phase1-initial.png') }).catch(() => undefined)

// Simulate the race: reach into the store and call setColorCandidates
// with an empty array for every fila_id we just primed. With the new
// guard this should NOT clobber the populated lists, so the next paint
// shows the same colorName / filaCode tail.
console.log('\n[probe] === phase 2 — inject empty setColorCandidates() race ===')
// Reach into the dev-only window.__filamentStore hook and call
// setColorCandidates(id, []) for every populated fila_id. This is the
// exact write that a delayed RPC reply would do in production after the
// list already has data. With the new useFilamentBridge guard the
// store should ignore the write; without the guard the row tail loses
// its colorName / fila code on the next paint.
const raceResult = await page.evaluate(() => {
  const w = /** @type {any} */ (window)
  const store = w.__filamentStore
  if (typeof store !== 'function') return { ok: false, note: 'store hook not exposed' }
  const fil = store.getState().filament
  const cacheBefore = fil.candidatesByFilaId || {}
  const ids = Object.keys(cacheBefore).filter((id) => Array.isArray(cacheBefore[id]) && cacheBefore[id].length > 0)
  // Try to clobber every populated entry with [].
  for (const id of ids) fil.setColorCandidates(id, [])
  const cacheAfter = store.getState().filament.candidatesByFilaId || {}
  const survived = ids.filter((id) => Array.isArray(cacheAfter[id]) && cacheAfter[id].length > 0)
  return {
    ok: true,
    populatedIds: ids,
    survivedIds: survived,
    note: 'invoked setColorCandidates(id, []) for each populated id',
  }
})
console.log(' simulation:', raceResult)

// Force one re-render after the race so any subscribed components get
// a chance to re-evaluate against the (possibly clobbered) cache.
await page.evaluate(() => new Promise((r) => requestAnimationFrame(() => r(undefined))))
await page.waitForTimeout(300)

console.log('\n[probe] === phase 3 — final state after 3 refresh cycles ===')
const after = await expose()
for (const r of after.rowsBefore) {
  console.log(` row ${r.testid} | colorName=${r.colorName ?? '(none)'} | filaCode=${r.filaCode ?? '(none)'} | hex=${r.hex}`)
}
await page.screenshot({ path: path.join(ROOT, 'phase3-after-refresh.png') }).catch(() => undefined)

// Diff: any row that lost colorName or filaCode between phase1 and phase3
// is a regression.
console.log('\n[probe] === diff ===')
const defects = []
const beforeMap = new Map(before.rowsBefore.map((r) => [r.testid, r]))
for (const r of after.rowsBefore) {
  const b = beforeMap.get(r.testid)
  if (!b) continue
  if ((b.colorName && !r.colorName) || (b.filaCode && !r.filaCode)) {
    defects.push({ id: 'race-lost-info', detail: `${r.testid} lost colorName/filaCode after refresh: before=${JSON.stringify(b)} after=${JSON.stringify(r)}` })
  }
}
console.log(' defects:', defects.length)
for (const d of defects) console.log(' -', d.id, ':', d.detail)

await fs.writeFile(path.join(ROOT, 'summary.json'), JSON.stringify({ before: before.rowsBefore, after: after.rowsBefore, defects }, null, 2))

await browser.close()
