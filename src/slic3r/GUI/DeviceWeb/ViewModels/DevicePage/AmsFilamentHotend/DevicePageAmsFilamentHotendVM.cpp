#include "DevicePageAmsFilamentHotendVM.hpp"
#include "DevicePageAmsFilamentHotendSchema.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebBridge.hpp"

#include "slic3r/GUI/AMSMaterialsSetting.hpp"
#include "slic3r/GUI/AMSSetting.hpp"
#include "slic3r/GUI/AmsMappingPopup.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"

#include <algorithm>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <chrono>
#include <wx/sizer.h>

namespace {

// Stable enum-like dry-state labels emitted as `ams_zones[].dry_status`. The
// Web side compares these strings against logical constants
// (`!== 'Idle' && !== 'Off'`), so they MUST stay untranslated English. The
// matching user-facing copy lives in `ams_zones[].dry_status_text`, produced
// by {@link dry_status_display_label}.
const char* dry_status_short_name(Slic3r::DevAms::DryStatus status)
{
    switch (status) {
    case Slic3r::DevAms::DryStatus::Off:                         return "Idle";
    case Slic3r::DevAms::DryStatus::Checking:                    return "Checking";
    case Slic3r::DevAms::DryStatus::Drying:                      return "Drying";
    case Slic3r::DevAms::DryStatus::Cooling:                     return "Cooling";
    case Slic3r::DevAms::DryStatus::Stopping:                    return "Stopping";
    case Slic3r::DevAms::DryStatus::Error:                       return "Error";
    case Slic3r::DevAms::DryStatus::CannotStopHeatOutofControl:  return "Heat fault";
    case Slic3r::DevAms::DryStatus::PrdTesting:                  return "Self test";
    }
    return "Idle";
}

// User-facing label for the dryer chamber, translated per current language.
// Web renders this directly as the dryer status chip text.
std::string dry_status_display_label(Slic3r::DevAms::DryStatus status)
{
    switch (status) {
    case Slic3r::DevAms::DryStatus::Off:                         return _u8L("Idle");
    case Slic3r::DevAms::DryStatus::Checking:                    return _u8L("Checking");
    case Slic3r::DevAms::DryStatus::Drying:                      return _u8L("Drying");
    case Slic3r::DevAms::DryStatus::Cooling:                     return _u8L("Cooling");
    case Slic3r::DevAms::DryStatus::Stopping:                    return _u8L("Stopping");
    case Slic3r::DevAms::DryStatus::Error:                       return _u8L("Error");
    case Slic3r::DevAms::DryStatus::CannotStopHeatOutofControl:  return _u8L("Heat fault");
    case Slic3r::DevAms::DryStatus::PrdTesting:                  return _u8L("Self test");
    }
    return _u8L("Idle");
}

} // namespace

namespace Slic3r { namespace GUI {

namespace {

int nozzle_pos_to_display_idx(int nozzle_pos)
{
    return nozzle_pos >= 0x10 ? (nozzle_pos - 0x10 + 1) : (nozzle_pos + 1);
}

std::string normalize_hex_for_web(const std::string& raw)
{
    if (raw.empty()) return "#D9D9D9";
    std::string hex = raw;
    if (hex[0] != '#') hex = "#" + hex;
    if (hex.size() >= 7) hex = hex.substr(0, 7);
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

std::string nozzle_flow_short_name(NozzleFlowType flow_type)
{
    switch (flow_type) {
    case NozzleFlowType::S_FLOW: return "Std";
    case NozzleFlowType::H_FLOW: return "HF";
    case NozzleFlowType::U_FLOW: return "UHF";
    case NozzleFlowType::T_FLOW: return "TF";
    default: return "";
    }
}

std::string filament_step_label(DevFilamentStep step)
{
    // Step labels are user-visible on the filament change progress panel;
    // wxWidgets translates them per current language before they hit JSON.
    switch (step) {
    case DevFilamentStep::STEP_IDLE:               return _u8L("Idling...");
    case DevFilamentStep::STEP_PAUSE:              return _u8L("Pause");
    case DevFilamentStep::STEP_HEAT_NOZZLE:        return _u8L("Heat the nozzle");
    case DevFilamentStep::STEP_CUT_FILAMENT:       return _u8L("Cut filament");
    case DevFilamentStep::STEP_PULL_CURR_FILAMENT: return _u8L("Pull back current filament");
    case DevFilamentStep::STEP_PUSH_NEW_FILAMENT:  return _u8L("Push new filament into extruder");
    case DevFilamentStep::STEP_GRAB_NEW_FILAMENT:  return _u8L("Grab new filament");
    case DevFilamentStep::STEP_PURGE_OLD_FILAMENT: return _u8L("Purge old filament");
    case DevFilamentStep::STEP_CHECK_POSITION:     return _u8L("Check filament location");
    case DevFilamentStep::STEP_SWITCH_EXTRUDER:    return _u8L("Switch extruder");
    case DevFilamentStep::STEP_SWITCH_HOTEND:      return _u8L("Switch hotend");
    case DevFilamentStep::STEP_AMS_FILA_COOLING:   return _u8L("Wait for AMS cooling");
    case DevFilamentStep::STEP_PUSH_SWITCHER_FILA: return _u8L("Switch current filament at Filament Track Switch");
    case DevFilamentStep::STEP_PULL_SWITCHER_FILA: return _u8L("Pull back current filament at Filament Track Switch");
    case DevFilamentStep::STEP_SWITCHER_SWITCH:    return _u8L("Switch track at Filament Track Switch");
    }
    return "";
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
    void on_dpi_changed(const wxRect& suggested_rect) override
    {
        SetSize(suggested_rect.GetSize());
    }
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
    void on_dpi_changed(const wxRect& suggested_rect) override
    {
        SetSize(suggested_rect.GetSize());
    }
};

nlohmann::json& bridge_debug_snapshot()
{
    static nlohmann::json snapshot = nlohmann::json::object();
    return snapshot;
}

void open_ams_materials_setting(const std::string& ams_id, const std::string& slot_id)
{
    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj || ams_id.empty() || slot_id.empty()) return;
    if (!machine_obj->get_tray(ams_id, slot_id).has_value()) return;

    auto tray = machine_obj->get_tray(ams_id, slot_id).value();

    int ams_id_int = 0;
    int slot_id_int = 0;
    try {
        ams_id_int = std::stoi(ams_id);
        slot_id_int = std::stoi(slot_id);
    } catch (...) {
        return;
    }

    wxWindow* parent = wxGetApp().mainframe;
    AMSMaterialsSetting dlg(parent, wxID_ANY);
    dlg.obj = machine_obj;
    dlg.ams_id = ams_id_int;
    dlg.slot_id = slot_id_int;

    std::string filament;
    std::string sn_number;
    std::string temp_max;
    std::string temp_min;
    wxString k_val;
    wxString n_val;

    k_val = wxString::Format("%.3f", tray.k);
    n_val = wxString::Format("%.3f", tray.n);
    wxColor color = DevAmsTray::decode_color(tray.color);

    std::vector<wxColour> cols;
    for (const auto& col : tray.cols) {
        cols.push_back(DevAmsTray::decode_color(col));
    }

    dlg.set_ctype(tray.ctype);
    dlg.ams_filament_id = tray.setting_id;
    if (dlg.ams_filament_id.empty()) {
        dlg.set_empty_color(color);
    } else {
        dlg.set_color(color);
        dlg.set_colors(cols);
    }

    dlg.m_is_third = !DevFilaSystem::IsBBL_Filament(tray.tag_uid);

#if !BBL_RELEASE_TO_PUBLIC
    if (machine_obj && (machine_obj->GetNozzleRack()->GetRackType() == DevNozzleRack::RackType_A ||
        machine_obj->GetNozzleRack()->GetRackType() == DevNozzleRack::RackType_B)) {
        dlg.m_is_third = true;
    }
#endif

    if (!dlg.m_is_third) {
        sn_number = tray.uuid;
        filament = tray.sub_brands;
        temp_max = tray.nozzle_temp_max;
        temp_min = tray.nozzle_temp_min;
    }

    dlg.Popup(filament, sn_number, temp_min, temp_max, k_val, n_val);
}

MachineObject* get_selected_machine_for_dialog()
{
    auto* dev_mgr = wxGetApp().getDeviceManager();
    return dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
}

void open_ams_settings()
{
    MachineObject* machine_obj = get_selected_machine_for_dialog();
    if (!machine_obj) return;

    wxWindow* parent = wxGetApp().mainframe;
    AMSSetting dlg(parent, wxID_ANY);
    dlg.UpdateByObj(machine_obj);
    dlg.ShowModal();
}

void open_auto_refill()
{
    MachineObject* machine_obj = get_selected_machine_for_dialog();
    if (!machine_obj) return;

    wxWindow* parent = wxGetApp().mainframe;
    AmsReplaceMaterialDialog dlg(parent);
    dlg.update_machine_obj(machine_obj);
    dlg.ShowModal();
}

void open_ext_ams_operation(const nlohmann::json& payload)
{
    MachineObject* machine_obj = get_selected_machine_for_dialog();
    if (!machine_obj) return;

    const int nozzle_pos = payload.value("nozzle_pos", -1);
    const int display_index = payload.value("display_index", -1);
    const std::string ams_id = payload.value("ams_id", "");
    const std::string slot_id = payload.value("slot_id", "");
    const int count = payload.value("bind_ams_slot_count", 0);

    wxWindow* parent = wxGetApp().mainframe;
    ExtAmsOperationDialog dlg(parent);

    const std::string route = (boost::format("/device_page/ams_ext_operation?nozzle_pos=%1%&display_index=%2%&ams_id=%3%&slot_id=%4%&count=%5%")
        % nozzle_pos % display_index % ams_id % slot_id % count).str();
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

} // namespace

nlohmann::json DevicePageAmsFilamentHotendVM::OnCommand(const std::string& submod,
                                                        const std::string& action,
                                                        const nlohmann::json& payload)
{
    try {
        if (submod == "state" && (action == "get" || action == "init")) {
            m_last_report_state.clear();
            return MakeResponse(GetModule(), submod, action, 0, "", BuildState());
        }
        if (submod == "action") {
            return HandleAction(action, payload);
        }
        if (submod == "debug") {
            return HandleDebug(action, payload);
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "DevicePageAmsFilamentHotendVM::OnCommand exception: " << e.what();
        return MakeResponse(GetModule(), submod, action, 2, e.what(), BuildState());
    }

    return MakeResponse(GetModule(), submod, action, 1, "unknown device page AMS filament hotend command", BuildState());
}

void DevicePageAmsFilamentHotendVM::ReportState(const std::string& submod, const std::string& action)
{
    if (!m_bridge) return;
    nlohmann::json state = BuildState();
    const std::string state_key = state.dump();
    if (state_key == m_last_report_state) {
        return;
    }

#if 1
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "[new]" << state.dump(1);
#endif

    m_last_report_state = state_key;
    m_bridge->ReportMsg(MakeResponse(GetModule(), submod, action, 0, "", state));
}

void DevicePageAmsFilamentHotendVM::OnSysColorChanged()
{
    ReportState("state", "theme_changed");
}

nlohmann::json DevicePageAmsFilamentHotendVM::BuildState() const
{
    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    // Default-init the two top-level pages with the empty state Web expects
    // when no machine is selected; we fill them in below.
    nlohmann::json data;
    data[SK::root::filament_page] = {
        {SK::filament_page::actions, {
            {SK::filament_actions::can_load,          true},
            {SK::filament_actions::can_load_external, true},
            {SK::filament_actions::can_load_auto,     true},
            {SK::filament_actions::can_unload,        true},
            {SK::filament_actions::can_dry,           true},
            {SK::filament_actions::show_ams_settings, false},
            {SK::filament_actions::show_auto_refill,  false},
            {SK::filament_actions::tips,              ""}
        }},
        {SK::filament_page::filament_change_status, {
            {SK::filament_change::active,  false},
            {SK::filament_change::mode,    SV::filament_change_mode::idle},
            {SK::filament_change::message, ""},
            {SK::filament_change::tips,    ""}
        }},
        {SK::filament_page::ams_dry_zones, nlohmann::json::array()},
        {SK::filament_page::hotend_slots, nlohmann::json::array()}
    };
    data[SK::root::hotend_page] = {
        {SK::hotend_page::actions, {
            {SK::hotend_actions::can_read_all_hotends,     false},
            {SK::hotend_actions::read_all_disabled_reason, ""}
        }},
        {SK::hotend_page::hotend_rack_status, {
            {SK::rack_status::active,        false},
            {SK::rack_status::mode,          SV::rack_mode::idle},
            {SK::rack_status::display_index, 0},
            {SK::rack_status::current,       0},
            {SK::rack_status::total,         0},
            {SK::rack_status::message,       ""},
            {SK::rack_status::tips,          ""}
        }},
        {SK::hotend_page::hotend_slots, nlohmann::json::array()}
    };

    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj) {
        data[SK::root::filament_page][SK::filament_page::actions][SK::filament_actions::tips] = _u8L("No selected machine");
        return data;
    }

    auto& fp = data[SK::root::filament_page];
    auto& hp = data[SK::root::hotend_page];

    auto nozzle_system = machine_obj->GetNozzleSystem();
    auto nozzle_rack = nozzle_system ? nozzle_system->GetNozzleRack() : nullptr;
    const int reading_idx = nozzle_rack ? nozzle_rack->GetReadingIdx() : 0;
    const int reading_count = nozzle_rack ? nozzle_rack->GetReadingCount() : 0;
    const bool rack_reading = reading_count > 0;
    const bool filament_changing = IsFilamentChangeActive(machine_obj);
    hp[SK::hotend_page::hotend_rack_status] = {
        {SK::rack_status::active, rack_reading},
        {SK::rack_status::mode,   rack_reading ? (reading_count > 1 ? SV::rack_mode::all : SV::rack_mode::single)
                                               : SV::rack_mode::idle},
        {SK::rack_status::display_index, rack_reading ? reading_idx : 0},
        {SK::rack_status::current,       rack_reading ? reading_idx : 0},
        {SK::rack_status::total,         reading_count},
        {SK::rack_status::message, rack_reading ? (reading_count > 1
            ? (boost::format(_u8L("Reading (%1% / %2%)")) % reading_idx % reading_count).str()
            : (boost::format(_u8L("Reading hotend %1%")) % reading_idx).str()) : ""},
        {SK::rack_status::tips, rack_reading ? _u8L("Please wait...") : std::string()}
    };
    const bool can_read_all = nozzle_rack && nozzle_rack->CtrlCanReadAll() && !rack_reading;
    hp[SK::hotend_page::actions][SK::hotend_actions::can_read_all_hotends] = can_read_all;
    if (!can_read_all) {
        hp[SK::hotend_page::actions][SK::hotend_actions::read_all_disabled_reason]
            = rack_reading ? _u8L("Hotend rack is reading") : _u8L("Hotend rack cannot read all");
    }
    if (filament_changing) {
        fp[SK::filament_page::actions][SK::filament_actions::can_load]          = false;
        fp[SK::filament_page::actions][SK::filament_actions::can_load_external] = false;
        fp[SK::filament_page::actions][SK::filament_actions::can_load_auto]     = false;
        fp[SK::filament_page::actions][SK::filament_actions::can_unload]        = false;
    }

    if (auto fila_system = machine_obj->GetFilaSystem()) {
        const bool has_connected_ams = !fila_system->GetAmsList().empty() && machine_obj->ams_exist_bits != 0;
        fp[SK::filament_page::actions][SK::filament_actions::show_ams_settings] = has_connected_ams;
        fp[SK::filament_page::actions][SK::filament_actions::show_auto_refill]  = has_connected_ams;
        // Per-slot ams_operation.units is the canonical source for AMS-unit
        // detail (the AMS Operation dialog reads from there). The Filament tab
        // reads dryer chamber data from `ams_dry_zones` instead — slots no
        // longer carry their own dryer fields.
        //
        // Only zones whose physical layout actually supports drying are
        // shipped (matches `DevAms::Zone::SupportDrying()`, today that means
        // `zone_slots.size() == 2`). Non-drying zones (e.g. the "everything
        // else" bucket some AMS units report) MUST be filtered out or the
        // DryerChamber decoration would wrap unrelated columns.
        for (const auto& [_, ams_unit] : fila_system->GetAmsList()) {
            if (!ams_unit) continue;
            for (const auto& [zone_id, zone] : ams_unit->GetZones()) {
                if (!zone.SupportDrying()) continue;
                const std::string dry_status_enum = zone.dry_status.has_value()
                    ? dry_status_short_name(*zone.dry_status)
                    : std::string();
                const std::string dry_status_label = zone.dry_status.has_value()
                    ? dry_status_display_label(*zone.dry_status)
                    : _u8L("Idle");
                fp[SK::filament_page::ams_dry_zones].push_back({
                    {SK::dryer_zone::ams_id,          ams_unit->GetAmsId()},
                    {SK::dryer_zone::zone_slots,      zone.zone_slots},
                    {SK::dryer_zone::temp,            zone.current_temperature},
                    {SK::dryer_zone::humidity_raw,    zone.humidity_percent},
                    {SK::dryer_zone::dry_time,        zone.left_dry_time},
                    {SK::dryer_zone::dry_status,      dry_status_enum},
                    {SK::dryer_zone::dry_status_text, dry_status_label}
                });
            }
        }

    }

    std::vector<int> nozzle_positions;
    std::map<int, std::optional<std::pair<int, int>>> nozzle_slots;
    const auto& rack_type = machine_obj->GetNozzleRack()->GetRackType();
    if (rack_type == DevNozzleRack::RackType_A || rack_type == DevNozzleRack::RackType_B) {
        const auto& total_nozzles = machine_obj->GetNozzleRack()->GetTotalRackNozzlePosSet();
        for (const auto& nozzle_pos : total_nozzles) {
            const auto& rack_nozzle_opt = machine_obj->GetNozzleSystem()->GetNozzleOptByPosId(nozzle_pos);
            if (!rack_nozzle_opt.has_value()) {
                nozzle_slots[nozzle_pos] = std::nullopt;
                nozzle_positions.push_back(nozzle_pos);
                continue;
            }

            const auto& ams_slots = rack_nozzle_opt->GetBindAmsSlots();
            if (!ams_slots.empty()) {
                int ams_id = ams_slots[0].first;
                int slot_id = ams_slots[0].second;
                if (auto fila_system = machine_obj->GetFilaSystem(); fila_system && fila_system->GetVirtualTray(ams_id).has_value()) {
                    slot_id = 0;
                }
                nozzle_slots[nozzle_pos] = std::make_pair(ams_id, slot_id);
            } else {
                nozzle_slots[nozzle_pos] = std::nullopt;
            }
            nozzle_positions.push_back(nozzle_pos);
        }
    }

    std::sort(nozzle_positions.begin(), nozzle_positions.end(), [](int lhs, int rhs) {
        return nozzle_pos_to_display_idx(lhs) < nozzle_pos_to_display_idx(rhs);
    });

    // Fallback for empty dryer slots without AMS binding.
    const Slic3r::DevAms* fallback_dryer_unit = nullptr;
    if (auto fila_system = machine_obj->GetFilaSystem()) {
        for (const auto& [_, ams_unit] : fila_system->GetAmsList()) {
            if (ams_unit && ams_unit->SupportDrying()) {
                fallback_dryer_unit = ams_unit;
                break;
            }
        }
    }

    // Field sets, indexed by both names, used to project the full slot json
    // into the per-page subsets.
    static const std::vector<const char*> kFilamentSlotFields = {
        SK::filament_slot::nozzle_pos,
        SK::filament_slot::display_index,
        SK::filament_slot::ams_id,
        SK::filament_slot::slot_id,
        SK::filament_slot::bind_ams,
        SK::filament_slot::bind_ams_slot_count,
        SK::filament_slot::enabled,
        SK::filament_slot::is_external_spool,
        SK::filament_slot::is_top_tier,
        SK::filament_slot::menu_actions,
        SK::filament_slot::ams_operation,
        SK::filament_slot::filament_card,
        SK::filament_slot::filament_line,
        SK::filament_slot::filament_nozzle,
    };

    std::string fallback_unloading_tip;
    nlohmann::json running_change_slot = nullptr;
    std::string running_change_operation = "change";
    std::string running_change_message;
    for (int nozzle_pos : nozzle_positions) {
        nlohmann::json full_slot = BuildSlot(machine_obj, nozzle_pos, nozzle_slots[nozzle_pos], fallback_dryer_unit);
        if (filament_changing) {
            const auto& card_section = full_slot.value(SK::filament_slot::filament_card, nlohmann::json::object());
            const std::string card_state = card_section.value(SK::filament_card::state, std::string());
            auto& tips = fp[SK::filament_page::actions][SK::filament_actions::tips];
            // "Slot N filament loading/unloading in progress" — translated on
            // the C++ side so the Web only prints the localized string.
            const std::string change_op = _u8L("loading/unloading");
            if (card_state == SV::card_state::loading) {
                tips = (boost::format(_u8L("Slot %1% filament %2% in progress"))
                    % full_slot.value(SK::filament_slot::display_index, 0)
                    % change_op).str();
                if (running_change_slot.is_null()) {
                    running_change_message = tips.get<std::string>();
                    running_change_slot = {
                        {SK::running_slot::ams_id,        full_slot.value(SK::filament_slot::ams_id, std::string())},
                        {SK::running_slot::slot_id,       full_slot.value(SK::filament_slot::slot_id, std::string())},
                        {SK::running_slot::display_index, full_slot.value(SK::filament_slot::display_index, 0)}
                    };
                }
            } else if (card_state == SV::card_state::unloading && fallback_unloading_tip.empty()) {
                fallback_unloading_tip = (boost::format(_u8L("Slot %1% filament %2% in progress"))
                    % full_slot.value(SK::filament_slot::display_index, 0)
                    % change_op).str();
                if (running_change_slot.is_null()) {
                    running_change_message = fallback_unloading_tip;
                    running_change_slot = {
                        {SK::running_slot::ams_id,        full_slot.value(SK::filament_slot::ams_id, std::string())},
                        {SK::running_slot::slot_id,       full_slot.value(SK::filament_slot::slot_id, std::string())},
                        {SK::running_slot::display_index, full_slot.value(SK::filament_slot::display_index, 0)}
                    };
                }
            }
        }

        // Project to per-page slot shapes.
        nlohmann::json filament_slot_json = nlohmann::json::object();
        for (const char* key : kFilamentSlotFields) {
            if (full_slot.contains(key)) {
                filament_slot_json[key] = full_slot.at(key);
            }
        }
        fp[SK::filament_page::hotend_slots].push_back(std::move(filament_slot_json));

        nlohmann::json hotend_slot_json = {
            {SK::hotend_slot::nozzle_pos,    full_slot.value(SK::filament_slot::nozzle_pos,     0)},
            {SK::hotend_slot::display_index, full_slot.value(SK::filament_slot::display_index,  0)},
            {SK::hotend_slot::ams_id,        full_slot.value(SK::filament_slot::ams_id,         std::string())},
            {SK::hotend_slot::slot_id,       full_slot.value(SK::filament_slot::slot_id,        std::string())},
            {SK::hotend_slot::enabled,       full_slot.value(SK::filament_slot::enabled,        false)},
            {SK::hotend_slot::taken,         full_slot.value(SK::filament_slot::is_on_extruder, false)},
            {SK::hotend_slot::hotend,        full_slot.value(SK::filament_slot::hotend,         nlohmann::json::object())}
        };
        hp[SK::hotend_page::hotend_slots].push_back(std::move(hotend_slot_json));
    }
    auto& fp_tips = fp[SK::filament_page::actions][SK::filament_actions::tips];
    if (filament_changing && fp_tips.get<std::string>().empty() && !fallback_unloading_tip.empty()) {
        fp_tips = fallback_unloading_tip;
    }
    if (filament_changing) {
        if (running_change_message.empty()) {
            running_change_message = fp_tips.get<std::string>();
        }
        fp[SK::filament_page::filament_change_status] = BuildFilamentChangeStatus(machine_obj, running_change_slot, running_change_operation, running_change_message);
    }

    return data;
}

nlohmann::json DevicePageAmsFilamentHotendVM::BuildSlot(MachineObject* machine_obj,
                                                        int nozzle_pos,
                                                        const std::optional<std::pair<int, int>>& slot_id,
                                                        const Slic3r::DevAms* fallback_dryer_unit) const
{
    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    const int display_idx = nozzle_pos_to_display_idx(nozzle_pos);
    const bool is_top_tier = (display_idx % 2) == 0;

    const DevAmsTray* tray = nullptr;
    DevAmsTray tray_copy;
    std::string ams_id;
    std::string tray_id;
    if (machine_obj && slot_id.has_value()) {
        ams_id = std::to_string(slot_id->first);
        tray_id = std::to_string(slot_id->second);
        if (auto fila_system = machine_obj->GetFilaSystem(); fila_system && fila_system->GetVirtualTray(slot_id->first).has_value()) {
            tray_id = "0";
        }
        auto opt_tray = machine_obj->get_tray(slot_id->first, slot_id->second);
        if (opt_tray.has_value()) {
            tray_copy = *opt_tray;
            tray = &tray_copy;
        }
    }

    // Rack positions remain visible even without AMS tray binding.
    const bool has_hotend_slot = true;
    const std::string card_state = ResolveCardState(machine_obj, tray, ams_id, tray_id, nozzle_pos);
    const std::string line_state = ResolveLineState(card_state, has_hotend_slot);
    const std::string nozzle_state = ResolveNozzleState(card_state, line_state);

    nlohmann::json bind_ams = nlohmann::json::array();
    nlohmann::json ams_operation_units = nlohmann::json::array();
    int bind_ams_slot_count = 0;
    if (machine_obj && machine_obj->GetNozzleSystem() && machine_obj->GetFilaSystem()) {
        const auto& rack_nozzle = machine_obj->GetNozzleSystem()->GetNozzleByPosId(nozzle_pos);
        for (int bind_ams_id : rack_nozzle.GetBindAms()) {
            const std::string bind_ams_id_str = std::to_string(bind_ams_id);
            const DevAms* bind_ams_unit = machine_obj->GetFilaSystem()->GetAmsById(bind_ams_id_str);
            const int slot_count = bind_ams_unit ? bind_ams_unit->GetSlotCount() : 0;
            bind_ams_slot_count += slot_count;
            bind_ams.push_back({
                {SK::bind_ams::ams_id,     bind_ams_id_str},
                {SK::bind_ams::ams_type,   bind_ams_unit ? std::to_string((int)bind_ams_unit->GetAmsType()) : ""},
                {SK::bind_ams::slot_count, slot_count}
            });
            if (bind_ams_unit) {
                ams_operation_units.push_back(BuildAmsUnitJson(machine_obj, bind_ams_unit));
            }
        }
    }

    std::string filament_name;
    std::string filament_type;
    std::string color = "#D9D9D9";
    std::string color_type = "single";
    int          filament_remain = 100; // 0~100; default full when device does not report
    if (tray && tray->is_exists) {
        if (tray->m_fila_type == "PLA-S") filament_type = "Sup.PLA";
        else if (tray->m_fila_type == "PA-S") filament_type = "Sup.PA";
        else if (tray->m_fila_type == "ABS-S") filament_type = "Sup.ABS";
        else filament_type = tray->m_fila_type;
        filament_name = !tray->sub_brands.empty() ? tray->sub_brands : filament_type;
        color = normalize_hex_for_web(tray->color);
        color_type = color_type_for_web(tray->ctype);
        if (tray->remain >= 0 && tray->remain <= 100) {
            filament_remain = tray->remain;
        }
    }

    // Dryer chamber UI is driven entirely by `filament_page.ams_zones[]`; the
    // Web side maps each zone's `zone_slots` (native AMS slot_id values) to
    // its corresponding filament column, so individual slots no longer carry
    // their own dryer membership / humidity / status fields.

    // ---- Hotend tab data (Figma 965:31757) ----
    nlohmann::json hotend;
    {
        auto rack_nozzle_info = machine_obj->GetNozzleSystem()->GetNozzleByPosId(nozzle_pos);
        const bool has_tool = has_hotend_slot && rack_nozzle_info.IsExist();
        const bool is_on_extruder = IsNozzleOnExtruder(machine_obj, nozzle_pos);

        std::string op_state = SV::hotend_op_state::idle;

        if (has_tool && rack_nozzle_info.IsAbnormal()) {
            op_state = SV::hotend_op_state::error;
        } else if (auto temp_status = rack_nozzle_info.GetSetRackNozzleTempStatus()) {
            switch (*temp_status) {
            case DevNozzle::NozzleTempStatus::Heating: op_state = SV::hotend_op_state::heating; break;
            case DevNozzle::NozzleTempStatus::Cooling: op_state = SV::hotend_op_state::cooling; break;
            case DevNozzle::NozzleTempStatus::Keeping:
            case DevNozzle::NozzleTempStatus::Idle:
            default:
                op_state = SV::hotend_op_state::idle;
                break;
            }
        } else if (rack_nozzle_info.GetTargetTemp() > rack_nozzle_info.GetCurrentTemp()) {
            op_state = SV::hotend_op_state::heating;
        } else if (rack_nozzle_info.GetCurrentTemp() > rack_nozzle_info.GetTargetTemp() && rack_nozzle_info.GetTargetTemp() > 0) {
            op_state = SV::hotend_op_state::cooling;
        }
        if (auto rack = machine_obj->GetNozzleSystem()->GetNozzleRack(); rack && rack->GetReadingCount() == 1 && rack->GetReadingIdx() == display_idx) {
            op_state = SV::hotend_op_state::reading;
        }

        hotend[SK::hotend::op_state]       = has_tool ? op_state : std::string(SV::hotend_op_state::idle);
        hotend[SK::hotend::tool_kind]      = has_tool ? SV::tool_kind::nozzle : SV::tool_kind::empty;
        hotend[SK::hotend::nozzle_label]   = has_tool ? rack_nozzle_info.GetNozzleDiameterStr().ToStdString() : "--";
        hotend[SK::hotend::nozzle_subtext] = has_tool ? nozzle_flow_short_name(rack_nozzle_info.GetNozzleFlowType()) : "";
        if (has_tool) {
            hotend[SK::hotend::current_temp] = rack_nozzle_info.GetCurrentTemp();
        } else {
            hotend[SK::hotend::current_temp] = nullptr;
        }
        hotend[SK::hotend::target_temp] = has_tool ? rack_nozzle_info.GetTargetTemp() : 0;
        hotend[SK::hotend::selected]    = is_on_extruder;
    }
    const bool nozzle_has_filament = machine_obj && machine_obj->GetNozzleSystem()
        ? machine_obj->GetNozzleSystem()->GetNozzleByPosId(nozzle_pos).HasFilamentInNozzle()
        : false;

    // External spool flag: true when this slot is currently fed by a virtual
    // tray (the external/Ext slot) instead of an AMS slot. The web UI renders
    // an "Ext" tag next to the slot index. Reported at the slot top level so
    // it stays meaningful even when no hotend is mounted.
    bool is_external_spool = false;
    if (machine_obj && slot_id.has_value()) {
        if (auto fila_system = machine_obj->GetFilaSystem()) {
            is_external_spool = fila_system->GetVirtualTray(slot_id->first).has_value();
        }
    }

    const bool is_on_extruder = machine_obj && IsNozzleOnExtruder(machine_obj, nozzle_pos);
    const nlohmann::json menu_actions = {
        {SK::menu_actions::can_edit,          card_state != SV::card_state::empty},
        {SK::menu_actions::can_load,          line_state == SV::line_state::no_filament && card_state != SV::card_state::empty},
        {SK::menu_actions::can_unload,        line_state == SV::line_state::loaded},
        {SK::menu_actions::can_read,          card_state != SV::card_state::reading && card_state != SV::card_state::empty},
        {SK::menu_actions::can_ams_operation, bind_ams_slot_count > 0}
    };

    const nlohmann::json card_section = {
        {SK::filament_card::state,       card_state},
        {SK::filament_card::color,       color},
        {SK::filament_card::color_type,  color_type},
        {SK::filament_card::name,        filament_name},
        {SK::filament_card::type,        filament_type},
        {SK::filament_card::remain,      filament_remain},
        {SK::filament_card::status_text, ResolveStatusText(card_state)}
    };

    const bool line_has_filament = nozzle_has_filament;
    const nlohmann::json line_section = {
        {SK::filament_line::state,          line_state},
        {SK::filament_line::has_filament,   line_has_filament},
        // No dedicated in-tube color in the device model; reuse the spool color
        // when there is filament in the line, empty string otherwise.
        {SK::filament_line::filament_color, line_has_filament ? color : std::string()}
    };

    const nlohmann::json nozzle_section = {
        {SK::filament_nozzle::state,          nozzle_state},
        {SK::filament_nozzle::has_filament,   nozzle_has_filament},
        {SK::filament_nozzle::filament_color, nozzle_has_filament ? color : std::string()},
        {SK::filament_nozzle::on_extruder,    is_on_extruder}
    };

    return {
        {SK::filament_slot::nozzle_pos,          nozzle_pos},
        {SK::filament_slot::display_index,       display_idx},
        {SK::filament_slot::ams_id,              ams_id},
        {SK::filament_slot::slot_id,             tray_id},
        {SK::filament_slot::bind_ams,            bind_ams},
        {SK::filament_slot::bind_ams_slot_count, bind_ams_slot_count},
        {SK::filament_slot::enabled,             has_hotend_slot},
        {SK::filament_slot::is_on_extruder,      is_on_extruder},
        {SK::filament_slot::is_external_spool,   is_external_spool},
        {SK::filament_slot::is_top_tier,         is_top_tier},
        {SK::filament_slot::menu_actions,        menu_actions},
        {SK::filament_slot::ams_operation, {
            {SK::ams_operation::available, bind_ams_slot_count > 0},
            {SK::ams_operation::units,     ams_operation_units}
        }},
        {SK::filament_slot::filament_card,       card_section},
        {SK::filament_slot::filament_line,       line_section},
        {SK::filament_slot::filament_nozzle,     nozzle_section},
        {SK::filament_slot::hotend,              hotend}
    };
}

nlohmann::json DevicePageAmsFilamentHotendVM::BuildAmsUnitJson(MachineObject* machine_obj, const DevAms* ams_unit) const
{
    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    nlohmann::json trays = nlohmann::json::array();
    if (!ams_unit) {
        return {
            {SK::ams_unit::ams_id,       ""},
            {SK::ams_unit::ams_type,     ""},
            {SK::ams_unit::slot_count,   0},
            {SK::ams_unit::humidity_raw, -1},
            {SK::ams_unit::dry_status,   ""},
            {SK::ams_unit::trays,        trays}
        };
    }

    for (const auto& [tray_id, tray] : ams_unit->GetTrays()) {
        if (!tray) continue;

        std::vector<std::string> colors = tray->cols;
        if (colors.empty() && !tray->color.empty()) {
            colors.push_back(tray->color);
        }
        for (auto& c : colors) {
            c = normalize_hex_for_web(c);
        }

        std::string tray_state = SV::tray_state::ready;
        if (!tray->is_exists) {
            tray_state = SV::tray_state::empty;
        } else if (machine_obj && tray->is_reading(machine_obj->tray_reading_bits)) {
            tray_state = SV::tray_state::reading;
        } else if (tray->setting_id.empty() || tray->m_fila_type.empty()) {
            tray_state = SV::tray_state::unknown;
        }

        const std::string tray_ams_id  = ams_unit->GetAmsId();
        const bool is_reading_now      = machine_obj && tray->is_reading(machine_obj->tray_reading_bits);
        const bool tray_is_loaded      = IsSlotLoadedInExtruder(machine_obj, tray_ams_id, tray_id);
        const bool tray_has_filament   = tray->is_exists && tray_state != SV::tray_state::empty;
        const nlohmann::json tray_actions = {
            {SK::tray_actions::can_read,   tray->is_exists && !is_reading_now},
            // Only one of load / unload should show: hide "load" once the tray
            // is already feeding the extruder, hide "unload" otherwise.
            {SK::tray_actions::can_load,   tray_has_filament && !tray_is_loaded && !is_reading_now},
            {SK::tray_actions::can_unload, tray_is_loaded && !is_reading_now}
        };

        trays.push_back({
            {SK::ams_tray::ams_id,        tray_ams_id},
            {SK::ams_tray::slot_id,       tray_id},
            {SK::ams_tray::exists,        tray->is_exists},
            {SK::ams_tray::tray_state,    tray_state},
            {SK::ams_tray::filament_type, tray->m_fila_type},
            {SK::ams_tray::filament_name, !tray->sub_brands.empty() ? tray->sub_brands : tray->m_fila_type},
            {SK::ams_tray::color,         normalize_hex_for_web(tray->color)},
            {SK::ams_tray::color_type,    color_type_for_web(tray->ctype)},
            {SK::ams_tray::colors,        colors},
            {SK::ams_tray::remain,        tray->remain},
            {SK::ams_tray::actions,       tray_actions}
        });
    }

    return {
        {SK::ams_unit::ams_id,       ams_unit->GetAmsId()},
        {SK::ams_unit::ams_type,     std::to_string((int)ams_unit->GetAmsType())},
        {SK::ams_unit::slot_count,   ams_unit->GetSlotCount()},
        {SK::ams_unit::humidity_raw, ams_unit->SupportHumidityPercent() ? ams_unit->GetHumidityPercent() : -1},
        {SK::ams_unit::dry_status,   ams_unit->GetDryStatus().has_value() ? dry_status_short_name(*ams_unit->GetDryStatus()) : ""},
        {SK::ams_unit::trays,        trays}
    };
}

std::string DevicePageAmsFilamentHotendVM::ResolveCardState(MachineObject* machine_obj,
                                                            const DevAmsTray* tray,
                                                            const std::string& ams_id,
                                                            const std::string& slot_id,
                                                            int nozzle_pos) const
{
    namespace SV = AmsHotendSchema::values;
    if (!tray || !tray->is_exists) {
        return SV::card_state::empty;
    }
    if (machine_obj && tray->is_reading(machine_obj->tray_reading_bits)) {
        return SV::card_state::reading;
    }

    //if (machine_obj && machine_obj->ams_status_main == AMS_STATUS_MAIN_UNKNOWN) {
    //    return SV::card_state::error;
    //}

    const bool listed_as_changing = IsSlotInFilamentChangeList(machine_obj, ams_id, slot_id);
    const bool is_filament_change = IsFilamentChangeActive(machine_obj);
    if (is_filament_change && listed_as_changing) {
        const bool loaded_in_extruder = IsSlotLoadedInExtruder(machine_obj, ams_id, slot_id);
        return loaded_in_extruder ? SV::card_state::unloading : SV::card_state::loading;
    }
    if (tray->setting_id.empty() || tray->m_fila_type.empty()) {
        return SV::card_state::unknown;
    }

    if (auto nozzle_opt = machine_obj->GetNozzleSystem()->GetNozzleOptByPosId(nozzle_pos); nozzle_opt.has_value() && nozzle_opt->HasFilamentInNozzle()) {
        return SV::card_state::ready_to_use;
    }
    
    return SV::card_state::ready_to_load;
}

// Web LineState follows the machine-side business states.
//
// `unknown` is treated as a flavor of `loaded`: filament is physically in the
// tube; the AMS just hasn't identified its type yet (the card shows "?").
// Without this mapping the feed line stays dashed grey for slots whose RFID
// failed to identify, which contradicts the Figma 1498:115500 design.
std::string DevicePageAmsFilamentHotendVM::ResolveLineState(const std::string& card_state, bool has_hotend_slot) const
{
    namespace SV = AmsHotendSchema::values;
    if (!has_hotend_slot) return SV::line_state::no_hotend;
    if (card_state == SV::card_state::loading)      return SV::line_state::loading;
    if (card_state == SV::card_state::unloading)    return SV::line_state::unloading;
    if (card_state == SV::card_state::ready_to_use) return SV::line_state::loaded;
    if (card_state == SV::card_state::unknown)      return SV::line_state::loaded;
    return SV::line_state::no_filament;
}

std::string DevicePageAmsFilamentHotendVM::ResolveNozzleState(const std::string& card_state, const std::string& line_state) const
{
    namespace SV = AmsHotendSchema::values;
    if (card_state == SV::card_state::error) return SV::nozzle_state::error;
    if (line_state == SV::line_state::loaded
        || line_state == SV::line_state::loading
        || line_state == SV::line_state::unloading) {
        return SV::nozzle_state::active;
    }
    return SV::nozzle_state::normal;
}

std::string DevicePageAmsFilamentHotendVM::ResolveStatusText(const std::string& card_state) const
{
    namespace SV = AmsHotendSchema::values;
    // Filament-card status_text is rendered as-is on the web; translate on
    // the C++ side so the payload carries the localized label.
    if (card_state == SV::card_state::reading)                                                 return _u8L("Reading");
    if (card_state == SV::card_state::loading || card_state == SV::card_state::unloading)      return _u8L("Loading/Unloading");
    if (card_state == SV::card_state::ready_to_load)                                           return _u8L("Ready to load");
    if (card_state == SV::card_state::ready_to_use)                                            return _u8L("Loaded");
    if (card_state == SV::card_state::empty)                                                   return "--";
    if (card_state == SV::card_state::unknown)                                                 return _u8L("Unknown filament");
    if (card_state == SV::card_state::error)                                                   return _u8L("Error");
    return "";
}

nlohmann::json DevicePageAmsFilamentHotendVM::BuildFilamentChangeStatus(MachineObject* machine_obj,
                                                                         const nlohmann::json& running_slot,
                                                                         const std::string& operation,
                                                                         const std::string& message) const
{
    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    nlohmann::json status = {
        {SK::filament_change::active,             IsFilamentChangeActive(machine_obj)},
        {SK::filament_change::mode,               SV::filament_change_mode::single},
        {SK::filament_change::operation,          operation.empty() ? std::string(SV::filament_change_operation::unknown) : operation},
        {SK::filament_change::running_slot,       running_slot},
        {SK::filament_change::current_step,       nullptr},
        {SK::filament_change::current_step_name,  ""},
        {SK::filament_change::steps,              nlohmann::json::array()},
        {SK::filament_change::step_labels,        nlohmann::json::array()},
        {SK::filament_change::current_step_index, 0},
        {SK::filament_change::current_step_total, 0},
        {SK::filament_change::message,            message},
        {SK::filament_change::tips,               _u8L("Other filaments are info-only")}
    };

    if (!machine_obj || !machine_obj->GetFilaSystem()) {
        return status;
    }

    auto fila_system = machine_obj->GetFilaSystem();
    const auto current_step = fila_system->GetCurrentFilamentChangeStep();
    if (current_step.has_value()) {
        status[SK::filament_change::current_step]      = static_cast<int>(*current_step);
        status[SK::filament_change::current_step_name] = filament_step_label(*current_step);
    }

    const auto steps = fila_system->GetFilamentChangeSteps();
    int current_index = 0;
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto step = steps[i];
        const std::string label = filament_step_label(step);
        if (label.empty()) {
            BOOST_LOG_TRIVIAL(warning) << "Unknown filament change step for web: " << static_cast<int>(step);
            continue;
        }
        status[SK::filament_change::steps].push_back(static_cast<int>(step));
        status[SK::filament_change::step_labels].push_back(label);
        if (current_step.has_value() && step == *current_step) {
            current_index = static_cast<int>(status[SK::filament_change::steps].size());
        }
    }
    status[SK::filament_change::current_step_index] = current_index;
    status[SK::filament_change::current_step_total] = static_cast<int>(status[SK::filament_change::steps].size());

    return status;
}

bool DevicePageAmsFilamentHotendVM::IsSlotLoadedInExtruder(MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id) const
{
    if (!machine_obj || ams_id.empty() || slot_id.empty()) {
        return false;
    }
    for (const auto& ext : machine_obj->GetExtderSystem()->GetExtruders()) {
        if (ext.GetSlotNow().ams_id == ams_id && ext.GetSlotNow().slot_id == slot_id && ext.HasFilamentInExt()) {
            return true;
        }
    }
    return false;
}

bool DevicePageAmsFilamentHotendVM::IsSlotTargetForExtruder(MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id) const
{
    if (!machine_obj || ams_id.empty() || slot_id.empty()) {
        return false;
    }
    for (const auto& ext : machine_obj->GetExtderSystem()->GetExtruders()) {
        const auto& target = ext.GetSlotTarget();
        if (target.ams_id == ams_id && target.slot_id == slot_id) {
            return true;
        }
    }
    return false;
}

bool DevicePageAmsFilamentHotendVM::IsSlotInFilamentChangeList(MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id) const
{
    if (!machine_obj || ams_id.empty() || slot_id.empty()) {
        return false;
    }
    auto fila_system = machine_obj->GetFilaSystem();
    if (!fila_system) {
        return false;
    }
    try {
        return fila_system->IsFilamentChangingSlot(std::stoi(ams_id), std::stoi(slot_id));
    } catch (...) {
        return false;
    }
}

bool DevicePageAmsFilamentHotendVM::IsFilamentChangeActive(MachineObject* machine_obj) const
{
    if (!machine_obj) {
        return false;
    }

    if (machine_obj->is_enable_np && machine_obj->GetExtderSystem()->IsBusyLoading()) {
        return true;
    } else if (machine_obj->ams_status_main == AMS_STATUS_MAIN_FILAMENT_CHANGE) {
        return true;
    }

    return false;
}

bool DevicePageAmsFilamentHotendVM::IsNozzleOnExtruder(MachineObject* machine_obj, int nozzle_pos) const
{
    if (!machine_obj || !machine_obj->GetExtderSystem()) {
        return false;
    }

    for (const auto& ext : machine_obj->GetExtderSystem()->GetExtruders()) {
        if (ext.HasNozzleInstalled()) {
            auto addrIdOpt = machine_obj->GetNozzleSystem()->GetExtNozzle(ext.GetExtId()).GetAddrId();
            if (addrIdOpt.has_value() && addrIdOpt.value() == nozzle_pos - 0x10) {
                return true;
            }
        }
    }

    return false;
}

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

nlohmann::json DevicePageAmsFilamentHotendVM::HandleDebug(const std::string& action, const nlohmann::json& payload)
{
#if !BBL_RELEASE_TO_PUBLIC
    if (action == "open_bridge_trace") {
        bridge_debug_snapshot() = BuildDebugSnapshot(payload);
        wxGetApp().CallAfter([]() {
            open_bridge_debug_dialog();
        });
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

nlohmann::json DevicePageAmsFilamentHotendVM::HandleAction(const std::string& action, const nlohmann::json& payload)
{
    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj) {
        return MakeResponse(GetModule(), "action", action, 1, _u8L("No selected machine"), BuildState());
    }

    const std::string ams_id = payload.value("ams_id", "");
    const std::string slot_id = payload.value("slot_id", "");
    auto fila_system = machine_obj->GetFilaSystem();

    if (action == "settings") {
        wxGetApp().CallAfter([]() {
            open_ams_settings();
        });
    } else if (action == "auto_refill") {
        wxGetApp().CallAfter([]() {
            open_auto_refill();
        });
    } else if (action == "ams_operation") {
        const nlohmann::json action_payload = payload;
        wxGetApp().CallAfter([action_payload]() {
            open_ext_ams_operation(action_payload);
        });
    } else if (action == "read_all_hotends") {
        // Same as wgtDeviceHotentPanel::ReadAll(): rack refresh all with GUI move warning.
        wxGetApp().CallAfter([]() {
            auto* dev_mgr = wxGetApp().getDeviceManager();
            MachineObject* mo = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
            if (!mo)
                return;
            if (auto rack = mo->GetNozzleSystem()->GetNozzleRack())
                rack->CtrlRackReadAll(true);
        });
    } else if (action == "read_hotend") {
        const int display_index = payload.value("display_index", 0);
        if (display_index <= 0) {
            return MakeResponse(GetModule(), "action", action, 2, _u8L("Invalid hotend display_index"), BuildState());
        }
        wxGetApp().CallAfter([display_index]() {
            auto* dev_mgr = wxGetApp().getDeviceManager();
            MachineObject* mo = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
            if (!mo)
                return;
            if (auto rack = mo->GetNozzleSystem()->GetNozzleRack())
                rack->CrtlRackReadNozzle(display_index - 1);
        });
    } else if (action == "set_hotend_temperature") {
        const int nozzle_pos = payload.value("nozzle_pos", -1);
        const int target_temp = payload.value("target_temp", -1);
        if (nozzle_pos < 0 || target_temp < 0 || target_temp > 350) {
            return MakeResponse(GetModule(), "action", action, 2, _u8L("Invalid hotend temperature payload"), BuildState());
        }
        auto nozzle_system = machine_obj->GetNozzleSystem();
        auto nozzle_rack = nozzle_system ? nozzle_system->GetNozzleRack() : nullptr;
        if (!nozzle_system || !nozzle_rack) {
            return MakeResponse(GetModule(), "action", action, 2, _u8L("No hotend rack"), BuildState());
        }
        const auto rack_nozzle = nozzle_system->GetNozzleByPosId(nozzle_pos);
        if (!rack_nozzle.IsExist() || rack_nozzle.IsAbnormal()) {
            return MakeResponse(GetModule(), "action", action, 2, _u8L("Hotend cannot set temperature"), BuildState());
        }
        nozzle_rack->SetNozzleTargetTemp(nozzle_pos, target_temp);
    } else if (action == "read" && fila_system && !ams_id.empty() && !slot_id.empty()) {
        if (const auto& ams_item = fila_system->GetAmsById(ams_id)) {
            try {
                if (machine_obj->is_enable_np || machine_obj->is_enable_ams_np) {
                    machine_obj->command_ams_refresh_rfid2(std::stoi(ams_id), std::stoi(slot_id));
                } else {
                    machine_obj->command_ams_refresh_rfid(std::to_string(ams_item->GetTrayId(std::stoi(slot_id))));
                }
            } catch (const std::exception& e) {
                return MakeResponse(GetModule(), "action", action, 2, e.what(), BuildState());
            }
        }
    } else if (action == "edit" && fila_system && !ams_id.empty() && !slot_id.empty()) {
        wxGetApp().CallAfter([ams_id, slot_id]() {
            open_ams_materials_setting(ams_id, slot_id);
        });
    } else if (action == "load" && fila_system && !ams_id.empty() && !slot_id.empty()) {
        try {
            int old_temp = -1;
            int new_temp = -1;
            auto target_tray = machine_obj->get_tray(ams_id, slot_id);
            if (target_tray && !target_tray->nozzle_temp_max.empty() && !target_tray->nozzle_temp_min.empty()) {
                new_temp = (atoi(target_tray->nozzle_temp_min.c_str()) + atoi(target_tray->nozzle_temp_max.c_str())) / 2;
            }
            if (DevAmsTray* curr_tray = machine_obj->get_curr_tray()) {
                if (!curr_tray->nozzle_temp_max.empty() && !curr_tray->nozzle_temp_min.empty()) {
                    old_temp = (atoi(curr_tray->nozzle_temp_min.c_str()) + atoi(curr_tray->nozzle_temp_max.c_str())) / 2;
                }
            }
            machine_obj->command_ams_change_filament(true, ams_id, slot_id, old_temp, new_temp, 0);
        } catch (const std::exception& e) {
            return MakeResponse(GetModule(), "action", action, 2, e.what(), BuildState());
        }
    } else if (action == "unload" && fila_system && !ams_id.empty() && !slot_id.empty()) {
        try {
            for (const auto& ext : machine_obj->GetExtderSystem()->GetExtruders()) {
                if (ext.GetSlotNow().ams_id == ams_id && ext.GetSlotNow().slot_id == slot_id) {
                    machine_obj->command_ams_change_filament(false, ams_id, "255");
                    break;
                }
            }
        } catch (const std::exception& e) {
            return MakeResponse(GetModule(), "action", action, 2, e.what(), BuildState());
        }
    } else if (action == "stop_filament_change") {
        const int result = machine_obj->command_ams_control("abort");
        if (result != 0) {
            return MakeResponse(GetModule(), "action", action, 2, "AMS filament change abort failed", BuildState());
        }
    }

    return MakeResponse(GetModule(), "action", action, 0, "", BuildState());
}

}} // namespace Slic3r::GUI
