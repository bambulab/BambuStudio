# 耗材管理器 AMS 自动同步云端

## Why

当前 AMS 同步路径只写本地：

```
设备 MQTT push -> GUI_App::on_device_update
                  -> wgtFilaManagerSync::on_device_update(obj)
                     -> sync_all_trays(obj)
                        -> wgtFilaManagerStore::add/update FilamentSpool
                           ⨯  ← 链路在这里断掉，不触发云端 push
```

云端 push 路径已存在（`wgtFilaManagerCloudSync::push_update_to_cloud` / `push_spool_to_cloud`），但只在用户手动 add / edit / delete 时由 `FilaManagerVM::HandleSpool` 联动触发。AMS 同步带来的库存变化（料卷余量、首次插入、料卷换槽、机型切换）**永远不会被推到云端**，跨端切换 / 跨设备时云端 spool 余量永远停留在上次手动编辑那一刻。

STUDIO-18155 诉求：

1. AMS 信息更新到本地后（设备页切换设备、首次连接、耗材管理器中切换设备），自动 push 到云端。
2. 推送频率不能高（IoT 端 N10 / N11 单次打印任务结束后才推送，本端不应反向轰炸云端），需要心跳节流 + 差分推送。
3. "同步"按钮支持手动覆盖触发一次。
4. 物理料卷需要稳定标识用于去重（评论补充）。

## What Changes

### 物理料卷标识
- **复用既有 `tag_uid`（云端 schema = `RFID`）作为物理料卷稳定标识**，不新增 `spool_uuid` 字段。
- 节流仅作用于"AMS 同步路径"产生的 spool（按定义 `tag_uid` 非空，因为 AMS RFID 是这条路径的输入）；手动添加无 RFID 的 spool 不参与本 change 的自动 push 流（它走用户 CRUD 路径，由 `FilaManagerVM::HandleSpool` 既有逻辑覆盖）。
- 云端 PUT body 维持现有 schema（禁止包含 RFID 字段，与 `wgtFilaManagerCloudSync.cpp:348-350` 注释一致）。

### AMS sync 行为收口（**关键边界**）
- 当前 `wgtFilaManagerSync::sync_all_trays` 在 match 不到时会自动 `add_spool` 入库，会让 store 被 AMS 现场快照污染（也是 STUDIO-18117 类风险面之一）。
- **本 change 把 sync 行为收口为"只更新 / 不新增 / 不删除 / 不动 identity"**：
  - match 命中 + spool 有 `total_net_weight > 0` → 走 update（**仅改 remain_percent / net_weight / status / bound_***）
  - match 命中 + spool `total_net_weight = 0` → **整条冻结**，连本地 percent 都不刷
  - match 未命中 → trace log + 跳过，**不再 auto-add**
  - AMS 拔卡 / 槽空 → **不删 store**（维持现状）
  - identity 字段（`tag_uid` / `color_code` / `setting_id`）→ **完全冻结**，由 store 防御覆盖
- 新增料卷的入库路径**仅保留** UI 主动入口（"添加耗材-从 AMS 读取" / 手动添加），由既有 `FilaManagerVM::HandleSpool` 联动云端 POST 路径承担，与本 change 解耦。
- 因此本 change 的**云端 push 链路只剩 PUT**，不引入 POST / DELETE。

### 余量字段映射（与云端 swagger 对齐）

云端 `UpdateFilamentV2Req` swagger 白名单（见 `wgtFilaManagerCloudSync.cpp:338-355`）**不存在 `remainPercent`**；唯一的"余量"字段是 `netWeight`（绝对克数）。

```cpp
// sync 内做单位换算
updated.net_weight = round(matched->total_net_weight * tray.remain / 100.0f);
```

- 反过来约束 spool 必须有 `total_net_weight`（STUDIO-18115 后已是必填字段，对话框采集）
- 没有的 spool（历史遗留）落入"冻结"路径，等用户主动编辑补齐
- PUT body 实际只发 `{ id, netWeight, filamentName: <既有兜底> }`，AMS sync 路径**不**触碰任何 identity / status / bound_* 字段（云端也不接受）

### 同步触发链
- 在 `wgtFilaManagerSync::sync_all_trays` 完成本地写入后，对**实际发生变化**的 spool 集合（`update_spool_if_changed=true`）调用 `wgtFilaManagerCloudSync::notify_ams_synced(...)`。
- 新增节流类 `AmsAutoPushThrottle`：以 `tag_uid` 为 key，记录 `last_pushed_at` 与 `last_pushed_net_weight`（克），按"双轨制"决策：
  - **第一道（前置必要）**：sync 阶段 `update_spool_if_changed` 已经保证只有"store 真发生变化"的 spool 进入 throttle —— 这条是 STUDIO-18155 描述里"耗材数据变化才更新"的代码体现
  - **第二道（自适应）**：`evaluate(tag_uid, net_weight, device_state)` 按设备状态分轨决策
    - net_weight 与上次推送一致 → 跳过（no_diff）
    - **设备忙**（`is_in_printing` / `is_in_calibration` / `is_in_extrusion_cali` / `ams_status_main != IDLE` 任一）→ 走 **10 min cooldown**，避免占用打印 / 校准 / 进退料期间的网络带宽
    - **设备闲** → 直推（不走 cooldown），让用户改料 / 拔卡换槽时云端立即同步
- `device_state` 由 sync 一次性算好整批共用，避免一次 sync 出现"半忙半闲"语义不一致。
- 复用既有 push 串行化队列（`20260417耗材管理器云端接入Web前端` 已建），不另起线程。

### 前端
- StatsView 顶部状态徽章右侧**新增独立按钮 "推送本地到云端"**，与既有"同步"按钮分离（避免与"从云端拉取"误操作）。
  - 点击 → `pushAllNow()` → `filament.sync.push_all_now` JSON-RPC，C++ 侧绕过 throttle 强制 push 一次
- 前端 store 暴露 `cloudAutoPushSummary`：最近一次自动 push 摘要（pushed/skipped/at）。
- 状态条 tooltip **仅在实际产生 push 时刷新**摘要；全 skipped 不更新（避免 IoT 高频 push 噪声）。

### 失败兜底
- AMS 自动 push 失败：仅写日志 + ReportMsg，不阻塞 AMS 同步主流程，不弹窗。
- 手动按钮触发的失败：显式 toast。

## Capabilities

### New Capabilities
- `fila-cloud-ams-autosync`：AMS 同步路径完成本地写入后，按 `tag_uid` 节流 + 差分判定决定是否触发云端 push；提供独立"推送本地到云端"手动覆盖入口。

### Modified Capabilities
- `fila-cloud-integration`：补充"AMS 同步路径触发 push（仅 PUT）"分支；既有"用户 CRUD 联动 push"分支保持不变。
- `fila-cloud-sync`（前序 change 已存在）：补充"自动 push 模式"参数（节流跳过原因 / 强制覆盖位）。
- `fila-ams-mqtt-sync`（既有能力）：写权限收口为"仅更新已有 spool"，移除 auto-add 分支；维持不删除现状。

## Impact

| 维度 | 影响 |
|---|---|
| **新增 C++ 类** | `AmsAutoPushThrottle`（轻量内存类，挂在 `wgtFilaManagerCloudSync` 私有成员） |
| **修改 C++** | `wgtFilaManagerSync.{h,cpp}`：移除 auto-add 分支 + 加 capability filter（total_net_weight）+ percent→net_weight 换算 + 调用 `update_spool_if_changed` + 通知 cloud sync；`wgtFilaManagerStore.{h,cpp}`：新增 `update_spool_if_changed`（不动既有 add/update_spool）+ identity 防御覆盖；`wgtFilaManagerCloudSync.{h,cpp}` 增加 `notify_ams_synced(...)` + `push_all_now()` + throttle；`FilaManagerVM.{cpp}` 增加 `filament.sync.push_all_now` action |
| **不动** | `wgtFilaManagerStore` 持久化 schema（无新字段、无迁移）、云端 PUT/POST schema、既有 `spool_to_cloud_*_json`（特别是 `spool_to_cloud_update_patch` swagger 白名单 + filamentName 兜底） |
| **行为变更（须显式说明）** | (1) AMS 同步对 store 的写权限收口：原本 AMS 自动 add 的料卷不再进 store，需要用户走"添加耗材-从 AMS 读取"对话框入库（Q5）。(2) 缺整卷净重的旧 spool 在 sync 期间整条冻结：连本地 percent 都不刷，需要用户编辑补齐才会重新参与（Q7）。两条都需在 release notes 中向用户说明。 |
| **修改前端** | `useFilamentBridge` 加 `pushAllNow()`；`StatsView` 新增独立"推送本地到云端"按钮；`AppStore.filament` 加 `cloudAutoPushSummary` |
| **网络压力** | 单次 AMS 同步最坏触发 4 个 spool push（A1-A4），节流后稳态下平均不到每 10 分钟一次；远小于设备 MQTT 推送频率 |
| **依赖前置** | 依赖 STUDIO-18115 引入的 `total_net_weight` 字段已在 store 中可用且对话框已采集；如该字段对老用户为空（历史遗留），sync 自动冻结对应 spool，不会 break 现有功能 |
| **登出态** | 未登录时 throttle 不参与；登录事件触发后才进入自动 push 路径 |
| **风险** | 多端并发：用户 A 在手机端编辑 spool，用户 A 又从机器 AMS 同步，两端覆盖顺序由"最后写入者赢"决定（与现有用户 CRUD 路径一致，本 change 不引入新冲突域） |
| **兼容** | 不改 schema、不动持久化结构；旧版本 Studio 不受影响 |
