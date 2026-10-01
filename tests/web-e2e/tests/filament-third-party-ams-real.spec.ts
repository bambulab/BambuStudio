/**
 * @studio-18340 @printer
 * JIRA: STUDIO-18340
 *
 * Real-data dry-run validation for same-RFID AMS import behavior. The final
 * write request is intercepted, so real inventory/cloud data is not modified.
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

interface AmsTrayPayload {
  slot_id?: string
  tag_uid?: string
  setting_id?: string
  color?: string
  colors?: string[]
  color_type?: number
  [key: string]: unknown
}

interface AmsUnitPayload {
  ams_id?: string
  trays?: AmsTrayPayload[]
}

interface AmsPayload {
  selected_dev_id?: string
  ams_units?: AmsUnitPayload[]
}

const VALIDATION_RESULT_MARKDOWN = `# STUDIO-18340 E2E 验证结果说明

## 验证目标

1. 使用真实 Studio 耗材管理器页面、真实 AMS 槽位数据和真实 UI 点击操作。
2. 选择一个真实 AMS 槽，并将表单改为第三方品牌 / 耗材类型。
3. 最终提交时，同 RFID 已存在的 spool 必须走 \`spool/update\`，不能走 \`spool/add\` 创建重复记录。
4. update payload 必须保留用户选择的第三方 \`brand\`、\`material_type\` 和 \`setting_id\`。
5. update payload 的 \`setting_id\` 不应退回 AMS tray 的官方 \`setting_id\`。
6. 为避免污染真实库存 / 云端，本用例拦截最终 \`spool/update|add\` 写请求并返回 synthetic success；其余数据读取均来自真实 C++ bridge。

## 操作步骤

1. 启动带 WebView2 CDP 的 BambuStudio。
2. 用户登录并打开 \`Device -> Filament Manager\`。
3. Playwright 通过 CDP 连接当前耗材管理器 WebView 页面。
4. 安装 bridge spy：记录真实 C++ response / request，并 dry-run 拦截最终写请求。
5. 点击 \`+ Add Filament\`。
6. 切换到 \`Read from AMS\`。
7. 读取真实 AMS 槽位。
8. 遍历真实非空 AMS 槽，找到一个允许编辑 Brand 的槽。
9. 在表单中选择第一个非 \`Bambu Lab\` 的第三方品牌，以及该品牌下第一个可用 material。
10. 点击 \`Add\`。
11. 断言捕获到的最终写请求满足验证目标，并将截图 / JSON 指标写入 Playwright HTML report。

## 报告附件

- \`01-ams-panel-real-data.png\`: AMS 面板真实数据截图。
- \`02-real-ams-slot-selected.png\`: 真实 AMS 槽选中后的截图。
- \`03-third-party-material-selected.png\`: 第三方品牌 / 类型选择后的截图。
- \`studio-18340-bridge-capture.json\`: bridge 请求 / 响应 / 最终写请求。
- \`studio-18340-validation-plan.md\`: 本说明文件。
`;

async function attachMarkdown(testInfo: TestInfo, name: string, value: string) {
  await testInfo.attach(name, {
    body: value,
    contentType: 'text/markdown',
  })
}

async function attachJson(testInfo: TestInfo, name: string, value: unknown) {
  await testInfo.attach(name, {
    body: JSON.stringify(value, null, 2),
    contentType: 'application/json',
  })
}

async function attachPageScreenshot(page: Page, testInfo: TestInfo, name: string) {
  await testInfo.attach(name, {
    body: await page.screenshot({ fullPage: true }),
    contentType: 'image/png',
  })
}

async function installBridgeSpy(page: Page) {
  await page.evaluate(() => {
    type CapturedPacket = {
      head: { version: string; type: string; seq: number; ts: number }
      body: BridgeBody
    }
    const win = window as unknown as {
      __studio18340?: CapturedBridgeState
      __studio18340Restore?: () => void
      chrome?: { webview?: { postMessage?: (message: string) => void } }
    }
    const original = win.chrome?.webview?.postMessage?.bind(win.chrome.webview)
    if (!original) throw new Error('chrome.webview.postMessage is unavailable; cannot spy real bridge.')

    const state: CapturedBridgeState = { requests: [], responses: [] }
    win.__studio18340 = state

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

          if (
            body.module === 'filament' &&
            body.submod === 'spool' &&
            (body.action === 'add' || body.action === 'update')
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
                    message: 'dry-run intercepted by STUDIO-18340 e2e',
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
    win.__studio18340Restore = () => {
      if (win.chrome?.webview) win.chrome.webview.postMessage = original
      delete win.__studio18340
      delete win.__studio18340Restore
    }
  })
}

async function readBridgeState(page: Page): Promise<CapturedBridgeState> {
  return page.evaluate(() => {
    const state = (window as unknown as { __studio18340?: CapturedBridgeState }).__studio18340
    return state ?? { requests: [], responses: [] }
  })
}

function latestAmsPayload(state: CapturedBridgeState): AmsPayload | undefined {
  const amsResponse = [...state.responses]
    .reverse()
    .find((body) => body.submod === 'ams' && body.payload)
  return amsResponse?.payload as AmsPayload | undefined
}

function trayPayloadFor(slot: AmsSlotSnapshot, payload: AmsPayload | undefined): AmsTrayPayload | undefined {
  for (const unit of payload?.ams_units ?? []) {
    if (String(unit.ams_id ?? '') !== String(slot.unit)) continue
    const tray = (unit.trays ?? []).find((item) => String(item.slot_id ?? '') === String(slot.tray))
    if (tray) return tray
  }
  return undefined
}

test.describe('STUDIO-18340 real AMS third-party import dry-run @studio-18340 @printer', () => {
  test.afterEach(async ({ page }) => {
    await page.evaluate(() => {
      const win = window as unknown as { __studio18340Restore?: () => void }
      win.__studio18340Restore?.()
    }).catch(() => undefined)
  })

  test('STUDIO-18340 real AMS import submits update for existing RFID and preserves third-party setting_id', async ({
    page,
    filamentList,
  }, testInfo) => {
    await attachMarkdown(testInfo, 'studio-18340-validation-plan.md', VALIDATION_RESULT_MARKDOWN)
    await installBridgeSpy(page)

    const dialog = await filamentList.openAddDialog()
    await dialog.switchTab('ams')
    await expect(dialog.amsGrid, 'AMS grid should be visible before sampling real slots').toBeVisible()
    await attachPageScreenshot(page, testInfo, '01-ams-panel-real-data.png')

    const slots = await dialog.listAmsSlots()
    await attachJson(testInfo, 'ams-slot-metrics.json', {
      totalSlots: slots.length,
      nonEmptySlots: slots.filter((slot) => !slot.empty).length,
      slots,
    })
    const nonEmptySlots = slots.filter((slot) => !slot.empty)
    const nonEmptySlot = nonEmptySlots[0]
    expect(
      nonEmptySlot,
      'Precondition failed: need at least one real non-empty AMS tray for STUDIO-18340 validation.',
    ).toBeTruthy()
    if (!nonEmptySlot) return

    let state = await readBridgeState(page)
    let editableSlot: AmsSlotSnapshot | undefined
    let tray: AmsTrayPayload | undefined
    for (const slot of nonEmptySlots) {
      await dialog.pickAmsSlot(slot.unit, slot.tray)
      await attachPageScreenshot(page, testInfo, `02-real-ams-slot-${slot.unit}-${slot.tray}-selected.png`)
      if (await dialog.canEditBrand()) {
        editableSlot = slot
        state = await readBridgeState(page)
        tray = trayPayloadFor(slot, latestAmsPayload(state))
        break
      }
    }
    expect(
      editableSlot,
      'Precondition failed: every real non-empty AMS tray currently locks the Brand field, so this environment cannot validate editing an AMS-read filament into a third-party filament. Check ams-slot-metrics.json and slot screenshots.',
    ).toBeTruthy()
    if (!editableSlot) return

    expect(
      tray?.tag_uid,
      'Precondition failed: selected real AMS tray must expose tag_uid in the real C++ AMS payload.',
    ).toBeTruthy()
    if (!tray?.tag_uid) return

    const brands = await dialog.listBrands()
    const thirdPartyBrand = brands.find((brand) => brand && !/bambu\s*lab/i.test(brand))
    expect(
      thirdPartyBrand,
      `Precondition failed: need a non-Bambu Lab brand in the real brand dropdown. Brands: ${brands.join(', ')}`,
    ).toBeTruthy()
    if (!thirdPartyBrand) return

    await dialog.selectBrand(thirdPartyBrand)
    const materials = await dialog.listMaterials()
    const material = materials[0]
    expect(
      material,
      `Precondition failed: third-party brand "${thirdPartyBrand}" has no selectable material.`,
    ).toBeTruthy()
    if (!material) return
    await dialog.selectMaterial(material)
    await attachPageScreenshot(page, testInfo, '03-third-party-material-selected.png')

    await dialog.confirm()
    await page.waitForFunction(() => {
      const state = (window as unknown as { __studio18340?: CapturedBridgeState }).__studio18340
      return !!state?.interceptedWrite
    })

    state = await readBridgeState(page)
    await attachJson(testInfo, 'studio-18340-bridge-capture.json', {
      selectedSlot: editableSlot,
      selectedTrayPayload: tray,
      selectedThirdPartyBrand: thirdPartyBrand,
      selectedMaterial: material,
      interceptedWrite: state.interceptedWrite,
      requestCount: state.requests.length,
      responseCount: state.responses.length,
      requests: state.requests,
    })

    const write = state.interceptedWrite
    expect(write, 'Expected to capture a dry-run spool write request.').toBeTruthy()
    expect(
      write?.action,
      'Expected same-RFID AMS import to update an existing spool. If this is "add", the selected real AMS RFID is not present in the current inventory, so STUDIO-18340 update-path cannot be validated with this slot.',
    ).toBe('update')
    expect(write?.payload?.brand).toBe(thirdPartyBrand)
    expect(String(write?.payload?.material_type ?? '')).not.toBe('')
    expect(String(write?.payload?.setting_id ?? '')).not.toBe('')
    if (tray.setting_id) {
      expect(
        write?.payload?.setting_id,
        'User-selected third-party setting_id must not fall back to the AMS tray setting_id.',
      ).not.toBe(tray.setting_id)
    }
  })
})
