## Why

校准任务发送成功后，页面会错误地跳转到"耗材管理"页面，而非预期的"校准"页面。根因是 `TabPosition` 枚举使用硬编码索引（`tpCalibration = 6`），但 Multi-device tab 是条件添加的——未启用时所有后续 tab 的实际索引比枚举值少 1，导致索引 6 指向了 Filament Manager 而非 Calibration。

## What Changes

- 将 `Plater::send_calibration_job_finished()` 中的 `request_select_tab(tpCalibration)` 改为通过面板指针查找的 `select_tab(m_calibration)`，避免硬编码索引偏移问题
- 排查项目中其他使用 `request_select_tab` 或硬编码 `TabPosition` 枚举进行页面切换的调用点，评估是否存在同类问题

## Capabilities

### New Capabilities

（无新增能力）

### Modified Capabilities

（无规格级别的行为变更——这是一个实现层面的 bug 修复，不涉及需求变更）

## Impact

- **受影响代码**：`src/slic3r/GUI/Plater.cpp` 中的 `send_calibration_job_finished()`，以及可能的其他 `request_select_tab` 调用点
- **受影响功能**：校准流程中"发送成功后跳转"的页面导航
- **风险**：极低，改用已有的 `select_tab(wxPanel*)` 方法，该方法内部通过 `FindPage` 查找实际索引，已在其他地方广泛使用
