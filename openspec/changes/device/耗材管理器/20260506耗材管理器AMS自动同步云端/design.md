# Design — 耗材管理器 AMS 自动同步云端

## 0. 决策摘要

| 项 | 决定 | 来源 |
|---|---|---|
| 物理料卷标识 | 复用 `tag_uid`（云端 schema = `RFID`），**不**新增 `spool_uuid` | Q1 拍板 |
| 节流策略 | **双轨制**：`update_spool_if_changed=false` 是前置必要短路；之后按设备状态分轨——**设备忙（打印 / 校准 / 进退料）→ 10 min cooldown**；**设备闲 → 余量变化即推**。10 min 硬编码，不做 AppConfig 配置 | Q2 + Q8 拍板 |
| 手动按钮 | 独立按钮"推送本地到云端"，与现有"同步"按钮分离 | Q3 拍板 |
| 全 skipped 时 UI 行为 | 仅在实际 push 时刷新摘要；全 skipped 不动 UI | Q4 拍板 |
| **AMS 同步语义** | **只更新已有 spool，不自动 add，不自动 delete**；新增料卷只走 UI 主动入口（"添加耗材-从 AMS 读取"） | Q5 拍板 |
| 云端 push 动词 | **仅 PUT**（不引入 POST / DELETE）；POST 留给用户主动 CRUD（既有路径），DELETE 同 | Q5 推论 |
| **余量字段映射** | sync 用 `total_net_weight × percent / 100` 算出 `net_weight`（克），PUT body 发云端唯一接受的余量字段 `netWeight`（不存在 `remainPercent`）；本地仍同步保留 `remain_percent` 字段供 UI 用 | Q6 拍板 |
| **缺 total_net_weight 的 spool** | **整条冻结**：sync 不动它的 store 字段（包括 `remain_percent`），不入 push 列表，仅 trace log；用户必须主动到耗材管理器补齐整卷重才会重新参与同步 | Q7 拍板 |
| AMS sync 改的字段集合 | **仅 `remain_percent` / `net_weight` / `status` 三项**；identity 字段（`tag_uid` / `color_code` / `setting_id`）由 sync 完全不动；`bound_dev_id` / `bound_ams_id` 仍由 sync 维护（云端 PUT 不接受这两字段，纯本地 UI 用，无污染风险）| Q5/Q6/Q7 推论 |

## 1. 数据流（最终态）

```
设备 MQTT push
       │
       ▼
GUI_App::on_device_update(MachineObject*)
       │
       ▼
wgtFilaManagerSync::on_device_update(obj)
       │
       └─► sync_all_trays(obj)
              │
              │  对每个有效 tray：
              │
              ├─► match_tray()
              │       │
              │       ├── 未命中 → **trace log 跳过**（不再 add_spool）
              │       │           reason=ams_unmatched_tray
              │       │           用户走 UI "添加耗材-从 AMS 读取"入库
              │       │
              │       └── 命中候选 spool S
              │               │
              │               ├── S.total_net_weight <= 0
              │               │     → **冻结**：trace log（reason=no_total_weight）
              │               │       不动 store / 不入 changed / 不刷余量
              │               │
              │               └── S.total_net_weight > 0
              │                     │
              │                     │  Q6：percent → 克数换算
              │                     │  new_net_weight =
              │                     │     round(S.total_net_weight * tray.remain / 100)
              │                     ▼
              │              update_spool_if_changed(updated)
              │                  仅改 remain_percent / net_weight / status
              │                       / bound_dev_id / bound_ams_id
              │                  identity 字段（tag_uid/color/setting_id）不动
              │                       │
              │                       ├── 字段全无变化 → false → 跳过
              │                       └── 至少一个变化 → true → 入 changed 列表
              │
              └─► changed 非空时：
                  device_state = compute_device_state(obj)   ← Q8：一次 sync 算一次
                  cloud_sync->notify_ams_synced(
                       std::vector<AmsChangedSpool>{
                           spool_id, tag_uid, net_weight },
                       device_state)
                            │
                            ▼
                    对每个 changed item：
                    AmsAutoPushThrottle::evaluate(
                        tag_uid, net_weight, device_state, now)
                            │
                            ├── tag_uid 为空            → SKIP no_rfid
                            ├── net_weight == last_push → SKIP no_diff
                            ├── device_state == Busy && < 10 min
                            │                           → SKIP cooldown
                            └── 否则                    → ACCEPT
                                （包括 device_state == Idle 的所有情况）
                            │
                            ▼
                既有 push_update_to_cloud(spool_id, patch)
                PUT /filaments/{id}
                body = { id, netWeight: <new>,
                         filamentName: <既有兜底，避免空字段> }
                  ※ 严禁 RFID / remainPercent（不存在）/ status / bound_*
                  ※ filamentName 兜底见 STUDIO-18117 既有逻辑
                            │
                            ▼
                    决策 Push 后**立即** record_success（乐观节流）
                            │
                            ▼
                    既有 push 串行队列（20260417 已建）
                            │
                  ┌──────────┼──────────┐
                  ▼                     ▼
                成功                   失败
                  │                     │
                  │                     ▼
                  │              log + dispatcher.push_failed observer
                  │              不阻塞 AMS 主流程，不弹窗
                  │              （throttle 状态不重置，下次仍走 cooldown）
                  ▼
            既有 push_done observer 链路（无须额外处理）

         若 pushed_count > 0：ReportMsg auto_push_summary
         （Q4：仅 push 实际发生时刷新前端摘要）

**乐观 vs 悲观节流权衡**：选择"决策 Push 即 record_success"而不是"成功才 record"。
权衡点：
- 网络抽风 / 服务端短暂故障 → 失败时 throttle 仍按"已 push"计 cooldown，10 min 后才会重试。这是**有意为之**的退避策略，避免抖动场景反复轰炸云端。
- 业务错误（404 / 400）→ 重试也会失败，cooldown 反而帮助降低无意义请求量。
- 用户着急的兜底：StatsView "推送本地到云端" 按钮**绕过** throttle 强制入队，不受 cooldown 限制。
```

### 1.1 三个动词的覆盖矩阵

| 本地 sync 动作 | 当前代码现状 | 本 change 决策 | 云端 endpoint | 备注 |
|---|---|---|---|---|
| **add**（AMS 见到本地没有的料）| line 46 自动 `add_spool` | **改：移除自动 add，trace log 跳过** | — | 用户主动走"添加耗材-从 AMS 读取"对话框入库（既有 UI 路径，云端 POST 已就绪）|
| **update**（match → 改 remain / status）| 走 `update_spool`，更新 percent + status + bound_* + tag_uid 回填 | **保留更新 percent / status / bound_***；**新增更新 net_weight（Q6 换算）**；**移除 tag_uid 回填**（identity 字段冻结）| PUT `/filaments/{id}` | 节流 + 差分跳过，PUT body 仅 `netWeight` + `filamentName` 兜底 |
| **delete**（AMS 拔卡 / 换槽）| 不删 | **维持不删** | — | 删除仅走用户主动 CRUD（`FilaManagerVM::HandleSpool` → DELETE）|
| **缺 total_net_weight 的 spool** | line 38-44 仍会更新 percent | **整条冻结**（Q7）| — | sync 不动它任何字段；用户主动到管理器补齐整卷重才能重新参与 |

设计意图：

- store 是用户库存的事实记录，**写权应在用户手里**——MQTT 只能改"已经在册"那一卷的余量
- AMS 上插了一卷新料 / 拔了一卷料 → 仅是"现场快照"事件，不应让 store 跟随增删
- identity 字段（tag_uid / color_code / setting_id）创建时定型，AMS 永远不动 → 把 STUDIO-18117 类风险（AMS 上的非规范信息污染本地 / 云端库存）的攻击面**整段封死**
- 云端 PUT 只有 `netWeight` 这一个余量字段（无 `remainPercent`），sync 必须做 percent → 克数换算才能让数据对得上 → 反过来约束 spool 必须有 `total_net_weight`，没有就直接冻结这条 spool（不容忍半残数据）
- 云端 push 链路因此简化为**仅 PUT，且 body 仅 netWeight + filamentName 兜底**；POST / DELETE 都走用户主动 CRUD 路径，与本 change 解耦

## 2. AmsAutoPushThrottle

### 2.1 接口

```cpp
class AmsAutoPushThrottle {
public:
    enum class Decision { Push, SkipNoRfid, SkipCooldown, SkipNoDiff };

    // 设备活动状态——决定是走 cooldown 节流还是直推（Q8）
    // Busy = 打印中 / 打印暂停 / 校准中 / 挤出校准 / AMS 状态非 IDLE
    //   （进退料 / 切料 / RFID 识别 / 自检 / 冷拉断料）
    // Idle = 上述全否
    enum class DeviceState { Busy, Idle };

    // tag_uid 为空 → SkipNoRfid
    // current_net_weight 为云端 PUT body 用的 netWeight（克）
    // device_state = Busy → 走 10 min cooldown 节流
    // device_state = Idle → 跳过 cooldown，仅靠 SkipNoDiff 防自反复推
    Decision evaluate(const std::string& tag_uid,
                     int64_t current_net_weight,
                     DeviceState device_state,
                     std::chrono::steady_clock::time_point now);

    void record_success(const std::string& tag_uid,
                        int64_t pushed_net_weight,
                        std::chrono::steady_clock::time_point now);

    void clear_for_tag(const std::string& tag_uid);    // push_all_now 的旁路
    void clear_all();                                   // 登出 / 切账号

    // 硬编码 10 min（仅 Busy 路径生效）；不暴露 setter（Q2 决定）
    static constexpr std::chrono::minutes kMinIntervalBusy{10};

private:
    struct Entry {
        std::chrono::steady_clock::time_point last_pushed_at;
        int64_t last_pushed_net_weight {-1};
    };
    std::unordered_map<std::string, Entry> m_entries;
    std::mutex m_mu;
};
```

### 2.2 决策表（Q8 升级版）

| 输入条件 | device_state | last_pushed_at | last_net_weight | current_net_weight | Decision |
|---|---|---|---|---|---|
| tag_uid 为空 | * | — | — | — | **SkipNoRfid** |
| 首次见到该 tag_uid | * | 不存在 | — | any | Push |
| 克数与上次推送一致 | * | * | 800g | 800g | **SkipNoDiff**（先于 cooldown 判，节省一次 push）|
| 克数变化 + **设备忙** + 距上次 < 10 min | Busy | 5 min ago | 800g | 700g | SkipCooldown |
| 克数变化 + **设备忙** + 距上次 ≥ 10 min | Busy | 12 min ago | 800g | 700g | Push |
| 克数变化 + **设备闲** | Idle | * | 800g | 700g | **Push**（忽略 cooldown）|
| 手动 push_all_now 触发 | * | * | * | * | Push（绕过 throttle，成功后仍 record_success） |

判定优先级（先后短路顺序，和实现一致）：
1. tag_uid 空 → SkipNoRfid
2. **首次（无 entry）→ Push**（必须先于 SkipNoDiff，避免 `-1` 默认值与合法 0g spool 巧合短路）
3. net_weight == last → SkipNoDiff
4. device_state == Busy && now - last_pushed_at < 10 min → SkipCooldown
5. 其他 → Push

注：
- 缺 `total_net_weight` 的 spool 早在 sync 阶段就被冻结（Q7），永远不会进 throttle.evaluate；不需要新增 `SkipNoTotalWeight` decision
- "device_state 闲时直推"在多 tray 同时变化的场景下也成立——它们走 push 串行队列，`record_success` 把 last_pushed_at 推到当前时刻，下一拨才能进入 cooldown
- 设备从忙切到闲的瞬间，throttle 不主动 trigger 任何 push；下次 sync hook 时按新状态判定即可（避免 watchdog 风格的副作用）

### 2.3 设备 busy 判定

由 `wgtFilaManagerSync::sync_all_trays` 在调用 `cloud_sync->notify_ams_synced` 之前**计算一次**，整批 changed 共用这个 state（一次 sync 内不会出现"半忙半闲"）：

```cpp
// 实现位置：wgtFilaManagerSync::sync_all_trays 内或工具函数
AmsAutoPushThrottle::DeviceState compute_device_state(MachineObject* obj) {
    if (!obj) return DeviceState::Idle;     // 容错

    // 1. 打印任务（含 RUNNING / PAUSE）
    if (obj->is_in_printing()) return DeviceState::Busy;

    // 2. 校准（机型校准 + 挤出校准）
    if (obj->is_in_calibration())     return DeviceState::Busy;
    if (obj->is_in_extrusion_cali())  return DeviceState::Busy;

    // 3. AMS 状态非 IDLE（进退料 / 切料 / RFID 识别 / 自检 / 冷拉断料 / 调试）
    if (obj->ams_status_main != AmsStatusMain::AMS_STATUS_MAIN_IDLE &&
        obj->ams_status_main != AmsStatusMain::AMS_STATUS_MAIN_UNKNOWN) {
        return DeviceState::Busy;
    }
    return DeviceState::Idle;
}
```

**字段来源权威定位**（避免后续误判）：

| 判定项 | 字段 / API | 文件 |
|---|---|---|
| 打印中 | `MachineObject::is_in_printing()` | `DeviceManager.hpp:714` |
| 打印暂停 | 同上（`is_in_printing` 已含 PAUSE）| 同上 |
| 机型校准 | `MachineObject::is_in_calibration()` | `DeviceManager.hpp:445` |
| 挤出校准 | `MachineObject::is_in_extrusion_cali()` | `DeviceManager.hpp:283` |
| AMS 状态机 | `MachineObject::ams_status_main` (`AmsStatusMain` enum)| `DeviceManager.hpp:274` / `DevDefs.h:41-52` |

### 2.3 内存与生命周期
- 内存占用：`unordered_map<tag_uid, ~32B Entry>` × 物理料卷数。100 卷 × 64B ≈ 6 KB，可忽略。
- **不持久化**：进程重启时 throttle 清零，重启后第一次 AMS 推会触发一次 push（用户重启 Studio 是低频事件，可接受）。
- 切账号 / 登出 → `clear_all()`，避免账号 A 的 throttle 影响账号 B。

## 3. sync 改造与差分采集

### 3.1 sync_all_trays 行为收口

按 Q5/Q6/Q7 决策，`wgtFilaManagerSync::sync_all_trays` 改造为**capability filter + update-only**：

```cpp
void sync_all_trays(MachineObject* obj) {
    // ... 既有的过滤 / 遍历 ...
    std::vector<AmsChangedSpool> changed;

    auto handle_tray = [&](const DevAmsTray& tray, const std::string& ams_id) {
        if (tray.setting_id.empty() && tray.tag_uid.empty()) return;

        const FilamentSpool* matched = match_tray(tray);
        if (!matched) {
            // Q5：未匹配 → 不再 add_spool，仅 trace
            BOOST_LOG_TRIVIAL(trace) << "[ams-sync] unmatched tray, skip auto-add"
                                     << " setting_id=" << tray.setting_id
                                     << " tag_uid="    << tray.tag_uid;
            return;
        }

        // Q7：缺 total_net_weight → 整条冻结，不动 store
        const float total_nw = matched->total_net_weight;  // 见 § 3.3 字段来源
        if (total_nw <= 0.f) {
            BOOST_LOG_TRIVIAL(trace) << "[ams-sync] frozen spool, no total_net_weight"
                                     << " spool_id=" << matched->spool_id;
            return;
        }

        // Q6：percent → 克数换算
        FilamentSpool updated = *matched;
        updated.remain_percent = tray.remain;
        updated.net_weight     = std::round(total_nw * tray.remain / 100.0f);
        updated.status         = (tray.remain == 0)  ? "empty"
                               : (tray.remain < 20)  ? "low" : "active";
        updated.bound_dev_id   = obj->get_dev_id();
        updated.bound_ams_id   = ams_id;
        // identity 字段（tag_uid / color_code / setting_id）保持 *matched 原值，不动

        if (m_store->update_spool_if_changed(updated)) {
            changed.push_back({updated.spool_id, updated.tag_uid,
                               static_cast<int>(updated.net_weight + 0.5f)});
        }
    };

    for (auto& [ams_id, ams] : fila_sys->GetAmsList()) {
        for (auto& [slot_id, tray] : ams->GetTrays()) {
            if (tray) handle_tray(*tray, ams_id);
        }
    }
    for (auto& vt_tray : obj->vt_slot) handle_tray(vt_tray, "ext");

    if (!changed.empty()) {
        m_store->set_dirty();
        if (m_cloud_sync) {
            // Q8：一次 sync 算一次设备状态，整批共用
            const auto device_state = compute_device_state(obj);
            m_cloud_sync->notify_ams_synced(changed, device_state);
        }
    }
}
```

### 3.2 store 层告知"是否发生变化"

引入新方法 `update_spool_if_changed`（不动现有 `add_spool` / `update_spool` 签名，避免影响手动 CRUD 路径）：

```cpp
// 仅当同步关心字段变化时才写入；返回 true 表示发生了变化、应入 changed 列表
bool wgtFilaManagerStore::update_spool_if_changed(const FilamentSpool& sp);
```

判定语义：
- 若 store 没有该 spool_id（极端竞态：store 被外部清掉了）→ 写日志告警 + 跳过，**不退化成 add**
- 比较字段集合（"sync 关心的字段"严格收口）：
  - **核心**：`net_weight`（决定云端 PUT 是否要发）
  - **派生**：`remain_percent`、`status`（云端不发，本地 UI 用）
  - **位置**：`bound_dev_id`、`bound_ams_id`（云端不发，本地 UI 用）
- 集合内字段全部相等 → 不写、返回 false
- 至少一个变化 → 写入这些字段（**只写这些字段**，identity 字段从输入 `sp` 中忽略）→ 返回 true

`tag_uid` / `color_code` / `setting_id` 写入策略：**完全不动**。即便输入 `sp` 里这些字段被 sync 误塞了值，`update_spool_if_changed` 在写入前会用 store 既有 spool 的对应字段覆盖回去（防御式编程，确保 sync 路径无法污染 identity）。

### 3.3 `total_net_weight` 字段来源

依赖既有 store schema：`FilamentSpool::total_net_weight`（STUDIO-18115 后已是必填字段，由"添加耗材"对话框采集）。本 change **不**新增字段，仅在 sync 内**消费**它做换算。

历史 spool（STUDIO-18115 之前的、或被旧版本 sync auto-add 出来的）若 total_net_weight = 0 → 落入 Q7 冻结路径。用户在新 UI 编辑该 spool 补齐总净重并保存（既有手动 CRUD 路径已会触发 PUT，把 totalNetWeight 推到云端）→ 下次 AMS sync 自动恢复参与。

## 4. 触发点对照

| 用户操作 | 既有路径 | 本 change 新增 |
|---|---|---|
| 手动 add / edit / delete spool | `FilaManagerVM::HandleSpool` 联动 `push_*_to_cloud` | 不变 |
| 设备页切换设备 | `GUI_App::on_device_update` → `wgtFilaManagerSync::on_device_update` → 仅本地 | 经 throttle push |
| 首次连接设备 | 同上 | 经 throttle push |
| 耗材管理器内切换设备 | 同上 | 经 throttle push |
| 设备 MQTT 周期 push | 同上（高频） | 必须 throttle，否则压云端 |
| 用户登录完成 | `pull_from_cloud`（已有） | 不变 |
| **新按钮 "推送本地到云端"** | 不存在 | 新增 `filament.sync.push_all_now` |

## 5. 前端 UI

### 5.1 按钮布局（Q3：独立按钮）

StatsView 顶部状态条：

```
[同步状态徽章]   [⟳ 同步]  [↥ 推送本地到云端]
                 既有       新增（独立按钮）
                 = 从云端拉取
```

- **位置**：现有"同步"按钮右侧，间距 8 px
- **图标**：上箭头 / 云上传图标，区别于"同步"的环形箭头
- **行为**：单击 → `pushAllNow()` → `filament.sync.push_all_now`
- **加载态**：按钮转动状态期间禁用、显示 spinner

### 5.2 状态条 tooltip（Q4：仅 push 时刷新）

徽章 tooltip 增加上次自动 push 摘要：

```
最近一次 AMS 同步推送（14:02:31）
  ✓ 推送 2 卷  ⊝ 跳过 6 卷（4 节流 / 2 无差分）
```

- 数据来源：C++ ReportMsg `submod=sync, action=auto_push_summary`
- **触发条件**：仅当 `pushed_count > 0` 时才发 ReportMsg；全部 skipped → 不发 → 前端摘要保持上次值不变
- **手动 push_all_now 触发**：成功后总是发摘要（含 pushed_count 即使为 0）+ toast

### 5.3 失败反馈

| 来源 | UX |
|---|---|
| 自动 push 失败（auto_push_error） | 仅写日志，不显式 toast，不刷状态条 |
| 手动 push_all_now 失败 | toast：「推送失败：{原因}」 |
| 手动 push_all_now 成功 | toast：「已推送 {n} 卷到云端」 |

## 6. 关键决策权衡

| 决策点 | 选择 | 备选 / 拒绝原因 |
|---|---|---|
| 物理料卷标识 | tag_uid（= 云端 RFID 字段）| spool_uuid 新字段：要改 schema、要迁移、要协调云端，复杂度上升且无新增价值 |
| 无 RFID 料卷处理 | 不参与 AMS 自动同步（走用户 CRUD 既有路径）| 强行参与：需新字段，与 Q1 决定矛盾 |
| Throttle 粒度 | per tag_uid | per device：粗，导致 A1 已推但 A2 也被搁置 |
| Throttle 算法 | **双轨制：忙时 cooldown + 差分；闲时仅差分** | 仅时间：闲时余量同步太慢；仅差分：忙时网络带宽被余量推占用；统一 10 min：闲时实时性差，用户改料后等到 10 min 后才看到云端刷新；无 cooldown：打印中网络压力上升 |
| Throttle 时长 | 忙时硬编码 10 min；闲时无 cooldown | 可配：暂不需要，等真有 ops 反馈再加 |
| **设备 busy 判定来源** | `is_in_printing` + `is_in_calibration` + `is_in_extrusion_cali` + `ams_status_main != IDLE` 任一即忙 | 仅看 print_status：错过校准 / 进退料；自定义 busy 字段：依赖云端推送，过早绑定数据源 |
| **device_state 在一次 sync 内的一致性** | 一次 sync 计算一次，整批共用 | 每个 spool 单独算：相同结果，浪费；按 push 队列入队时刻算：sync 可能跨多个 tray，状态可能漂移到不一致语义 |
| Throttle 持久化 | 内存 only | 落盘：复杂度上升；进程生命周期内有效已能解决 IoT 反向轰炸 |
| 推送队列 | 复用既有 push 串行队列 | 起新队列：增加复杂度；既有队列已带失败隔离 |
| 手动按钮形态 | 独立按钮 | 二级菜单：与"拉取"耦合，误操作风险高 |
| 全 skipped UI 行为 | 不刷新 | 总刷新：IoT 高频 push 时 tooltip 满是"0/8"无意义 |
| AMS 自动 push 失败 | 日志 + ReportMsg，不弹 | 弹 toast：设备频繁切换会刷屏；静默：错误不可观测 |
| **AMS 见到陌生料卷** | trace log 跳过 | 自动 add 入库（现状，被 Q5 否决）：易让 store 被 AMS 现场快照污染；弹"是否入库"对话框：超出本 change 范围，留给独立 UX change |
| **AMS 拔卡** | 不动 store | 自动 delete：与"账本"语义冲突；用户切机器时 store 会塌陷 |
| **余量字段映射** | sync 用 total_net_weight 算出 net_weight，PUT 发 netWeight | 用 percent 直推：云端无 remainPercent 字段，请求 400；要求云端扩字段：阻塞本 change，不必要 |
| **缺 total_net_weight 的 spool** | 整条冻结，sync 不动它 | 仅冻结 push 但本地仍刷 percent：让本地 UI 显示越来越离谱；仅冻结 net_weight 不冻结 percent：双数据源不同步、UI 算克数会错 |
| **identity 字段（tag_uid / color / setting_id）AMS 是否能改** | **完全不改**，由 store 防御覆盖 | 允许 sync 写：是 STUDIO-18117 类风险源头；按需写：增加判定复杂度 |
| **bound_dev_id / bound_ams_id** | sync 维护 | 不动：跨机搬料时本地 UI 显示错位 |

## 7. 边界与开放问题

### 不在本 change 范围
- 多端冲突合并（A 设备改了余量，B 设备同时改了同一料卷余量）：仍走"最后写入者赢"，与现有用户 CRUD 路径一致。
- 跨账号迁移：本 change 不涉及（throttle 切账号清账即可）。
- 离线缓存：进程内 throttle 不持久化；离线期间 AMS 变化在重新登录后由 throttle 第一次 push 携带。
- 无 RFID 的手动料卷：不参与 AMS 自动同步路径（按定义这条路径输入就是 AMS RFID）。
- **AMS 自动入库**：本 change 明确**不做**。AMS 上检测到本地没有的料卷时仅 trace log，UI 上不弹"是否入库"提示。用户必须主动到耗材管理器 → "添加耗材-从 AMS 读取"完成入库。这条 UX 由"添加耗材"对话框既有路径承担，与本 change 解耦。
- **AMS 拔卡 / 换槽自动删除**：维持现状，**不做**。AMS 是现场快照，不应反向影响用户库存账本。删除仅走用户主动 CRUD（既有 DELETE 路径）。
- **identity 字段从 AMS 写入**：本 change 明确**不做**。`tag_uid` / `color_code` / `setting_id` 创建时填好后冻结，AMS 永远不动。用户要改这些 identity 字段必须走 UI 编辑。
- **冻结 spool 的 UI 提示**：缺 `total_net_weight` 的 spool 在本 change 阶段**仅 trace log**，不在 UI 上加角标 / 红点 / 提示气泡。后续可单独起一个 UX change 加 hint，但本 change 不绑。
- **percent 字段是否同时推云端**：云端 PUT 暂无 `remainPercent` 字段；本 change 只推 `netWeight`。若后续云端扩字段，本 change 不预留代码路径，到时再开新 change 添加。

### 待后续验证项目
- 节流 10 分钟是否合适，需要实测 N10/N11 真实 push 节奏与云端 RPS 上限。
- 手动按钮的位置最终视觉效果（与"同步"按钮间距、图标对齐），由设计组复核。

### 已拍板项（不再改动）
- **心跳语义采用解读 A（2026-05-06 拍板）**：STUDIO-18155 描述里的"心跳检查 ≥ 10 分钟"理解为"≥ 10 分钟检查一次是否要推"，差分相同则不推；**不**做"≥ 30 min 必推一次"的 keepalive 兜底。理由：
  - 云端无须靠 PUT 反向确认本端在线（已有 IM/MQTT 心跳通道）
  - 余量真正不变时反复推没有数据价值，纯增云端 RPS
  - 若后续运营反馈需要定时心跳，再开新 change，本 change 不预留代码路径
- **节流双轨语义（Q8，2026-05-06 拍板）**：忙时 10 min cooldown 解决"打印中 IoT 高频推余量占网络带宽"问题；闲时直推解决"用户改完料等 10 分钟才看到云端同步"的体验问题。两者有清晰的触发分界（is_in_printing / is_in_calibration / is_in_extrusion_cali / ams_status_main != IDLE 任一即忙），不会出现模棱两可。
- 当前 throttle 决策表（§ 2.2）即为最终形态。

## 8. 关联资料

- 既有云端 push 实现：`src/slic3r/GUI/fila_manager/wgtFilaManagerCloudSync.{h,cpp}`
  - **`spool_to_cloud_update_patch` (line 336-400)**：UpdateFilamentV2Req swagger 白名单的权威定义。本 change 仅在此函数现有 `take_int("net_weight", "netWeight")` 之外**不引入任何新字段**——netWeight 这条映射是云端 PUT 推余量的唯一通路。
  - `spool_to_cloud_update_json` (line 402-426)：`filamentName` 兜底逻辑（STUDIO-18117 已加）。本 change 走 sync push 时同样必须依赖该兜底，否则可能复现 STUDIO-18117 的 unsanitized series 问题。
  - `cloud_to_spool` (line 152-153)：`bound_dev_id` / `bound_ams_id` ↔ `trayIdName` 的方向映射（cloud → local），证明云端**只在 createType=ams 的 POST 才接受 trayIdName**，PUT 阶段两者都不接受。
  - `spool_to_cloud_create` (line 168-184)：create body 包含 RFID + trayIdName + netWeight + totalNetWeight 字段。
- 既有 push 串行队列：`20260417耗材管理器云端接入Web前端/design.md` § 4.3
- AMS 同步主路径：`src/slic3r/GUI/fila_manager/wgtFilaManagerSync.{h,cpp}`
- STUDIO-18155 原始诉求与评论
- STUDIO-17964（编辑保存空 patch 不发 PUT）：本 change 复用其"空 patch 短路"经验，`update_spool_if_changed` 返回 false 时不入 push 队列即等价。
- STUDIO-18115（净重 vs 整卷净重区分 UI）：本 change 依赖该 change 引入的 `total_net_weight` 字段。
- STUDIO-18117（云端 PUT 缺 filamentName 兜底）：本 change 的 PUT body 走相同兜底逻辑；同时通过"identity 字段冻结 + Q5 不 auto-add"把 18117 类风险面进一步缩小。
