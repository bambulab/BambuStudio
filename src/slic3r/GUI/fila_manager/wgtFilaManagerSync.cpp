#include "wgtFilaManagerSync.h"
#include "wgtFilaManagerStore.h"

#include <boost/log/trivial.hpp>

#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"

namespace Slic3r { namespace GUI {

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

    std::string dev_id = obj->get_dev_id();

    for (auto& [ams_id, ams] : fila_sys->GetAmsList()) {
        if (!ams) continue;
        for (auto& [slot_id, tray] : ams->GetTrays()) {
            if (!tray || (tray->setting_id.empty() && tray->tag_uid.empty()))
                continue;

            const FilamentSpool* matched = match_tray(*tray);
            if (matched) {
                FilamentSpool updated    = *matched;
                updated.remain_percent   = tray->remain;
                updated.bound_dev_id     = dev_id;
                updated.bound_ams_id     = ams_id;
                if (!tray->tag_uid.empty()) updated.tag_uid = tray->tag_uid;
                updated.status = tray->remain == 0 ? "empty" : (tray->remain < 20 ? "low" : "active");
                m_store->update_spool(updated);
            } else {
                m_store->add_spool(create_spool_from_tray(*tray, dev_id, ams_id));
            }
        }
    }

    for (auto& tray : obj->vt_slot) {
        if (tray.setting_id.empty() && tray.tag_uid.empty())
            continue;

        const FilamentSpool* matched = match_tray(tray);
        if (matched) {
            FilamentSpool updated    = *matched;
            updated.remain_percent   = tray.remain;
            updated.bound_dev_id     = dev_id;
            updated.bound_ams_id     = "ext";
            if (!tray.tag_uid.empty()) updated.tag_uid = tray.tag_uid;
            updated.status = tray.remain == 0 ? "empty" : (tray.remain < 20 ? "low" : "active");
            m_store->update_spool(updated);
        } else {
            m_store->add_spool(create_spool_from_tray(tray, dev_id, "ext"));
        }
    }

    m_store->set_dirty();
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

FilamentSpool wgtFilaManagerSync::create_spool_from_tray(const DevAmsTray& tray,
                                                          const std::string& dev_id,
                                                          const std::string& ams_id)
{
    FilamentSpool spool;
    spool.tag_uid        = tray.tag_uid;
    spool.setting_id     = tray.setting_id;
    spool.color_code     = tray.color;
    spool.remain_percent = tray.remain;
    spool.entry_method   = "ams_sync";
    spool.bound_dev_id   = dev_id;
    spool.bound_ams_id   = ams_id;
    spool.status         = tray.remain < 20 ? "low" : "active";
    return spool;
}

}} // namespace Slic3r::GUI
