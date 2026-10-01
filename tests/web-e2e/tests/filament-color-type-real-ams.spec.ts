/**
 * @studio-18341 @printer
 * JIRA: STUDIO-18341
 *
 * Real-device validation for AMS gradient/multicolor color semantics.
 * Requires a running Studio session with Filament Manager open.
 */
import type { Page, TestInfo } from '@playwright/test'
import { test, expect } from '../src/fixtures/filament'
import type { AmsSlotSnapshot } from '../src/pages/FilamentAddDialog'

interface SlotValidationMetric {
  role: 'gradient' | 'multicolor'
  slot: AmsSlotSnapshot
  previewHexes: string[]
  previewColorEvidence: {
    background: string
    segmentBackgrounds: string[]
  }
}

const VALIDATION_RESULT_MARKDOWN = `# STUDIO-18341 E2E 验证结果说明

## 验证目标

1. 真实 AMS 渐变槽必须保持 \`data-color-type=0\`，且 \`data-colors\` 至少包含 2 个颜色。
2. 真实 AMS 多拼槽必须保持 \`data-color-type=1\`，且 \`data-colors\` 至少包含 2 个颜色。
3. 点击真实渐变槽后，预览栏 hex 列表必须与该槽 \`data-colors\` 完全一致。
4. 点击真实多拼槽后，预览栏 hex 列表必须与该槽 \`data-colors\` 完全一致。
5. 渐变预览必须渲染为 smooth gradient。
6. 多拼预览必须渲染为硬分段色块，分段数量不少于颜色数量。
7. 验证必须接入真实 Studio 页面、真实 AMS 数据和真实点击操作，不使用 mock bridge / 注入数据。

## 操作步骤

1. 启动带 WebView2 CDP 的 BambuStudio。
2. 用户登录并打开 \`Device -> Filament Manager\`。
3. Playwright 通过 CDP 连接当前耗材管理器 WebView 页面。
4. 点击 \`+ Add Filament\`。
5. 切换到 \`Read from AMS\`。
6. 读取真实 AMS 槽位 DOM 指标：
   - \`data-testid\`
   - \`data-empty\`
   - \`data-color-type\`
   - \`data-colors\`
   - 槽位文本
7. 自动筛选一个真实渐变槽：\`data-color-type=0\` 且 \`data-colors.length > 1\`。
8. 自动筛选一个真实多拼槽：\`data-color-type=1\` 且 \`data-colors.length > 1\`。
9. 点击真实渐变槽，截图并采集预览栏 hex / 背景证据。
10. 点击真实多拼槽，截图并采集预览栏 hex / 分段色块证据。
11. 将截图和 JSON 指标写入 Playwright HTML report。

## 报告附件

- \`01-ams-panel-real-data.png\`: AMS 面板真实数据截图。
- \`02-gradient-slot-selected.png\`: 真实渐变槽选中后的截图。
- \`03-multicolor-slot-selected.png\`: 真实多拼槽选中后的截图。
- \`ams-slot-metrics.json\`: 所有真实 AMS 槽位指标。
- \`studio-18341-validation-metrics.json\`: 渐变 / 多拼选中后的断言证据。
- \`studio-18341-validation-plan.md\`: 本说明文件。
`;

async function attachJson(testInfo: TestInfo, name: string, value: unknown) {
  await testInfo.attach(name, {
    body: JSON.stringify(value, null, 2),
    contentType: 'application/json',
  })
}

async function attachMarkdown(testInfo: TestInfo, name: string, value: string) {
  await testInfo.attach(name, {
    body: value,
    contentType: 'text/markdown',
  })
}

async function attachPageScreenshot(page: Page, testInfo: TestInfo, name: string) {
  await testInfo.attach(name, {
    body: await page.screenshot({ fullPage: true }),
    contentType: 'image/png',
  })
}

function findRequiredSlot(slots: AmsSlotSnapshot[], colorType: '0' | '1') {
  return slots.find((slot) => !slot.empty && slot.colorType === colorType && slot.colors.length > 1)
}

test.describe('STUDIO-18341 real AMS colour semantics @studio-18341 @printer', () => {
  test('STUDIO-18341 real AMS gradient and multicolor trays render with correct type and preview evidence', async ({
    page,
    filamentList,
  }, testInfo) => {
    await attachMarkdown(testInfo, 'studio-18341-validation-plan.md', VALIDATION_RESULT_MARKDOWN)

    const dialog = await filamentList.openAddDialog()
    await dialog.switchTab('ams')
    await expect(dialog.amsGrid, 'AMS grid should be visible before sampling real slots').toBeVisible()
    await attachPageScreenshot(page, testInfo, '01-ams-panel-real-data.png')

    const slots = await dialog.listAmsSlots()
    await attachJson(testInfo, 'ams-slot-metrics.json', {
      totalSlots: slots.length,
      nonEmptySlots: slots.filter((slot) => !slot.empty).length,
      multiHexSlots: slots.filter((slot) => slot.colors.length > 1).length,
      slots,
    })

    const gradient = findRequiredSlot(slots, '0')
    const multicolor = findRequiredSlot(slots, '1')

    expect(
      gradient,
      'Precondition failed: need one real AMS gradient tray with data-color-type=0 and at least 2 colors. ' +
        'Check ams-slot-metrics.json and 01-ams-panel-real-data.png in the HTML report.',
    ).toBeTruthy()
    expect(
      multicolor,
      'Precondition failed: need one real AMS multicolor tray with data-color-type=1 and at least 2 colors. ' +
        'Check ams-slot-metrics.json and 01-ams-panel-real-data.png in the HTML report.',
    ).toBeTruthy()
    if (!gradient || !multicolor) return

    await dialog.pickAmsSlot(gradient.unit, gradient.tray)
    await expect(dialog.previewBar, 'gradient preview should appear after picking the real tray').toBeVisible()
    await attachPageScreenshot(page, testInfo, '02-gradient-slot-selected.png')
    const gradientPreview = await dialog.readPreview()
    const gradientColorEvidence = await dialog.readPreviewColorEvidence()
    expect(gradientPreview.hexes, 'gradient preview hex list must match the real AMS colors').toEqual(gradient.colors)
    expect(gradientColorEvidence.background.toLowerCase(), 'gradient preview should be rendered as a CSS gradient').toContain('linear-gradient')

    await dialog.pickAmsSlot(multicolor.unit, multicolor.tray)
    await expect(dialog.previewBar, 'multicolor preview should appear after picking the real tray').toBeVisible()
    await attachPageScreenshot(page, testInfo, '03-multicolor-slot-selected.png')
    const multicolorPreview = await dialog.readPreview()
    const multicolorColorEvidence = await dialog.readPreviewColorEvidence()
    expect(multicolorPreview.hexes, 'multicolor preview hex list must match the real AMS colors').toEqual(multicolor.colors)
    expect(
      multicolorColorEvidence.segmentBackgrounds.length,
      'multicolor preview should render one or more hard color segments',
    ).toBeGreaterThanOrEqual(multicolor.colors.length)

    const metrics: SlotValidationMetric[] = [
      {
        role: 'gradient',
        slot: gradient,
        previewHexes: gradientPreview.hexes,
        previewColorEvidence: gradientColorEvidence,
      },
      {
        role: 'multicolor',
        slot: multicolor,
        previewHexes: multicolorPreview.hexes,
        previewColorEvidence: multicolorColorEvidence,
      },
    ]
    await attachJson(testInfo, 'studio-18341-validation-metrics.json', metrics)

    await dialog.cancel()
  })
})
