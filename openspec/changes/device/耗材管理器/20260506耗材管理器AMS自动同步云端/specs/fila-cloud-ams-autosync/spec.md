## ADDED Requirements

### Requirement: AMS 自动同步触发云端 PUT

系统 SHALL 在 `wgtFilaManagerSync::sync_all_trays` 完成本地 spool 写入后，自动通过 `wgtFilaManagerCloudSync::notify_ams_synced` 把变化推送到云端，不依赖用户手动触发。

#### Scenario: AMS 推送一卷有变化的料卷

- **WHEN** 设备 MQTT push 触发 `sync_all_trays`，其中某 tray 命中既有 spool S 且 `Store::update_spool_if_changed(S)` 返回 true
- **THEN** SHALL 把 `{spool_id, tag_uid, net_weight}` 收入 changed 列表
- **AND** sync 完成后 SHALL 调一次 `cloud_sync->notify_ams_synced(changed, device_state)`，其中 `device_state` 由 `compute_device_state(obj)` 计算得到

#### Scenario: 全部 tray 都没变化

- **WHEN** AMS push 触发 sync 但所有 tray 命中的 spool 都没产生 sync 关心字段的变化
- **THEN** SHALL **不**调用 `notify_ams_synced`，云端无任何 PUT 请求

#### Scenario: cloud_sync 未初始化

- **WHEN** sync 完成有 changed 列表但 `wxGetApp().fila_manager_cloud_sync()` 返回 nullptr（启动早期 / 析构期）
- **THEN** SHALL 静默跳过云端联动，本地 store 状态不受影响

### Requirement: 设备状态决定节流行为

系统 SHALL 用 `compute_device_state(MachineObject*)` 把设备状态归并为 `Busy` 或 `Idle`，作为 `AmsAutoPushThrottle::evaluate` 的入参。

#### Scenario: 打印 / 校准 / 挤出校准期间

- **WHEN** `obj->is_in_printing()` / `obj->is_in_calibration()` / `obj->is_in_extrusion_cali()` 中任一返回 true
- **THEN** SHALL 返回 `DeviceState::Busy`

#### Scenario: AMS 状态非空闲

- **WHEN** 设备未在打印 / 校准，但 `obj->ams_status_main` 既不是 `AMS_STATUS_MAIN_IDLE` 也不是 `AMS_STATUS_MAIN_UNKNOWN`（即处于进退料 / 切料 / RFID 识别 / 自检 / 冷拉断料 / 调试）
- **THEN** SHALL 返回 `DeviceState::Busy`

#### Scenario: 设备完全空闲

- **WHEN** 上述忙判据全否
- **THEN** SHALL 返回 `DeviceState::Idle`

#### Scenario: device_state 整批共用

- **WHEN** 一次 `sync_all_trays` 处理 N 个 tray
- **THEN** SHALL 仅在末尾一次调用 `compute_device_state(obj)`，整批共用同一个 device_state；中途状态切换不影响本批 throttle 决策

### Requirement: 双轨节流类 AmsAutoPushThrottle

系统 SHALL 提供 `Slic3r::GUI::AmsAutoPushThrottle` 类对 AMS 路径的云端 PUT 做差分 + 设备状态自适应节流；非 AMS 路径（用户主动 CRUD / push_all_now）SHALL 不经过该节流。

#### Scenario: 决策接口

- **WHEN** 调用方需要决策一次 push 是否要发
- **THEN** throttle SHALL 提供 `evaluate(tag_uid, current_net_weight, device_state, now)` 返回 `Decision { Push, SkipNoRfid, SkipCooldown, SkipNoDiff }`

#### Scenario: 决策优先级

- **WHEN** `evaluate` 收到入参
- **THEN** SHALL 按以下短路顺序判定（与实现一致）：
  1. `tag_uid.empty()` → `SkipNoRfid`
  2. tag 不在 entries（**首次**）→ `Push`（先于 SkipNoDiff，避免默认值与合法 0 克 spool 巧合短路）
  3. `last_pushed_net_weight == current_net_weight` → `SkipNoDiff`
  4. `device_state == Busy && now - last_pushed_at < kMinIntervalBusy(10 min)` → `SkipCooldown`
  5. 其他（含 `device_state == Idle` 任意时段） → `Push`

#### Scenario: 闲态绕过 cooldown

- **WHEN** 上次推送 5 min 前、`device_state == Idle`、当前克数与上次不同
- **THEN** SHALL 返回 `Push`，不受 10 min cooldown 约束

#### Scenario: 忙态走 cooldown

- **WHEN** 上次推送 5 min 前、`device_state == Busy`、当前克数与上次不同
- **THEN** SHALL 返回 `SkipCooldown`；12 min 后再调 SHALL 返回 `Push`

#### Scenario: 闲态仍尊重差分

- **WHEN** 上次推送 1 min 前、`device_state == Idle`、当前克数等于上次
- **THEN** SHALL 返回 `SkipNoDiff`，不会因闲态强行 Push

#### Scenario: 决策记账

- **WHEN** 调用方决定执行 Push
- **THEN** SHALL 调 `record_success(tag_uid, pushed_net_weight, now)` 把本次推送的克数与时刻写入 entries，作为下一次 evaluate 的基准

#### Scenario: 单 tag 重置 / 全部重置

- **WHEN** 调 `clear_for_tag(tag_uid)`
- **THEN** SHALL 移除该 tag 的 entry，下次 evaluate 必返回 `Push`

- **WHEN** 调 `clear_all()`
- **THEN** SHALL 清空所有 entries

#### Scenario: 线程安全

- **WHEN** UI 线程与 dispatcher 回调线程并发调 evaluate / record_success / clear_*
- **THEN** SHALL 用 `std::mutex` 保护 entries map，所有公开方法互斥

### Requirement: AMS auto-push 摘要观察者

系统 SHALL 在 `wgtFilaManagerCloudSync` 暴露 `set_on_auto_push_summary` 观察者接口，由 `FilaManagerVM` 在生命周期内注册，用于把摘要转发到 Web 前端。

#### Scenario: 摘要发送时机（auto trigger）

- **WHEN** `notify_ams_synced` 完成所有 tray 的决策且 `pushed > 0`
- **THEN** SHALL 调用 `m_on_auto_push_summary({trigger:"auto", device_state, pushed, skipped_cooldown, skipped_no_diff, skipped_no_rfid})`

#### Scenario: 摘要发送时机（auto + 全 skipped）

- **WHEN** `notify_ams_synced` 完成所有 tray 的决策但 `pushed == 0`
- **THEN** SHALL **不**触发观察者，避免 UI 噪声

#### Scenario: 摘要发送时机（manual trigger）

- **WHEN** `push_all_now` 完成入队（无论 enqueued 是否为 0）
- **THEN** SHALL 调用 `m_on_auto_push_summary({trigger:"manual", device_state:"manual", pushed, skipped_no_rfid, skipped_no_total_nw, ...})`

#### Scenario: VM 转发到 Web

- **WHEN** `FilaManagerVM` 的 observer 收到摘要
- **THEN** SHALL 通过 `m_bridge->ReportMsg(MakeResp("sync", "auto_push_summary", 0, "", payload))` 转发到前端

### Requirement: 手动 push_all_now 入口

系统 SHALL 提供 `wgtFilaManagerCloudSync::push_all_now()` 与 `FilaManagerVM::HandleSync("push_all_now")` 配对，作为绕过 throttle 的"推送本地到云端"入口。

#### Scenario: JSON-RPC 路由

- **WHEN** Web 端发送 `{module:"filament", submod:"sync", action:"push_all_now"}`
- **THEN** `FilaManagerVM` SHALL 路由到 `cloud_sync->push_all_now()` 并立即返回 `build_sync_state()`

#### Scenario: 候选 spool 过滤

- **WHEN** `push_all_now` 遍历 store 所有 spool
- **THEN** SHALL 仅对满足 `tag_uid.empty()==false` **且** `effective_total_net_weight() > 0` 的 spool 入队 PUT；其余 spool 计入 `skipped_no_rfid` / `skipped_no_total_nw`

#### Scenario: 绕过 throttle.evaluate

- **WHEN** 候选 spool 通过过滤
- **THEN** SHALL 直接 `disp->enqueue_push_update(spool_id, {net_weight: ...})`，**不**调用 `throttle.evaluate`

#### Scenario: 仍 record_success 保持状态一致

- **WHEN** 入队成功
- **THEN** SHALL 调 `throttle.record_success(tag_uid, net_weight, now)`，避免下一次 AMS sync 又立即重新 push 同一克数

#### Scenario: 未登录 / cloud_sync 不可用

- **WHEN** `wxGetApp().fila_manager_cloud_sync()` 返回 nullptr
- **THEN** `HandleSync` SHALL 返回 `error_code = -1, message = "cloud disabled"`，前端 toast 提示失败

### Requirement: PUT body 字段最小化

系统 SHALL 在 AMS 自动 push 与 push_all_now 路径上仅向云端发送 `net_weight` 余量字段，不发送 `remain_percent` / `status` / `bound_*` / `tag_uid` / `color_code` 等其他字段。

#### Scenario: notify_ams_synced PUT body

- **WHEN** `notify_ams_synced` 决策 Push
- **THEN** SHALL 调 `dispatcher->enqueue_push_update(spool_id, {"net_weight": <克数>})`；patch 仅含 net_weight，由 `spool_to_cloud_update_patch` 与 `spool_to_cloud_update_json` 兜底成 UpdateFilamentV2Req body

#### Scenario: push_all_now PUT body

- **WHEN** `push_all_now` 入队
- **THEN** SHALL 同样仅 patch `{"net_weight": <克数>}`

### Requirement: 登出清节流账本

系统 SHALL 在 `GUI_App::request_user_logout` 处理流程内清空 `m_fila_manager_cloud_sync->throttle()`，避免跨账号污染。

#### Scenario: 登出触发清账

- **WHEN** 登出流程进入 dispatcher 清队那一段（`m_fila_manager_cloud_disp->clear_pending()` 之后）
- **THEN** SHALL 调 `m_fila_manager_cloud_sync->throttle().clear_all()`

#### Scenario: 重新登录后首次 sync

- **WHEN** 账号 B 登录后 AMS 推第一次 push
- **THEN** 由于 throttle 已清空，所有 tag 都视为"首次" → 直接 Push（不受账号 A 残留 cooldown 影响）

### Requirement: AMS auto-push 摘要前端转发

系统 SHALL 在 Web 端 `useFilamentBridge` 监听 `submod=sync, action=auto_push_summary` ReportMsg，写入 `AppStore.filament.cloudAutoPushSummary`，并按 trigger 类型决定是否弹 toast。

#### Scenario: auto trigger 不弹 toast

- **WHEN** `auto_push_summary` 的 `trigger == "auto"`
- **THEN** SHALL 仅写入 store，不弹 toast；CloudBadge tooltip 通过 store 取摘要展示

#### Scenario: manual trigger 弹 toast

- **WHEN** `trigger == "manual"`
- **THEN** SHALL 弹 toast：`pushed > 0` 时 info 级 "Pushed {{n}} spools to cloud"；`pushed == 0` 时 warn 级 "No spools to push"

#### Scenario: history 记录

- **WHEN** 收到任意 trigger 的摘要
- **THEN** SHALL 调 `appendCloudSyncHistory` 记一条 `kind:'push', op:'update', status:'ok'` 条目，detail 携带完整摘要 payload
