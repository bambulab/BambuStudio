## 1. 修复校准页面跳转

- [x] 1.1 将 `src/slic3r/GUI/Plater.cpp:19180` 的 `p->main_frame->request_select_tab(MainFrame::TabPosition::tpCalibration)` 改为 `p->main_frame->select_tab(p->main_frame->m_calibration)`

## 2. 验证

- [x] 2.1 确认 `select_tab(wxPanel*)` 方法在 `send_calibration_job_finished` 的调用上下文中可正常工作（主线程、m_calibration 非空）
- [x] 2.2 排查项目中其他 `request_select_tab` 调用点，确认是否存在同类索引偏移问题（记录结果，本次不修改）
