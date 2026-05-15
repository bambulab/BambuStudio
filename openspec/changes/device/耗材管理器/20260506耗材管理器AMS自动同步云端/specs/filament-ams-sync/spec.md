## MODIFIED Requirements

### Requirement: 料槽匹配策略

`match_tray` SHALL 优先使用 `tag_uid` 精确匹配；否则在可唯一确定时使用 `setting_id` + `color` 匹配；**未匹配时 SHALL 静默跳过，不再自动创建新 spool**。新增料卷只走 UI "添加耗材-从 AMS 读取" 入口。

#### Scenario: RFID 命中

- **WHEN** 料槽携带的 `tag_uid` 与某 spool 一致
- **THEN** SHALL 用 `Store::update_spool_if_changed` 更新该 spool 的余量与绑定信息（仅写 sync 关心字段；identity 字段防御覆盖）

#### Scenario: 未匹配静默跳过

- **WHEN** 既无 `tag_uid` 命中，也无 `setting_id+color` 唯一命中
- **THEN** SHALL 仅 `BOOST_LOG_TRIVIAL(trace)` 记一行 `[ams-sync] unmatched tray, skip auto-add` 并跳过该 tray
- **AND** SHALL **不**调用任何 `add_spool` / `create_spool_from_tray`，本地 store 不变
- **AND** 用户走"添加耗材-从 AMS 读取"对话框时，该 tray 才会进入 store 并触发云端 POST

#### Scenario: 缺整卷净重整条冻结

- **WHEN** match_tray 命中 spool S 但 `S.effective_total_net_weight() <= 0`
- **THEN** SHALL 仅 `trace` 记 `[ams-sync] frozen spool, no total_net_weight`，并**完全不动** S 在 store 内的任何字段（包括 `remain_percent`）
- **AND** SHALL **不**触发云端 PUT
- **AND** 用户在 UI 编辑 S 补齐 `total_net_weight` 后，下次 AMS sync 该 spool 自动恢复参与同步

### Requirement: 设备更新触发同步

系统 SHALL 在设备 MQTT/状态更新路径中调用 Sync（如 `on_device_update` / `sync_all_trays`），使 AMS 余量与耗材库一致；具体挂钩点以实现为准（如 `GUI_App` 消息回调）。

#### Scenario: 同步全部料槽

- **WHEN** 对某 `MachineObject` 执行 `sync_all_trays`
- **THEN** SHALL 遍历相关 tray 与 vt_slot，对每条命中的 spool 执行
   "percent → 克数换算 → `update_spool_if_changed`"
- **AND** SHALL 收集所有 `update_spool_if_changed` 返回 true 的 spool 到 changed 列表
- **AND** SHALL 仅当 changed 非空时调一次 `m_store->set_dirty()`

#### Scenario: percent → net_weight 换算

- **WHEN** 命中 spool S 且 `S.effective_total_net_weight() > 0`
- **THEN** SHALL 计算 `net_weight = round(total_net_weight × tray.remain ÷ 100.0)` 写入 `S.net_weight`（克）
- **AND** SHALL 同时刷新 `remain_percent / status / bound_dev_id / bound_ams_id`
- **AND** SHALL **不**改写 identity 字段（spool_id / tag_uid / color_code / setting_id / entry_method / created_at / cloud_synced）；即便 sync 误塞 identity，`Store::update_spool_if_changed` 也会防御覆盖

#### Scenario: 联动云端 push

- **WHEN** sync 完成且 changed 列表非空
- **THEN** SHALL 一次性调 `compute_device_state(obj)` 拿 device_state，再调 `cloud_sync->notify_ams_synced(changed, device_state)`
- **AND** 节流 / PUT body / observer 行为按 `fila-cloud-ams-autosync` capability 规定

## REMOVED Requirements

### Requirement: 自动录入

**Reason**: STUDIO-18155 决策 Q5 把 AMS 同步收口为"只更新已有 spool"。原"未匹配时自动创建新 spool"的行为会让 AMS 现场快照（错插的卡 / 错认的 RFID）污染长期库存账本，与"耗材库 = 用户主动管理的清单"语义冲突。新增料卷统一走 UI "添加耗材-从 AMS 读取" 入口。
**Migration**: 既有依赖该自动录入路径的代码已删除（`wgtFilaManagerSync::create_spool_from_tray` + `add_spool` 调用）。用户体验上："插一卷新料 → 自动出现在耗材管理器列表"的链路被切断；用户必须打开"添加耗材"对话框→选"从 AMS 读取"→ 在弹出的 tray 列表选择该料卷确认录入。
