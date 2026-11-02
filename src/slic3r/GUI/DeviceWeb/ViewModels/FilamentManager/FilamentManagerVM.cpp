// src/slic3r/GUI/DeviceWeb/ViewModels/FilamentManager/FilamentManagerVM.cpp
#include "FilamentManagerVM.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebBridge.hpp"

#include <algorithm>
#include <vector>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/CreateFilamentWebDialog.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevConfigUtil.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/EncodedFilament.hpp"
#include "slic3r/GUI/fila_manager/wgtFilaManagerColorType.h"
#include "slic3r/GUI/fila_manager/wgtFilaManagerStore.h"
#include "slic3r/GUI/fila_manager/wgtFilaManagerCloudClient.h"
#include "slic3r/GUI/fila_manager/wgtFilaManagerCloudSync.h"
#include "slic3r/GUI/fila_manager/wgtFilaManagerCloudDispatcher.h"
#include "slic3r/Utils/NetworkAgent.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <wx/app.h>
#include <wx/colour.h>
#include <wx/string.h>
#include <iomanip>
#include <sstream>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

std::string wx_to_utf8(const wxString& value)
{
    return std::string(value.ToUTF8().data());
}

bool is_spool_cloud_write_action(const std::string& action)
{
    return action == "add"
        || action == "batch_add"
        || action == "batch_create"
        || action == "update"
        || action == "remove"
        || action == "batch_remove";
}

bool can_write_spool_to_cloud(NetworkAgent* agent)
{
    return agent && agent->is_user_login() && agent->is_server_connected();
}

std::string normalize_ams_hex_for_web(const std::string& raw)
{
    if (raw.empty()) return {};
    std::string hex = raw;
    if (hex[0] != '#') hex = "#" + hex;
    if (hex.size() > 9) hex = hex.substr(0, 9);
    return hex;
}

std::string normalize_ams_rfid_for_web(const std::string& tray_uuid, const std::string& tag_uid)
{
    return tag_uid.size() == 16 && tag_uid.substr(12, 2) == "01" ? tray_uuid : std::string();
}

nlohmann::json build_tray_json(const DevAmsTray* tray,
                               const std::string& slot_id,
                               const std::string& label,
                               bool exists)
{
    nlohmann::json t;
    t["slot_id"]    = slot_id;
    t["tray_label"] = label;
    t["is_exists"]  = exists;
    if (tray && exists) {
        t["is_unknown"]   = !tray->is_tray_info_ready();
        t["tag_uid"]      = normalize_ams_rfid_for_web(tray->uuid, tray->tag_uid);
        t["tray_id_name"] = tray->tray_id_name;
        t["setting_id"]   = tray->setting_id;
        t["fila_type"]    = tray->m_fila_type;
        t["sub_brands"]   = tray->sub_brands;
        t["color"]        = normalize_ams_hex_for_web(tray->color);
        t["weight"]       = tray->weight;
        t["remain"]       = tray->remain;
        t["remain_g"]     = tray->remain_g;
        if (auto rw = tray->get_filament_remain_weight())
            t["remain_weight"] = *rw;
        else
            t["remain_weight"] = nullptr;
        t["diameter"]     = tray->diameter;
        t["is_bbl"]       = tray->is_bbl;

        nlohmann::json colors = nlohmann::json::array();
        std::vector<std::string> src_colors = tray->cols;
        if (src_colors.empty()) src_colors.push_back(tray->color);
        for (const auto& c : src_colors) {
            std::string hex = normalize_ams_hex_for_web(c);
            if (!hex.empty()) colors.push_back(hex);
        }
        t["colors"]     = colors;
        const int color_type = from_ams_color_type(tray->ctype, colors.size());
        t["color_type"] = color_type;

        if (!tray->setting_id.empty() && !colors.empty()) {
            if (auto* clr_query = wxGetApp().get_filament_color_code_query()) {
                std::vector<wxString> hex_colors;
                for (const auto& c : colors) {
                    if (c.is_string())
                        hex_colors.emplace_back(wxString::FromUTF8(c.get<std::string>()));
                }
                if (auto* color_info = clr_query->GetFilaInfo(
                        wxString::FromUTF8(tray->setting_id), hex_colors, color_type)) {
                    t["color_name"]      = color_info->GetFilaColorName().utf8_string();
                    t["fila_color_code"] = color_info->GetFilaColorCode().utf8_string();
                }
            }
        }
    }
    return t;
}

} // namespace

FilamentManagerVM::FilamentManagerVM()
{
#if !BBL_RELEASE_TO_PUBLIC
    wxGetApp().set_fila_debug_sink([this](const nlohmann::json& payload) {
        if (m_bridge) {
            m_bridge->ReportMsg(MakeResp("debug", "log", 0, "", payload));
        }
    });
#endif
    // Subscribe to dispatcher state changes so we can push reports to the web.
    if (auto* disp = wxGetApp().fila_manager_cloud_disp()) {
        disp->set_on_state_changed([this]() { publish_sync_state(); });
        disp->set_on_pull_done([this](int added, int updated) {
            publish_pull_done(added, updated);
        });
        disp->set_on_push_failed([this](const std::string& id,
                                        const std::string& op,
                                        int code,
                                        const std::string& msg) {
            publish_push_failed(id, op, code, msg);
        });
        disp->set_on_push_done([this](const std::string& id,
                                      const std::string& op) {
            if (m_bridge) {
                m_bridge->ReportMsg(MakeResp("spool", "list", 0, "", build_spool_list()));
            }
            publish_push_done(id, op);
            if (op == "create" && !id.empty()) {
                m_pending_pull_added_hint += 1;
            }
            if (auto* d = wxGetApp().fila_manager_cloud_disp()) {
                d->enqueue_pull();
            }
        });
    }
    if (auto* sync = wxGetApp().fila_manager_cloud_sync()) {
        sync->set_on_auto_push_summary([this](const nlohmann::json& summary) {
            if (m_bridge) {
                m_bridge->ReportMsg(MakeResp("sync", "auto_push_summary", 0, "", summary));
            }
        });
    }
}

FilamentManagerVM::~FilamentManagerVM()
{
#if !BBL_RELEASE_TO_PUBLIC
    wxGetApp().set_fila_debug_sink(nullptr);
#endif
    if (auto* disp = wxGetApp().fila_manager_cloud_disp()) {
        disp->set_on_state_changed(nullptr);
        disp->set_on_pull_done(nullptr);
        disp->set_on_push_failed(nullptr);
        disp->set_on_push_done(nullptr);
    }
    if (auto* sync = wxGetApp().fila_manager_cloud_sync()) {
        sync->set_on_auto_push_summary(nullptr);
    }
}

nlohmann::json FilamentManagerVM::OnCommand(
    const std::string& submod,
    const std::string& action,
    const nlohmann::json& payload)
{
    BOOST_LOG_TRIVIAL(info) << "submod=" << submod
                            << " action=" << action;

    if (submod == "init")          return HandleInit(action, payload);
    if (submod == "spool")         return HandleSpool(action, payload);
    if (submod == "preset")        return HandlePreset(action, payload);
    if (submod == "machine")       return HandleMachine(action, payload);
    if (submod == "ams")           return HandleAms(action, payload);
    if (submod == "sync")          return HandleSync(action, payload);
    if (submod == "config")        return HandleConfig(action, payload);
    if (submod == "colors")        return HandleColors(action, payload);

    return MakeResp(submod, action, -1, "unknown submod");
}

void FilamentManagerVM::ReportState(const std::string& submod, const std::string& action)
{
    if (!m_bridge) return;

    if (submod == "spool") {
        nlohmann::json body = MakeResp("spool", "list", 0, "", build_spool_list());
        m_bridge->ReportMsg(body);
        return;
    }

    if (submod == "sync") {
        publish_sync_state();
        return;
    }

    if (submod == "machine" && action == "selected_changed") {
        nlohmann::json payload;
        payload["machines"] = build_machine_list();
        payload["ams"]      = build_ams_data();
        // Keep a flat convenience field at the top of the payload so the
        // frontend does not need to peek into the nested objects.
        payload["selected_dev_id"] = payload["ams"].value("selected_dev_id", std::string());
        nlohmann::json body = MakeResp("machine", "selected_changed", 0, "", payload);
        m_bridge->ReportMsg(body);
        return;
    }
}

nlohmann::json FilamentManagerVM::HandleInit(const std::string& action, const nlohmann::json& /*payload*/)
{
    bool dark = wxGetApp().app_config->get("dark_color_mode") == "1";
    nlohmann::json data;
    data["theme"]   = dark ? "dark" : "light";
    data["spools"]  = build_spool_list();
    data["presets"] = build_preset_options();
    data["cloud_sync"] = build_sync_state();
#if !BBL_RELEASE_TO_PUBLIC
    data["debug_enabled"] = true;
#else
    data["debug_enabled"] = false;
#endif
    return MakeResp("init", action, 0, "", data);
}

nlohmann::json FilamentManagerVM::HandleSpool(const std::string& action, const nlohmann::json& payload)
{
    auto* store = wxGetApp().fila_manager_store();
    auto* disp  = wxGetApp().fila_manager_cloud_disp();
    auto* agent = wxGetApp().getAgent();

    if (action == "list") {
        return MakeResp("spool", action, 0, "", build_spool_list());
    }
    if (is_spool_cloud_write_action(action) && (!store || !disp || !can_write_spool_to_cloud(agent))) {
        return MakeResp("spool", action, -2, "cloud sync requires sign-in and network");
    }
    if (action == "add") {
        publish_debug_log("data", "info", "HandleSpool add: enter",
                          "Begin processing spool/add on C++ side",
                          {{"has_store", store != nullptr}});

        FilamentSpool s;
        try {
            s = FilamentSpool::from_json(payload);
        } catch (const std::exception& e) {
            publish_debug_log("data", "error", "HandleSpool add: from_json threw",
                              "Failed to parse incoming spool payload",
                              {{"what", e.what()}, {"payload", payload}});
            return MakeResp("spool", action, 3, std::string("from_json: ") + e.what());
        }
        s.cloud_synced = false;

        publish_debug_log("data", "info", "Spool create queued",
                          "A new spool create request was queued for cloud",
                          {{"action", "add"}});
        if (disp) disp->enqueue_push_create(s);
        publish_sync_state();
        return MakeResp("spool", action, 0, "", build_spool_list());
    }
    if (action == "batch_add") {
        std::vector<FilamentSpool> new_spools;
        int qty = payload.value("quantity", 1);
        nlohmann::json sd = payload.contains("spool") ? payload["spool"] : nlohmann::json::object();
        for (int i = 0; i < qty; ++i) {
            FilamentSpool s = FilamentSpool::from_json(sd);
            s.cloud_synced = false;
            new_spools.push_back(s);
        }
        publish_debug_log("data", "info", "Spool batch create queued",
                          "Multiple spool create requests were queued for cloud",
                          {{"action", "batch_add"}, {"count", qty}});
        if (disp) for (auto& spool : new_spools) disp->enqueue_push_create(spool);
        publish_sync_state();
        return MakeResp("spool", action, 0, "", build_spool_list());
    }
    if (action == "batch_create") {
        const nlohmann::json creates = payload.contains("creates") && payload["creates"].is_array()
                                           ? payload["creates"]
                                           : nlohmann::json::array();
        const nlohmann::json updates = payload.contains("updates") && payload["updates"].is_array()
                                           ? payload["updates"]
                                           : nlohmann::json::array();
        publish_debug_log("data", "info", "Spool batch_create queued",
                          "Batch create/update request from AMS multi-select",
                          {{"action", "batch_create"},
                           {"creates", static_cast<int>(creates.size())},
                           {"updates", static_cast<int>(updates.size())}});

        int created = 0;
        for (const auto& entry : creates) {
            try {
                FilamentSpool s = FilamentSpool::from_json(entry);
                s.cloud_synced = false;
                if (disp) disp->enqueue_push_create(s);
                ++created;
            } catch (const std::exception& e) {
                publish_debug_log("data", "warn", "batch_create: create entry rejected",
                                  "Skipping malformed create payload",
                                  {{"what", e.what()}});
            }
        }

        int updated = 0;
        for (const auto& entry : updates) {
            if (!entry.is_object()) continue;
            const std::string updated_id = entry.value("spool_id", "");
            if (updated_id.empty()) {
                publish_debug_log("data", "warn", "batch_create: update entry missing spool_id",
                                  "Skipping update without spool_id",
                                  {{"keys", static_cast<int>(entry.size())}});
                continue;
            }
            if (!store || store->get_spool(updated_id) == nullptr) {
                publish_debug_log("data", "warn", "batch_create: target spool not found",
                                  "Local store has no record matching spool_id",
                                  {{"spool_id", updated_id}});
                continue;
            }
            if (disp) disp->enqueue_push_update(updated_id, entry);
            ++updated;
        }

        publish_debug_log("data", "info", "Spool batch_create accepted",
                          "Enqueued create + update batch from AMS multi-select",
                          {{"created", created}, {"updated", updated}});
        publish_sync_state();
        return MakeResp("spool", action, 0, "", build_spool_list());
    }
    if (action == "update") {
        const std::string updated_id = payload.value("spool_id", "");
        bool can_update = false;
        if (store && !updated_id.empty()) {
            can_update = store->get_spool(updated_id) != nullptr;
            if (can_update) {
                publish_debug_log("data", "info", "Spool update queued",
                                  "A spool update request was queued for cloud",
                                  {{"action", "update"},
                                   {"spool_id", updated_id},
                                   {"patch_keys", static_cast<int>(payload.is_object() ?
                                        payload.size() : 0)}});
            } else {
                publish_debug_log("data", "warn", "Local spool patch skipped",
                                  "apply_patch could not find the target spool",
                                  {{"action", "update"}, {"spool_id", updated_id}});
            }
        }
        if (disp && can_update) disp->enqueue_push_update(updated_id, payload);
        publish_sync_state();
        return MakeResp("spool", action, 0, "", build_spool_list());
    }
    if (action == "remove") {
        std::string sid = payload.value("spool_id", "");
        if (store && !sid.empty() && store->get_spool(sid)) {
            publish_debug_log("data", "info", "Spool delete queued",
                              "A spool delete request was queued for cloud",
                              {{"action", "remove"}, {"spool_id", sid}});
        }
        if (disp && !sid.empty()) disp->enqueue_push_delete({sid});
        publish_sync_state();
        return MakeResp("spool", action, 0, "", build_spool_list());
    }
    if (action == "batch_remove") {
        std::vector<std::string> ids;
        if (payload.contains("spool_ids")) {
            for (auto& sid : payload["spool_ids"]) ids.push_back(sid.get<std::string>());
        }
        if (store) {
            publish_debug_log("data", "info", "Spool batch delete queued",
                              "Multiple spool delete requests were queued for cloud",
                              {{"action", "batch_remove"},
                               {"count", static_cast<int>(ids.size())},
                               {"spool_ids", ids}});
        }
        if (disp && !ids.empty()) disp->enqueue_push_delete(ids);
        publish_sync_state();
        return MakeResp("spool", action, 0, "", build_spool_list());
    }
    if (action == "mark_empty") {
        std::string sid;
        if (store) {
            const FilamentSpool* sp = store->get_spool(payload.value("spool_id", ""));
            if (sp) {
                FilamentSpool u = *sp;
                u.status = "empty"; u.remain_percent = 0;
                u.cloud_synced = false;
                store->update_spool(u);
                sid = u.spool_id;
                publish_debug_log("data", "info", "Spool marked empty",
                                  "A spool was marked empty in the local store",
                                  {{"action", "mark_empty"}, {"spool_id", sid}});
            }
        }
        if (disp && !sid.empty()) disp->enqueue_push_update(sid, nlohmann::json::object());
        publish_sync_state();
        return MakeResp("spool", action, 0, "", build_spool_list());
    }
    if (action == "toggle_favorite") {
        std::string sid;
        if (store) {
            const FilamentSpool* sp = store->get_spool(payload.value("spool_id", ""));
            if (sp) {
                FilamentSpool u = *sp;
                u.favorite = !u.favorite;
                u.cloud_synced = false;
                store->update_spool(u);
                sid = u.spool_id;
                publish_debug_log("data", "info", "Spool favorite toggled",
                                  "A spool favorite flag changed in the local store",
                                  {{"action", "toggle_favorite"}, {"spool_id", sid}, {"favorite", u.favorite}});
            }
        }
        if (disp && !sid.empty()) disp->enqueue_push_update(sid, nlohmann::json::object());
        publish_sync_state();
        return MakeResp("spool", action, 0, "", build_spool_list());
    }
    if (action == "archive") {
        std::string sid;
        if (store) {
            const FilamentSpool* sp = store->get_spool(payload.value("spool_id", ""));
            if (sp) {
                FilamentSpool u = *sp;
                u.status = "archived";
                u.cloud_synced = false;
                store->update_spool(u);
                sid = u.spool_id;
                publish_debug_log("data", "info", "Spool archived",
                                  "A spool was archived in the local store",
                                  {{"action", "archive"}, {"spool_id", sid}});
            }
        }
        if (disp && !sid.empty()) disp->enqueue_push_update(sid, nlohmann::json::object());
        publish_sync_state();
        return MakeResp("spool", action, 0, "", build_spool_list());
    }

    return MakeResp("spool", action, -1, "unknown action");
}

nlohmann::json FilamentManagerVM::HandleSync(const std::string& action, const nlohmann::json& payload)
{
    auto* disp = wxGetApp().fila_manager_cloud_disp();

    if (action == "status") {
        return MakeResp("sync", action, 0, "", build_sync_state());
    }
    if (action == "pull") {
        if (!disp) return MakeResp("sync", action, -1, "cloud disabled");
        publish_debug_log("data", "info", "Manual pull requested",
                          "The web page requested a cloud pull",
                          {{"action", "pull"}});
        disp->enqueue_pull();
        return MakeResp("sync", action, 0, "", build_sync_state());
    }
    if (action == "push_all_now") {
        auto* cloud = wxGetApp().fila_manager_cloud_sync();
        if (!cloud) return MakeResp("sync", action, -1, "cloud disabled");
        publish_debug_log("data", "info", "Manual push_all_now requested",
                          "The web page requested a manual push of all local spools",
                          {{"action", "push_all_now"}});
        cloud->push_all_now();
        return MakeResp("sync", action, 0, "", build_sync_state());
    }

    return MakeResp("sync", action, -1, "unknown action");
}

nlohmann::json FilamentManagerVM::HandleConfig(const std::string& action, const nlohmann::json& payload)
{
    auto* sync = wxGetApp().fila_manager_cloud_sync();

    if (action == "fetch") {
        const bool force = payload.value("force", false);
        if (!force && !m_cached_cloud_config.empty()) {
            publish_debug_log("data", "info", "Cloud config served from cache",
                              "Returning cached filament config to the web page",
                              {{"action", "config.fetch"}, {"cached", true}});
            return MakeResp("config", action, 0, "cached", m_cached_cloud_config);
        }

        if (!sync) return MakeResp("config", action, -1, "cloud disabled");
        sync->fetch_filament_config([this](const nlohmann::json& cfg) {
            m_cached_cloud_config = cfg;
            publish_debug_log("http", "info", "Cloud config fetched",
                              "Filament config returned from cloud",
                              {{"action", "config.fetch"}, {"keys", cfg.is_object() ? static_cast<int>(cfg.size()) : 0}});
            if (m_bridge) {
                m_bridge->ReportMsg(MakeResp("config", "fetched", 0, "", cfg));
            }
        });
        publish_debug_log("data", "info", "Cloud config fetch requested",
                          "The web page requested filament config from cloud",
                          {{"action", "config.fetch"}, {"cached", false}, {"force", force}});
        return MakeResp("config", action, 0, "pending", nlohmann::json::object());
    }

    return MakeResp("config", action, -1, "unknown action");
}

nlohmann::json FilamentManagerVM::HandlePreset(const std::string& action, const nlohmann::json& payload)
{
    if (action == "list") {
        return MakeResp("preset", action, 0, "", build_preset_options());
    }
    if (action == "create_custom") {
        const std::string vendor = payload.value("vendor", "");
        const std::string type   = payload.value("type", "");
        const std::string serial = payload.value("serial", "");
        const std::string client_request_id = payload.value("client_request_id", "");
        wxGetApp().CallAfter([this, vendor, type, serial, client_request_id]() {
            CreateFilamentWebDialog dlg(wxGetApp().mainframe, vendor, type, serial, true);
            const int res = dlg.ShowModal();
            const bool ok = res == wxID_OK;
            if (ok && wxGetApp().mainframe)
                wxGetApp().mainframe->update_side_preset_ui();
            if (!m_bridge) return;

            nlohmann::json completion = {
                {"ok", ok},
                {"client_request_id", client_request_id},
            };
            if (ok && !dlg.created_filament().is_null() && !dlg.created_filament().empty())
                completion["created"] = dlg.created_filament();
            m_bridge->ReportMsg(MakeResp("preset", "create_custom_done", 0, "", completion));
            if (ok)
                m_bridge->ReportMsg(MakeResp("preset", "list", 0, "", build_preset_options()));
        });
        return MakeResp("preset", action, 0, "pending");
    }
    return MakeResp("preset", action, -1, "unknown action");
}

nlohmann::json FilamentManagerVM::HandleMachine(const std::string& action, const nlohmann::json& payload)
{
    if (action == "list") {
        return MakeResp("machine", action, 0, "", build_machine_list());
    }
    if (action == "request_pushall") {
        std::string dev_id = payload.value("dev_id", "");
        auto* mgr = wxGetApp().getDeviceManager();
        if (!mgr) {
            return MakeResp("machine", action, -1, "device manager not ready");
        }
        MachineObject* obj = nullptr;
        if (!dev_id.empty()) {
            obj = mgr->get_my_machine(dev_id);
        } else if (MachineObject* sel = mgr->get_selected_machine()) {
            obj = sel;
            dev_id = sel->get_dev_id();
        }
        if (!obj) {
            return MakeResp("machine", action, -2, "machine not found");
        }
        obj->command_get_version();
        obj->command_request_push_all(/*request_now=*/true);
        return MakeResp("machine", action, 0, "", {{"dev_id", dev_id}});
    }
    return MakeResp("machine", action, -1, "unknown action");
}

nlohmann::json FilamentManagerVM::HandleAms(const std::string& action, const nlohmann::json& payload)
{
    if (action == "list") {
        std::string dev_id         = payload.value("dev_id", "");
        bool        switch_selected = payload.value("switch_selected", false);
        if (switch_selected && !dev_id.empty()) {
            auto* mgr = wxGetApp().getDeviceManager();
            if (mgr) {
                MachineObject* cur = mgr->get_selected_machine();
                if (!cur || cur->get_dev_id() != dev_id) {
                    mgr->set_selected_machine(dev_id);
                }
            }
        }
        return MakeResp("ams", action, 0, "", build_ams_data());
    }
    return MakeResp("ams", action, -1, "unknown action");
}

nlohmann::json FilamentManagerVM::HandleColors(const std::string& action, const nlohmann::json& payload)
{
    if (action != "query_for_id")
        return MakeResp("colors", action, -1, "unknown action");

    nlohmann::json out;
    const std::string fila_id = payload.value("fila_id", std::string{});
    out["fila_id"]    = fila_id;
    out["fila_type"]  = std::string{};
    out["candidates"] = nlohmann::json::array();

    if (fila_id.empty())
        return MakeResp("colors", action, 0, "", out);

    auto* clr_query = wxGetApp().get_filament_color_code_query();
    if (!clr_query)
        return MakeResp("colors", action, 0, "", out);

    auto* codes = clr_query->GetFilaInfoMap(wxString::FromUTF8(fila_id));
    if (!codes)
        return MakeResp("colors", action, 0, "", out);
    out["fila_type"] = codes->GetFilaType().utf8_string();

    nlohmann::json arr = nlohmann::json::array();
    auto* color_map = codes->GetFilamentColor2CodeMap();
    if (color_map) {
        for (auto& kv : *color_map) {
            const FilamentColor& fc      = kv.first;
            FilamentColorCode*   code    = kv.second;
            if (!code) continue;

            nlohmann::json item;
            item["color_code"] = code->GetFilaColorCode().utf8_string();
            item["name"]       = code->GetFilaColorName().utf8_string();
            item["color_type"] = from_filament_color_type(fc.m_color_type);

            nlohmann::json hex_arr = nlohmann::json::array();
            for (const auto& c : fc.GetColors()) {
                hex_arr.push_back(
                    wxString::Format("#%02X%02X%02X%02X", c.Red(), c.Green(), c.Blue(), c.Alpha()).utf8_string());
            }
            item["colors"] = hex_arr;
            arr.push_back(item);
        }
    }
    out["candidates"] = arr;
    return MakeResp("colors", action, 0, "", out);
}

void FilamentManagerVM::OnSysColorChanged()
{
    if (!m_bridge) return;
    bool dark = wxGetApp().app_config->get("dark_color_mode") == "1";
    nlohmann::json data;
    data["theme"] = dark ? "dark" : "light";
    m_bridge->ReportMsg(MakeResp("init", "theme_changed", 0, "", data));
}

nlohmann::json FilamentManagerVM::MakeResp(
    const std::string& submod, const std::string& action,
    int code, const std::string& msg, const nlohmann::json& payload)
{
    return MakeResponse("filament", submod, action, code, msg, payload);
}

nlohmann::json FilamentManagerVM::build_sync_state() const
{
    auto*        disp  = wxGetApp().fila_manager_cloud_disp();
    auto*        agent = wxGetApp().getAgent();
    const bool   logged_in = agent && agent->is_user_login();
    nlohmann::json s;
    s["logged_in"]      = logged_in;
    s["is_syncing"]     = disp ? disp->is_busy()        : false;
    s["is_pulling"]     = disp ? disp->is_pulling()     : false;
    s["last_synced_at"] = disp ? disp->last_synced_at() : std::string();
    s["last_error"] = {
        {"code",    disp ? disp->last_error_code()    : 0},
        {"message", disp ? disp->last_error_message() : std::string()},
    };
    return s;
}

void FilamentManagerVM::publish_sync_state()
{
    if (!m_bridge) return;
    m_bridge->ReportMsg(MakeResp("sync", "state", 0, "", build_sync_state()));
}

void FilamentManagerVM::publish_pull_done(int added, int updated)
{
    if (!m_bridge) return;
    if (m_pending_pull_added_hint > 0) {
        const int hint = m_pending_pull_added_hint;
        m_pending_pull_added_hint = 0;
        added   += hint;
        updated  = std::max(0, updated - hint);
    }

    nlohmann::json body;
    body["added"]   = added;
    body["updated"] = updated;
    body["state"]   = build_sync_state();
    body["spools"]  = build_spool_list();
    m_bridge->ReportMsg(MakeResp("sync", "pull_done", 0, "", body));
}

void FilamentManagerVM::publish_push_failed(const std::string& spool_id,
                                        const std::string& op,
                                        int code,
                                        const std::string& message)
{
    if (auto* disp = wxGetApp().fila_manager_cloud_disp()) {
        disp->enqueue_pull();
    }
    if (!m_bridge) return;
    nlohmann::json body;
    body["spool_id"] = spool_id;
    body["op"]       = op;
    body["code"]     = code;
    body["message"]  = message;
    body["state"]    = build_sync_state();
    m_bridge->ReportMsg(MakeResp("sync", "push_failed", 0, "", body));
}

void FilamentManagerVM::publish_push_done(const std::string& spool_id, const std::string& op)
{
    if (!m_bridge) return;
    nlohmann::json body;
    body["spool_id"] = spool_id;
    body["op"]       = op;
    body["state"]    = build_sync_state();
    m_bridge->ReportMsg(MakeResp("sync", "push_done", 0, "", body));
}

void FilamentManagerVM::publish_debug_log(const std::string& category,
                                      const std::string& level,
                                      const std::string& title,
                                      const std::string& summary,
                                      const nlohmann::json& detail)
{
    wxGetApp().emit_fila_debug_log(category, level, title, summary, detail);
}

nlohmann::json FilamentManagerVM::build_spool_list()
{
    auto* store = wxGetApp().fila_manager_store();
    auto* agent = wxGetApp().getAgent();
    if (!agent || !agent->is_user_login())
        return nlohmann::json::array();
    nlohmann::json spools = store ? store->spools_to_json() : nlohmann::json::array();
    if (!spools.is_array()) return spools;

    auto* clr_query = wxGetApp().get_filament_color_code_query();
    if (!clr_query) return spools;

    for (auto& sp_json : spools) {
        if (!sp_json.is_object()) continue;
        if (!sp_json.value("color_name", std::string()).empty()) continue;

        const std::string setting_id = sp_json.value("setting_id", std::string());
        if (setting_id.empty()) continue;

        FilamentColor fc;
        if (sp_json.contains("colors") && sp_json["colors"].is_array() && !sp_json["colors"].empty()) {
            for (const auto& hex : sp_json["colors"]) {
                if (!hex.is_string()) continue;
                const std::string h = hex.get<std::string>();
                if (h.size() > 3) fc.AddColor(wxColour(wxString::FromUTF8(h)));
            }
        } else {
            const std::string code = sp_json.value("color_code", std::string());
            if (code.size() > 3) fc.AddColor(wxColour(wxString::FromUTF8(code)));
        }
        if (fc.GetColors().empty()) continue;

        fc.m_color_type = to_filament_color_type(sp_json.value("color_type", 2), fc.ColorCount());

        wxString name = clr_query->GetFilaColorName(wxString::FromUTF8(setting_id), fc);
        if (!name.IsEmpty())
            sp_json["color_name"] = name.utf8_string();
    }
    return spools;
}

nlohmann::json FilamentManagerVM::build_preset_options()
{
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return {{"vendors", nlohmann::json::array()}};
    auto& filaments = bundle->filaments;
    std::map<std::string, std::map<std::string, std::vector<nlohmann::json>>> vendor_type_items;
    std::set<std::string> filament_id_set;
    for (auto it = filaments.begin(); it != filaments.end(); ++it) {
        Preset& preset = *it;
        const bool is_user_defined = !preset.is_system && !preset.is_default;
        if (!is_user_defined && filaments.get_preset_base(*it) != &preset)
            continue;

        std::string vendor = it->config.get_filament_vendor();
        std::string type   = it->config.get_filament_type();
        if (vendor.empty()) continue;
        if (type.empty()) type = "Other";
        const std::string dedupe_key = is_user_defined
            ? (vendor + "\n" + type + "\n" + it->name)
            : (it->filament_id.empty() ? (vendor + "\n" + type + "\n" + it->name) : it->filament_id);
        if (!filament_id_set.insert(dedupe_key).second)
            continue;

        std::string shown_name = filaments.get_preset_alias(*it, true);
        if (shown_name.empty())
            shown_name = it->display_name();
        if (shown_name.empty())
            continue;
        nlohmann::json entry = {
            {"name", shown_name},
            {"series", shown_name},
            {"filament_id", it->filament_id},
            {"setting_id", it->setting_id}
        };
        if (is_user_defined) entry["is_user"] = true;
        vendor_type_items[vendor][type].push_back(entry);
    }

    nlohmann::json vendors_arr = nlohmann::json::array();
    for (auto& [vname, type_map] : vendor_type_items) {
        nlohmann::json types_arr = nlohmann::json::array();
        for (auto& [tname, items] : type_map) {
            nlohmann::json series_arr = nlohmann::json::array();
            nlohmann::json items_arr  = nlohmann::json::array();
            for (const auto& item : items) {
                const std::string name = item.value("name", "");
                if (!name.empty()) series_arr.push_back(name);
                items_arr.push_back(item);
            }
            types_arr.push_back({{"name", tname}, {"series", series_arr}, {"items", items_arr}});
        }
        vendors_arr.push_back({{"name", vname}, {"types", types_arr}});
    }

    return {{"vendors", vendors_arr}};
}

nlohmann::json FilamentManagerVM::build_machine_list()
{
    nlohmann::json result = {{"machines", nlohmann::json::array()},
                             {"selected_dev_id", ""}};
    try {
        auto* dev_mgr = wxGetApp().getDeviceManager();
        if (!dev_mgr) return result;

        std::map<std::string, MachineObject*> ml = dev_mgr->get_my_machine_list();
        for (auto& [id, obj] : dev_mgr->get_user_machinelist()) {
            if (obj && ml.find(id) == ml.end()) ml[id] = obj;
        }

        nlohmann::json arr = nlohmann::json::array();
        for (auto& [id, obj] : ml) {
            if (!obj) continue;
            std::string name = obj->get_dev_name();
            if (name.empty()) name = id;
            arr.push_back({{"dev_id", id},
                           {"dev_name", name},
                           {"is_online", obj->is_online()},
                           {"is_lan", obj->is_lan_mode_printer()}});
        }
        result["machines"] = arr;

        MachineObject* sel = dev_mgr->get_selected_machine();
        if (sel) result["selected_dev_id"] = sel->get_dev_id();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[FilamentVM] build_machine_list: " << e.what();
    }
    return result;
}

nlohmann::json FilamentManagerVM::build_ams_data()
{
    nlohmann::json empty = {{"selected_dev_id", ""},
                            {"ams_units", nlohmann::json::array()},
                            {"ext_slots", nlohmann::json::array()},
                            {"is_rack", false}};
    try {
        auto* dev_mgr = wxGetApp().getDeviceManager();
        if (!dev_mgr) return empty;

        MachineObject* sel = dev_mgr->get_selected_machine();
        std::string sel_id = sel ? sel->get_dev_id() : "";
        bool is_rack = false;
        if (auto rack = sel ? sel->GetNozzleRack() : nullptr)
            is_rack = rack->IsDirectConnectedRack();

        nlohmann::json ams_arr = nlohmann::json::array();
        nlohmann::json ext_arr = nlohmann::json::array();
        if (sel) {
            auto fila_sys = sel->GetFilaSystem();
            if (fila_sys) {
                for (auto& [ams_id, ams] : fila_sys->GetAmsList()) {
                    if (!ams) continue;
                    nlohmann::json trays = nlohmann::json::array();
                    const int slot_count = ams->GetSlotCount();

                    const DevAmsType raw_type = ams->IsAmsLiteMixed()
                        ? DevAmsType::AMS_LITE_MIXED
                        : ams->GetAmsType();

                    for (int i = 0; i < slot_count; ++i) {
                        const std::string slot_id = std::to_string(i);
                        DevAmsTray* tray = ams->GetTray(slot_id);
                        if (!tray) continue;
                        std::string name;
                        try {
                            name = DevFilaSystem::GetTrayNameByAmsSlotId(sel_id, std::stoi(ams_id), i, raw_type);
                        } catch (...) {
                            name = wxGetApp().transition_tridid(ams->GetTrayId(i), std::nullopt, sel).ToStdString();
                        }
                        trays.push_back(build_tray_json(
                            tray, slot_id, name, tray->is_exists));
                    }
                    ams_arr.push_back({{"ams_id", ams_id},
                                       {"ams_type", static_cast<int>(ams->GetAmsType())},
                                       {"display_name", wx_to_utf8(ams->GetDisplayName())},
                                       {"trays", trays}});
                }

                const auto& vt_slots = fila_sys->GetVirtualTrays();
                for (auto it = vt_slots.crbegin(); it != vt_slots.crend(); ++it) {
                    const DevAmsTray& vt_tray = it->second;
                    const std::string ams_id = vt_tray.id.empty() ? it->first : vt_tray.id;
                    std::string label;
                    try {
                        const int vt_id_int = std::stoi(ams_id);
                        label = DevFilaSystem::GetTrayNameByAmsSlotId(sel_id, vt_id_int, vt_id_int, DevAmsType::EXT_SPOOL);
                    } catch (...) {
                        label = vt_tray.get_slot_name(sel);
                    }
                    if (label == "Ext-L" || label == "Ext-R") label = "Ext";
                    const bool exists = vt_tray.is_exists || vt_tray.is_tray_info_ready();
                    nlohmann::json t = build_tray_json(&vt_tray, "0", label, exists);
                    t["ams_id"] = ams_id;
                    ext_arr.push_back(t);
                }
            }
        }
        return {{"selected_dev_id", sel_id},
                {"ams_units", ams_arr},
                {"ext_slots", ext_arr},
                {"is_rack", is_rack}};
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[FilamentVM] build_ams_data: " << e.what();
        return empty;
    }
}

}} // namespace Slic3r::GUI
