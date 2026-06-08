#include "wgtFilaManagerSync.h"
#include "wgtFilaManagerStore.h"
#include "wgtFilaManagerCloudSync.h"
#include "AmsAutoPushThrottle.h"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"

#include <wx/app.h>
#include <boost/log/trivial.hpp>

#include <cmath>

namespace Slic3r { namespace GUI {

namespace {

// STUDIO-18155 design § 2.3：判定一台设备当前是否处于"忙"状态。
// 忙 = 打印 / 校准 / 挤出校准 / AMS 状态非 IDLE（含进退料 / 切料 /
//       RFID 识别 / 自检 / 冷拉断料 / 调试）。
// 任一为真即忙。Idle 仅在以上全否时才成立。
AmsAutoPushThrottle::DeviceState compute_device_state(MachineObject* obj)
{
    if (!obj) return AmsAutoPushThrottle::DeviceState::Idle;

    if (obj->is_in_printing())       return AmsAutoPushThrottle::DeviceState::Busy;
    if (obj->is_in_calibration())    return AmsAutoPushThrottle::DeviceState::Busy;
    if (obj->is_in_extrusion_cali()) return AmsAutoPushThrottle::DeviceState::Busy;

    if (obj->ams_status_main != AmsStatusMain::AMS_STATUS_MAIN_IDLE
        && obj->ams_status_main != AmsStatusMain::AMS_STATUS_MAIN_UNKNOWN) {
        return AmsAutoPushThrottle::DeviceState::Busy;
    }
    return AmsAutoPushThrottle::DeviceState::Idle;
}

} // namespace

wgtFilaManagerSync::wgtFilaManagerSync(wgtFilaManagerStore* store)
    : m_store(store)
{}

void wgtFilaManagerSync::on_device_update(MachineObject* obj)
{
    if (!obj || !m_store) return;
    sync_all_trays(obj);
}

void wgtFilaManagerSync::sync_all_trays(MachineObject* obj)
{
    if (!obj || !m_store) return;

    auto fila_sys = obj->GetFilaSystem();
    if (!fila_sys) return;

    const std::string dev_id = obj->get_dev_id();
    bool any_changed         = false;
    std::vector<wgtFilaManagerCloudSync::AmsChangedSpool> changed;

    // STUDIO-18155 / openspec 20260506 单 tray 处理逻辑：
    //   1. 过滤无 setting_id / tag_uid 的空槽
    //   2. match → 命中既有 store spool；未命中 → trace log 跳过（Q5）
    //   3. 命中后若 effective_total_net_weight <= 0 → trace log 冻结（Q7）
    //   4. percent → 克数换算（Q6），仅写 net_weight / remain_percent / status
    //      / bound_dev_id / bound_ams_id 这五个 sync 关心字段；identity/display
    //      字段由 update_spool_if_changed 在 store 层防御覆盖，不可被 sync 改写
    auto handle_tray = [&](const DevAmsTray& tray, const std::string& ams_id) {
        if (tray.setting_id.empty() && tray.tag_uid.empty()) return;

        const FilamentSpool* matched = match_tray(tray);
        if (!matched) {
            // Q5：未匹配 → 不再 add_spool。新增料卷只走 UI "添加耗材-从 AMS
            // 读取" 入口，避免 AMS 现场快照污染长期库存账本。
            BOOST_LOG_TRIVIAL(trace)
                << "[ams-sync] unmatched tray, skip auto-add"
                << " setting_id=" << tray.setting_id
                << " tag_uid="    << tray.tag_uid;
            return;
        }

        // Q7：缺整卷净重的 spool 整条冻结。连本地 percent 都不刷，避免
        // 半残数据漂移导致 UI 越来越离谱。用户在管理器编辑该 spool 补齐
        // total_net_weight 后下次 AMS sync 自动恢复参与。
        const double total_nw = matched->effective_total_net_weight();
        if (total_nw <= 0.0) {
            BOOST_LOG_TRIVIAL(trace)
                << "[ams-sync] frozen spool, no total_net_weight"
                << " spool_id=" << matched->spool_id;
            return;
        }

        FilamentSpool updated  = *matched;
        // Q6：percent (0..100) × total_net_weight (克) / 100 → 当前净重 (克)。
        // 这是云端 PUT 唯一接受的余量字段（UpdateFilamentV2Req::netWeight）。
        // 用 double 中间量算完再 round → int64，避开 float 精度丢失，确保
        // 本地 store 与下面 changed 列表里给 throttle 用的数值完全一致。
        const int64_t net_weight_g =
            static_cast<int64_t>(std::round(total_nw * tray.remain / 100.0));
        updated.net_weight     = static_cast<double>(net_weight_g);
        updated.remain_percent = tray.remain;
        updated.status         = (tray.remain == 0)  ? "empty"
                              : (tray.remain < 20)   ? "low" : "active";
        updated.bound_dev_id   = dev_id;
        updated.bound_ams_id   = ams_id;
        // identity/display 字段（spool_id / tag_uid / color_code / colors /
        // color_type / setting_id / entry_method / created_at / cloud_synced）
        // 保持 *matched 原值。
        // 即便此处误赋值，update_spool_if_changed 会用 store 既有值覆盖回去
        // （STUDIO-18117 教训：AMS 不允许动 identity）。

        if (m_store->update_spool_if_changed(updated)) {
            any_changed = true;
            // identity 字段从 store 既有 spool 取（防御覆盖后值），不能直接
            // 用 updated.tag_uid——sync 路径上的 tag_uid 不可信。
            const FilamentSpool* persisted = m_store->get_spool(matched->spool_id);
            const std::string&   tag       = persisted ? persisted->tag_uid : matched->tag_uid;
            changed.push_back({
                matched->spool_id,
                tag,
                net_weight_g
            });
        }
    };

    for (auto& [ams_id, ams] : fila_sys->GetAmsList()) {
        if (!ams) continue;
        for (auto& [slot_id, tray] : ams->GetTrays()) {
            if (tray) handle_tray(*tray, ams_id);
        }
    }
    for (auto& vt_tray : obj->vt_slot) {
        handle_tray(vt_tray, "ext");
    }

    if (any_changed) m_store->set_dirty();

    // STUDIO-18155：sync 完成本地写入后联动云端 push。
    //   1. device_state 一次 sync 算一次，整批共用（design § 2.3）
    //   2. cloud_sync 内部按 throttle 决策决定是否真发 PUT
    //   3. cloud_sync 不可用（未登录 / 未初始化）时静默跳过——AMS 本地同步
    //      链路必须不阻塞、不弹窗
    if (!changed.empty()) {
        if (auto* cloud = wxGetApp().fila_manager_cloud_sync()) {
            const auto device_state = compute_device_state(obj);
            cloud->notify_ams_synced(changed, device_state);
        }
    }
}

const FilamentSpool* wgtFilaManagerSync::match_tray(const DevAmsTray& tray)
{
    if (!tray.tag_uid.empty()) {
        auto* sp = m_store->find_by_tag_uid(tray.tag_uid);
        if (sp) return sp;
    }
    if (!tray.setting_id.empty()) {
        auto* sp = m_store->find_by_setting_and_color(tray.setting_id, tray.color);
        if (sp) return sp;
    }
    return nullptr;
}

}} // namespace Slic3r::GUI
