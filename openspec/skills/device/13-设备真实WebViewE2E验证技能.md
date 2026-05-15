---
name: device-real-webview-e2e
description: >-
  用真实运行的 BambuStudio WebView2 跑设备域 Playwright E2E。用于用户要求
  "跑真实 E2E"、"验证 WebView/耗材管理器/AMS/云端联调"、"重新生成 dist 后验证"、
  "用 printer 用例复测 Jira" 时。
---

# 设备真实 WebView E2E 验证技能

把设备域 WebView 功能跑在真实 `bambu-studio.exe`、真实 WebView2、真实 bridge / AMS / 云端环境上验证。
它和 `@webview-only` mock 用例互补：mock 先锁业务契约，真实 `@printer` 再证明当前桌面进程和设备状态能通过。

## 适用场景

- 用户要求"重新生成 dist 后验证"、"用真实 Studio 跑 E2E"；
- Jira/Gerrit 涉及耗材管理器、AMS、设备页、云端同步等 WebView 功能；
- 需要证明问题不是前端自洽，而是真实 C++ bridge / 设备 payload / WebView DOM 都正确；
- 需要 dry-run 拦截最终写请求，验证 payload 但不污染真实库存或云端。

## 不适用

- 只需要静态分析或 mock 回归：直接跑 `@webview-only`；
- 需要本地 C++ 编译调试：先走 `device-local-build` / `device-local-debug`；
- 需要提交或推送：验证完成后再走 `device-local-commit` / `device-gerrit-push`。

## 标准流程

1. **确认用例入口**
   - 真实 printer：`tests/web-e2e/tests/*real*.spec.ts` 或带 `@printer` 标签；
   - mock 回归：`tests/web-e2e/tests/*regressions*.spec.ts` 或 `@webview-only`；
   - Playwright 配置：`tests/web-e2e/playwright.config.ts`。

2. **重新生成 WebView dist（如前端有改动）**
   ```powershell
   pnpm build
   cmake --build build_release --target device_page_build --config Release -j 4
   ```
   - 第一条在 `src/slic3r/GUI/DeviceWeb/device_page` 下执行；
   - 第二条在仓库根目录执行，把 dist 复制到 `resources/web/device_page/dist`；
   - 若 Studio 已运行，运行目录 `build_release/src/Release/resources/...` 可能被锁，必须重启后再覆盖。

3. **启动或确认真实 Studio CDP**
   - 推荐端口：`STUDIO_E2E_CDP_PORT=9222`；
   - 已运行时先探测：
     ```powershell
     Invoke-RestMethod http://127.0.0.1:9222/json/list
     ```
   - 若未暴露 CDP，用 `tests/web-e2e/scripts/check-cdp.ps1 -Mode launch` 启动。

4. **让用户准备真实页面**
   - 登录目标账号；
   - 打开 `Device -> Filament Manager` 或对应 WebView 页面；
   - 选择目标打印机 / AMS 槽位；
   - 等页面可见后再跑用例。

5. **运行用例**
   ```powershell
   pnpm exec playwright test tests/<spec-file>.spec.ts --project=printer --grep "STUDIO-xxxxx"
   ```
   常用：
   - `tests/filament-third-party-ams-real.spec.ts --grep "STUDIO-18340"`
   - `tests/filament-color-type-real-ams.spec.ts --grep "STUDIO-18341"`

6. **保存证据**
   - 让用例通过 `testInfo.attach()` 写入 Markdown、JSON、截图；
   - 失败时看 `test-results/` 和 HTML report；
   - 对真实写操作优先 dry-run 拦截 `spool/add|update`，验证 payload 后返回 synthetic success。

## 真实用例设计原则

- 断言必须绑定真实事实：例如物理槽位 A2/A4 是渐变、A3 是多拼时，不要只按页面自己的 `data-color-type` 反向筛选；
- 若只验证"页面吐出的标签和渲染自洽"，必须再补 raw payload、物理槽位或人工已知事实作为证据；
- `@printer` 用例可以有前置条件失败，但失败信息必须告诉用户需要准备什么设备/库存状态；
- 任何会写库存/云端的真实用例，默认拦截最后写请求做 dry-run；除非用户明确允许真实写入；
- 报告附件至少包含：验证计划 Markdown、真实槽位/bridge JSON、关键截图。

## 常见问题

| 现象 | 处理 |
| --- | --- |
| `9222` 无法连接 | Studio 不是带 CDP 启动；用 `check-cdp.ps1 -Mode launch` 重启 |
| 找不到耗材页面 | 用户还没切到 `#/filament`；让用户打开耗材管理器再 probe |
| 覆盖 dist 失败 | 运行中的 Studio 锁住 `build_release/src/Release/resources/...`；关闭后复制 |
| `@printer` 前置失败 | 真实设备状态不满足；按失败信息准备 AMS 槽、RFID、库存或云端账号 |
| E2E 通过但用户仍复现 | 检查测试是否只验证自洽，补 raw payload / 物理槽位 / 人工事实断言 |

## 归档约定

可复用 Jira E2E 用例要沉淀到对应专题的 `自测/自动化E2E用例索引.md`：

- 写清 Jira、测试文件、标签、命令、前置条件、是否 dry-run、核心断言；
- 真实 `@printer` 和 mock `@webview-only` 分开列；
- 新增用例后同步更新专题 `README.md` 的自测入口。
