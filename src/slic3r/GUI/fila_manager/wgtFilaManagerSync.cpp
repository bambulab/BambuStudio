#include "wgtFilaManagerSync.h"
#include "wgtFilaManagerStore.h"
#include "wgtFilaManagerCloudSync.h"
#include "wgtFilaManagerCloudDispatcher.h"
#include "AmsAutoPushThrottle.h"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Monitor.hpp"
#include "slic3r/GUI/StatusPanel.hpp"

#include <wx/app.h>
#include <boost/log/trivial.hpp>

#include <cmath>
#include <set>

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

bool wgtFilaManagerSync::on_device_update(MachineObject* obj)
{
    if (!obj || !m_store) return false;
    if (!obj->is_online()) return false;  // 离线不处理，保留在位字段
    check_new_filament_hint(obj);
    const bool sync_changed   = sync_all_trays(obj);
    const bool deduct_changed = check_print_finished_and_deduct(obj);
    return sync_changed || deduct_changed;
}

bool wgtFilaManagerSync::on_device_disconnect(const std::string& dev_id,
                                              const std::string& dev_name)
{
    if (!m_store) return false;
    // 断连时清除该设备所有槽位的历史在位状态，避免重连后首轮 sync
    // 把所有槽位都误判为"重新插入"并触发 reset。
    const std::string prefix = dev_id + ":";
    for (auto it = m_prev_tray_exists.begin(); it != m_prev_tray_exists.end(); ) {
        if (it->first.rfind(prefix, 0) == 0)
            it = m_prev_tray_exists.erase(it);
        else
            ++it;
    }
    m_prev_print_status.erase(dev_id);
    // 空 present_now → was_our_hold 的 spool 全部清字段
    const std::map<std::string, MountUpdate> empty;
    return m_store->apply_mount_diff(dev_id, dev_name, empty);
}

bool wgtFilaManagerSync::sync_all_trays(MachineObject* obj)
{
    if (!obj || !m_store) return false;

    auto fila_sys = obj->GetFilaSystem();
    if (!fila_sys) return false;

    const std::string dev_id   = obj->get_dev_id();
    const std::string dev_name = obj->get_dev_name();

    bool any_changed           = false;
    std::vector<wgtFilaManagerCloudSync::AmsChangedSpool> changed;

    // 本轮观察到"在本机 AMS 上"的 spool 集合。收集完再一次性交给 store 做 diff apply，
    // 避免"清空 → 重填"过程中前端观察到中间态导致跳变。
    std::map<std::string, MountUpdate> present_now;

    // 从 get_version 报文缓存中取各 AMS 单元的序列号，key 为 ams_id 整数。
    const auto ams_ver_map = obj->get_ams_version();

    // STUDIO-18155 / openspec 20260506 单 tray 处理逻辑：
    //   1. 过滤无 setting_id / tag_uid 的空槽
    //   2. match → 命中既有 store spool；未命中 → trace log 跳过（Q5）
    //   3. 命中后若 effective_total_net_weight <= 0 → trace log 冻结（Q7）
    //   4. percent → 克数换算（Q6），仅写 net_weight / remain_percent / status
    //      / bound_dev_id / bound_ams_id 这五个 sync 关心字段；identity/display
    //      字段由 update_spool_if_changed 在 store 层防御覆盖，不可被 sync 改写
    auto handle_tray = [&](const DevAmsTray& tray, const std::string& ams_id,
                           int ams_type_int) {
        // key 用于追踪该槽位上一轮的 is_exists 状态（检测重新插入跳变）。
        const std::string tray_key = dev_id + ":" + ams_id + ":" + tray.id;

        // 物理上已拔出（is_exists=false）的槽位直接跳过，让 apply_mount_diff
        // 把它从 present_now 中排除，触发拔出事件。
        // 注意：官方 RFID 耗材拔出后 tray.tag_uid 仍然非空（保留最后一次上报
        // 的 NFC 硬件 ID），若只靠 setting_id/tag_uid 判空会错过拔出检测，
        // 导致对应 spool 的在位状态无法被清除。
        if (!tray.is_exists) {
            m_prev_tray_exists[tray_key] = false;
            return;
        }

        if (tray.setting_id.empty() && tray.tag_uid.empty()) return;

        const FilamentSpool* matched = match_tray(tray, dev_id, ams_id);
        if (!matched) {
            // Q5：未匹配 → 不再 add_spool。新增料卷只走 UI "添加耗材-从 AMS
            // 读取" 入口，避免 AMS 现场快照污染长期库存账本。
            BOOST_LOG_TRIVIAL(info)
                << "[ams-sync] unmatched tray, skip"
                << " ams_id=" << ams_id
                << " slot_id=" << tray.id
                << " setting_id=" << tray.setting_id
                << " tag_uid=" << tray.tag_uid
                << " color=" << tray.color;
            return;
        }

        BOOST_LOG_TRIVIAL(info)
            << "[ams-sync] matched tray -> spool_id=" << matched->spool_id
            << " ams_id=" << ams_id << " slot_id=" << tray.id;

        // 登记"本轮在位"，供后续 apply_mount_diff 使用。
        int ams_id_int = -1;
        try { ams_id_int = std::stoi(ams_id); } catch (...) {}
        MountUpdate mu;
        mu.ams_id   = ams_id_int;
        mu.ams_type = ams_type_int;
        mu.slot_id  = tray.id;
        {
            auto ver_it = ams_ver_map.find(ams_id_int);
            mu.ams_sn = (ver_it != ams_ver_map.end()) ? ver_it->second.sn : "";
        }
        present_now[matched->spool_id] = mu;

        // 固件正在读取 RFID 时（Refreshing/Initializing），耗材重量和身份字段
        // 尚不稳定，跳过云端推送，避免用中间态数据覆盖云端记录。
        // mount tracking（present_now）已在上方正常登记，不受此守卫影响。
        if (tray.remain_fetch_status == DevAmsTray::RemainFetchStatus::Refreshing ||
            tray.remain_fetch_status == DevAmsTray::RemainFetchStatus::Initializing) {
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
        // Q6：直接复用 get_filament_remain_weight()
        // 优先 remain_g，fallback 为 tray.weight × remain%。
        // nullopt 表示固件确认空 / 无有效数据 → 写 0（status 会被写为 "empty"）。
        const int64_t net_weight_g = tray.get_filament_remain_weight().value_or(0);
        // 固件 remain/remain_g 均为 -1（无有效余量数据）时，保留 store 既有值，
        // 避免用无效哨兵覆盖正确的本地克重/百分比，导致前端显示跳变。
        const bool has_valid_remain = (tray.remain >= 0 || tray.remain_g >= 0);
        if (has_valid_remain) {
            updated.net_weight     = static_cast<double>(net_weight_g);
            updated.remain_percent = tray.remain;
            updated.status         = (tray.remain == 0)  ? "empty"
                                  : (tray.remain < 20)   ? "low" : "active";
        } else {
            updated.net_weight     = matched->net_weight;
            updated.remain_percent = matched->remain_percent;
            updated.status         = matched->status;
            BOOST_LOG_TRIVIAL(warning) << "[ams-sync] no valid remain data for spool " << matched->spool_id
                                      << " remain=" << tray.remain << " remain_g=" << tray.remain_g
                                      << " → keeping existing net_weight=" << matched->net_weight
                                      << " remain_percent=" << matched->remain_percent;
        }
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
        int ams_type_int = static_cast<int>(ams->GetAmsType());
        for (auto& [slot_id, tray] : ams->GetTrays()) {
            if (tray) handle_tray(*tray, ams_id, ams_type_int);
        }
    }
    for (auto& vt_tray : obj->vt_slot) {
        handle_tray(vt_tray, "ext", static_cast<int>(DevAmsType::EXT_SPOOL));
    }

    // 一次性 diff apply：本机拥有权范围内做增/删/改，
    // 未变化的 spool 字段保持原值 → 前端看到稳定值不跳变。
    // out_changed_ids 收集本轮在位字段发生变化的 spool id，供后续单独推云端。
    std::vector<std::string>         mount_changed_ids;
    std::vector<EjectedSlotSnapshot> ejected_snapshots;
    const bool mount_changed = m_store->apply_mount_diff(
        dev_id, dev_name, present_now, &mount_changed_ids, &ejected_snapshots);

    if (any_changed || mount_changed) m_store->set_dirty();

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

    // 拔出事件由路径 C 独占，从路径 B 列表中排除，避免同时发两个接口
    if (!ejected_snapshots.empty()) {
        std::set<std::string> ejected_ids;
        for (const auto& snap : ejected_snapshots)
            ejected_ids.insert(snap.spool_id);
        mount_changed_ids.erase(
            std::remove_if(mount_changed_ids.begin(), mount_changed_ids.end(),
                           [&ejected_ids](const std::string& id) {
                               return ejected_ids.count(id) > 0;
                           }),
            mount_changed_ids.end());
    }

    // 在位字段单独变化（余量未变）的 spool → 批量同步到云端（路径 B）。
    // 路径 B 按 tag_uid 区分官方 RFID 卷与手动录入卷：
    //   - 官方 RFID 卷（is_valid_tag_uid==true） → sync_ams_to_cloud（POST /ams/sync）
    //   - 手动录入卷（is_valid_tag_uid==false）  → sync_slot_bindings_to_cloud
    //       （POST /slot-mappings/sync，bind payload 带 spoolId/rfid）
    // 两路允许并发推送同一 spool（云端幂等，最后到达者覆盖）。
    if (!mount_changed_ids.empty()) {
        if (auto* cloud = wxGetApp().fila_manager_cloud_sync()) {
            std::vector<std::string> rfid_ids, manual_ids;
            for (const auto& sid : mount_changed_ids) {
                const FilamentSpool* sp = m_store->get_spool(sid);
                if (!sp) continue;
                if (FilamentSpool::is_valid_tag_uid(sp->tag_uid))
                    rfid_ids.push_back(sid);
                else
                    manual_ids.push_back(sid);
            }

            if (!rfid_ids.empty()) {
                BOOST_LOG_TRIVIAL(info)
                    << "[ams-sync] path B rfid: dev=" << dev_id
                    << " count=" << rfid_ids.size()
                    << " -> CALL sync_ams_to_cloud";
                cloud->sync_ams_to_cloud(dev_id, rfid_ids);
            }
            if (!manual_ids.empty()) {
                BOOST_LOG_TRIVIAL(info)
                    << "[ams-sync] path B manual: dev=" << dev_id
                    << " count=" << manual_ids.size()
                    << " -> CALL sync_slot_bindings_to_cloud (bind)";
                cloud->sync_slot_bindings_to_cloud(dev_id, manual_ids, /*is_bind=*/true);
            }
        }
    }

    // 路径 C：拔出事件 → 通过 slot-mappings/sync 解绑云端槽位
    if (!ejected_snapshots.empty()) {
        if (auto* cloud = wxGetApp().fila_manager_cloud_sync()) {
            BOOST_LOG_TRIVIAL(info)
                << "[ams-sync] path C: dev=" << obj->get_dev_id()
                << " ejected=" << ejected_snapshots.size()
                << " -> CALL sync_slot_mappings_to_cloud";

            cloud->sync_slot_mappings_to_cloud(obj->get_dev_id(), ejected_snapshots);
        }
    }

    return mount_changed;
}

bool wgtFilaManagerSync::check_print_finished_and_deduct(MachineObject* obj)
{
    if (!obj || !m_store) return false;

    const std::string dev_id       = obj->get_dev_id();
    const std::string print_status = obj->print_status;
    const std::string prev_status  = m_prev_print_status[dev_id];
    m_prev_print_status[dev_id]    = print_status;

    if (print_status != "FINISH" || prev_status == "FINISH")
        return false;

    auto pending = m_store->take_pending_consumption(dev_id);
    if (!pending.has_value())
        return false;

    bool any_changed = false;
    for (const auto& [slot_key, used_g] : pending->per_slot_used_g) {
        if (used_g <= 0.0) continue;

        const FilamentSpool* matched = m_store->find_by_slot(dev_id, slot_key.first, slot_key.second);
        if (!matched) {
            BOOST_LOG_TRIVIAL(warning)
                << "[FilaManager] finish deduction skip: no spool bound to dev="
                << dev_id << " ams_id=" << slot_key.first
                << " slot_id=" << slot_key.second
                << " job_key=" << pending->job_key;
            continue;
        }

        const std::string spool_id = matched->spool_id;
        if (!m_store->deduct_consumption(spool_id, used_g, pending->job_key))
            continue;

        any_changed = true;
        BOOST_LOG_TRIVIAL(info)
            << "[FilaManager] deducted used_g=" << used_g
            << " spool_id=" << spool_id
            << " dev_id=" << dev_id
            << " job_key=" << pending->job_key;

        // The deduction is local-only until pushed — without this push the
        // next pull_from_cloud() reverts net_weight to the stale cloud value.
        // Enqueue immediately so the dispatcher FIFO lands the new weight
        // before any subsequently queued pull. Safe when logged out: the op
        // no-ops and the weight_push_pending flag keeps pulls from
        // overwriting the local deduction in the meantime.
        if (auto* disp = wxGetApp().fila_manager_cloud_disp()) {
            disp->enqueue_push_update(spool_id, nlohmann::json{
                {"net_weight",       matched->net_weight},
                {"total_net_weight", matched->effective_total_net_weight()},
            });
            BOOST_LOG_TRIVIAL(info)
                << "[FilaManager] enqueued weight push spool_id=" << spool_id
                << " net_weight=" << matched->net_weight;
        }
    }

    return any_changed;
}

const FilamentSpool* wgtFilaManagerSync::match_tray(const DevAmsTray& tray,
                                                    const std::string& dev_id,
                                                    const std::string& ams_id)
{
    // 0. 槽位锚优先：用户手动绑定耗材到 AMS 槽位时，AMSMaterialsSetting 会调用
    //    wgtFilaManagerStore::force_mount_spool 把 (dev_id, ams_id, slot_id) 钉
    //    在 spool 上；重启后云端 pull 也会恢复这些字段。此分支唯一能解决"同款
    //    多卷"（setting_id+color 都相同）经模糊匹配 count>1 返回 nullptr 的场景。
    //    命中后必须做 slot_pin_still_valid 校验：若 spool 的类型/颜色已被用户
    //    手动改动，视为不同物理卷，锚立即失效退回常规匹配。
    if (!dev_id.empty() && !ams_id.empty() && !tray.id.empty()) {
        if (auto* pinned = m_store->find_by_slot(dev_id, ams_id, tray.id)) {
            if (slot_pin_still_valid(*pinned, tray))
                return pinned;
        }
    }

    // 1. RFID/UUID 精确匹配：
    //    tray.uuid 是云端分配的 UUID（32字符），与耗材入库时存储的 spool.tag_uid 格式一致。
    //    tray.tag_uid 是 NFC 芯片硬件 ID（16字符），两者不同，不能混用。
    if (!tray.uuid.empty()) {
        auto* sp = m_store->find_by_tag_uid(tray.uuid);
        if (sp) return sp;
        // 有效 UUID 但库里无匹配记录：该耗材未录入，不降级到 setting+color 模糊匹配，
        // 避免将官方 RFID 耗材的在位信息错误写到仅类型/颜色相同的手动录入耗材上。
        if (FilamentSpool::is_valid_tag_uid(tray.uuid)) return nullptr;
    }
    // 2. setting_id + color 唯一模糊匹配（同款多卷时 count>1 会返回 nullptr）
    if (!tray.setting_id.empty()) {
        auto* sp = m_store->find_by_setting_and_color(tray.setting_id, tray.color);
        if (sp) return sp;
    }
    return nullptr;
}

bool wgtFilaManagerSync::slot_pin_still_valid(const FilamentSpool& sp,
                                              const DevAmsTray&    tray)
{
    // 用户表态的锚失效条件：颜色 / 品牌 / 类型 / 系列 手动改任一 → 视为不同物理卷。
    // brand / material_type / series 在耗材管理器编辑对话框里被手动修改，
    // 这里逐字段比对而不是只看 setting_id，避免 setting_id 组合口径变化导致漏判。
    // AMS 侧 tray.sub_brands 对应 series；tray.m_fila_type 对应 material_type；
    // 品牌无独立 tray 字段（tray.setting_id 前 3 位间接表达），因此若品牌被改，
    // setting_id 会随之变，用 setting_id 兜底品牌变化即可。
    if (!tray.setting_id.empty() && sp.setting_id != tray.setting_id) return false;
    if (!tray.sub_brands.empty()  && !sp.series.empty()
        && sp.series != tray.sub_brands) return false;
    if (!tray.m_fila_type.empty() && !sp.material_type.empty()
        && sp.material_type != tray.m_fila_type) return false;

    // color 归一化：spool.color_code 是 "#RRGGBB"（6 位带 #），
    // tray.color 是 "RRGGBBAA"（8 位无 #），统一到 6 位大写 RRGGBB 再比较。
    if (!tray.color.empty()) {
        auto norm = [](const std::string& c) {
            std::string s = c;
            if (!s.empty() && s[0] == '#') s = s.substr(1);
            if (s.size() == 8) s = s.substr(0, 6);
            for (auto& ch : s) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
            return s;
        };
        if (norm(sp.color_code) != norm(tray.color)) return false;
    }

    return true;
}

void wgtFilaManagerSync::check_new_filament_hint(MachineObject* obj)
{
    if (!obj || !m_store) return;
    auto fila_sys = obj->GetFilaSystem();
    if (!fila_sys) return;

    const std::string dev_id = obj->get_dev_id();

    for (auto& [ams_id, ams] : fila_sys->GetAmsList()) {
        if (!ams) continue;
        for (auto& [slot_id, tray] : ams->GetTrays()) {
            if (!tray) continue;
            const std::string key = dev_id + ":" + ams_id + ":" + tray->id;

            if (!tray->is_exists || !DevFilaSystem::IsBBL_Filament(tray->tag_uid)) {
                // 耗材拔出时，清除该槽位的 skip 记录，让下次插入重新评估
                auto it = m_slot_skipped_uuid.find(key);
                if (it != m_slot_skipped_uuid.end()) {
                    m_skipped_uuids.erase(it->second);
                    m_slot_skipped_uuid.erase(it);
                }
                notify_new_filament_hint(ams_id, tray->id, false);
                continue;
            }

            // RFID 尚未读取完成，不作判断，等下次 tick
            if (tray->remain_fetch_status == DevAmsTray::RemainFetchStatus::Refreshing ||
                tray->remain_fetch_status == DevAmsTray::RemainFetchStatus::Initializing)
                continue;

            if (tray->uuid.empty()) continue;

            // 用户已对该卷选择"Not now"，在拔出前抑制角标
            if (m_skipped_uuids.count(tray->uuid)) {
                m_slot_skipped_uuid[key] = tray->uuid;  // 记录反向映射，供拔出时清除
                continue;
            }

            const bool not_in_store = m_store->find_by_tag_uid(tray->uuid) == nullptr;
            notify_new_filament_hint(ams_id, tray->id, not_in_store);
        }
    }
}

void wgtFilaManagerSync::skip_new_filament_hint(const std::string& uuid)
{
    if (uuid.empty()) return;
    m_skipped_uuids.insert(uuid);
}

void wgtFilaManagerSync::notify_new_filament_hint(const std::string& ams_id,
                                                   const std::string& slot_id,
                                                   bool               show)
{
    wxGetApp().CallAfter([ams_id, slot_id, show]() {
        auto* mf = wxGetApp().mainframe;
        if (!mf || !mf->m_monitor) return;
        auto* panel = mf->m_monitor->get_status_panel();
        if (panel) panel->set_ams_new_filament_hint(ams_id, slot_id, show);
    });
}

}} // namespace Slic3r::GUI
