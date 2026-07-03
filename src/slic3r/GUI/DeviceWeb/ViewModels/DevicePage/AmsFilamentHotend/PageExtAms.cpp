#include "ViewModel.hpp"
#include "ActionHandlerExtAms.hpp"
#include "ResolverSlotState.hpp"

#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"

#include <boost/format.hpp>
#include <algorithm>
#include <chrono>
#include <vector>
#include <wx/sizer.h>

namespace Slic3r { namespace GUI {

namespace {

std::string normalize_hex_for_web(const std::string& raw)
{
    if (raw.empty()) return "#D9D9D9";
    std::string hex = raw;
    if (hex[0] != '#') hex = "#" + hex;
    if (hex.size() > 9) hex = hex.substr(0, 9);
    return hex;
}

std::string color_type_for_web(DevFilaColorType color_type)
{
    switch (color_type) {
    case DevFilaColorType::CTYPE_MULTI:    return "multi";
    case DevFilaColorType::CTYPE_GRADIANT: return "gradient";
    case DevFilaColorType::CTYPE_SINGLE:   return "single";
    }
    return "single";
}

class ExtAmsOperationDialog : public DPIDialog
{
public:
    explicit ExtAmsOperationDialog(wxWindow* parent)
        : DPIDialog(parent, wxID_ANY, _L("Ext AMS Operation"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    {
        SetClientSize(wxSize(FromDIP(520), FromDIP(420)));
        SetMinClientSize(wxSize(FromDIP(520), FromDIP(420)));
    }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override { SetSize(suggested_rect.GetSize()); }
};

class AmsBridgeDebugDialog : public DPIDialog
{
public:
    explicit AmsBridgeDebugDialog(wxWindow* parent)
        : DPIDialog(parent, wxID_ANY, _L("AMS Bridge Debug"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
    {
        SetClientSize(wxSize(FromDIP(900), FromDIP(700)));
        SetMinClientSize(wxSize(FromDIP(720), FromDIP(520)));
    }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override { SetSize(suggested_rect.GetSize()); }
};

class ExtFilamentEditDialog : public DPIDialog
{
public:
    explicit ExtFilamentEditDialog(wxWindow* parent, int content_height)
        : DPIDialog(parent, wxID_ANY, _L("Edit Filament"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    {
        SetClientSize(wxSize(FromDIP(620), FromDIP(content_height)));
        SetMinClientSize(wxSize(FromDIP(620), FromDIP(content_height)));
    }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override { SetSize(suggested_rect.GetSize()); }
};

nlohmann::json& bridge_debug_snapshot()
{
    static nlohmann::json snapshot = nlohmann::json::object();
    return snapshot;
}

wxWindow*& ext_filament_edit_dialog()
{
    static wxWindow* dlg = nullptr;
    return dlg;
}

bool is_ext_dialog_slot_loaded(MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id)
{
    if (!machine_obj || !machine_obj->GetExtderSystem()) return false;
    for (const auto& ext : machine_obj->GetExtderSystem()->GetExtruders()) {
        if (ext.GetSlotNow().ams_id == ams_id && ext.GetSlotNow().slot_id == slot_id && ext.HasFilamentInExt()) return true;
    }
    return false;
}

nlohmann::json build_ext_dialog_slot(const DevicePageAmsFilamentHotendVM& vm, MachineObject* machine_obj, const nlohmann::json& source)
{
    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    const int addr_id = source.value("addr_id", 0);
    const int display_index = source.value("display_index", 0);
    const std::string ams_id = source.value("ams_id", std::string());
    const std::string slot_id = source.value("slot_id", std::string());
    const bool loaded = is_ext_dialog_slot_loaded(machine_obj, ams_id, slot_id);
    const auto tray = machine_obj ? machine_obj->get_tray(ams_id, slot_id) : std::nullopt;

    std::string state = SV::card_state::unknown;
    std::string color = "#D9D9D9";
    std::string color_type = "single";
    std::string name;
    std::string type;
    int remain = 100;
    std::vector<std::string> colors;

    if (tray.has_value() && !tray->m_fila_type.empty()) {
        type = tray->m_fila_type;
        name = vm.ResolveFilamentCardText(&*tray, type);
        color = normalize_hex_for_web(tray->color);
        color_type = color_type_for_web(tray->ctype);
        colors = tray->cols;
        if (colors.empty() && !tray->color.empty()) colors.push_back(tray->color);
        for (auto& c : colors) c = normalize_hex_for_web(c);
        remain = DevicePageFilamentSlotStateResolver::ResolveDisplayRemain(&*tray);
        state = loaded ? SV::card_state::ready_to_use : SV::card_state::ready_to_load;
    }

    return {
        {SK::filament_slot::addr_id,           addr_id},
        {SK::filament_slot::display_index,     display_index},
        {SK::filament_slot::ams_id,            ams_id},
        {SK::filament_slot::slot_id,           slot_id},
        {SK::filament_slot::enabled,           true},
        {SK::filament_slot::is_external_spool, true},
        {SK::filament_slot::filament_card, {
            {SK::filament_card::state,       state},
            {SK::filament_card::color,       color},
            {SK::filament_card::color_type,  color_type},
            {SK::filament_card::colors,      colors},
            {SK::filament_card::text,        name},
            {SK::filament_card::name,        name},
            {SK::filament_card::type,        type},
            {SK::filament_card::remain,      remain},
            {SK::filament_card::status_text, state == SV::card_state::unknown ? _u8L("Unknown filament") : std::string()}
        }},
        {SK::filament_slot::filament_line, {
            {SK::filament_line::state,          loaded ? SV::line_state::loaded : SV::line_state::no_filament},
            {SK::filament_line::has_filament,   loaded},
            {SK::filament_line::filament_color, loaded ? color : std::string()}
        }},
        {SK::filament_slot::filament_nozzle, {
            {SK::filament_nozzle::state,          loaded ? SV::nozzle_state::active : SV::nozzle_state::normal},
            {SK::filament_nozzle::has_filament,   loaded},
            {SK::filament_nozzle::filament_color, loaded ? color : std::string()},
            {SK::filament_nozzle::on_extruder,    false}
        }},
        {SK::filament_slot::select_action, {
            {SK::select_action::can_select, !loaded},
            {SK::select_action::reason,     loaded ? _u8L("External filament already loaded into hotend") : std::string()},
            {SK::select_action::ext_label,  true}
        }}
    };
}

MachineObject* get_selected_machine_for_dialog()
{
    auto* dev_mgr = wxGetApp().getDeviceManager();
    return dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
}

void open_ext_ams_operation(const nlohmann::json& payload)
{
    MachineObject* machine_obj = get_selected_machine_for_dialog();
    if (!machine_obj) return;

    const int addr_id = payload.value("addr_id", -1);
    const int display_index = payload.value("display_index", -1);
    const std::string ams_id = payload.value("ams_id", "");
    const std::string slot_id = payload.value("slot_id", "");
    const int count = payload.value("bind_ams_slot_count", 0);

    wxWindow* parent = wxGetApp().mainframe;
    ExtAmsOperationDialog dlg(parent);
    const std::string route = (boost::format("/device_page/ams_ext_operation?addr_id=%1%&display_index=%2%&ams_id=%3%&slot_id=%4%&count=%5%")
        % addr_id % display_index % ams_id % slot_id % count).str();
    auto* host = new DeviceWebHost(&dlg, DeviceWebHostMode::DevicePageAmsFilamentHotend, route);
    host->SetMinSize(wxSize(dlg.FromDIP(520), dlg.FromDIP(420)));
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(host, 1, wxEXPAND);
    dlg.SetSizer(sizer);
    dlg.Layout();
    dlg.ShowModal();
}

void open_bridge_debug_dialog()
{
    wxWindow* parent = wxGetApp().mainframe;
    AmsBridgeDebugDialog dlg(parent);
    auto* host = new DeviceWebHost(&dlg, DeviceWebHostMode::DevicePageAmsFilamentHotend, "/device_page/ams_bridge_debug");
    host->SetMinSize(wxSize(dlg.FromDIP(720), dlg.FromDIP(520)));
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(host, 1, wxEXPAND);
    dlg.SetSizer(sizer);
    dlg.Layout();
    dlg.ShowModal();
}

std::string make_ext_edit_slots_param(const nlohmann::json& slots)
{
    std::vector<std::string> parts;
    if (slots.is_array()) {
        for (const auto& s : slots) {
            const std::string ams_id = s.value("ams_id", std::string());
            const std::string slot_id = s.value("slot_id", std::string());
            const int display_index = s.value("display_index", 0);
            if (ams_id.empty() || slot_id.empty() || display_index <= 0) continue;
            parts.push_back((boost::format("%1%|%2%|%3%") % ams_id % slot_id % display_index).str());
        }
    }
    std::string joined;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) joined += ",";
        joined += parts[i];
    }
    return joined;
}

int open_ext_filament_edit_dialog(const nlohmann::json& payload)
{
    wxWindow* parent = wxGetApp().mainframe;
    const std::string intent = payload.value("intent", std::string("edit"));
    const std::string mode = payload.value("mode", std::string(AmsHotendSchema::values::mode::batch_ext_load));
    const nlohmann::json slot_array = payload.value("slots", nlohmann::json::array());
    const nlohmann::json run_slot_array = payload.value("run_slots", nlohmann::json::array());
    const std::string slots = make_ext_edit_slots_param(slot_array);
    const std::string run_slots = make_ext_edit_slots_param(run_slot_array);
    const int slot_count = slot_array.is_array() ? static_cast<int>(slot_array.size()) : 1;
    const bool show_warning = intent == "run";
    const int content_height = std::clamp((show_warning ? 58 : 12) + 40 + std::max(1, slot_count) * 54 + 66, 194, 392);

    ExtFilamentEditDialog dlg(parent, content_height);
    ext_filament_edit_dialog() = &dlg;
    const std::string route = (boost::format("/device_page/ams_ext_filament_edit?intent=%1%&mode=%2%&slots=%3%&run_slots=%4%") % intent % mode % slots % run_slots).str();
    auto* host = new DeviceWebHost(&dlg, DeviceWebHostMode::DevicePageAmsFilamentHotend, route);
    host->SetMinSize(wxSize(dlg.FromDIP(620), dlg.FromDIP(content_height)));
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(host, 1, wxEXPAND);
    dlg.SetSizer(sizer);
    dlg.Layout();
    const int result = dlg.ShowModal();

    if (ext_filament_edit_dialog() == &dlg) ext_filament_edit_dialog() = nullptr;
    return result;
}

} // namespace

nlohmann::json DevicePageAmsFilamentHotendVM::BuildDebugSnapshot(const nlohmann::json& payload) const
{
    nlohmann::json snapshot;
    snapshot["created_ts"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    snapshot["cpp_state"] = BuildState();
    snapshot["web_state"] = payload.value("web_state", nlohmann::json::object());
    snapshot["web_trace"] = payload.value("web_trace", nlohmann::json::array());
    snapshot["cpp_trace"] = m_bridge ? m_bridge->RecentTrace() : nlohmann::json::array();
    return snapshot;
}

void DevicePageAmsFilamentHotendVM::OpenBatchFilamentEditDialog(const nlohmann::json& payload)
{
    const int result = open_ext_filament_edit_dialog(payload);
    // The dialog closed after the user pressed Auto Load (run committed). This
    // runs on the main-page VM instance, so explicitly push it back to Idle:
    // arm the one-shot flag (so the next focus/poll state.get returns normal)
    // and report immediately so the N1x AMS page leaves batch selection.
    if (result == wxID_APPLY) {
        m_active_mode = AmsHotendSchema::values::mode::normal;
        m_pending_exit_batch = true;
        ReportState("state", "changed");
    }
}

nlohmann::json DevicePageAmsFilamentHotendVM::HandleDebug(const std::string& action, const nlohmann::json& payload)
{
#if !BBL_RELEASE_TO_PUBLIC
    if (action == "open_bridge_trace") {
        bridge_debug_snapshot() = BuildDebugSnapshot(payload);
        wxGetApp().CallAfter([]() { open_bridge_debug_dialog(); });
        return MakeResponse(GetModule(), "debug", action, 0, "", bridge_debug_snapshot());
    }
    if (action == "snapshot") {
        nlohmann::json snapshot = bridge_debug_snapshot();
        snapshot["dialog_snapshot_ts"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        snapshot["dialog_cpp_state"] = BuildState();
        snapshot["dialog_cpp_trace"] = m_bridge ? m_bridge->RecentTrace() : nlohmann::json::array();
        return MakeResponse(GetModule(), "debug", action, 0, "", snapshot);
    }
#else
    (void) payload;
#endif
    return MakeResponse(GetModule(), "debug", action, 1, "unknown debug action", nlohmann::json::object());
}

std::optional<nlohmann::json> DevicePageExtAmsActionHandler::TryHandle(DevicePageAmsFilamentHotendVM& vm,
                                                                       const std::string& action,
                                                                       const nlohmann::json& payload,
                                                                       MachineObject* /*machine_obj*/)
{
    if (action == "ams_operation") {
        const nlohmann::json action_payload = payload;
        wxGetApp().CallAfter([action_payload]() { open_ext_ams_operation(action_payload); });
    } else if (action == "batch_filament_edit" || action == "batch_ext_edit") {
        const nlohmann::json action_payload = payload;
        wxGetApp().CallAfter([vm_ptr = &vm, action_payload]() { vm_ptr->OpenBatchFilamentEditDialog(action_payload); });
    } else if (action == "close_dialog") {
        wxGetApp().CallAfter([]() {
            if (auto* dlg = ext_filament_edit_dialog()) {
                if (auto* dialog = dynamic_cast<wxDialog*>(dlg)) dialog->EndModal(wxID_OK);
                else dlg->Close();
            }
        });
    } else if (action == "close_dialog_after_run") {
        // Close with wxID_APPLY so the opener (main-page VM) knows the user
        // committed the load and can drop the page back to Idle.
        wxGetApp().CallAfter([]() {
            if (auto* dlg = ext_filament_edit_dialog()) {
                if (auto* dialog = dynamic_cast<wxDialog*>(dlg)) dialog->EndModal(wxID_APPLY);
                else dlg->Close();
            }
        });
    } else {
        return std::nullopt;
    }

    return vm.MakeResponse(vm.GetModule(), "action", action, 0, "", vm.BuildState());
}

}} // namespace Slic3r::GUI
