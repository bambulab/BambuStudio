# STUDIO-18340 E2E 验证结果说明

## 验证目标

1. 使用真实 Studio 耗材管理器页面、真实 AMS 槽位数据和真实 UI 点击操作。
2. 选择一个真实 AMS 槽，并将表单改为第三方品牌 / 耗材类型。
3. 最终提交时，同 RFID 已存在的 spool 必须走 `spool/update`，不能走 `spool/add` 创建重复记录。
4. update payload 必须保留用户选择的第三方 `brand`、`material_type` 和 `setting_id`。
5. update payload 的 `setting_id` 不应退回 AMS tray 的官方 `setting_id`。
6. 为避免污染真实库存 / 云端，本用例拦截最终 `spool/update|add` 写请求并返回 synthetic success；其余数据读取均来自真实 C++ bridge。

## 操作步骤

1. 启动带 WebView2 CDP 的 BambuStudio。
2. 用户登录并打开 `Device -> Filament Manager`。
3. Playwright 通过 CDP 连接当前耗材管理器 WebView 页面。
4. 安装 bridge spy：记录真实 C++ response / request，并 dry-run 拦截最终写请求。
5. 点击 `+ Add Filament`。
6. 切换到 `Read from AMS`。
7. 读取真实 AMS 槽位。
8. 遍历真实非空 AMS 槽，找到一个允许编辑 Brand 的槽。
9. 在表单中选择第一个非 `Bambu Lab` 的第三方品牌，以及该品牌下第一个可用 material。
10. 点击 `Add`。
11. 断言捕获到的最终写请求满足验证目标，并将截图 / JSON 指标写入 Playwright HTML report。

## 报告附件

- `01-ams-panel-real-data.png`: AMS 面板真实数据截图。
- `02-real-ams-slot-<unit>-<tray>-selected.png`: 每个被尝试的真实 AMS 槽选中后的截图。
- `ams-slot-metrics.json`: 所有真实 AMS 槽位指标。
- `03-third-party-material-selected.png`: 第三方品牌 / 类型选择后的截图。
- `studio-18340-bridge-capture.json`: bridge 请求 / 响应 / 最终写请求。
- `studio-18340-validation-plan.md`: 本说明文件，会随 Playwright HTML report 一起展示。

## 执行说明

命令：

```powershell
playwright test tests/filament-third-party-ams-real.spec.ts --project=printer
```

如果结果失败且最终写请求是 `spool/add`，通常表示当前选中的真实 AMS RFID 尚未存在于耗材库存，无法验证“同 RFID 更新既有 spool”路径。需要先准备一个已经从该 AMS RFID 导入过的真实 spool，再重新运行。

如果结果失败且提示 Brand 字段被锁定，通常表示当前真实 AMS 槽都有设备侧品牌 / 材料信息，UI 不允许在该入口改成第三方耗材。需要准备一个可编辑 Brand 的真实 AMS 槽，或改用真实已入库 spool 的编辑入口做验证。
