# 耗材管理器自动化 E2E 用例索引

本索引记录耗材管理器可复用的 Playwright E2E 用例，按 Jira / 场景沉淀验证目标、前置条件、命令和证据附件。

## 使用入口

| 类型 | 适用场景 | 命令形态 |
| --- | --- | --- |
| `@webview-only` | 前端状态、bridge 契约、无真实设备依赖的回归 | `pnpm exec playwright test <spec> --project=webview-only` |
| `@printer` | 需要真实 Studio、真实 WebView2、真实设备 / AMS / 云端数据 | `pnpm exec playwright test <spec> --project=printer --grep "STUDIO-xxxxx"` |

真实用例执行前先按 `device-real-webview-e2e` 技能确认：dist 已生成、Studio 以 CDP 启动、用户已登录并切到耗材管理器页面。

## 用例清单

### STUDIO-18341 · AMS 渐变 / 多拼颜色类型

| 项 | 内容 |
| --- | --- |
| 问题 | 耗材管理器里 AMS 槽位和当前选择颜色把渐变 / 多拼显示反了 |
| 真实用例 | `tests/web-e2e/tests/filament-color-type-real-ams.spec.ts` |
| 标签 | `@studio-18341 @printer` |
| 命令 | `pnpm exec playwright test tests/filament-color-type-real-ams.spec.ts --project=printer --grep "STUDIO-18341"` |
| 前置条件 | 真实 AMS 至少有一个多色渐变槽和一个多拼槽；页面已打开 `Device -> Filament Manager` |
| 核心断言 | 渐变槽 `data-color-type=0`，多拼槽 `data-color-type=1`；点击后 preview hex 与槽位 `data-colors` 一致；渐变为 smooth gradient，多拼为硬分段 |
| 证据附件 | `studio-18341-validation-plan.md`、`ams-slot-metrics.json`、`studio-18341-validation-metrics.json`、槽位/preview 截图 |
| 注意 | 用例必须避免只验证"页面自洽"；如果 Jira 指定 A2/A4/A3 等物理槽位，需把物理槽位事实纳入断言或附件说明 |

Mock 回归：

- `tests/web-e2e/tests/filament-color-type-regressions.spec.ts`
- 标签：`@studio-18341 @webview-only`
- 覆盖：mock AMS payload 中 `color_type=0` 渲染渐变，`color_type=1` 渲染多拼硬分段。

### STUDIO-18340 · AMS 读取同 RFID 更新而非重复新增

| 项 | 内容 |
| --- | --- |
| 问题 | 从 AMS 读取导入时，同 RFID 已存在的 spool 应更新，不应创建重复记录；用户选择的第三方材料不能被 AMS 官方 setting_id 覆盖 |
| 真实用例 | `tests/web-e2e/tests/filament-third-party-ams-real.spec.ts` |
| 标签 | `@studio-18340 @printer` |
| 命令 | `pnpm exec playwright test tests/filament-third-party-ams-real.spec.ts --project=printer --grep "STUDIO-18340"` |
| 前置条件 | 真实 AMS 有非空槽；该槽 RFID 在当前耗材库存中已有对应 spool；Brand 字段可编辑；存在非 `Bambu Lab` 的第三方品牌与可选材料 |
| 核心断言 | 最终写请求为 `spool/update` 而非 `spool/add`；payload 保留第三方 `brand/material_type/setting_id`；`setting_id` 不回退到 AMS tray 官方值 |
| 写入策略 | 安装 bridge spy，dry-run 拦截最终 `spool/add|update` 并返回 synthetic success，不污染真实库存 / 云端 |
| 证据附件 | `studio-18340-validation-plan.md`、`ams-slot-metrics.json`、`studio-18340-bridge-capture.json`、槽位/第三方材料截图 |

Mock 回归：

- `tests/web-e2e/tests/filament-color-type-regressions.spec.ts`
- 标签：`@studio-18340 @webview-only`
- 覆盖：mock 同 RFID 场景必须发 `spool/update`，不能发 `spool/add`，且 setting_id 保持用户选择。

### STUDIO-18126 · 列表分组按钮状态

| 项 | 内容 |
| --- | --- |
| 问题 | 列表工具栏分组按钮在重构中多次回归 |
| 用例 | `tests/web-e2e/tests/filament-group-toggle.spec.ts` |
| 标签 | `@filament-baseline @webview-only` |
| 命令 | `pnpm exec playwright test tests/filament-group-toggle.spec.ts --project=webview-only` |
| 核心断言 | 点击分组按钮后 `data-grouped` 翻转；再次点击恢复初始状态 |

## 新增用例归档规则

1. 在测试文件头部写 Jira、标签和是否真实设备依赖；
2. 真实 `@printer` 用例必须写清前置条件和失败提示；
3. 会写库存 / 云端的用例默认 dry-run 拦截最终写请求；
4. 用 `testInfo.attach()` 输出 Markdown 计划、JSON 指标和关键截图；
5. 新增 Jira 用例后同步补充本文件和 `耗材管理器/README.md` 的自测入口。
