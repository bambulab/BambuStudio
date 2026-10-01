# STUDIO-18341 E2E 验证结果说明

## 验证目标

1. 真实 AMS 渐变槽必须保持 `data-color-type=0`，且 `data-colors` 至少包含 2 个颜色。
2. 真实 AMS 多拼槽必须保持 `data-color-type=1`，且 `data-colors` 至少包含 2 个颜色。
3. 点击真实渐变槽后，预览栏 hex 列表必须与该槽 `data-colors` 完全一致。
4. 点击真实多拼槽后，预览栏 hex 列表必须与该槽 `data-colors` 完全一致。
5. 渐变预览必须渲染为 smooth gradient。
6. 多拼预览必须渲染为硬分段色块，分段数量不少于颜色数量。
7. 验证必须接入真实 Studio 页面、真实 AMS 数据和真实点击操作，不使用 mock bridge / 注入数据。

## 操作步骤

1. 启动带 WebView2 CDP 的 BambuStudio。
2. 用户登录并打开 `Device -> Filament Manager`。
3. Playwright 通过 CDP 连接当前耗材管理器 WebView 页面。
4. 点击 `+ Add Filament`。
5. 切换到 `Read from AMS`。
6. 读取真实 AMS 槽位 DOM 指标：
   - `data-testid`
   - `data-empty`
   - `data-color-type`
   - `data-colors`
   - 槽位文本
7. 自动筛选一个真实渐变槽：`data-color-type=0` 且 `data-colors.length > 1`。
8. 自动筛选一个真实多拼槽：`data-color-type=1` 且 `data-colors.length > 1`。
9. 点击真实渐变槽，截图并采集预览栏 hex / 背景证据。
10. 点击真实多拼槽，截图并采集预览栏 hex / 分段色块证据。
11. 将截图和 JSON 指标写入 Playwright HTML report。

## 报告附件

- `01-ams-panel-real-data.png`: AMS 面板真实数据截图。
- `02-gradient-slot-selected.png`: 真实渐变槽选中后的截图。
- `03-multicolor-slot-selected.png`: 真实多拼槽选中后的截图。
- `ams-slot-metrics.json`: 所有真实 AMS 槽位指标。
- `studio-18341-validation-metrics.json`: 渐变 / 多拼选中后的断言证据。
- `studio-18341-validation-plan.md`: 本说明文件，会随 Playwright HTML report 一起展示。

## 最近一次执行结果

- 命令：`playwright test tests/filament-color-type-real-ams.spec.ts --project=printer`
- 最近一次结果：`1 passed`
