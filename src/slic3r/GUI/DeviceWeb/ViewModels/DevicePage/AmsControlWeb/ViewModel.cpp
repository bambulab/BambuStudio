#include "ViewModel.hpp"
#include "Schema.hpp"
#include "ViewModelActions.hpp"
#include "ViewModelDataBuilder.hpp"
#include "ViewModelDisplayBuilder.hpp"

#include "slic3r/GUI/DeviceWeb/DeviceWebBridge.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSwitch.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"

#include <boost/log/trivial.hpp>
#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <wx/sizer.h>

namespace Slic3r { namespace GUI {

namespace SchemaFormat = AmsControlWebSchema::format;

namespace {

class AmsControlWebDebugDialog : public DPIDialog
{
public:
    explicit AmsControlWebDebugDialog(wxWindow* parent)
        : DPIDialog(parent, wxID_ANY, _L("AMS Control Web Debug"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
    {
        SetClientSize(wxSize(FromDIP(900), FromDIP(700)));
        SetMinClientSize(wxSize(FromDIP(720), FromDIP(520)));
    }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override { SetSize(suggested_rect.GetSize()); }
};

nlohmann::json& ams_control_web_debug_snapshot()
{
    static nlohmann::json snapshot = nlohmann::json::object();
    return snapshot;
}

void open_ams_control_web_debug_dialog()
{
    wxWindow* parent = wxGetApp().mainframe;
    AmsControlWebDebugDialog dlg(parent);
    auto* host = new DeviceWebHost(&dlg, DeviceWebHostMode::DevicePageAmsControlWeb, "/device_page/ams_control_web_debug");
    host->SetMinSize(wxSize(dlg.FromDIP(720), dlg.FromDIP(520)));
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(host, 1, wxEXPAND);
    dlg.SetSizer(sizer);
    dlg.Layout();
    dlg.ShowModal();
}

// Port of the static sGetSwitchInfo() in StatusPanel.cpp. An empty reason means
// the button is enabled, and the order of the checks decides which reason the
// user gets to see.
void get_switch_info(MachineObject* machine_obj,
                     const SchemaFormat::State& state,
                     std::string& load_error_info,
                     std::string& unload_error_info)
{
    load_error_info.clear();
    unload_error_info.clear();

    const auto reject_both = [&](const std::string& reason) {
        load_error_info   = reason;
        unload_error_info = reason;
    };

    if (!machine_obj) {
        reject_both(_u8L("Please select a printer"));
        return;
    }

    if (machine_obj->is_in_printing() && !machine_obj->can_resume()) {
        reject_both(_u8L("The printer is busy on other print job"));
        return;
    }

    auto fila_system = machine_obj->GetFilaSystem();
    const std::string& ams_id  = state.selected_ams_id;
    const std::string& slot_id = state.selected_slot_id;
    const bool is_virtual_tray = devPrinterUtil::IsVirtualSlot(ams_id);

    if (machine_obj->can_resume() && !is_virtual_tray) {
        reject_both(_u8L("When printing is paused, filament loading and unloading are only supported for external slots."));
        return;
    }

    auto extder_system = machine_obj->GetExtderSystem();
    const bool in_switch_filament =
        (machine_obj->is_enable_np && extder_system && extder_system->IsBusyLoading()) ||
        machine_obj->ams_status_main == AMS_STATUS_MAIN_FILAMENT_CHANGE;
    if (in_switch_filament) {
        reject_both(_u8L("Current extruder is busy changing filament"));
        return;
    }

    const SchemaFormat::Tray* selected = AmsControlWebData::FindTray(state.data, ams_id, slot_id);
    if (!selected) {
        reject_both(_u8L("Choose an AMS slot then press \"Load\" or \"Unload\" button to automatically load or unload filaments."));
        return;
    }

    auto fila_switch = machine_obj->GetFilaSwitch();
    const bool switch_installed = fila_switch && fila_switch->IsInstalled();
    if (switch_installed) {
        if (is_virtual_tray) {
            reject_both(_u8L("\"Load\" or \"Unload\" is not supported for external spool while using Filament Track Switch."));
            return;
        }
        if (!fila_switch->IsReady()) {
            reject_both(_u8L("The Filament Track Switch has not been setup. Please setup on printer."));
            return;
        }
    }

    // Past this point load and unload are judged separately.
    if (!switch_installed && extder_system) {
        for (const auto& ext : extder_system->GetExtruders()) {
            if (ext.GetSlotNow().ams_id == ams_id && ext.GetSlotNow().slot_id == slot_id && ext.HasFilamentInExt())
                load_error_info = _u8L("Current slot has alread been loaded");
        }
    }
    if (!is_virtual_tray && !selected->is_exists)
        load_error_info = _u8L("The selected slot is empty.");

    auto ams_item = fila_system ? fila_system->GetAmsById(ams_id) : nullptr;
    if (!ams_item) return;

    const auto extder_id_opt = ams_item->GetCurrentExtruderId();
    if (!extder_id_opt.has_value()) {
        unload_error_info = switch_installed ? _u8L("The selected slot is not loaded in the extruder.")
                                             : _u8L("No extruder found for the selected slot.");
        return;
    }

    // The classic version dereferences the extruder before testing it; guard it.
    std::optional<DevExtder> extder;
    if (extder_system) extder = extder_system->GetExtderById(extder_id_opt.value());
    if (!extder || !extder->HasFilamentInExt() ||
        extder->GetSlotNow().ams_id != ams_id || extder->GetSlotNow().slot_id != slot_id)
        unload_error_info = _u8L("The selected slot is not loaded in the extruder.");
}

} // namespace

nlohmann::json DevicePageAmsControlWebVM::OnCommand(const std::string& submod,
                                                    const std::string& action,
                                                    const nlohmann::json& payload)
{
    try {
        if (submod == "state" && (action == "get" || action == "init")) {
            return MakeResponse(GetModule(), submod, action, 0, "", BuildState());
        }
        if (submod == "action") {
            return HandleAction(action, payload);
        }
        if (submod == "debug") {
            return HandleDebug(action, payload);
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "DevicePageAmsControlWebVM::OnCommand exception: " << e.what();
        return MakeResponse(GetModule(), submod, action, 2, e.what(), BuildState());
    }

    return MakeResponse(GetModule(), submod, action, 1,
                        "unknown ams control web command", BuildState());
}

nlohmann::json DevicePageAmsControlWebVM::HandleAction(const std::string& action,
                                                       const nlohmann::json& payload)
{
    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;

    if (auto handled = AmsControlWebActionHandler::TryHandle(*this, action, payload, machine_obj))
        return std::move(handled.value());

    return MakeResponse(GetModule(), "action", action, 1,
                        "unknown ams control web action", BuildState());
}

void DevicePageAmsControlWebVM::SetSelectedSlot(const std::string& ams_id, const std::string& slot_id)
{
    m_selected_ams_id  = ams_id;
    m_selected_slot_id = slot_id;
    m_last_report_state.clear();
}

void DevicePageAmsControlWebVM::PromoteActiveAms(const std::string& ams_id)
{
    if (ams_id.empty()) return;

    auto it = std::find(m_active_ams_ids.begin(), m_active_ams_ids.end(), ams_id);
    if (it != m_active_ams_ids.end()) m_active_ams_ids.erase(it);
    m_active_ams_ids.insert(m_active_ams_ids.begin(), ams_id);
    m_last_report_state.clear();
}

nlohmann::json DevicePageAmsControlWebVM::BuildDebugSnapshot(const nlohmann::json& payload) const
{
    nlohmann::json snapshot;
    snapshot["created_ts"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    snapshot["cpp_state"] = BuildState();
    snapshot["web_state"] = payload.value("web_state", nlohmann::json::object());
    snapshot["web_trace"] = payload.value("web_trace", nlohmann::json::array());
    // DeviceWebBridge does not record a message trace on this branch, so the
    // debug page only shows the web-side trace.
    snapshot["cpp_trace"] = nlohmann::json::array();
    return snapshot;
}

nlohmann::json DevicePageAmsControlWebVM::HandleDebug(const std::string& action, const nlohmann::json& payload)
{
#if !BBL_RELEASE_TO_PUBLIC
    if (action == "open_bridge_trace") {
        ams_control_web_debug_snapshot() = BuildDebugSnapshot(payload);
        wxGetApp().CallAfter([]() { open_ams_control_web_debug_dialog(); });
        return MakeResponse(GetModule(), "debug", action, 0, "", ams_control_web_debug_snapshot());
    }
    if (action == "snapshot") {
        nlohmann::json snapshot = ams_control_web_debug_snapshot();
        snapshot["dialog_snapshot_ts"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        snapshot["dialog_cpp_state"] = BuildState();
        snapshot["dialog_cpp_trace"] = nlohmann::json::array();
        return MakeResponse(GetModule(), "debug", action, 0, "", snapshot);
    }
#else
    (void) payload;
#endif
    return MakeResponse(GetModule(), "debug", action, 1, "unknown debug action", nlohmann::json::object());
}

void DevicePageAmsControlWebVM::ReportState(const std::string& submod, const std::string& action)
{
    if (!m_bridge) return;

    nlohmann::json state = BuildState();

    std::string state_key;
    try {
        state_key = state.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " state.dump() failed: " << e.what();
        return;
    }
    if (state_key == m_last_report_state) return;

    m_last_report_state = state_key;
    m_bridge->ReportMsg(MakeResponse(GetModule(), submod, action, 0, "", state));
}

void DevicePageAmsControlWebVM::OnSysColorChanged()
{
    ReportState("state", "theme_changed");
}

void DevicePageAmsControlWebVM::NotifyNewRfidFilament(const std::string& ams_id, const std::string& slot_id)
{
    AmsControlWebDisplay::NotifyNewRfidFilament(ams_id, slot_id);
}

void DevicePageAmsControlWebVM::DismissFilamentMgrHint(const std::string& ams_id, const std::string& slot_id)
{
    AmsControlWebDisplay::DismissFilamentMgrHint(ams_id, slot_id);
}

nlohmann::json DevicePageAmsControlWebVM::BuildState() const
{
    SchemaFormat::State state;

    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj || !machine_obj->GetFilaSystem())
        return state;

    const auto loaded = AmsControlWebData::Build(machine_obj, state.data);
    state.loaded_ams_id  = loaded.ams_id;
    state.loaded_slot_id = loaded.slot_id;

    ResolveSelection(state);
    FillActions(machine_obj, state);
    state.active_ams_ids = m_active_ams_ids;

    AmsControlWebDisplay::Build(machine_obj, state);

    return state;
}

void DevicePageAmsControlWebVM::ResolveSelection(SchemaFormat::State& state) const
{
    // Nothing picked is a state of its own, so it survives to the page rather than
    // being treated as a stale pick and filled in. get_switch_info turns it into the
    // "choose a slot" tip on both footer buttons.
    if (m_selected_ams_id.empty()) {
        state.selected_ams_id.clear();
        state.selected_slot_id.clear();
        return;
    }

    std::string ams_id  = m_selected_ams_id;
    std::string slot_id = m_selected_slot_id;

    // The picked slot can disappear when the AMS is unplugged; fall back to the
    // loaded slot, then to the first slot there is.
    if (!AmsControlWebData::FindTray(state.data, ams_id, slot_id)) {
        if (!state.loaded_ams_id.empty()) {
            ams_id  = state.loaded_ams_id;
            slot_id = state.loaded_slot_id;
        } else if (!state.data.ams_units.empty() && !state.data.ams_units.front().trays.empty()) {
            ams_id  = state.data.ams_units.front().trays.front().ams_id;
            slot_id = state.data.ams_units.front().trays.front().slot_id;
        } else if (!state.data.ext_slots.empty()) {
            ams_id  = state.data.ext_slots.front().ams_id;
            slot_id = state.data.ext_slots.front().slot_id;
        } else {
            ams_id.clear();
            slot_id.clear();
        }
    }

    state.selected_ams_id  = ams_id;
    state.selected_slot_id = slot_id;
}

void DevicePageAmsControlWebVM::FillActions(MachineObject* machine_obj, SchemaFormat::State& state) const
{
    auto fila_system = machine_obj->GetFilaSystem();
    const bool has_connected_ams = !fila_system->GetAmsList().empty() && machine_obj->ams_exist_bits != 0;
    // The auto-refill entry lives in the AMS settings dialog on this branch, so the
    // footer keeps the button hidden.
    state.actions.show_auto_refill = false;
    state.actions.show_settings    = has_connected_ams;

    get_switch_info(machine_obj, state, state.actions.load_tips, state.actions.unload_tips);
    state.actions.can_load   = state.actions.load_tips.empty();
    state.actions.can_unload = state.actions.unload_tips.empty();
}

}} // namespace Slic3r::GUI
