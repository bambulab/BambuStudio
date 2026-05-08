#include "wgtFilaManagerPanel.h"
#include "wgtFilaManagerStore.h"

#include <wx/sizer.h>
#include <boost/log/trivial.hpp>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevConfigUtil.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <iomanip>
#include <sstream>

namespace Slic3r { namespace GUI {

/* ================================================================
 *  Construction / lifecycle
 * ================================================================ */

wgtFilaManagerPanel::wgtFilaManagerPanel(wxWindow* parent, wxWindowID id,
    const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    wxString strlang = wxGetApp().current_language_code_safe();
    if (!strlang.empty())
        m_home_url = wxString::Format("file://%s/web/fila_manager/index.html?lang=%s",
            from_u8(resources_dir()), strlang);
    else
        m_home_url = wxString::Format("file://%s/web/fila_manager/index.html",
            from_u8(resources_dir()));

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    m_browser = WebView::CreateWebView(this, m_home_url);
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "[FilaManager] Failed to create WebView";
        SetSizer(sizer);
        return;
    }

#if !BBL_RELEASE_TO_PUBLIC
    m_browser->EnableAccessToDevTools(true);
    m_browser->EnableContextMenu(true);
#endif

    sizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    m_browser->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED,
        &wgtFilaManagerPanel::OnWebMsg, this, m_browser->GetId());
    m_browser->Bind(wxEVT_WEBVIEW_LOADED,
        [this](wxWebViewEvent&) { InitBridge(); });

    register_handlers();

    SetSizer(sizer);
    Layout();
    Fit();
}

wgtFilaManagerPanel::~wgtFilaManagerPanel() {}

void wgtFilaManagerPanel::InitBridge()
{
    static const std::string bridge = R"JS(
        if (!window.__cppPush) {
            window.__cppPush = function(pkt) {
                try {
                    document.dispatchEvent(
                        new CustomEvent('cpp:fila', { detail: pkt })
                    );
                } catch(e) { console.error('[FM] __cppPush error:', e); }
            };
        }
    )JS";

    WebView::RunScript(m_browser, wxString(bridge));
    BOOST_LOG_TRIVIAL(info) << "[FilaManager] bridge injected";

    if (!m_bridge_ready) {
        m_bridge_ready = true;
        flush_msg_queue();
    }
}

void wgtFilaManagerPanel::msw_rescale() {}

void wgtFilaManagerPanel::on_sys_color_changed()
{
    if (m_bridge_ready) {
        bool dark = wxGetApp().app_config->get("dark_color_mode") == "1";
        push_to_web("theme_changed", {{"theme", dark ? "dark" : "light"}});
    }
}

bool wgtFilaManagerPanel::Show(bool show)
{
    if (show && m_bridge_ready) {
        auto* store = wxGetApp().fila_manager_store();
        if (store && store->is_dirty()) {
            push_to_web("spool_list", build_spool_list());
            store->clear_dirty();
        }
    }
    return wxPanel::Show(show);
}

/* ================================================================
 *  C++ → JS transport
 * ================================================================ */

void wgtFilaManagerPanel::SendMsg(nlohmann::json msg)
{
    msg["v"]  = FM_PROTOCOL_VERSION;
    msg["ts"] = now_ms();

    std::string js = "window.__cppPush(" + msg.dump() + ");";

    if (!m_bridge_ready) {
        m_msg_queue.push_back(js);
        BOOST_LOG_TRIVIAL(info) << "[FilaManager] queued (bridge not ready), queue=" << m_msg_queue.size();
        return;
    }

    if (!m_browser) {
        BOOST_LOG_TRIVIAL(error) << "[FilaManager] SendMsg: m_browser is null";
        return;
    }

    CallAfter([this, js] {
        if (!m_browser) return;
        if (!WebView::RunScript(m_browser, js))
            BOOST_LOG_TRIVIAL(error) << "[FilaManager] SendMsg RunScript failed";
    });
}

void wgtFilaManagerPanel::flush_msg_queue()
{
    if (m_msg_queue.empty()) return;
    BOOST_LOG_TRIVIAL(info) << "[FilaManager] flushing " << m_msg_queue.size() << " queued messages";
    for (auto& js : m_msg_queue) {
        CallAfter([this, js] {
            if (!m_browser) return;
            WebView::RunScript(m_browser, js);
        });
    }
    m_msg_queue.clear();
}

void wgtFilaManagerPanel::send_response(int seq, int code, const nlohmann::json& data)
{
    BOOST_LOG_TRIVIAL(info) << "[FilaManager] → response seq=" << seq << " code=" << code;
    SendMsg({{"type", "response"}, {"seq", seq}, {"code", code}, {"data", data}});
}

void wgtFilaManagerPanel::push_to_web(const std::string& command, const nlohmann::json& data)
{
    BOOST_LOG_TRIVIAL(info) << "[FilaManager] → push cmd=" << command;
    SendMsg({{"type", "push"}, {"command", command}, {"data", data}});
}

/* ================================================================
 *  JS → C++ message dispatch
 * ================================================================ */

void wgtFilaManagerPanel::OnWebMsg(wxWebViewEvent& evt)
{
    try {
        // ToStdString() uses the current C locale, which on Windows CJK
        // builds mangles non-ASCII characters (Chinese notes, emoji, etc.)
        // before the JSON parser sees them. Force UTF-8 to match the
        // C++->JS direction (SendMsg builds the script via nlohmann dump
        // and needs the request round-trip to stay in UTF-8).
        nlohmann::json j = nlohmann::json::parse(evt.GetString().ToUTF8().data());
        std::string type = j.value("type", "");

        if (type != "request") {
            BOOST_LOG_TRIVIAL(warning) << "[FilaManager] unexpected msg type: " << type;
            return;
        }

        int         seq = j.value("seq", 0);
        std::string cmd = j.value("command", "");
        nlohmann::json data = j.contains("data") ? j["data"] : nlohmann::json::object();
        BOOST_LOG_TRIVIAL(info) << "[FilaManager] ← request seq=" << seq << " cmd=" << cmd;

        auto it = m_handlers.find(cmd);
        if (it != m_handlers.end()) {
            try {
                it->second(seq, data);
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "[FilaManager] handler error cmd=" << cmd << ": " << e.what();
                send_response(seq, -1, {{"error", std::string("internal error: ") + e.what()}});
            }
        } else {
            BOOST_LOG_TRIVIAL(warning) << "[FilaManager] unknown cmd=" << cmd;
            send_response(seq, -1, {{"error", "unknown command: " + cmd}});
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[FilaManager] Bad JS message: " << e.what();
    }
}

/* ================================================================
 *  Command registry — each handler is a self-contained lambda
 * ================================================================ */

void wgtFilaManagerPanel::register_handlers()
{
    m_handlers["init"] = [this](int seq, const nlohmann::json& /*data*/) {
        if (!m_bridge_ready) m_bridge_ready = true;

        bool dark = wxGetApp().app_config->get("dark_color_mode") == "1";
        nlohmann::json result;
        result["theme"]   = dark ? "dark" : "light";
        result["spools"]  = build_spool_list();
        result["presets"] = build_preset_options();
        send_response(seq, 0, result);
        flush_msg_queue();
    };

    m_handlers["get_spool_list"] = [this](int seq, const nlohmann::json&) {
        send_response(seq, 0, build_spool_list());
    };

    m_handlers["get_preset_options"] = [this](int seq, const nlohmann::json&) {
        send_response(seq, 0, build_preset_options());
    };

    m_handlers["get_machine_list"] = [this](int seq, const nlohmann::json&) {
        send_response(seq, 0, build_machine_list());
    };

    m_handlers["get_ams_data"] = [this](int seq, const nlohmann::json& data) {
        std::string dev_id = data.value("dev_id", "");
        if (!dev_id.empty()) {
            auto* mgr = wxGetApp().getDeviceManager();
            if (mgr) mgr->set_selected_machine(dev_id);
        }
        send_response(seq, 0, build_ams_data());
    };

    /* ---- Spool CRUD ---- */

    auto respond_spool_list = [this](int seq) {
        send_response(seq, 0, build_spool_list());
    };

    m_handlers["add_spool"] = [this, respond_spool_list](int seq, const nlohmann::json& data) {
        auto* store = wxGetApp().fila_manager_store();
        if (store) { store->add_spool(FilamentSpool::from_json(data)); store->save(); }
        respond_spool_list(seq);
    };

    m_handlers["batch_add"] = [this, respond_spool_list](int seq, const nlohmann::json& data) {
        auto* store = wxGetApp().fila_manager_store();
        if (store) {
            int qty = data.value("quantity", 1);
            nlohmann::json sd = data.contains("spool") ? data["spool"] : nlohmann::json::object();
            for (int i = 0; i < qty; ++i) store->add_spool(FilamentSpool::from_json(sd));
            store->save();
        }
        respond_spool_list(seq);
    };

    m_handlers["update_spool"] = [this, respond_spool_list](int seq, const nlohmann::json& data) {
        auto* store = wxGetApp().fila_manager_store();
        if (store) { store->update_spool(FilamentSpool::from_json(data)); store->save(); }
        respond_spool_list(seq);
    };

    m_handlers["remove_spool"] = [this, respond_spool_list](int seq, const nlohmann::json& data) {
        auto* store = wxGetApp().fila_manager_store();
        if (store) { store->remove_spool(data.value("spool_id", "")); store->save(); }
        respond_spool_list(seq);
    };

    m_handlers["batch_remove"] = [this, respond_spool_list](int seq, const nlohmann::json& data) {
        auto* store = wxGetApp().fila_manager_store();
        if (store && data.contains("spool_ids")) {
            for (auto& sid : data["spool_ids"]) store->remove_spool(sid.get<std::string>());
            store->save();
        }
        respond_spool_list(seq);
    };

    m_handlers["mark_empty"] = [this, respond_spool_list](int seq, const nlohmann::json& data) {
        auto* store = wxGetApp().fila_manager_store();
        if (store) {
            const FilamentSpool* sp = store->get_spool(data.value("spool_id", ""));
            if (sp) {
                FilamentSpool u = *sp;
                u.status = "empty"; u.remain_percent = 0;
                store->update_spool(u); store->save();
            }
        }
        respond_spool_list(seq);
    };

    m_handlers["toggle_favorite"] = [this, respond_spool_list](int seq, const nlohmann::json& data) {
        auto* store = wxGetApp().fila_manager_store();
        if (store) {
            const FilamentSpool* sp = store->get_spool(data.value("spool_id", ""));
            if (sp) {
                FilamentSpool u = *sp;
                u.favorite = !u.favorite;
                store->update_spool(u); store->save();
            }
        }
        respond_spool_list(seq);
    };

    m_handlers["archive_spool"] = [this, respond_spool_list](int seq, const nlohmann::json& data) {
        auto* store = wxGetApp().fila_manager_store();
        if (store) {
            const FilamentSpool* sp = store->get_spool(data.value("spool_id", ""));
            if (sp) {
                FilamentSpool u = *sp;
                u.status = "archived";
                store->update_spool(u); store->save();
            }
        }
        respond_spool_list(seq);
    };
}

/* ================================================================
 *  Data builders
 * ================================================================ */

nlohmann::json wgtFilaManagerPanel::build_spool_list()
{
    auto* store = wxGetApp().fila_manager_store();
    return store ? store->spools_to_json() : nlohmann::json::array();
}

nlohmann::json wgtFilaManagerPanel::build_preset_options()
{
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return {{"vendors", nlohmann::json::array()}};

    MachineObject* obj = nullptr;
    if (auto* dev_mgr = wxGetApp().getDeviceManager())
        obj = dev_mgr->get_selected_machine();

    std::set<std::string> printer_names;
    if (obj && obj->GetExtderSystem()) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(1) << obj->GetExtderSystem()->GetNozzleDiameter(0);
        printer_names = bundle->get_printer_names_by_printer_type_and_nozzle(
            DevPrinterConfigUtil::get_printer_display_name(obj->printer_type),
            stream.str());
    }

    auto& filaments = bundle->filaments;
    std::map<std::string, std::map<std::string, std::vector<nlohmann::json>>> vendor_type_items;
    std::set<std::string> filament_id_set;
    for (auto it = filaments.begin(); it != filaments.end(); ++it) {
        Preset& preset = *it;
        if (filaments.get_preset_base(*it) != &preset)
            continue;
        if (obj && !it->is_system && !obj->is_support_user_preset)
            continue;
        if (!printer_names.empty()) {
            ConfigOption* printer_opt = it->config.option("compatible_printers");
            ConfigOptionStrings* printer_strs = dynamic_cast<ConfigOptionStrings*>(printer_opt);
            if (!printer_strs)
                continue;

            bool compatible_printer = false;
            for (const auto& printer_str : printer_strs->values) {
                if (printer_names.find(printer_str) != printer_names.end()) {
                    compatible_printer = true;
                    break;
                }
            }
            if (!compatible_printer)
                continue;
        }

        std::string vendor = it->config.get_filament_vendor();
        std::string type   = it->config.get_filament_type();
        if (vendor.empty()) continue;
        if (type.empty()) type = "Other";

        const std::string dedupe_key = it->filament_id.empty()
            ? (vendor + "\n" + type + "\n" + it->name)
            : it->filament_id;
        if (!filament_id_set.insert(dedupe_key).second)
            continue;

        std::string shown_name = filaments.get_preset_alias(*it, true);
        if (shown_name.empty())
            shown_name = it->display_name();
        if (shown_name.empty())
            continue;

        vendor_type_items[vendor][type].push_back({
            {"name", shown_name},
            {"series", shown_name},
            {"filament_id", it->filament_id},
            {"setting_id", it->setting_id}
        });
    }

    nlohmann::json vendors_arr = nlohmann::json::array();
    for (auto& [vname, type_map] : vendor_type_items) {
        nlohmann::json types_arr = nlohmann::json::array();
        for (auto& [tname, items] : type_map) {
            nlohmann::json series_arr = nlohmann::json::array();
            for (auto& item : items) {
                const std::string name = item.value("name", "");
                if (!name.empty()) series_arr.push_back(name);
            }
            types_arr.push_back({{"name", tname}, {"series", series_arr}, {"items", items}});
        }
        vendors_arr.push_back({{"name", vname}, {"types", types_arr}});
    }
    return {{"vendors", vendors_arr}};
}

nlohmann::json wgtFilaManagerPanel::build_machine_list()
{
    nlohmann::json result = {{"machines", nlohmann::json::array()}};
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
            arr.push_back({{"dev_id", id}, {"dev_name", name}, {"is_online", obj->is_online()}});
        }
        result["machines"] = arr;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[FilaManager] build_machine_list: " << e.what();
    }
    return result;
}

nlohmann::json wgtFilaManagerPanel::build_ams_data()
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
        BOOST_LOG_TRIVIAL(error) << "[FilaManager] build_ams_data: " << e.what();
        return empty;
    }
}

}} // namespace Slic3r::GUI
