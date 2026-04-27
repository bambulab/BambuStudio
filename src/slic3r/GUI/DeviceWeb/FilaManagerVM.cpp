// src/slic3r/GUI/DeviceWeb/FilaManagerVM.cpp
#include "FilaManagerVM.hpp"
#include "DeviceWebBridge.hpp"

#include <algorithm>
#include <boost/log/trivial.hpp>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/fila_manager/wgtFilaManagerStore.h"
#include "slic3r/GUI/fila_manager/wgtFilaManagerCloudClient.h"
#include "slic3r/GUI/fila_manager/wgtFilaManagerCloudSync.h"
#include "slic3r/GUI/fila_manager/wgtFilaManagerCloudDispatcher.h"
#include "libslic3r/PresetBundle.hpp"

#include <wx/app.h>

namespace Slic3r { namespace GUI {

FilaManagerVM::FilaManagerVM()
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
        // Cloud is the source of truth: whenever a local push succeeds we
        // immediately enqueue a pull so the next spool list the web sees is
        // whatever the cloud just acknowledged. The dispatcher is single-slot
        // so the pull runs right after this push op returns to idle.
        disp->set_on_push_done([this](const std::string& id,
                                      const std::string& op) {
            if (m_bridge) {
                m_bridge->ReportMsg(MakeResp("spool", "list", 0, "", build_spool_list()));
            }
            publish_push_done(id, op);
            // STUDIO-17956: remember that a new spool was just pushed to the
            // cloud. The reconciliation pull enqueued below will observe
            // before==after locally (add_spool is local-first), so its
            // size-diff estimate of `added` is always 0. Carry the real
            // delta into publish_pull_done via this hint.
            if (op == "create") {
                m_pending_pull_added_hint += 1;
            }
            if (auto* d = wxGetApp().fila_manager_cloud_disp()) {
                d->enqueue_pull();
            }
        });
    }
}

FilaManagerVM::~FilaManagerVM()
{
#if !BBL_RELEASE_TO_PUBLIC
    wxGetApp().set_fila_debug_sink(nullptr);
#endif
    // Detach observer callbacks — we don't want the dispatcher to hold a
    // dangling this-pointer when VM dies before GUI_App shuts down.
    if (auto* disp = wxGetApp().fila_manager_cloud_disp()) {
        disp->set_on_state_changed(nullptr);
        disp->set_on_pull_done(nullptr);
        disp->set_on_push_failed(nullptr);
        disp->set_on_push_done(nullptr);
    }
}

nlohmann::json FilaManagerVM::OnCommand(
    const std::string& submod,
    const std::string& action,
    const nlohmann::json& payload)
{
    BOOST_LOG_TRIVIAL(info) << "FilaManagerVM::OnCommand submod=" << submod
                            << " action=" << action;

    if (submod == "init")          return HandleInit(action, payload);
    if (submod == "spool")         return HandleSpool(action, payload);
    if (submod == "preset")        return HandlePreset(action, payload);
    if (submod == "machine")       return HandleMachine(action, payload);
    if (submod == "ams")           return HandleAms(action, payload);
    if (submod == "sync")          return HandleSync(action, payload);
    if (submod == "config")        return HandleConfig(action, payload);

    return MakeResp(submod, action, -1, "unknown submod");
}

void FilaManagerVM::ReportState(const std::string& submod, const std::string& action)
{
    if (!m_bridge) return;

    if (submod == "spool") {
        nlohmann::json body = MakeResp("spool", "list", 0, "", build_spool_list());
        m_bridge->ReportMsg(body);
        return;
    }

    // F4.7: broadcast the Studio-wide machine selection change so the
    // "Read from AMS" dialog can stay in sync with whatever printer the
    // rest of Studio is pointing at (triggered by
    // DeviceManager::OnSelectedMachineChanged).
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

/* ================================================================
 *  Resource handlers
 * ================================================================ */

nlohmann::json FilaManagerVM::HandleInit(const std::string& action, const nlohmann::json& /*payload*/)
{
    bool dark = wxGetApp().app_config->get("dark_color_mode") == "1";
    nlohmann::json data;
    data["theme"]   = dark ? "dark" : "light";
    data["spools"]  = build_spool_list();
    data["presets"] = build_preset_options();
    // Include current login/sync state so the frontend can render the correct
    // UI immediately without waiting for a separate fetchCloudSyncStatus() call.
    data["cloud_sync"] = build_sync_state();
#if !BBL_RELEASE_TO_PUBLIC
    data["debug_enabled"] = true;
#else
    data["debug_enabled"] = false;
#endif
    return MakeResp("init", action, 0, "", data);
}

nlohmann::json FilaManagerVM::HandleSpool(const std::string& action, const nlohmann::json& payload)
{
    auto* store = wxGetApp().fila_manager_store();
    auto* disp  = wxGetApp().fila_manager_cloud_disp();

    if (action == "list") {
        return MakeResp("spool", action, 0, "", build_spool_list());
    }
    if (action == "add") {
        // Breadcrumbs: when the UI reports "add didn't take effect / no HTTP
        // fired", the debug log lets us see which of (parse / insert / save /
        // enqueue) silently threw. Each step is wrapped in its own try/catch
        // so a failure in save() still lets us return a response to the web
        // layer instead of stalling the UI for 5s.
        publish_debug_log("data", "info", "HandleSpool add: enter",
                          "Begin processing spool/add on C++ side",
                          {{"has_store", store != nullptr}});

        std::string created_id;
        if (store) {
            FilamentSpool s;
            try {
                s = FilamentSpool::from_json(payload);
            } catch (const std::exception& e) {
                publish_debug_log("data", "error", "HandleSpool add: from_json threw",
                                  "Failed to parse incoming spool payload",
                                  {{"what", e.what()}, {"payload", payload}});
                return MakeResp("spool", action, 3, std::string("from_json: ") + e.what());
            }
            // Freshly added locally -> not yet acknowledged by cloud.
            // The dispatcher flips cloud_synced=true after the push succeeds,
            // and the follow-up auto-pull then rewrites the row from the
            // cloud snapshot (which is the source of truth).
            s.cloud_synced = false;

            try {
                created_id = store->add_spool(s);
            } catch (const std::exception& e) {
                publish_debug_log("data", "error", "HandleSpool add: add_spool threw",
                                  "Failed to insert spool into in-memory store",
                                  {{"what", e.what()}});
                return MakeResp("spool", action, 4, std::string("add_spool: ") + e.what());
            }

            try {
                store->save();
            } catch (const std::exception& e) {
                publish_debug_log("data", "error", "HandleSpool add: save threw",
                                  "Failed to persist spool store to disk (often non-UTF8 bytes in a legacy row)",
                                  {{"what", e.what()}, {"new_spool_id", created_id}});
                // Row is already in memory; we still try to enqueue the
                // cloud push so the user's intent is not lost.
            }

            publish_debug_log("data", "info", "Local spool added",
                              "A new spool was saved to the local store",
                              {{"action", "add"}, {"spool_id", created_id}});
        }
        if (disp && !created_id.empty()) disp->enqueue_push_create(created_id);
        publish_sync_state();
        return MakeResp("spool", action, 0, "", build_spool_list());
    }
    if (action == "batch_add") {
        std::vector<std::string> new_ids;
        if (store) {
            int qty = payload.value("quantity", 1);
            nlohmann::json sd = payload.contains("spool") ? payload["spool"] : nlohmann::json::object();
            for (int i = 0; i < qty; ++i) {
                FilamentSpool s = FilamentSpool::from_json(sd);
                s.cloud_synced = false;
                new_ids.push_back(store->add_spool(s));
            }
            store->save();
            publish_debug_log("data", "info", "Local spools batch added",
                              "Multiple spools were saved to the local store",
                              {{"action", "batch_add"}, {"count", qty}, {"spool_ids", new_ids}});
        }
        if (disp) for (auto& id : new_ids) disp->enqueue_push_create(id);
        publish_sync_state();
        return MakeResp("spool", action, 0, "", build_spool_list());
    }
    if (action == "update") {
        // 前端应当只传 {spool_id, 本次变更的字段...}。做 selective merge，
        // 不再整体覆盖 —— 这样 entry_method / tag_uid / created_at / bound_* /
        // cloud_synced 等系统字段不会被清空（STUDIO-17964 Problem A）。
        const std::string updated_id = payload.value("spool_id", "");
        bool patched = false;
        if (store && !updated_id.empty()) {
            patched = store->apply_patch(updated_id, payload);
            if (patched) {
                store->save();
                publish_debug_log("data", "info", "Local spool updated",
                                  "A spool was updated in the local store (patch)",
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
        if (disp && patched) disp->enqueue_push_update(updated_id, payload);
        publish_sync_state();
        return MakeResp("spool", action, 0, "", build_spool_list());
    }
    if (action == "remove") {
        std::string sid = payload.value("spool_id", "");
        if (store && !sid.empty()) {
            store->remove_spool(sid); store->save();
            publish_debug_log("data", "info", "Local spool removed",
                              "A spool was deleted from the local store",
                              {{"action", "remove"}, {"spool_id", sid}});
        }
        // Always try to delete on the cloud. If the row was never pushed
        // the cloud returns 404 which the dispatcher just logs -- the
        // subsequent auto-pull brings local and cloud back in sync anyway.
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
            for (auto& id : ids) store->remove_spool(id);
            store->save();
            publish_debug_log("data", "info", "Local spools batch removed",
                              "Multiple spools were deleted from the local store",
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
                store->update_spool(u); store->save();
                sid = u.spool_id;
                publish_debug_log("data", "info", "Spool marked empty",
                                  "A spool was marked empty in the local store",
                                  {{"action", "mark_empty"}, {"spool_id", sid}});
            }
        }
        // status / remain_percent 都是本地专有字段，不在云端 Update 白名单内：
        // 传空 patch，dispatcher 会直接把本条标记为 cloud_synced，不发空 PUT。
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
                store->update_spool(u); store->save();
                sid = u.spool_id;
                publish_debug_log("data", "info", "Spool favorite toggled",
                                  "A spool favorite flag changed in the local store",
                                  {{"action", "toggle_favorite"}, {"spool_id", sid}, {"favorite", u.favorite}});
            }
        }
        // favorite 仅本地字段，传空 patch；dispatcher 会跳过 PUT。
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
                store->update_spool(u); store->save();
                sid = u.spool_id;
                publish_debug_log("data", "info", "Spool archived",
                                  "A spool was archived in the local store",
                                  {{"action", "archive"}, {"spool_id", sid}});
            }
        }
        // status 仅本地字段（"archived"），传空 patch；dispatcher 会跳过 PUT。
        if (disp && !sid.empty()) disp->enqueue_push_update(sid, nlohmann::json::object());
        publish_sync_state();
        return MakeResp("spool", action, 0, "", build_spool_list());
    }

    return MakeResp("spool", action, -1, "unknown action");
}

nlohmann::json FilaManagerVM::HandleSync(const std::string& action, const nlohmann::json& payload)
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

    return MakeResp("sync", action, -1, "unknown action");
}

nlohmann::json FilaManagerVM::HandleConfig(const std::string& action, const nlohmann::json& /*payload*/)
{
    auto* sync = wxGetApp().fila_manager_cloud_sync();

    if (action == "fetch") {
        // If we already have cached config, return immediately (frontend can
        // always pass force=true via payload to re-fetch; not implemented here
        // because the backing client has no force flag).
        if (!m_cached_cloud_config.empty()) {
            publish_debug_log("data", "info", "Cloud config served from cache",
                              "Returning cached filament config to the web page",
                              {{"action", "config.fetch"}, {"cached", true}});
            return MakeResp("config", action, 0, "cached", m_cached_cloud_config);
        }

        if (!sync) return MakeResp("config", action, -1, "cloud disabled");

        // fetch_filament_config is async; we synthesize an immediate "pending"
        // response, then report the actual config via a 'config/fetched'
        // report when the HTTP call returns.
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
                          {{"action", "config.fetch"}, {"cached", false}});
        return MakeResp("config", action, 0, "pending", nlohmann::json::object());
    }

    return MakeResp("config", action, -1, "unknown action");
}

nlohmann::json FilaManagerVM::HandlePreset(const std::string& action, const nlohmann::json& /*payload*/)
{
    if (action == "list") {
        return MakeResp("preset", action, 0, "", build_preset_options());
    }
    return MakeResp("preset", action, -1, "unknown action");
}

nlohmann::json FilaManagerVM::HandleMachine(const std::string& action, const nlohmann::json& payload)
{
    if (action == "list") {
        return MakeResp("machine", action, 0, "", build_machine_list());
    }
    if (action == "request_pushall") {
        // User pressed the refresh button next to the Printer dropdown in the
        // "Read from AMS" dialog. Mirror what SelectMachineDialog does when a
        // printer is picked: ask the device to resend its full state package
        // (get_version + pushall) so the dialog's AMS tray snapshot converges
        // on the current tray RFID / weight / remain without having to wait
        // for the next spontaneous push.
        //
        // This is read-only with respect to Studio's globally selected
        // machine: we do NOT call set_selected_machine here. A separate
        // ams/list { switch_selected:true } call is responsible for that and
        // it is only sent when the user actually picks a different printer in
        // the dropdown.
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

nlohmann::json FilaManagerVM::HandleAms(const std::string& action, const nlohmann::json& payload)
{
    if (action == "list") {
        // ams/list has two callers:
        //   1) User picks a different printer in the "Read from AMS"
        //      dropdown -> frontend sends { dev_id, switch_selected:true }
        //      and we actually call DeviceManager::set_selected_machine,
        //      which changes Studio's globally selected machine.
        //   2) The 1.5s poll while the dialog is open / the mirror of an
        //      external machine switch / any pure tray-snapshot refresh ->
        //      frontend omits switch_selected, and we only return the AMS
        //      snapshot of whatever machine is currently selected without
        //      touching global state.
        // Before this split, every poll tick carrying dev_id fell through
        // to set_selected_machine and flooded the log with "set current
        // printer" requests.
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

/* ================================================================
 *  Theme
 * ================================================================ */

void FilaManagerVM::OnSysColorChanged()
{
    if (!m_bridge) return;
    bool dark = wxGetApp().app_config->get("dark_color_mode") == "1";
    nlohmann::json data;
    data["theme"] = dark ? "dark" : "light";
    m_bridge->ReportMsg(MakeResp("init", "theme_changed", 0, "", data));
}

/* ================================================================
 *  Helper
 * ================================================================ */

nlohmann::json FilaManagerVM::MakeResp(
    const std::string& submod, const std::string& action,
    int code, const std::string& msg, const nlohmann::json& payload)
{
    return MakeResponse("filament", submod, action, code, msg, payload);
}

/* ================================================================
 *  Cloud sync state
 * ================================================================ */

nlohmann::json FilaManagerVM::build_sync_state() const
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

void FilaManagerVM::publish_sync_state()
{
    if (!m_bridge) return;
    m_bridge->ReportMsg(MakeResp("sync", "state", 0, "", build_sync_state()));
}

void FilaManagerVM::publish_pull_done(int added, int updated)
{
    if (!m_bridge) return;

    // STUDIO-17956: dispatcher's size-diff `added` is always 0 for the
    // reconciliation pull that follows a local add+push_create (add_spool is
    // local-first, so before_sz == after_sz). Each successful create push
    // bumps m_pending_pull_added_hint by 1; consume it here so the toast
    // reports the true "+N added" instead of "+0 added". Shift the delta out
    // of `updated` too — the same spools count as either freshly-added or
    // already-up-to-date, not both.
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

void FilaManagerVM::publish_push_failed(const std::string& spool_id,
                                        const std::string& op,
                                        int code,
                                        const std::string& message)
{
    if (!m_bridge) return;
    nlohmann::json body;
    body["spool_id"] = spool_id;
    body["op"]       = op;
    body["code"]     = code;
    body["message"]  = message;
    m_bridge->ReportMsg(MakeResp("sync", "push_failed", 0, "", body));
}

void FilaManagerVM::publish_push_done(const std::string& spool_id, const std::string& op)
{
    if (!m_bridge) return;
    nlohmann::json body;
    body["spool_id"] = spool_id;
    body["op"]       = op;
    body["state"]    = build_sync_state();
    m_bridge->ReportMsg(MakeResp("sync", "push_done", 0, "", body));
}

void FilaManagerVM::publish_debug_log(const std::string& category,
                                      const std::string& level,
                                      const std::string& title,
                                      const std::string& summary,
                                      const nlohmann::json& detail)
{
    wxGetApp().emit_fila_debug_log(category, level, title, summary, detail);
}

/* ================================================================
 *  Data builders (migrated from wgtFilaManagerPanel)
 * ================================================================ */

nlohmann::json FilaManagerVM::build_spool_list()
{
    auto* store = wxGetApp().fila_manager_store();
    auto* agent = wxGetApp().getAgent();
    if (!agent || !agent->is_user_login())
        return nlohmann::json::array();
    return store ? store->spools_to_json() : nlohmann::json::array();
}

nlohmann::json FilaManagerVM::build_preset_options()
{
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return {{"vendors", nlohmann::json::array()}};

    std::map<std::string, std::map<std::string, std::set<std::string>>> vendor_type_series;
    std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> vendor_type_series_ids;
    for (auto it = bundle->filaments.begin(); it != bundle->filaments.end(); ++it) {
        std::string vendor = it->config.get_filament_vendor();
        std::string type   = it->config.get_filament_type();
        if (vendor.empty()) continue;
        if (type.empty()) type = "Other";

        std::string dname = it->display_name();
        std::string series;
        if (dname.size() > type.size() && dname.compare(0, type.size(), type) == 0) {
            series = dname.substr(type.size());
            size_t start = series.find_first_not_of(' ');
            if (start != std::string::npos) series = series.substr(start);
            else series.clear();
        }
        const std::string normalized_series = series.empty() ? "" : series;
        vendor_type_series[vendor][type].insert(normalized_series);
        vendor_type_series_ids[vendor][type].emplace(normalized_series, it->filament_id);
    }

    nlohmann::json vendors_arr = nlohmann::json::array();
    for (auto& [vname, type_map] : vendor_type_series) {
        nlohmann::json types_arr = nlohmann::json::array();
        for (auto& [tname, series_set] : type_map) {
            nlohmann::json series_arr = nlohmann::json::array();
            nlohmann::json items_arr  = nlohmann::json::array();
            for (auto& s : series_set)
            {
                if (!s.empty()) series_arr.push_back(s);
                std::string filament_id;
                if (auto vendor_it = vendor_type_series_ids.find(vname); vendor_it != vendor_type_series_ids.end()) {
                    if (auto type_it = vendor_it->second.find(tname); type_it != vendor_it->second.end()) {
                        if (auto series_it = type_it->second.find(s); series_it != type_it->second.end())
                            filament_id = series_it->second;
                    }
                }
                items_arr.push_back({{"series", s}, {"filament_id", filament_id}});
            }
            types_arr.push_back({{"name", tname}, {"series", series_arr}, {"items", items_arr}});
        }
        vendors_arr.push_back({{"name", vname}, {"types", types_arr}});
    }
    return {{"vendors", vendors_arr}};
}

nlohmann::json FilaManagerVM::build_machine_list()
{
    // F4.7: include the Studio-wide selected machine so the web "Read from
    // AMS" tab can default to the same printer the rest of Studio is using,
    // instead of picking the first online device in the list.
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
            // is_lan mirrors SelectMachineDialog's "<name>(LAN)" convention so
            // the web Printer dropdown can label LAN-mode printers the same
            // way the native Send-to-printer dialog does.
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

nlohmann::json FilaManagerVM::build_ams_data()
{
    nlohmann::json empty = {{"selected_dev_id", ""}, {"ams_units", nlohmann::json::array()}};
    try {
        auto* dev_mgr = wxGetApp().getDeviceManager();
        if (!dev_mgr) return empty;

        MachineObject* sel = dev_mgr->get_selected_machine();
        std::string sel_id = sel ? sel->get_dev_id() : "";

        nlohmann::json ams_arr = nlohmann::json::array();
        if (sel) {
            auto fila_sys = sel->GetFilaSystem();
            if (fila_sys) {
                for (auto& [ams_id, ams] : fila_sys->GetAmsList()) {
                    if (!ams) continue;
                    nlohmann::json trays = nlohmann::json::array();
                    for (auto& [slot_id, tray] : ams->GetTrays()) {
                        nlohmann::json t;
                        t["slot_id"]   = slot_id;
                        t["is_exists"] = tray && tray->is_exists;
                        if (tray && tray->is_exists) {
                            t["tag_uid"]    = tray->tag_uid;
                            t["setting_id"] = tray->setting_id;
                            t["fila_type"]  = tray->m_fila_type;
                            t["sub_brands"] = tray->sub_brands;
                            std::string color = tray->color;
                            if (!color.empty() && color[0] != '#') color = "#" + color;
                            t["color"]    = color;
                            t["weight"]   = tray->weight;
                            t["remain"]   = tray->remain;
                            t["diameter"] = tray->diameter;
                            t["is_bbl"]   = tray->is_bbl;
                        }
                        trays.push_back(t);
                    }
                    ams_arr.push_back({{"ams_id", ams_id}, {"ams_type", static_cast<int>(ams->GetAmsType())}, {"trays", trays}});
                }
            }
        }
        return {{"selected_dev_id", sel_id}, {"ams_units", ams_arr}};
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[FilamentVM] build_ams_data: " << e.what();
        return empty;
    }
}

}} // namespace Slic3r::GUI
