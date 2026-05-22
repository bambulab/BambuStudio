#include "DevicePageAmsFilamentHotendVM.hpp"
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

// Stable dry-state labels for the Web payload.
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
    switch (step) {
    case DevFilamentStep::STEP_IDLE:               return "Idling...";
    case DevFilamentStep::STEP_PAUSE:              return "Pause";
    case DevFilamentStep::STEP_HEAT_NOZZLE:        return "Heat the nozzle";
    case DevFilamentStep::STEP_CUT_FILAMENT:       return "Cut filament";
    case DevFilamentStep::STEP_PULL_CURR_FILAMENT: return "Pull back current filament";
    case DevFilamentStep::STEP_PUSH_NEW_FILAMENT:  return "Push new filament into extruder";
    case DevFilamentStep::STEP_GRAB_NEW_FILAMENT:  return "Grab new filament";
    case DevFilamentStep::STEP_PURGE_OLD_FILAMENT: return "Purge old filament";
    case DevFilamentStep::STEP_CHECK_POSITION:     return "Check filament location";
    case DevFilamentStep::STEP_SWITCH_EXTRUDER:    return "Switch extruder";
    case DevFilamentStep::STEP_SWITCH_HOTEND:      return "Switch hotend";
    case DevFilamentStep::STEP_AMS_FILA_COOLING:   return "Wait for AMS cooling";
    case DevFilamentStep::STEP_PUSH_SWITCHER_FILA: return "Switch current filament at Filament Track Switch";
    case DevFilamentStep::STEP_PULL_SWITCHER_FILA: return "Pull back current filament at Filament Track Switch";
    case DevFilamentStep::STEP_SWITCHER_SWITCH:    return "Switch track at Filament Track Switch";
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
    nlohmann::json data;
    data["selected_dev_id"] = "";
    data["hotend_slots"] = nlohmann::json::array();
    data["actions"] = {
        {"can_load", true},
        {"can_load_external", true},
        {"can_load_auto", true},
        {"can_unload", true},
        {"can_dry", true},
        {"can_read_all_hotends", false},
        {"read_all_disabled_reason", ""},
        {"show_ams_settings", false},
        {"show_auto_refill", false},
        {"tips", ""}
    };
    data["hotend_rack_status"] = {
        {"active", false},
        {"mode", "idle"},
        {"display_index", 0},
        {"current", 0},
        {"total", 0},
        {"message", ""},
        {"tips", ""}
    };
    data["filament_change_status"] = {
        {"active", false},
        {"mode", "idle"},
        {"message", ""},
        {"tips", ""}
    };

    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj) {
        data["actions"]["tips"] = "No selected machine";
        return data;
    }

    data["selected_dev_id"] = machine_obj->get_dev_id();
    auto nozzle_system = machine_obj->GetNozzleSystem();
    auto nozzle_rack = nozzle_system ? nozzle_system->GetNozzleRack() : nullptr;
    const int reading_idx = nozzle_rack ? nozzle_rack->GetReadingIdx() : 0;
    const int reading_count = nozzle_rack ? nozzle_rack->GetReadingCount() : 0;
    const bool rack_reading = reading_count > 0;
    const bool filament_changing = IsFilamentChangeActive(machine_obj);
    data["hotend_rack_status"] = {
        {"active", rack_reading},
        {"mode", rack_reading ? (reading_count > 1 ? "all" : "single") : "idle"},
        {"display_index", rack_reading ? reading_idx : 0},
        {"current", rack_reading ? reading_idx : 0},
        {"total", reading_count},
        {"message", rack_reading ? (reading_count > 1
            ? (boost::format("读取中（%1% / %2%）") % reading_idx % reading_count).str()
            : (boost::format("%1%号热端读取中") % reading_idx).str()) : ""},
        {"tips", rack_reading ? "请稍后..." : ""}
    };
    const bool can_read_all = nozzle_rack && nozzle_rack->CtrlCanReadAll() && !rack_reading;
    data["actions"]["can_read_all_hotends"] = can_read_all;
    if (!can_read_all) {
        data["actions"]["read_all_disabled_reason"] = rack_reading ? "Hotend rack is reading" : "Hotend rack cannot read all";
    }
    if (filament_changing) {
        data["actions"]["can_load"] = false;
        data["actions"]["can_load_external"] = false;
        data["actions"]["can_load_auto"] = false;
        data["actions"]["can_unload"] = false;
    }

    if (auto fila_system = machine_obj->GetFilaSystem()) {
        const bool has_connected_ams = !fila_system->GetAmsList().empty() && machine_obj->ams_exist_bits != 0;
        data["actions"]["show_ams_settings"] = has_connected_ams;
        data["actions"]["show_auto_refill"] = has_connected_ams;

        data["ams"] = {
            {"units", nlohmann::json::array()},
            {"zones", nlohmann::json::array()}
        };
        for (const auto& [_, ams_unit] : fila_system->GetAmsList()) {
            if (!ams_unit) continue;

            nlohmann::json trays = nlohmann::json::array();
            for (const auto& [tray_id, tray] : ams_unit->GetTrays()) {
                if (!tray) continue;

                std::vector<std::string> colors = tray->cols;
                if (colors.empty() && !tray->color.empty()) {
                    colors.push_back(tray->color);
                }
                for (auto& c : colors) {
                    c = normalize_hex_for_web(c);
                }

                std::string tray_state = "ready";
                if (!tray->is_exists) {
                    tray_state = "empty";
                } else if (machine_obj && tray->is_reading(machine_obj->tray_reading_bits)) {
                    tray_state = "reading";
                } else if (tray->setting_id.empty() || tray->m_fila_type.empty()) {
                    tray_state = "unknown";
                }

                trays.push_back({
                    {"ams_id", ams_unit->GetAmsId()},
                    {"slot_id", tray_id},
                    {"exists", tray->is_exists},
                    {"tray_state", tray_state},
                    {"filament_type", tray->m_fila_type},
                    {"filament_name", !tray->sub_brands.empty() ? tray->sub_brands : tray->m_fila_type},
                    {"color", normalize_hex_for_web(tray->color)},
                    {"color_type", color_type_for_web(tray->ctype)},
                    {"colors", colors},
                    {"reading", machine_obj && tray->is_reading(machine_obj->tray_reading_bits)},
                    {"remain", tray->remain}
                });
            }

            data["ams"]["units"].push_back({
                {"ams_id", ams_unit->GetAmsId()},
                {"ams_type", std::to_string((int)ams_unit->GetAmsType())},
                {"slot_count", ams_unit->GetSlotCount()},
                {"exist", ams_unit->IsExist()},
                {"temp", ams_unit->GetCurrentTemperature()},
                {"humidity", ams_unit->SupportHumidityLevel() ? ams_unit->GetHumidityLevel() : -1},
                {"humidity_raw", ams_unit->SupportHumidityPercent() ? ams_unit->GetHumidityPercent() : -1},
                {"dry_time", ams_unit->GetLeftDryTime()},
                {"dry_status", ams_unit->GetDryStatus().has_value() ? dry_status_short_name(*ams_unit->GetDryStatus()) : ""},
                {"dry_sub_status", ams_unit->GetDrySubStatus().has_value() ? std::to_string((int)*ams_unit->GetDrySubStatus()) : ""},
                {"trays", trays}
            });

            for (const auto& [zone_id, zone] : ams_unit->GetZones()) {
                data["ams"]["zones"].push_back({
                    {"ams_id", ams_unit->GetAmsId()},
                    {"zone_id", zone.zone_id},
                    {"zone_slots", zone.zone_slots},
                    {"temp", zone.current_temperature},
                    {"humidity", zone.humidity_level},
                    {"humidity_raw", zone.humidity_percent},
                    {"dry_time", zone.left_dry_time},
                    {"dry_status", zone.dry_status.has_value() ? dry_status_short_name(*zone.dry_status) : ""},
                    {"dry_sub_status", zone.dry_sub_status.has_value() ? std::to_string((int)*zone.dry_sub_status) : ""},
                    {"info", zone.info}
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

    std::string fallback_unloading_tip;
    nlohmann::json running_change_slot = nullptr;
    std::string running_change_operation = "change";
    std::string running_change_message;
    for (int nozzle_pos : nozzle_positions) {
        nlohmann::json slot = BuildSlot(machine_obj, nozzle_pos, nozzle_slots[nozzle_pos], fallback_dryer_unit);
        if (filament_changing) {
            const std::string card_state = slot.value("card_state", std::string());
            if (card_state == "Loading") {
                data["actions"]["tips"] = (boost::format("%1%号%2%")
                    % slot.value("display_index", 0)
                    % "进/退料中").str();
                if (running_change_slot.is_null()) {
                    running_change_message = data["actions"]["tips"].get<std::string>();
                    running_change_slot = {
                        {"ams_id", slot.value("ams_id", std::string())},
                        {"slot_id", slot.value("slot_id", std::string())},
                        {"display_index", slot.value("display_index", 0)}
                    };
                }
            } else if (card_state == "Unloading" && fallback_unloading_tip.empty()) {
                fallback_unloading_tip = (boost::format("%1%号%2%")
                    % slot.value("display_index", 0)
                    % "进/退料中").str();
                if (running_change_slot.is_null()) {
                    running_change_message = fallback_unloading_tip;
                    running_change_slot = {
                        {"ams_id", slot.value("ams_id", std::string())},
                        {"slot_id", slot.value("slot_id", std::string())},
                        {"display_index", slot.value("display_index", 0)}
                    };
                }
            }
        }
        data["hotend_slots"].push_back(std::move(slot));
    }
    if (filament_changing && data["actions"]["tips"].get<std::string>().empty() && !fallback_unloading_tip.empty()) {
        data["actions"]["tips"] = fallback_unloading_tip;
    }
    if (filament_changing) {
        if (running_change_message.empty()) {
            running_change_message = data["actions"]["tips"].get<std::string>();
        }
        data["filament_change_status"] = BuildFilamentChangeStatus(machine_obj, running_change_slot, running_change_operation, running_change_message);
    }

    return data;
}

nlohmann::json DevicePageAmsFilamentHotendVM::BuildSlot(MachineObject* machine_obj,
                                                        int nozzle_pos,
                                                        const std::optional<std::pair<int, int>>& slot_id,
                                                        const Slic3r::DevAms* fallback_dryer_unit) const
{
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
    const std::string card_state = ResolveCardState(machine_obj, tray, ams_id, tray_id);
    const std::string line_state = ResolveLineState(card_state, has_hotend_slot);
    const std::string nozzle_state = ResolveNozzleState(card_state, line_state);

    nlohmann::json bind_ams = nlohmann::json::array();
    int bind_ams_slot_count = 0;
    if (machine_obj && machine_obj->GetNozzleSystem() && machine_obj->GetFilaSystem()) {
        const auto& rack_nozzle = machine_obj->GetNozzleSystem()->GetNozzleByPosId(nozzle_pos);
        for (int bind_ams_id : rack_nozzle.GetBindAms()) {
            const std::string bind_ams_id_str = std::to_string(bind_ams_id);
            const DevAms* bind_ams_unit = machine_obj->GetFilaSystem()->GetAmsById(bind_ams_id_str);
            const int slot_count = bind_ams_unit ? bind_ams_unit->GetSlotCount() : 0;
            bind_ams_slot_count += slot_count;
            bind_ams.push_back({
                {"ams_id", bind_ams_id_str},
                {"ams_type", bind_ams_unit ? std::to_string((int)bind_ams_unit->GetAmsType()) : ""},
                {"slot_count", slot_count}
            });
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

    // Dryer UI is slot-level; humidity/status are AMS-level.
    bool         in_dryer            = false;
    int          dryer_humidity_pct  = -1;
    std::string  dryer_status_text;
    if (machine_obj && slot_id.has_value() && !ams_id.empty()) {
        if (auto fila_system = machine_obj->GetFilaSystem()) {
            if (auto zone_item = fila_system->GetZoneBySlot(slot_id->first, slot_id->second)) {
                // to be fixed by AP, to use zone_item->SupportDrying()
                if (slot_id->second == 5 || slot_id->second == 7) {
                    in_dryer = true;
                    dryer_humidity_pct = zone_item->GetHumidityPercent();
                    if (auto status = zone_item->GetDryStatus()) {
                        dryer_status_text = dry_status_short_name(*status);
                    } else {
                        dryer_status_text = "Idle";
                    }
                }
            } else if(auto ams_unit = fila_system->GetAmsById(ams_id)){
                if (ams_unit->SupportDrying()) {
                    in_dryer = true;

                    if (ams_unit->SupportHumidityPercent()) {
                        dryer_humidity_pct = ams_unit->GetHumidityPercent();
                    }

                    if (auto status = ams_unit->GetDryStatus()) {
                        dryer_status_text = dry_status_short_name(*status);
                    } else {
                        dryer_status_text = "Idle";
                    }
                }
            }
        }
    }

    // ---- Hotend tab data (Figma 965:31757) ----
    nlohmann::json hotend;
    {
        auto rack_nozzle_info = machine_obj->GetNozzleSystem()->GetNozzleByPosId(nozzle_pos);
        const bool has_tool = has_hotend_slot && rack_nozzle_info.IsExist();
        const bool is_on_extruder = IsNozzleOnExtruder(machine_obj, nozzle_pos);

        std::string op_state = "Idle";

        if (has_tool && rack_nozzle_info.IsAbnormal()) {
            op_state = "Error";
        } else if (auto temp_status = rack_nozzle_info.GetSetRackNozzleTempStatus()) {
            switch (*temp_status) {
            case DevNozzle::NozzleTempStatus::Heating: op_state = "Heating"; break;
            case DevNozzle::NozzleTempStatus::Cooling: op_state = "Cooling"; break;
            case DevNozzle::NozzleTempStatus::Keeping:
            case DevNozzle::NozzleTempStatus::Idle:
            default:
                op_state = "Idle";
                break;
            }
        } else if (rack_nozzle_info.GetTargetTemp() > rack_nozzle_info.GetCurrentTemp()) {
            op_state = "Heating";
        } else if (rack_nozzle_info.GetCurrentTemp() > rack_nozzle_info.GetTargetTemp() && rack_nozzle_info.GetTargetTemp() > 0) {
            op_state = "Cooling";
        }
        if (auto rack = machine_obj->GetNozzleSystem()->GetNozzleRack(); rack && rack->GetReadingCount() == 1 && rack->GetReadingIdx() == display_idx) {
            op_state = "Reading";
        }

        hotend["op_state"]       = has_tool ? op_state : "Idle";
        hotend["tool_kind"]      = has_tool ? "nozzle" : "empty";
        hotend["nozzle_label"]   = has_tool ? rack_nozzle_info.GetNozzleDiameterStr().ToStdString() : "--";
        hotend["nozzle_subtext"] = has_tool ? nozzle_flow_short_name(rack_nozzle_info.GetNozzleFlowType()) : "";
        if (has_tool) {
            hotend["current_temp"] = rack_nozzle_info.GetCurrentTemp();
        } else {
            hotend["current_temp"] = nullptr;
        }
        hotend["target_temp"]    = has_tool ? rack_nozzle_info.GetTargetTemp() : 0;
        hotend["selected"]       = is_on_extruder;
    }

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
        {"can_edit", card_state != "Empty"},
        {"can_load", line_state == "no_filament" && card_state != "Empty"},
        {"can_unload", line_state == "loaded"},
        {"can_read", card_state != "Reading" && card_state != "Empty"},
        {"can_ams_operation", bind_ams_slot_count > 0}
    };

    return {
        {"nozzle_pos", nozzle_pos},
        {"display_index", display_idx},
        {"ams_id", ams_id},
        {"slot_id", tray_id},
        {"bind_ams", bind_ams},
        {"bind_ams_slot_count", bind_ams_slot_count},
        {"card_state", card_state},
        {"line_state", line_state},
        {"nozzle_state", nozzle_state},
        {"filament_name", filament_name},
        {"filament_type", filament_type},
        {"color", color},
        {"color_type", color_type},
        {"filament_remain", filament_remain},
        {"status_text", ResolveStatusText(card_state)},
        {"enabled", has_hotend_slot},
        {"is_on_extruder", is_on_extruder},
        {"is_external_spool", is_external_spool},
        {"is_top_tier", is_top_tier},
        {"in_dryer", in_dryer},
        {"dryer_humidity_pct", dryer_humidity_pct},
        {"dryer_status_text", dryer_status_text},
        {"menu_actions", menu_actions},
        {"hotend", hotend}
    };
}

std::string DevicePageAmsFilamentHotendVM::ResolveCardState(MachineObject* machine_obj,
                                                            const DevAmsTray* tray,
                                                            const std::string& ams_id,
                                                            const std::string& slot_id) const
{
    if (!tray || !tray->is_exists) {
        return "Empty";
    }
    if (machine_obj && tray->is_reading(machine_obj->tray_reading_bits)) {
        return "Reading";
    }
    if (machine_obj && machine_obj->ams_status_main == AMS_STATUS_MAIN_UNKNOWN) {
        return "Error";
    }

    const bool loaded_in_extruder = IsSlotLoadedInExtruder(machine_obj, ams_id, slot_id);
    const bool listed_as_changing = IsSlotInFilamentChangeList(machine_obj, ams_id, slot_id);
    const bool is_filament_change = IsFilamentChangeActive(machine_obj);
    if (is_filament_change && listed_as_changing) {
        return loaded_in_extruder ? "Unloading" : "Loading";
    }
    if (tray->setting_id.empty() || tray->m_fila_type.empty()) {
        return "Unknown";
    }
    return loaded_in_extruder ? "ReadyToUse" : "ReadyToLoad";
}

// Web LineState follows the machine-side business states.
std::string DevicePageAmsFilamentHotendVM::ResolveLineState(const std::string& card_state, bool has_hotend_slot) const
{
    if (!has_hotend_slot) return "no_hotend";
    if (card_state == "Loading") return "loading";
    if (card_state == "Unloading") return "unloading";
    if (card_state == "ReadyToUse") return "loaded";
    return "no_filament";
}

std::string DevicePageAmsFilamentHotendVM::ResolveNozzleState(const std::string& card_state, const std::string& line_state) const
{
    if (card_state == "Error") return "Error";
    if (line_state == "loaded" || line_state == "loading" || line_state == "unloading") return "Active";
    return "Normal";
}

std::string DevicePageAmsFilamentHotendVM::ResolveStatusText(const std::string& card_state) const
{
    if (card_state == "Reading") return "读取中";
    if (card_state == "Loading" || card_state == "Unloading") return "进/退料中";
    if (card_state == "ReadyToLoad") return "待进料";
    if (card_state == "ReadyToUse") return "已进料";
    if (card_state == "Empty") return "--";
    if (card_state == "Unknown") return "未知耗材";
    if (card_state == "Error") return "错误状态";
    return "";
}

nlohmann::json DevicePageAmsFilamentHotendVM::BuildFilamentChangeStatus(MachineObject* machine_obj,
                                                                         const nlohmann::json& running_slot,
                                                                         const std::string& operation,
                                                                         const std::string& message) const
{
    nlohmann::json status = {
        {"active", IsFilamentChangeActive(machine_obj)},
        {"mode", "single"},
        {"operation", operation.empty() ? "unknown" : operation},
        {"running_slot", running_slot},
        {"current_step", nullptr},
        {"current_step_name", ""},
        {"steps", nlohmann::json::array()},
        {"step_labels", nlohmann::json::array()},
        {"current_step_index", 0},
        {"current_step_total", 0},
        {"message", message},
        {"tips", "其他耗材仅可编辑信息"}
    };

    if (!machine_obj || !machine_obj->GetFilaSystem()) {
        return status;
    }

    auto fila_system = machine_obj->GetFilaSystem();
    const auto current_step = fila_system->GetCurrentFilamentChangeStep();
    if (current_step.has_value()) {
        status["current_step"] = static_cast<int>(*current_step);
        status["current_step_name"] = filament_step_label(*current_step);
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
        status["steps"].push_back(static_cast<int>(step));
        status["step_labels"].push_back(label);
        if (current_step.has_value() && step == *current_step) {
            current_index = static_cast<int>(status["steps"].size());
        }
    }
    status["current_step_index"] = current_index;
    status["current_step_total"] = static_cast<int>(status["steps"].size());

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

    return machine_obj->ams_status_main == AMS_STATUS_MAIN_FILAMENT_CHANGE;
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
        return MakeResponse(GetModule(), "action", action, 1, "No selected machine", BuildState());
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
            return MakeResponse(GetModule(), "action", action, 2, "Invalid hotend display_index", BuildState());
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
            return MakeResponse(GetModule(), "action", action, 2, "Invalid hotend temperature payload", BuildState());
        }
        auto nozzle_system = machine_obj->GetNozzleSystem();
        auto nozzle_rack = nozzle_system ? nozzle_system->GetNozzleRack() : nullptr;
        if (!nozzle_system || !nozzle_rack) {
            return MakeResponse(GetModule(), "action", action, 2, "No hotend rack", BuildState());
        }
        const auto rack_nozzle = nozzle_system->GetNozzleByPosId(nozzle_pos);
        if (!rack_nozzle.IsExist() || rack_nozzle.IsAbnormal()) {
            return MakeResponse(GetModule(), "action", action, 2, "Hotend cannot set temperature", BuildState());
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
