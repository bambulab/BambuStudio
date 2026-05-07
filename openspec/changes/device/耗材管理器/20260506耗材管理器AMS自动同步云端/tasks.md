# Tasks — 耗材管理器 AMS 自动同步云端

> 改动按"节流类 → sync 行为收口 → 同步链路 → 前端 UI → 文档归档"五段推进，可拆分独立提交。
>
> 决策已敲定（见 design § 0）：
> - tag_uid 当 throttle key、独立按钮、仅 push 时刷新 UI
> - **AMS 同步只 update，不 auto-add，不 auto-delete**（Q5）
> - **PUT body 余量字段是 `netWeight`（克），sync 用 `total_net_weight × percent / 100` 换算**（Q6）
> - **缺 `total_net_weight` 的 spool 整条冻结**（连本地 percent 都不刷）（Q7）
> - **identity 字段（tag_uid / color_code / setting_id）AMS sync 完全不动**（Q5/Q6/Q7 推论）
> - **节流双轨**：忙（打印 / 校准 / 进退料）时 10 min cooldown；闲时直推（Q8）
> - 云端 push 链路**仅 PUT**，POST/DELETE 走用户主动 CRUD

## 1. 节流类 AmsAutoPushThrottle

- [ ] 1.1 新增 `src/slic3r/GUI/fila_manager/AmsAutoPushThrottle.{h,cpp}`
- [ ] 1.2 类内 enum：`Decision { Push, SkipNoRfid, SkipCooldown, SkipNoDiff }` + `DeviceState { Busy, Idle }`
- [ ] 1.3 实现 `evaluate(tag_uid, current_net_weight, device_state, now)` 决策逻辑（参见 design § 2.2 表，按以下短路顺序）：
  1. tag_uid 为空 → SkipNoRfid
  2. last_net_weight == current_net_weight → SkipNoDiff
  3. 不在 entries 中（首次）→ Push
  4. device_state == Busy && now - last_pushed_at < 10 min → SkipCooldown
  5. 其他（含 device_state == Idle）→ Push
  - **注**：参数是 net_weight（克）而不是 percent，与 PUT body 字段对齐（Q6）；DeviceState 决定是否走 cooldown（Q8）
- [ ] 1.4 实现 `record_success(tag_uid, pushed_net_weight, now)`
- [ ] 1.5 实现 `clear_for_tag(tag_uid)`、`clear_all()`
- [ ] 1.6 加单元测试 `tests/fila_manager/AmsAutoPushThrottleTest.cpp`，覆盖：
  - 空 tag_uid + 任意 state → SkipNoRfid
  - 首次（任意 state）→ Push
  - 同克数（任意 state）→ SkipNoDiff（先于 cooldown 短路）
  - **Busy + 冷却内 5 min + 异克数** → SkipCooldown
  - **Busy + 冷却外 12 min + 异克数** → Push
  - **Idle + 冷却内 5 min + 异克数** → Push（忽略 cooldown，Q8 关键）
  - **Idle + 冷却外 12 min + 异克数** → Push
  - **Idle + 冷却内 5 min + 同克数** → SkipNoDiff（差分仍生效）
  - clear_for_tag 后下次 → Push
  - clear_all 后所有 tag 下次都 → Push
- [ ] 1.7 单元测试接入既有测试 cmake target

## 2. Sync 行为收口（Q5/Q6/Q7 核心改动）

- [ ] 2.1 `wgtFilaManagerSync::sync_all_trays` **去掉 add_spool 分支**（Q5）：
  - 既有 line 46 `m_store->add_spool(create_spool_from_tray(...))` 和对应的 vt_slot 分支（line 65）整段删除
  - 改为：`match_tray` 未命中 → `BOOST_LOG_TRIVIAL(trace)` 记一行 `[ams-sync] unmatched tray, skip auto-add` + setting_id + tag_uid，然后 continue
- [ ] 2.2 sync 内增加 **capability filter**（Q7）：match 命中后判 `matched->total_net_weight <= 0` → `BOOST_LOG_TRIVIAL(trace)` 记一行 `[ams-sync] frozen spool, no total_net_weight` + spool_id，然后 continue（不动 store / 不入 changed）
- [ ] 2.3 sync 内做 **percent → 克数换算**（Q6）：
  ```cpp
  updated.remain_percent = tray.remain;
  updated.net_weight = std::round(matched->total_net_weight * tray.remain / 100.0f);
  updated.status = (tray.remain == 0) ? "empty"
                 : (tray.remain < 20) ? "low" : "active";
  updated.bound_dev_id = obj->get_dev_id();
  updated.bound_ams_id = ams_id;
  // identity 字段（tag_uid / color_code / setting_id）保持 *matched 原值，不动
  ```
- [ ] 2.4 用 `update_spool_if_changed`（见 § 2.5）替换原 `update_spool` 调用，拿"本次是否真发生变化"
- [ ] 2.5 `wgtFilaManagerStore` 新增 public 方法：
  ```cpp
  // 仅当同步关心字段（net_weight / remain_percent / status /
  //   bound_dev_id / bound_ams_id）发生变化时才写入
  // 返回 true 表示发生了变化、应入 changed 列表
  // 若 spool_id 在 store 中不存在（极端竞态）→ log warn + 返回 false（不退化为 add）
  // identity 字段（tag_uid / color_code / setting_id / spool_id）输入即使带值
  // 也会被 store 既有值覆盖回去（防御 sync 路径污染 identity）
  bool update_spool_if_changed(const FilamentSpool& sp);
  ```
- [ ] 2.6 `set_dirty()` 仅在 changed 列表非空时调（避免空 sync 触发持久化）
- [ ] 2.7 不动既有 `add_spool` / `update_spool` 签名 —— 手动 CRUD 路径（"添加耗材"对话框等）继续用
- [ ] 2.8 单元测试 `tests/fila_manager/fila_manager_tests_main.cpp`（与 throttle 共用同一 target）：
  - **Store::update_spool_if_changed**（无 GUI 依赖）：
    - 输入 spool_id 不存在 → 返回 false 且不退化为 add（Q5 硬约束）
    - sync 关心字段全相等 → 返回 false（差分判定）
    - 每个 sync 关心字段（net_weight / remain_percent / status / bound_dev_id / bound_ams_id）独立变化都能触发 update
    - **identity 字段防御覆盖**：输入 sp 同时改 identity（tag_uid / color_code / setting_id / entry_method / cloud_synced）+ 元字段（brand / series / note / initial_weight）+ net_weight，仅 net_weight 被写入，其他全部保持 store 原值
  - **FilamentSpool::effective_total_net_weight**：新规约 / legacy 减法 / 空 / 损坏数据四种场景
  - sync_all_trays 整体行为（capability filter / percent→克数 / auto-add 移除）走集成测试 § 7.11/7.14/7.18，**不在单测覆盖**——mock MachineObject / DevAmsTray 复杂度过高，性价比低

## 3. 云端同步链路（仅 PUT，body 仅 netWeight + filamentName 兜底）

- [ ] 3.1 `wgtFilaManagerCloudSync` 持有 `AmsAutoPushThrottle` 私有成员
- [ ] 3.2 `wgtFilaManagerCloudSync` 新增 public 接口：
  ```cpp
  struct AmsChangedSpool {
      std::string spool_id;
      std::string tag_uid;
      int64_t     net_weight;       // 已由 sync 算好的克数
  };
  void notify_ams_synced(const std::vector<AmsChangedSpool>& changed,
                         AmsAutoPushThrottle::DeviceState device_state);
  ```
  注意：**不**带 ChangeKind 字段（Q5，全是 Updated）；用 `net_weight` 而非 `remain_percent`（Q6）；`device_state` 由 sync 一次性算好整批共用（Q8）
- [ ] 3.3 实现 `notify_ams_synced`：
  - 遍历 changed → `throttle.evaluate(tag_uid, net_weight, device_state, now)`
  - 全部走既有 `push_update_to_cloud(spool_id, patch)` → PUT /filaments/{id}
  - patch 字段：**仅 `net_weight`** —— `spool_to_cloud_update_patch` 既有 `take_int("net_weight", "netWeight")` 直接处理；其他字段（remain_percent / status / bound_*）云端 PUT 不接受，patch 不要塞
  - `spool_to_cloud_update_json` 既有 filamentName 兜底逻辑（line 415-423）会把 series 拼上去，**不需要本 change 改这条逻辑**
  - 收集 pushed_count / skipped_cooldown / skipped_no_diff / skipped_no_rfid
  - **若 pushed_count > 0**：通过 `m_bridge->ReportMsg` 推送 `submod=sync, action=auto_push_summary, payload={pushed, skipped_cooldown, skipped_no_diff, skipped_no_rfid, device_state, at}`（payload 加 device_state 让前端 UI 可解释为啥被节流，便于排查）
  - 全 skipped 时不发 ReportMsg（Q4 决定）
  - **不涉及 POST 与 DELETE**：见 design § 1.1 决策矩阵
- [ ] 3.4 **乐观 record_success**：在 `notify_ams_synced` 内决策 Push 之后**立即**调 `throttle.record_success`，不等 dispatcher 回调
- [ ] 3.5 push 失败：dispatcher 既有 `set_on_push_failed` observer 已被 FilaManagerVM 转 `publish_push_failed` ReportMsg，本 change **不**新增 `auto_push_error` 路径——避免与既有失败语义重复（throttle 状态保留即可）
- [ ] 3.6 `wgtFilaManagerSync::sync_all_trays` 在本地写入完成后：
  - 收集 `update_spool_if_changed` 返回 true 的 spool 到 `changed` 列表
  - changed 非空时调 `compute_device_state(obj)` 算 device_state，再调 `cloud_sync->notify_ams_synced(changed, device_state)`
- [ ] 3.7 在 `wgtFilaManagerSync` 内（或独立工具函数）实现 `compute_device_state(MachineObject*)`，按 design § 2.3 字段表实现
- [ ] 3.8 登出事件订阅：`GUI_App::on_user_logout` 触发 `cloud_sync->throttle().clear_all()`

## 4. JSON-RPC 手动覆盖入口

- [ ] 4.1 `FilaManagerVM::HandleSpool` 路由 `submod=sync, action=push_all_now`
- [ ] 4.2 `wgtFilaManagerCloudSync` 新增 `push_all_now()`：
  - 遍历 store 所有 `tag_uid` 非空的 spool
  - 此处所有 spool 都已 in store，对应云端动作均为 PUT；跳过 throttle 直接入既有 `push_update_to_cloud` 队列
  - 成功后 record_success（保留节流状态一致性）
  - 完成后总是发 auto_push_summary（含 pushed_count 即使为 0）
- [ ] 4.3 该 action 响应 payload 包含 `enqueued_count`，前端 toast 用

## 5. 前端

- [ ] 5.1 `useFilamentBridge` 增加 `pushAllNow()` 方法
- [ ] 5.2 `AppStore.filament` 新增 `cloudAutoPushSummary: { pushed, skipped_cooldown, skipped_no_diff, skipped_no_rfid, device_state: 'busy' | 'idle', at } | null`（device_state 让 UI 解释"为啥被节流"）
- [ ] 5.3 监听 `submod=sync, action=auto_push_summary` ReportMsg → 写入 `cloudAutoPushSummary`
  - 注意：C++ 全 skipped 时不会发，因此前端 store 无需在 0/N 场景处理
- [ ] 5.4 ~~监听 `submod=sync, action=auto_push_error`~~ —— 取消，AMS auto-push 失败仍通过既有 dispatcher `push_failed` observer 链路转发；本 change 不新增独立失败 ReportMsg 路径，避免失败语义重复
- [ ] 5.5 StatsView 顶部新增独立按钮"推送本地到云端"（与"同步"按钮分离）：
  - 位置：状态徽章和"同步"按钮右侧，间距 8 px
  - 图标：云上传 / 上箭头
  - 单击 → `pushAllNow()`
  - 加载态：spinner + 禁用
- [ ] 5.6 同步徽章 tooltip 显示 `cloudAutoPushSummary` 摘要
- [ ] 5.7 手动 push_all_now 失败 toast / 成功 toast
- [ ] 5.8 i18n 文案：`locales/*.json` 增加：
  - `Push Local to Cloud` / `推送本地到云端`
  - `Last AMS Auto-Sync` / `最近一次 AMS 自动同步`
  - `Pushed {{n}}, Skipped {{n}}` / `推送 {{n}} 卷，跳过 {{n}} 卷`
  - `Pushed {{n}} spools to cloud` / `已推送 {{n}} 卷到云端`
  - `Push to cloud failed: {{reason}}` / `推送到云端失败：{{reason}}`
- [ ] 5.9 `tsc + vite build`，重建 `resources/web/device_page/dist/`

## 6. 文档归档

- [ ] 6.1 在本 change 目录补 `specs/fila-cloud-ams-autosync/spec.md`（按既有 change 的 spec 模板格式）
- [ ] 6.2 更新 `openspec/specs/device/功能架构文档/耗材管理器整体架构.md` § 4.3 AMS 同步段，加上"完成后联动云端 push（双轨节流：忙时 10 min cooldown + 差分；闲时直推）"，并在"Sync 写权限"段说明 Q5/Q6/Q7 行为收口
- [ ] 6.3 更新 `openspec/specs/device/来源映射表.md`：纳入 `wgtFilaManagerCloudSync.notify_ams_synced` / `AmsAutoPushThrottle`
- [ ] 6.4 更新 `openspec/specs/device/主题索引.md`：新增"AMS 自动同步云端"主题词条
- [ ] 6.5 更新 `openspec/specs/device/耗材管理器/README.md`（如有）

## 7. 验证项

- [ ] 7.1 单机：连一台 N10/N11 → AMS 推 → 本地 store 更新（net_weight 与 remain_percent 同步刷）→ 云端 spool netWeight 随之刷新
- [ ] 7.2 切设备：A 设备 → B 设备，B 设备的 AMS 数据触发 push，A 设备的 spool 不受影响
- [ ] 7.3 节流压测：模拟 30 秒内 10 次 MQTT push（同 4 槽，余量不变） → 实际只发 0 次 PUT（SkipNoDiff）
- [ ] 7.4 差分跳过：净重未变（任何设备状态）→ throttle 跳过，无 ReportMsg
- [ ] 7.5 **Q8 验证：闲态直推**
  - 准备：设备在 IDLE（非打印 / 非校准 / ams_status_main=IDLE）；spool S 已记录 last_push（5 min 前）
  - AMS 推 percent 从 80 → 60，net_weight 跨阈值
  - 期望：device_state=Idle，throttle.evaluate 不走 cooldown → 一次 PUT
  - tooltip / log 显示 device_state=Idle
- [ ] 7.6 **Q8 验证：打印中节流**
  - 准备：设备 is_in_printing()=true；spool S 已记录 last_push（5 min 前）
  - AMS 推 percent 从 80 → 60
  - 期望：device_state=Busy，5 min 内 → SkipCooldown 不发；12 min 后再推 → 发
- [ ] 7.7 **Q8 验证：校准中节流**
  - 准备：设备 is_in_calibration()=true 或 is_in_extrusion_cali()=true
  - 期望：device_state=Busy，按 7.6 cooldown 行为
- [ ] 7.8 **Q8 验证：进退料 / 切料中节流**
  - 准备：设备 ams_status_main=AMS_STATUS_MAIN_FILAMENT_CHANGE
  - 期望：device_state=Busy，按 7.6 cooldown 行为
- [ ] 7.9 **Q8 验证：状态切换不主动 trigger**
  - 准备：spool 余量变了一次后被 SkipCooldown
  - 设备从 Busy 切到 Idle（打印结束）
  - 期望：throttle 不主动发 push；下次 AMS sync 推到该 spool 时再按新状态 Push（避免 watchdog 副作用）
- [ ] 7.10 手动按钮：点击 → 当前所有有 RFID + 有 total_net_weight 的 spool 不分 throttle 强制 PUT，toast 显示推送数
- [ ] 7.11 登出 → 重登：throttle 清账，新一次 AMS 同步全员触发 push
- [ ] 7.12 离线 / 网络中断：自动 push 失败仅日志，不弹 toast，AMS 本地同步流程不阻塞
- [ ] 7.13 无 RFID 的手动料卷：AMS 路径不会触碰它（验证 store 写入流不会标记为 Updated 触发 sync）
- [ ] 7.14 PUT body 校验：通过抓包确认 update body **仅含 `id` / `netWeight` / `filamentName`** 三字段（不含 RFID / status / remainPercent / bound_*；任何额外字段都会让云端 400 / circuit breaker -29）
- [ ] 7.15 **Q5 验证：AMS 见到陌生料卷不入库**
  - 准备：本地 store 清空所有 spool
  - AMS 上插一卷新料 → MQTT push 到 sync
  - 期望：耗材管理器列表 **不出现新条目**，仅 trace 日志记录 unmatched
  - 用户走"添加耗材-从 AMS 读取"对话框 → 列表才出现 + 云端 POST
- [ ] 7.16 **Q5 验证：AMS 拔卡不删库**
  - 准备：本地 store 有一卷 RFID=R1 的 spool
  - AMS 拔掉 R1 那卷 → MQTT push（tray.is_exists=false / 槽空）
  - 期望：耗材管理器列表中 R1 那条**仍在**，bound_dev_id / bound_ams_id 不动 / 无云端 DELETE
- [ ] 7.17 **Q6 验证：percent → net_weight 换算正确**
  - 准备：spool S 的 total_net_weight=1000g
  - AMS 推 percent=75 → 期望 store 内 S.net_weight=750
  - PUT body 中 netWeight=750
- [ ] 7.18 **Q7 验证：缺 total_net_weight 整条冻结**
  - 准备：手动构造一条 total_net_weight=0 的 spool S（模拟历史遗留数据）
  - AMS 推 S 对应 tray，percent 从 50 → 30 → 80
  - 期望：S.remain_percent **始终保持 0/初始值**（**不**跟 AMS 走）
  - S.net_weight 始终为 0
  - 云端无任何 PUT 请求
  - trace log 看到 "frozen spool, no total_net_weight"
  - 后续：用户在 UI 编辑 S，保存 total_net_weight=1500 + 当前余量 600 → 既有 CRUD PUT 推云端 → 下次 AMS sync S 自动恢复参与
- [ ] 7.19 **identity 字段防御验证**
  - 准备：人为修改 sync 代码，在 updated 里塞 `tag_uid="GARBAGE"` / `color_code="#FF0000"` / `setting_id="abc"`
  - 期望：`update_spool_if_changed` 后，store 内 spool 的 identity 字段**保持原值不变**
- [ ] 7.20 单测全过：`AmsAutoPushThrottleTest` + `SyncBehaviorTest`
- [ ] 7.21 前端 `tsc + vite build` pass
- [ ] 7.22 C++ 本地 Release build pass（device-local-build）

## 8. 提交计划

- [ ] 8.1 提交 1：节流类 + 单元测试（`AmsAutoPushThrottle`）
- [ ] 8.2 提交 2：**Sync 行为收口**（`wgtFilaManagerSync` 移除 auto-add + `Store::update_spool_if_changed`）+ 对应单测 —— 这一步独立可验证："只更新不新增"语义可以单独提交灰度
- [ ] 8.3 提交 3：云端同步链路 + JSON-RPC 入口（`notify_ams_synced` + `push_all_now`）
- [ ] 8.4 提交 4：前端 UI（独立按钮 + 摘要 tooltip + dist 重建）
- [ ] 8.5 提交 5：文档归档（specs / 整体架构 / 索引更新）
- [ ] 8.6 推送 Gerrit（device-gerrit-push）+ 回写 STUDIO-18155 评论（device-jira-comment）
