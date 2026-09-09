#include "wgtFilaManagerSync.h"
#include "wgtFilaManagerStore.h"
#include "wgtFilaManagerCloudSync.h"
#include "wgtFilaManagerCloudClient.h"
#include "wgtFilaManagerCloudDispatcher.h"
#include "AmsAutoPushThrottle.h"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceManager.hpp"

#include <wx/app.h>
#include <boost/log/trivial.hpp>

#include <cmath>
#include <set>

namespace Slic3r { namespace GUI {

namespace {

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
    if (!obj->is_online()) return false;
    calibrate_pending_badges(obj);
    check_and_register_new_rfid_spools(obj);
    return sync_all_trays(obj);
}

void wgtFilaManagerSync::calibrate_pending_badges(MachineObject* obj)
{
    if (!obj || !m_store) return;
    auto fila_sys = obj->GetFilaSystem();
    if (!fila_sys) return;

    const std::string dev_id = obj->get_dev_id();
    const std::string prefix = dev_id + ":";
    auto& ams_list = fila_sys->GetAmsList();

    for (auto it = m_pending_badges.begin(); it != m_pending_badges.end(); ) {
        if (it->first.rfind(prefix, 0) != 0) { ++it; continue; }
        const std::string rest = it->first.substr(prefix.size());
        const auto sep = rest.find(':');
        if (sep == std::string::npos) { ++it; continue; }
        const std::string ams_id  = rest.substr(0, sep);
        const std::string slot_id = rest.substr(sep + 1);

        bool stale = true;
        auto ams_it = ams_list.find(ams_id);
        if (ams_it != ams_list.end() && ams_it->second && ams_it->second->IsExist()) {
            auto& trays = ams_it->second->GetTrays();
            auto tray_it = trays.find(slot_id);
            if (tray_it != trays.end() && tray_it->second && tray_it->second->is_exists) {
                const auto& tray = *tray_it->second;
                stale = !tray.uuid.empty() && tray.uuid != it->second;
            }
        }

        if (stale)
            it = m_pending_badges.erase(it);
        else
            ++it;
    }
}

void wgtFilaManagerSync::check_and_register_new_rfid_spools(MachineObject* obj)
{
    if (!obj || !m_store) return;

    auto* disp = wxGetApp().fila_manager_cloud_disp();
    const bool pull_ready = !disp || (!disp->last_synced_at().empty() && !disp->is_pulling());
    if (!pull_ready) return;

    auto fila_sys = obj->GetFilaSystem();
    if (!fila_sys) return;

    for (auto& [ams_id_key, ams_ptr] : fila_sys->GetAmsList()) {
        if (!ams_ptr || !ams_ptr->IsExist() || ams_ptr->GetAmsType() == DevAmsType::AMS_LITE) continue;
        for (auto& [slot_id_key, tray] : ams_ptr->GetTrays()) {
            if (!tray) continue;
            if (tray->tag_uid.size() != 16 || tray->tag_uid.substr(12, 2) != "01")
                continue;
            const std::string& uuid = tray->uuid;
            if (!FilamentSpool::is_valid_tag_uid(uuid)) continue;
            if (m_store->find_by_tag_uid(uuid) != nullptr) {
                m_auto_added_rfid_uuids.erase(uuid);  // keep set in sync so delete+reinsert re-triggers
                continue;
            }
            if (m_auto_added_rfid_uuids.count(uuid)) continue;

            m_auto_added_rfid_uuids.insert(uuid);

            BBL::AmsSyncItem sync_item;
            sync_item.RFID         = uuid;
            sync_item.filamentId   = tray->setting_id;
            sync_item.filamentType = tray->m_fila_type;
            sync_item.filamentName = tray->sub_brands;
            {
                std::string color = tray->color;
                if (!color.empty() && color[0] != '#') color = "#" + color;
                sync_item.color = color;
            }
            sync_item.colorType       = static_cast<int>(tray->ctype);
            sync_item.colors          = tray->cols;
            sync_item.netWeight       = tray->get_filament_remain_weight().value_or(0);
            try { sync_item.totalNetWeight = std::stoi(tray->weight); } catch (...) {}
            sync_item.trayIdName      = tray->tray_id_name;
            sync_item.slotId          = slot_id_key;
            try { sync_item.amsId = std::stoi(ams_id_key); } catch (...) {}
            sync_item.amsType         = static_cast<int>(tray->ams_type);
            if (auto* bundle = wxGetApp().preset_bundle) {
                auto info = bundle->get_filament_by_filament_id(tray->setting_id);
                if (info.has_value()) sync_item.filamentVendor = info->vendor;
            }
            {
                const auto ver_map = obj->get_ams_version();
                auto ver_it = ver_map.find(sync_item.amsId);
                if (ver_it != ver_map.end()) sync_item.amsSn = ver_it->second.sn;
            }

            BBL::AmsSyncParams params;
            params.devId = obj->get_dev_id();
            params.items.push_back(std::move(sync_item));
            m_pending_badges[obj->get_dev_id() + ":" + ams_id_key + ":" + slot_id_key] = uuid;

            wgtFilaManagerCloudClient client;
            client.sync_ams(std::move(params),
                [this, uuid](const nlohmann::json&) {
                    wxGetApp().CallAfter([this, uuid]() {
                        m_auto_added_rfid_uuids.erase(uuid);
                        if (auto* d = wxGetApp().fila_manager_cloud_disp())
                            d->enqueue_pull();
                    });
                },
                [uuid](int code, const std::string& err) {
                    BOOST_LOG_TRIVIAL(warning)
                        << "[auto_add_rfid] sync_ams failed uuid=" << uuid
                        << " code=" << code << " err=" << err;
                    // uuid stays in m_auto_added_rfid_uuids; no retry until reconnect
                });
        }
    }
}

void wgtFilaManagerSync::drain_filament_hints(const std::string& dev_id)
{
    const std::string prefix = dev_id + ":";
    for (const auto& [key, uuid] : m_pending_badges) {
        if (key.rfind(prefix, 0) != 0) continue;
        const std::string rest = key.substr(prefix.size());
        const auto sep = rest.find(':');
        if (sep == std::string::npos) continue;
        wxGetApp().notify_new_rfid_filament(rest.substr(0, sep), rest.substr(sep + 1));
    }
}

void wgtFilaManagerSync::dismiss_pending_badge(const std::string& dev_id,
                                               const std::string& ams_id,
                                               const std::string& slot_id)
{
    m_pending_badges.erase(dev_id + ":" + ams_id + ":" + slot_id);
}

bool wgtFilaManagerSync::on_device_disconnect(const std::string& dev_id,
                                              const std::string& dev_name)
{
    if (!m_store) return false;
    const std::string prefix = dev_id + ":";
    for (auto it = m_prev_tray_exists.begin(); it != m_prev_tray_exists.end(); ) {
        if (it->first.rfind(prefix, 0) == 0)
            it = m_prev_tray_exists.erase(it);
        else
            ++it;
    }
    m_auto_added_rfid_uuids.clear();
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
    std::map<std::string, MountUpdate> present_now;

    const auto ams_ver_map = obj->get_ams_version();

    auto handle_tray = [&](const DevAmsTray& tray, const std::string& ams_id,
                           int ams_type_int) {
        const std::string tray_key = dev_id + ":" + ams_id + ":" + tray.id;

        if (!tray.is_exists) {
            m_prev_tray_exists[tray_key] = false;
            return;
        }

        if (tray.setting_id.empty() && tray.tag_uid.empty()) return;

        const FilamentSpool* matched = match_tray(tray, dev_id, ams_id);
        if (!matched) {
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

        if (tray.remain_fetch_status == DevAmsTray::RemainFetchStatus::Refreshing ||
            tray.remain_fetch_status == DevAmsTray::RemainFetchStatus::Initializing) {
            return;
        }

        const double total_nw = matched->effective_total_net_weight();
        if (total_nw <= 0.0) {
            BOOST_LOG_TRIVIAL(trace)
                << "[ams-sync] frozen spool, no total_net_weight"
                << " spool_id=" << matched->spool_id;
            return;
        }

        FilamentSpool updated  = *matched;
        const int64_t net_weight_g = tray.get_filament_remain_weight().value_or(0);
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

        if (m_store->update_spool_if_changed(updated)) {
            any_changed = true;
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

    std::vector<std::string>         mount_changed_ids;
    std::vector<EjectedSlotSnapshot> ejected_snapshots;
    const bool mount_changed = m_store->apply_mount_diff(
        dev_id, dev_name, present_now, &mount_changed_ids, &ejected_snapshots);

    if (any_changed || mount_changed) m_store->set_dirty();

    if (!changed.empty()) {
        if (auto* cloud = wxGetApp().fila_manager_cloud_sync()) {
            const auto device_state = compute_device_state(obj);
            cloud->notify_ams_synced(changed, device_state);
        }
    }

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

const FilamentSpool* wgtFilaManagerSync::match_tray(const DevAmsTray& tray,
                                                    const std::string& dev_id,
                                                    const std::string& ams_id)
{
    if (!dev_id.empty() && !ams_id.empty() && !tray.id.empty()) {
        if (auto* pinned = m_store->find_by_slot(dev_id, ams_id, tray.id)) {
            if (slot_pin_still_valid(*pinned, tray))
                return pinned;
        }
    }
    if (!tray.uuid.empty()) {
        auto* sp = m_store->find_by_tag_uid(tray.uuid);
        if (sp) return sp;
        if (FilamentSpool::is_valid_tag_uid(tray.uuid)) return nullptr;
    }
    if (!tray.setting_id.empty()) {
        auto* sp = m_store->find_by_setting_and_color(tray.setting_id, tray.color);
        if (sp) return sp;
    }
    return nullptr;
}

bool wgtFilaManagerSync::slot_pin_still_valid(const FilamentSpool& sp,
                                              const DevAmsTray&    tray)
{
    if (!tray.setting_id.empty() && sp.filament_id != tray.setting_id) return false;
    if (!tray.sub_brands.empty()  && !sp.series.empty()
        && sp.series != tray.sub_brands) return false;
    if (!tray.m_fila_type.empty() && !sp.material_type.empty()
        && sp.material_type != tray.m_fila_type) return false;
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

}} // namespace Slic3r::GUI
