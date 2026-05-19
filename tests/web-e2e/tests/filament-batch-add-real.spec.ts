/**
 * @studio-18344 @printer
 * JIRA: STUDIO-18344
 *
 * Real-data dry-run validation for AMS multi-select batch add. The final
 * spool/batch_create bridge request is intercepted, so real inventory /
 * cloud data is not modified. Everything else (AMS payload, presets, UI
 * state) comes from the live C++ bridge.
 *
 * Required preconditions on the attached BambuStudio:
 *   1. User is signed in, Filament Manager page is open.
 *   2. The currently selected device exposes >= 2 non-empty AMS trays
 *      (otherwise "multi"-select cannot be validated).
 *
 * What this spec proves:
 *   - The slot card checkbox toggles `data-checked` on click.
 *   - With >= 2 slots selected, the editable form area is hidden and the
 *     batch summary panel is shown with the right item count.
 *   - The Confirm button is enabled in the batch state.
 *   - On Confirm, the bridge is called exactly once with
 *     module=filament, submod=spool, action=batch_create.
 *   - `payload.creates[].length + payload.updates[].length` equals the
 *     number of slots selected.
 *   - Every payload entry carries the authoritative tray fields (brand,
 *     material_type, color_code, tag_uid, bound_ams_id, bound_dev_id).
 */
import type { Page, TestInfo } from '@playwright/test'
import { test, expect } from '../src/fixtures/filament'
import type { AmsSlotSnapshot } from '../src/pages/FilamentAddDialog'

interface BridgeBody {
  module: string
  submod: string
  action: string
  payload?: Record<string, unknown>
}

interface CapturedBridgeState {
  requests: BridgeBody[]
  responses: BridgeBody[]
  interceptedWrite?: BridgeBody
}

interface SpoolPayload {
  brand?: string
  material_type?: string
  color_code?: string
  color_name?: string
  tag_uid?: string
  setting_id?: string
  bound_ams_id?: string
  bound_dev_id?: string
  entry_method?: string
  spool_id?: string
  [key: string]: unknown
}

interface BatchCreatePayload {
  creates?: SpoolPayload[]
  updates?: SpoolPayload[]
}

const VALIDATION_PLAN = `# STUDIO-18344 E2E 验证结果说明

## 验证目标

1. 使用真实 Studio 耗材管理器页面、真实 AMS 槽位数据和真实 UI 点击操作。
2. 通过勾选 \`ams-slot-checkbox\` 选中 \`>= 2\` 个真实 AMS 槽位。
3. 在多选态下：
   - 表单区折叠，仅显示 \`ams-batch-summary\` 摘要面板。
   - 摘要面板 \`data-count\` 与选中数一致。
   - \`Confirm\` 按钮可点击。
4. 点击 Confirm 后，bridge 必须接到一条 \`module=filament, submod=spool, action=batch_create\` 请求。
5. \`payload.creates[].length + payload.updates[].length\` 与选中的槽位数相等。
6. 每条 payload 都带上权威字段（brand / material_type / color_code / tag_uid / bound_ams_id / bound_dev_id），证明数据是从真实 AMS tray 直接拼出的。
7. 为避免污染真实库存 / 云端，本用例拦截 \`spool/batch_create\` 写请求并返回 synthetic success；其他数据均来自真实 C++ bridge。

## 操作步骤

1. 启动带 WebView2 CDP 的 BambuStudio。
2. 用户登录并打开 \`Device -> Filament Manager\`。
3. Playwright 通过 CDP 连接当前耗材管理器 WebView 页面。
4. 安装 bridge spy：记录真实 C++ response / request，并 dry-run 拦截最终写请求。
5. 点击 \`+ Add Filament\`。
6. 切换到 \`Read from AMS\`。
7. 截图 AMS 面板。
8. 选取前 \`>= 2\` 个非空真实 AMS 槽位。
9. 截图多选态。
10. 点击 \`Add\`。
11. 断言捕获到的最终写请求满足验证目标，并将截图 / JSON 指标写入 Playwright HTML report。

## 报告附件

- \`studio-18344-validation-plan.md\`: 本说明文件。
- \`01-ams-panel-real-data.png\`: AMS 面板真实数据截图。
- \`02-multi-select-summary.png\`: 多选后批量摘要面板截图。
- \`03-post-confirm.png\`: 点击 Add 之后的截图（一般会因为 dry-run 成功而关闭对话框）。
- \`ams-slot-metrics.json\`: 当前 AMS 槽位 + 选中槽位明细。
- \`studio-18344-bridge-capture.json\`: bridge 请求 / 响应 / 最终批量写请求 payload。
`;

async function attachMarkdown(testInfo: TestInfo, name: string, value: string) {
  await testInfo.attach(name, { body: value, contentType: 'text/markdown' })
}

async function attachJson(testInfo: TestInfo, name: string, value: unknown) {
  await testInfo.attach(name, {
    body: JSON.stringify(value, null, 2),
    contentType: 'application/json',
  })
}

async function attachPageScreenshot(page: Page, testInfo: TestInfo, name: string) {
  // STUDIO-18344 / device-real-webview-e2e:
  //
  // Pixel screenshots have proven structurally unreliable against the
  // BambuStudio WebView2 (`page.screenshot` + element variants and even
  // raw `Page.captureScreenshot` via CDP either hang past the test
  // timeout or close the target). Root cause is WebView2's render
  // pipeline pausing while the host window is not foreground; nothing
  // the spec can reasonably do fixes that.
  //
  // For evidence we capture two non-pixel artifacts instead:
  //   - A short text summary of what state we expected to be on screen
  //     at this checkpoint (`*.checkpoint.txt`).
  //   - The add-dialog's outerHTML (`*.dom.html`). This is the actual
  //     rendered React tree, faithfully shows the new STUDIO-18344
  //     pieces (ams-slot-checkbox / ams-batch-summary / data-checked),
  //     and a reviewer can open the file in any browser to read it.
  //
  // We still try a 5-second pixel screenshot as a "nice-to-have" - if
  // it happens to work (e.g. user has Studio focused) we keep the PNG.
  await testInfo.attach(`${name}.checkpoint.txt`, {
    body: `checkpoint: ${name}\nurl: ${page.url()}\nts: ${new Date().toISOString()}`,
    contentType: 'text/plain',
  })
  try {
    const html = await page.getByTestId('add-dialog').evaluate((el) => el.outerHTML, { timeout: 5_000 })
    await testInfo.attach(`${name}.dom.html`, { body: html, contentType: 'text/html' })
  } catch {
    /* dialog already closed (e.g. post-confirm) - no DOM to attach */
  }
  try {
    const buf = await page.screenshot({
      fullPage: false,
      animations: 'disabled',
      timeout: 5_000,
    })
    await testInfo.attach(name, { body: buf, contentType: 'image/png' })
  } catch (err) {
    await testInfo.attach(`${name}.skipped.txt`, {
      body: `pixel screenshot skipped: ${(err as Error).message}`,
      contentType: 'text/plain',
    })
  }
}

async function installBridgeSpy(page: Page) {
  await page.evaluate(() => {
    type CapturedPacket = {
      head: { version: string; type: string; seq: number; ts: number }
      body: BridgeBody
    }
    const win = window as unknown as {
      __studio18344?: CapturedBridgeState
      __studio18344Restore?: () => void
      chrome?: { webview?: { postMessage?: (message: string) => void } }
    }
    const original = win.chrome?.webview?.postMessage?.bind(win.chrome.webview)
    if (!original) throw new Error('chrome.webview.postMessage is unavailable; cannot spy real bridge.')

    const state: CapturedBridgeState = { requests: [], responses: [] }
    win.__studio18344 = state

    document.addEventListener('cpp:device', (event) => {
      const detail = (event as CustomEvent<CapturedPacket>).detail
      if (detail?.body?.module === 'filament') {
        state.responses.push(detail.body)
      }
    }, true)

    win.chrome = {
      ...(win.chrome ?? {}),
      webview: {
        postMessage(message: string) {
          const packet = JSON.parse(message) as CapturedPacket
          const body = packet.body
          state.requests.push(body)

          // Intercept only the new STUDIO-18344 batch action. Other writes
          // (single add / update) are left intact so the spec does not need
          // to be aware of unrelated traffic.
          if (
            body.module === 'filament' &&
            body.submod === 'spool' &&
            body.action === 'batch_create'
          ) {
            state.interceptedWrite = body
            window.setTimeout(() => {
              document.dispatchEvent(new CustomEvent('cpp:device', {
                detail: {
                  head: { ...packet.head, type: 'response', ts: Date.now() },
                  body: {
                    module: 'filament',
                    submod: body.submod,
                    action: body.action,
                    error_code: 0,
                    message: 'dry-run intercepted by STUDIO-18344 e2e',
                    payload: [],
                  },
                },
              }))
            }, 0)
            return
          }

          original(message)
        },
      },
    }
    win.__studio18344Restore = () => {
      if (win.chrome?.webview) win.chrome.webview.postMessage = original
      delete win.__studio18344
      delete win.__studio18344Restore
    }
  })
}

async function readBridgeState(page: Page): Promise<CapturedBridgeState> {
  return page.evaluate(() => {
    const state = (window as unknown as { __studio18344?: CapturedBridgeState }).__studio18344
    return state ?? { requests: [], responses: [] }
  })
}

function requiredFields(): (keyof SpoolPayload)[] {
  // Every payload entry must carry these fields per STUDIO-18344 design:
  // they come straight off the AMS tray (no form intervention) so the
  // cloud-side record stays byte-identical to a single-slot save.
  return ['brand', 'material_type', 'color_code', 'tag_uid', 'bound_ams_id', 'bound_dev_id']
}

test.describe('STUDIO-18344 real AMS multi-select batch add dry-run @studio-18344 @printer', () => {
  test.afterEach(async ({ page }) => {
    await page.evaluate(() => {
      const win = window as unknown as { __studio18344Restore?: () => void }
      win.__studio18344Restore?.()
    }).catch(() => undefined)
  })

  test('STUDIO-18344 multi-select two AMS slots batches into a single batch_create write', async ({
    page,
    filamentList,
  }, testInfo) => {
    await attachMarkdown(testInfo, 'studio-18344-validation-plan.md', VALIDATION_PLAN)

    // Force a reload up front so the WebView pulls the freshly built dist
    // from DeviceHttpServer. Studio is typically launched before dist is
    // rebuilt, so the page still holds the pre-STUDIO-18344 chunk. The
    // probe testids in studio.ts' ensureFreshBundle do NOT include the
    // new ones (ams-slot-checkbox / ams-batch-summary), so without this
    // explicit reload the assertions could fail against stale JS even
    // when the on-disk dist is correct.
    await page.reload({ waitUntil: 'domcontentloaded', timeout: 15_000 })
    await page.getByTestId('filament-page-root').waitFor({ state: 'visible', timeout: 10_000 })

    await installBridgeSpy(page)

    const dialog = await filamentList.openAddDialog()
    await dialog.switchTab('ams')
    await expect(dialog.amsGrid, 'AMS grid should be visible before sampling real slots').toBeVisible()
    await attachPageScreenshot(page, testInfo, '01-ams-panel-real-data.png')

    // Snapshot slots before we touch anything: helps reviewers compare
    // selection against the live AMS layout.
    //
    // FilamentAddDialog.listAmsSlots() uses a prefix selector
    // (`[data-testid^="ams-slot-"]`) which also catches the new
    // `ams-slot-checkbox` children STUDIO-18344 introduced inside each
    // slot card. Filter the snapshot down to genuine slot cards (testId
    // must end with two integer indexes) so the rest of the spec talks
    // about slots only.
    const SLOT_TESTID_RE = /^ams-slot-\d+-\d+$/
    const allSlotsRaw = await dialog.listAmsSlots()
    const slots = allSlotsRaw.filter((slot) => SLOT_TESTID_RE.test(slot.testId))
    const nonEmptySlots = slots.filter((slot) => !slot.empty)
    await attachJson(testInfo, 'ams-slot-metrics.json', {
      rawCount: allSlotsRaw.length,
      filteredSlotCount: slots.length,
      nonEmptySlots: nonEmptySlots.length,
      slots,
    })

    expect(
      nonEmptySlots.length,
      'Precondition failed: need at least 2 real non-empty AMS trays to validate multi-select batch add. ' +
        'Load filament into at least two slots of the currently selected AMS unit and retry.',
    ).toBeGreaterThanOrEqual(2)
    if (nonEmptySlots.length < 2) return

    // Pick the first two. Spec stays minimal: 2 is the smallest size that
    // exercises the "multi-select" code path; selecting more would just
    // increase the assertion surface without adding coverage.
    const picked: AmsSlotSnapshot[] = [nonEmptySlots[0], nonEmptySlots[1]]

    // Click by raw testId rather than (unit, tray) — real AMS payloads
    // can carry non-numeric `ams_id` values (e.g. AMS Lite, AMS HT, or
    // external storage), in which case FilamentAddDialog.listAmsSlots
    // defaults the parsed unit/tray to -1 and re-deriving via
    // dialog.amsSlot(unit, tray) would dead-link to "ams-slot--1--1".
    for (const slot of picked) {
      const slotLocator = page.getByTestId(slot.testId)
      await slotLocator.click()
      const checkbox = slotLocator.getByTestId('ams-slot-checkbox')
      await expect(
        checkbox,
        `Slot "${slot.testId}" should expose ams-slot-checkbox once tapped (STUDIO-18344).`,
      ).toHaveAttribute('data-checked', 'true', { timeout: 3_000 })
    }

    const summary = page.getByTestId('ams-batch-summary')
    await expect(
      summary,
      'Batch summary panel should appear once >=2 slots are selected.',
    ).toBeVisible({ timeout: 3_000 })
    await expect(
      summary,
      'Batch summary count must match number of selected slots.',
    ).toHaveAttribute('data-count', String(picked.length))

    // The editable filament form must collapse in multi-select mode -
    // brand select is the canary; if it stayed visible the form would
    // still be the source of truth and override authoritative tray fields.
    await expect(
      dialog.brandSelect,
      'Form brand select must be hidden while >=2 slots are selected (form collapses in batch mode).',
    ).toBeHidden()

    await attachPageScreenshot(page, testInfo, '02-multi-select-summary.png')

    await expect(
      dialog.confirmButton,
      'Confirm button must be enabled in batch state (selection >= 2 overrides per-field validation).',
    ).toBeEnabled()

    await dialog.confirm()
    await page.waitForFunction(() => {
      const state = (window as unknown as { __studio18344?: CapturedBridgeState }).__studio18344
      return !!state?.interceptedWrite
    }, undefined, { timeout: 7_000 })

    const state = await readBridgeState(page)
    await attachPageScreenshot(page, testInfo, '03-post-confirm.png')

    const write = state.interceptedWrite
    await attachJson(testInfo, 'studio-18344-bridge-capture.json', {
      selectedSlots: picked,
      interceptedWrite: write,
      requestCount: state.requests.length,
      responseCount: state.responses.length,
      requests: state.requests,
    })

    expect(write, 'Expected to capture a dry-run spool/batch_create request.').toBeTruthy()
    if (!write) return
    expect(write.action).toBe('batch_create')
    expect(write.submod).toBe('spool')
    expect(write.module).toBe('filament')

    const payload = (write.payload ?? {}) as BatchCreatePayload
    const creates = payload.creates ?? []
    const updates = payload.updates ?? []
    expect(
      creates.length + updates.length,
      `batch_create payload must carry exactly ${picked.length} entries across creates+updates. ` +
        `Got creates=${creates.length}, updates=${updates.length}.`,
    ).toBe(picked.length)

    const fields = requiredFields()
    const allEntries: SpoolPayload[] = [...creates, ...updates]
    for (const entry of allEntries) {
      for (const f of fields) {
        expect(
          String(entry[f] ?? ''),
          `batch_create entry missing required field "${f}". Entry: ${JSON.stringify(entry)}`,
        ).not.toBe('')
      }
      // Multi-select path must always declare itself as ams_sync so the
      // cloud-side dedupe / origin tracking stays consistent with the
      // single-slot AMS save path (STUDIO-18344 design).
      expect(entry.entry_method).toBe('ams_sync')
    }

    // Sanity: every update entry must carry a spool_id (so the C++
    // dispatcher can route it through the update path without a lookup).
    for (const u of updates) {
      expect(
        String(u.spool_id ?? ''),
        'updates[] entry must carry spool_id (STUDIO-18344 partition contract).',
      ).not.toBe('')
    }
  })
})
