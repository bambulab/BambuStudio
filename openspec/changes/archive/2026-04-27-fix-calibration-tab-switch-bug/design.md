## Context

主界面使用 `Tabbook`（自定义 wxNotebook）管理多个 tab 页面。`TabPosition` 枚举为每个 tab 分配了硬编码索引（0-10）。但部分 tab 是条件添加的（如 Multi-device 仅在 `is_enable_multi_machine()` 时添加），导致实际运行时的 tab 索引与枚举值不一致。

当前校准任务发送完成后，`Plater::send_calibration_job_finished()` 调用 `request_select_tab(tpCalibration)`（索引 6），在 Multi-device 未启用时实际跳转到了 Filament Manager（实际索引 6）。

项目中已有安全的页面切换方法 `select_tab(wxPanel* panel)`，内部通过 `FindPage` 动态查找面板的实际索引，不依赖硬编码值。

## Goals / Non-Goals

**Goals:**
- 修复校准发送完成后跳转到错误页面的 bug
- 使用已有的指针查找方式替代硬编码索引跳转

**Non-Goals:**
- 不重构整个 `TabPosition` 枚举系统（影响范围过大）
- 不修改其他非校准相关的 `request_select_tab` 调用（需单独评估）
- 不改变校准流程的业务逻辑（发送成功后跳回校准页面仍是预期行为）

## Decisions

### Decision 1: 用 `select_tab(wxPanel*)` 替代 `request_select_tab(TabPosition)`

**选择**：将 `Plater.cpp:19180` 的 `request_select_tab(tpCalibration)` 改为 `select_tab(m_calibration)`

**替代方案**：
- A) 根据 `is_enable_multi_machine()` 动态计算偏移量 → 脆弱，未来增删 tab 会再次打破
- B) 重构 `TabPosition` 枚举为动态查找系统 → 正确但影响范围过大，不适合 bug 修复
- C) 在 `request_select_tab` 中改用 `FindPage` → 可行但需要传入 panel 指针，接口变动大

**理由**：方案最小化改动，复用已有的安全方法，且 `select_tab(wxPanel*)` 在项目中已被广泛使用（`MainFrame.cpp:3927`）。

### Decision 2: 需要注意线程安全

`select_tab(wxPanel*)` 是同步调用，而原来的 `request_select_tab` 通过 `wxQueueEvent` 异步投递事件。校准发送完成回调 `send_calibration_job_finished` 是在主线程（通过 `EVT_SEND_CALIBRATION_FINISHED` wxEvent 触发）中执行的，所以直接调用 `select_tab` 是安全的。

## Risks / Trade-offs

- **[极低] 行为微变**：原来是异步切换（`wxQueueEvent`），改为同步切换（直接调用）。由于已经在主线程 UI 事件回调中，实际无差异 → 不需要额外处理
- **[低] 其他调用点可能有同类问题**：项目中还有 `print_job_finished` 等地方也用 `request_select_tab(tpMonitor)`，`tpMonitor = 3` 在任何配置下都是固定的（之前没有条件 tab），暂不受影响 → 后续可统一排查
