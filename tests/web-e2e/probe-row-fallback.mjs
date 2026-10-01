// Verify the new two-stage fallback in resolveCandidateForSpool:
//   stage 1: spool.colors[] hex-multiset -> candidate
//   stage 2: spool.color_code (single hex) -> single-hex candidate
// We don't have window.__filamentStore (production tree-shake) so the
// only window into spool.colors[] is the DetailDialog colour swatch +
// label. If the swatch is a `linear-gradient` we know spool.colors[] has
// 2+ hexes; if it's a flat rgb(...) the row was rendered as single hex
// (and stage 1 collapsed to length 1, equivalent to stage 2).
//
// Also dumps the AddEditDialog candidate panel for the row's own
// fila_id, so we can see exactly which candidate hex sequences are
// available against which to match. Combined with row tail + detail
// dialog, this pins down whether each row matched the cache or not and
// why.
//
// Output: .evidence/row-fallback/summary.json

import { chromium } from '@playwright/test'
import fs from 'node:fs/promises'
import path from 'node:path'

const CDP = 'http://127.0.0.1:9222'
const ROOT = path.resolve('.evidence/row-fallback')
await fs.rm(ROOT, { recursive: true, force: true })
await fs.mkdir(ROOT, { recursive: true })

const browser = await chromium.connectOverCDP(CDP, { timeout: 10_000 })
const ctx = browser.contexts()[0]
const page = ctx.pages().find((p) => p.url().includes('localhost:13628') && p.url().includes('#/filament'))
if (!page) { console.error('no filament page'); process.exit(1) }
await page.bringToFront()
await page.waitForTimeout(1_000)
console.log('using:', page.url())

async function ensureNoDialog() {
  await page.evaluate(() => {
    document.querySelectorAll('[data-testid="dialog-cancel"], [data-testid="detail-dialog-close"], [data-testid="confirm-dialog-cancel"]')
      .forEach((b) => /** @type {HTMLElement} */ (b).click())
  }).catch(() => undefined)
  await page.waitForTimeout(150)
  await page.keyboard.press('Escape').catch(() => undefined)
  await page.waitForTimeout(150)
}

async function rowTail() {
  return page.evaluate(() => {
    return Array.from(document.querySelectorAll('tr[data-testid^="filament-row-"]')).map((row) => {
      const cell = row.querySelector('[data-testid="filament-row-color"]')
      return {
        tid: row.getAttribute('data-testid'),
        name: row.querySelector('[data-testid="filament-row-name"]')?.textContent?.trim() ?? null,
        colorCode: cell?.getAttribute('data-color-code') ?? null,
        colorType: row.getAttribute('data-color-type'),
        swatch: row.querySelector('[data-testid="filament-row-color-swatch"]')?.getAttribute('style') ?? null,
        colorName: row.querySelector('[data-testid="filament-row-color-name"]')?.textContent?.trim() ?? null,
        filaCode: row.querySelector('[data-testid="filament-row-fila-code"]')?.textContent?.trim() ?? null,
        hexLabel: row.querySelector('[data-testid="filament-row-color-hex"]')?.textContent?.trim() ?? null,
      }
    })
  })
}

await ensureNoDialog()
console.log('\n=== STEP 1: baseline row tail (after reload) ===')
const before = await rowTail()
for (const r of before) console.log(' ', JSON.stringify(r))

// For each row open detail to see the swatch shape
console.log('\n=== STEP 2: per-row diagnostics ===')
const out = []
for (const r of before) {
  console.log(`\n  -- row ${r.tid} (${r.name} ${r.colorCode}) --`)
  await ensureNoDialog()
  const sel = `tr[data-testid="${r.tid}"]`

  // 2a) Open detail to read swatch CSS — gradient => spool.colors.length >= 2
  await page.locator(`${sel} [data-testid="filament-row-detail"]`).click({ force: true }).catch(() => undefined)
  await page.getByTestId('detail-dialog').waitFor({ state: 'visible', timeout: 4_000 }).catch(() => undefined)
  await page.waitForTimeout(200)
  const detail = await page.evaluate(() => {
    return {
      swatch: document.querySelector('[data-testid="detail-color-swatch"]')?.getAttribute('style') ?? null,
      label: document.querySelector('[data-testid="detail-color-label"]')?.textContent?.trim() ?? null,
    }
  })
  await page.locator('[data-testid="detail-dialog-close"]').click({ force: true }).catch(() => undefined)
  await page.waitForTimeout(150)

  // 2b) Open Add-Similar to see what own-fila_id candidates look like
  await page.locator(`${sel} [data-testid="filament-row-add-similar"]`).click({ force: true }).catch(() => undefined)
  await page.getByTestId('add-dialog').waitFor({ state: 'visible', timeout: 4_000 }).catch(() => undefined)
  await page.waitForTimeout(800)
  const candidates = await page.evaluate(() => {
    return Array.from(document.querySelectorAll('[data-testid^="color-candidate-"]')).map((el) => ({
      code: el.getAttribute('data-color-code'),
      name: el.getAttribute('data-color-name'),
      type: el.getAttribute('data-color-type'),
      colors: el.getAttribute('data-colors'),
      selected: el.getAttribute('data-selected'),
    }))
  })
  // Also peek the preview-bar that the dialog itself populated for this spool
  const preview = await page.evaluate(() => {
    return {
      name: document.querySelector('[data-testid="preview-color-name"]')?.textContent?.trim() ?? null,
      code: document.querySelector('[data-testid="preview-fila-code"]')?.textContent?.trim() ?? null,
      hex:  document.querySelector('[data-testid="preview-hex-list"]')?.textContent?.trim() ?? null,
      selected: (() => {
        const el = document.querySelector('[data-testid^="color-candidate-"][data-selected="true"]')
        return el ? { code: el.getAttribute('data-color-code'), name: el.getAttribute('data-color-name'), colors: el.getAttribute('data-colors') } : null
      })(),
    }
  })
  await page.locator('[data-testid="dialog-cancel"]').click({ force: true }).catch(() => undefined)
  await page.waitForTimeout(150)

  // Stage analyses for the new resolver
  const isGradient = !!detail.swatch && /linear-gradient/.test(detail.swatch)
  const inferredSpoolHexes = isGradient
    ? (detail.swatch.match(/#[0-9A-Fa-f]{3,8}/g) || [])
    : (r.colorCode ? [r.colorCode] : [])
  const dedupSpoolHexes = [...new Set(inferredSpoolHexes.map((h) => h.toUpperCase()))]
  const stage1Match = candidates.find((c) => {
    const ch = (c.colors || '').split(',').filter(Boolean).map((h) => h.toUpperCase())
    if (ch.length !== dedupSpoolHexes.length) return false
    return ch.slice().sort().join(',') === dedupSpoolHexes.slice().sort().join(',')
  })
  const stage2Match = !stage1Match && r.colorCode ? candidates.find((c) => {
    const ch = (c.colors || '').split(',').filter(Boolean).map((h) => h.toUpperCase())
    return ch.length === 1 && ch[0] === r.colorCode.toUpperCase()
  }) : null

  const dump = {
    row: r,
    detail,
    inferredSpoolHexes: dedupSpoolHexes,
    stage1Match: stage1Match ? { code: stage1Match.code, name: stage1Match.name, colors: stage1Match.colors } : null,
    stage2Match: stage2Match ? { code: stage2Match.code, name: stage2Match.name, colors: stage2Match.colors } : null,
    addDialogPreview: preview,
    candidatesCount: candidates.length,
    // Heads-up: the candidate panel in Add-Similar mixes *official* candidates
    // (whose `code` is BBL like "11400") with *user-owned* candidates (whose
    // `code` is the hex itself, name often empty). Filter to the official ones
    // for clarity.
    officialCandidatesCount: candidates.filter((c) => c.code && !c.code.startsWith('#') && !/^[0-9A-F]{6,8}$/i.test(c.code)).length,
  }
  out.push(dump)
  console.log(`    detail.swatch=${detail.swatch}`)
  console.log(`    detail.label =${detail.label}`)
  console.log(`    inferredHexes=${dedupSpoolHexes.join(',')}`)
  console.log(`    candidates total=${candidates.length} official=${dump.officialCandidatesCount}`)
  console.log(`    stage1 match=${stage1Match ? `${stage1Match.name}/${stage1Match.code}` : 'NONE'}`)
  console.log(`    stage2 match=${stage2Match ? `${stage2Match.name}/${stage2Match.code}` : 'NONE'}`)
  console.log(`    preview-bar : name=${preview.name} code=${preview.code} hex=${preview.hex}`)
  console.log(`    preview.selectedCand=${preview.selected ? `${preview.selected.name}/${preview.selected.code}/[${preview.selected.colors}]` : 'NONE'}`)
}

console.log('\n=== STEP 3: row tail after diagnostics (warm cache) ===')
const after = await rowTail()
for (const r of after) console.log(' ', JSON.stringify(r))

const summary = {
  startedAt: new Date().toISOString(),
  rowsBefore: before,
  rowsAfter: after,
  perRow: out,
}
await fs.writeFile(path.join(ROOT, 'summary.json'), JSON.stringify(summary, null, 2))
await browser.close()
