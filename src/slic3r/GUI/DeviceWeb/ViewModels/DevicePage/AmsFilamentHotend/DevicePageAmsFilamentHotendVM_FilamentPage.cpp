#include "DevicePageAmsFilamentHotendVM.hpp"

#include "slic3r/GUI/AMSDryControl.hpp"
#include "slic3r/GUI/AMSMaterialsSetting.hpp"
#include "slic3r/GUI/AMSSetting.hpp"
#include "slic3r/GUI/AmsMappingPopup.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <vector>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r { namespace GUI {

namespace {

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

std::string filament_step_label(DevFilamentStep step)
{
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

int addr_id_to_display_idx(int addr_id) { return addr_id + 1; }

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
    } catch (...) { return; }

    wxWindow* parent = wxGetApp().mainframe;
    AMSMaterialsSetting dlg(parent, wxID_ANY);
    dlg.obj = machine_obj;
    dlg.ams_id = ams_id_int;
    dlg.slot_id = slot_id_int;

    std::string filament;
    std::string sn_number;
    std::string temp_max;
    std::string temp_min;
    wxString k_val = wxString::Format("%.3f", tray.k);
    wxString n_val = wxString::Format("%.3f", tray.n);
    wxColor color = DevAmsTray::decode_color(tray.color);

    std::vector<wxColour> cols;
    for (const auto& col : tray.cols) cols.push_back(DevAmsTray::decode_color(col));

    dlg.set_ctype(tray.ctype);
    dlg.ams_filament_id = tray.setting_id;
    if (dlg.ams_filament_id.empty()) dlg.set_empty_color(color);
    else {
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

void open_ams_settings()
{
    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj) return;

    wxWindow* parent = wxGetApp().mainframe;
    AMSSetting dlg(parent, wxID_ANY);
    dlg.UpdateByObj(machine_obj);
    dlg.ShowModal();
}

void open_auto_refill()
{
    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj) return;

    wxWindow* parent = wxGetApp().mainframe;
    AmsReplaceMaterialDialog dlg(parent);
    dlg.update_machine_obj(machine_obj);
    dlg.ShowModal();
}

} // namespace

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
        if (colors.empty() && !tray->color.empty()) colors.push_back(tray->color);
        for (auto& c : colors) c = normalize_hex_for_web(c);

        std::string tray_state = SV::tray_state::ready;
        if (!tray->is_exists) tray_state = SV::tray_state::empty;
        else if (machine_obj && tray->is_reading(machine_obj->tray_reading_bits)) tray_state = SV::tray_state::reading;
        else if (tray->setting_id.empty() || tray->m_fila_type.empty()) tray_state = SV::tray_state::unknown;

        const std::string tray_ams_id = ams_unit->GetAmsId();
        const bool is_reading_now = machine_obj && tray->is_reading(machine_obj->tray_reading_bits);
        const bool tray_is_loaded = IsSlotLoadedInExtruder(machine_obj, tray_ams_id, tray_id);
        const bool tray_has_filament = tray->is_exists && tray_state != SV::tray_state::empty;
        const nlohmann::json tray_actions = {
            {SK::tray_actions::can_read,   tray->is_exists && !is_reading_now},
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
                                                            int addr_id) const
{
    namespace SV = AmsHotendSchema::values;
    if (!tray || !tray->is_exists) return SV::card_state::empty;
    if (machine_obj && tray->is_reading(machine_obj->tray_reading_bits)) return SV::card_state::reading;
    if (tray->setting_id.empty() || tray->m_fila_type.empty()) return SV::card_state::unknown;

    const bool listed_as_changing = IsSlotInFilamentChangeList(machine_obj, ams_id, slot_id);
    const bool is_filament_change = IsFilamentChangeActive(machine_obj);
    if (is_filament_change && listed_as_changing) {
        const bool loaded_in_extruder = IsSlotLoadedInExtruder(machine_obj, ams_id, slot_id);
        return loaded_in_extruder ? SV::card_state::unloading : SV::card_state::loading;
    }

    if (auto nozzle_opt = machine_obj->GetNozzleSystem()->GetNozzleOptByAddrId(addr_id); nozzle_opt.has_value() && nozzle_opt->HasFilamentInNozzle()) {
        return SV::card_state::ready_to_use;
    }
    return SV::card_state::ready_to_load;
}

std::string DevicePageAmsFilamentHotendVM::ResolveLineState(const std::string& card_state,
                                                            bool has_hotend_slot,
                                                            bool filament_changing,
                                                            bool listed_as_changing,
                                                            bool loaded_in_nozzle) const
{
    namespace SV = AmsHotendSchema::values;
    if (!has_hotend_slot) return SV::line_state::no_hotend;
    if (filament_changing && listed_as_changing) return loaded_in_nozzle ? SV::line_state::unloading : SV::line_state::loading;
    if (card_state == SV::card_state::loading) return SV::line_state::loading;
    if (card_state == SV::card_state::unloading) return SV::line_state::unloading;
    if (card_state == SV::card_state::ready_to_use) return SV::line_state::loaded;
    if (card_state == SV::card_state::unknown) return loaded_in_nozzle ? SV::line_state::loaded : SV::line_state::no_filament;
    return SV::line_state::no_filament;
}

std::string DevicePageAmsFilamentHotendVM::ResolveNozzleState(const std::string& card_state, const std::string& line_state) const
{
    namespace SV = AmsHotendSchema::values;
    if (card_state == SV::card_state::error) return SV::nozzle_state::error;
    if (line_state == SV::line_state::loaded || line_state == SV::line_state::loading || line_state == SV::line_state::unloading) {
        return SV::nozzle_state::active;
    }
    return SV::nozzle_state::normal;
}

std::string DevicePageAmsFilamentHotendVM::ResolveStatusText(const std::string& card_state) const
{
    namespace SV = AmsHotendSchema::values;
    if (card_state == SV::card_state::reading) return _u8L("Reading");
    if (card_state == SV::card_state::loading || card_state == SV::card_state::unloading) return _u8L("Loading/Unloading");
    if (card_state == SV::card_state::ready_to_load) return _u8L("Ready to load");
    if (card_state == SV::card_state::ready_to_use) return _u8L("Loaded");
    if (card_state == SV::card_state::empty) return "--";
    if (card_state == SV::card_state::unknown) return _u8L("Unknown filament");
    if (card_state == SV::card_state::error) return _u8L("Error");
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
    if (!machine_obj || !machine_obj->GetFilaSystem()) return status;

    auto fila_system = machine_obj->GetFilaSystem();
    const auto current_step = fila_system->GetCurrentFilamentChangeStep();
    if (current_step.has_value()) {
        status[SK::filament_change::current_step] = static_cast<int>(*current_step);
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

    const auto& changing_slots = fila_system->GetFilamentChangingSlots();
    if (changing_slots.size() >= 2) {
        status[SK::filament_change::mode] = SV::filament_change_mode::batch_running;
        const std::string running_ams_id = running_slot.is_object() ? running_slot.value(SK::running_slot::ams_id, std::string()) : std::string();
        const std::string running_slot_id = running_slot.is_object() ? running_slot.value(SK::running_slot::slot_id, std::string()) : std::string();

        nlohmann::json items = nlohmann::json::array();
        int running_index_1based = 0;
        std::map<std::pair<int, int>, int> slot_to_display_index;
        if (machine_obj && machine_obj->GetNozzleRack() && machine_obj->GetNozzleSystem()) {
            const auto& rack_type = machine_obj->GetNozzleRack()->GetRackType();
            if (rack_type == DevNozzleRack::RackType_A || rack_type == DevNozzleRack::RackType_B) {
                const int rack_count = machine_obj->GetNozzleRack()->GetTotalRackNozzleCount();
                for (int addr_id = 0; addr_id < rack_count; ++addr_id) {
                    const auto& rack_nozzle_opt = machine_obj->GetNozzleSystem()->GetNozzleOptByAddrId(addr_id);
                    if (!rack_nozzle_opt.has_value()) continue;
                    const auto& ams_slots = rack_nozzle_opt->GetBindAmsSlots();
                    if (ams_slots.empty()) continue;
                    int ams_id = ams_slots[0].first;
                    int slot_id = ams_slots[0].second;
                    if (auto fila = machine_obj->GetFilaSystem(); fila && fila->GetVirtualTray(ams_id).has_value()) slot_id = 0;
                    slot_to_display_index[{ams_id, slot_id}] = addr_id_to_display_idx(addr_id);
                }
            }
        }

        for (size_t i = 0; i < changing_slots.size(); ++i) {
            const auto& s = changing_slots[i];
            const std::string ams_str = std::to_string(s.first);
            const std::string slot_str = std::to_string(s.second);
            int display_idx = static_cast<int>(i) + 1;
            if (auto it = slot_to_display_index.find({s.first, s.second}); it != slot_to_display_index.end()) display_idx = it->second;
            const bool is_running = (!running_ams_id.empty() && ams_str == running_ams_id && !running_slot_id.empty() && slot_str == running_slot_id);
            const std::string item_state = is_running ? std::string(SV::batch_item_state::running) : std::string(SV::batch_item_state::pending);
            if (is_running) running_index_1based = static_cast<int>(i) + 1;
            items.push_back({
                {SK::batch_progress_item::display_index, display_idx},
                {SK::batch_progress_item::state,         item_state},
                {SK::batch_progress_item::reason,        ""}
            });
        }
        status[SK::filament_change::batch_running] = {
            {SK::batch_progress::operation, operation.empty() ? std::string(SV::filament_change_operation::unknown) : operation},
            {SK::batch_progress::current,   running_index_1based},
            {SK::batch_progress::total,     static_cast<int>(changing_slots.size())},
            {SK::batch_progress::items,     items}
        };
    }

    return status;
}

bool DevicePageAmsFilamentHotendVM::IsSlotLoadedInExtruder(MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id) const
{
    if (!machine_obj || ams_id.empty() || slot_id.empty()) return false;
    for (const auto& ext : machine_obj->GetExtderSystem()->GetExtruders()) {
        if (ext.GetSlotNow().ams_id == ams_id && ext.GetSlotNow().slot_id == slot_id && ext.HasFilamentInExt()) return true;
    }
    return false;
}

bool DevicePageAmsFilamentHotendVM::IsSlotLoadedInNozzle(Slic3r::MachineObject* machine_obj, int addr_id, const std::string& ams_id, const std::string& slot_id) const
{
    if (machine_obj) {
        auto nozzle_opt = machine_obj->GetNozzleSystem()->GetNozzleOptByAddrId(addr_id);
        if (nozzle_opt && nozzle_opt->HasFilamentInNozzle()) {
            const auto& slots = nozzle_opt->GetBindAmsSlots();
            for (auto slot : slots) {
                if (std::to_string(slot.first) == ams_id && std::to_string(slot.second) == slot_id) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool DevicePageAmsFilamentHotendVM::IsSlotTargetForExtruder(MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id) const
{
    if (!machine_obj || ams_id.empty() || slot_id.empty()) return false;
    for (const auto& ext : machine_obj->GetExtderSystem()->GetExtruders()) {
        const auto& target = ext.GetSlotTarget();
        if (target.ams_id == ams_id && target.slot_id == slot_id) return true;
    }
    return false;
}

bool DevicePageAmsFilamentHotendVM::IsSlotInFilamentChangeList(MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id) const
{
    if (!machine_obj || ams_id.empty() || slot_id.empty()) return false;
    auto fila_system = machine_obj->GetFilaSystem();
    if (!fila_system) return false;
    try {
        return fila_system->IsFilamentChangingSlot(std::stoi(ams_id), std::stoi(slot_id));
    } catch (...) {
        return false;
    }
}

bool DevicePageAmsFilamentHotendVM::IsFilamentChangeActive(MachineObject* machine_obj) const
{
    if (!machine_obj) return false;
    if (machine_obj->is_enable_np && machine_obj->GetExtderSystem()->IsBusyLoading()) return true;
    if (machine_obj->ams_status_main == AMS_STATUS_MAIN_FILAMENT_CHANGE) return true;

    auto fila_sys = machine_obj->GetFilaSystem();
    if (machine_obj->ams_status_main == AMS_STATUS_MAIN_ASSIST && !fila_sys->GetFilamentChangeSteps().empty() && !fila_sys->GetFilamentChangingSlots().empty()) {
        return true;
    }

    return false;
}

static int s_command_unload_filament(MachineObject* obj_, const std::string& ams_id, const std::string& slot_id, int old_temp, int new_temp, std::optional<int> extruder_id)
{
    if (obj_) {
        json j;
        try {
            int ams_id_int = atoi(ams_id.c_str());
            int slot_id_int = atoi(slot_id.c_str());
            j["print"]["command"] = "ams_change_filament";
            j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
            j["print"]["curr_temp"] = old_temp;
            j["print"]["tar_temp"] = new_temp;
            j["print"]["ams_id"] = ams_id_int;
            j["print"]["target"] = 255;

            if (obj_->GetFilaSystem()->IsVirtualTrayId(ams_id)) {
                j["print"]["slot_id"] = 255; // the new protocol to mark unload
            } else {
                j["print"]["slot_id"] = slot_id_int;
            }

            if (extruder_id.has_value()) {
                j["print"]["extruder_id"] = *extruder_id;
            }

        } catch (const std::exception&) {}

        return obj_->publish_json(j);
    }
}

std::optional<nlohmann::json> DevicePageAmsFilamentHotendVM::HandleFilamentAction(const std::string& action,
                                                                                  const nlohmann::json& payload,
                                                                                  MachineObject* machine_obj)
{
    const std::string ams_id = payload.value("ams_id", "");
    const std::string slot_id = payload.value("slot_id", "");
    auto fila_system = machine_obj->GetFilaSystem();

    if (action == "settings") {
        wxGetApp().CallAfter([]() { open_ams_settings(); });
    } else if (action == "auto_refill") {
        wxGetApp().CallAfter([]() { open_auto_refill(); });
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
        wxGetApp().CallAfter([this, ams_id, slot_id]() {
            open_ams_materials_setting(ams_id, slot_id);
            ReportState("state", "changed");
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

            s_command_unload_filament(machine_obj, ams_id, slot_id, old_temp, new_temp, 0);
        } catch (const std::exception& e) {
            return MakeResponse(GetModule(), "action", action, 2, e.what(), BuildState());
        }
    } else if (action == "dry") {
        std::string dry_ams_id = ams_id;
        if (dry_ams_id.empty() && fila_system) {
            for (const auto& [_, ams_unit] : fila_system->GetAmsList()) {
                if (ams_unit && ams_unit->SupportDrying()) {
                    dry_ams_id = ams_unit->GetAmsId();
                    break;
                }
            }
        }
        if (dry_ams_id.empty()) {
            return MakeResponse(GetModule(), "action", action, 2, "No dryable AMS unit found", BuildState());
        }
        wxGetApp().CallAfter([dry_ams_id]() {
            auto* dev_mgr = wxGetApp().getDeviceManager();
            MachineObject* mo = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
            if (!mo || !mo->GetFilaSystem()) return;
            wxWindow* parent = wxGetApp().mainframe;
            AMSDryCtrWin dlg(parent);
            dlg.set_ams_id(dry_ams_id);
            dlg.update(mo->GetFilaSystem(), mo);
            dlg.enableSelfRefresh(true);
            dlg.ShowModal();
        });
    } else if (action == "stop_filament_change") {
        const int result = machine_obj->command_ams_control("abort");
        if (result != 0) {
            return MakeResponse(GetModule(), "action", action, 2, "AMS filament change abort failed", BuildState());
        }
    } else if (action == "batch_run") {
        namespace SV = AmsHotendSchema::values;
        const std::string batch_mode = payload.value("mode", std::string());
        const auto& slots_array = payload.value("slots", nlohmann::json::array());
        if (slots_array.empty()) {
            return MakeResponse(GetModule(), "action", action, 2, _u8L("No slots selected for batch run"), BuildState());
        }
        const bool is_load = (batch_mode == SV::mode::batch_ams_load || batch_mode == SV::mode::batch_ext_load);
        std::vector<std::pair<int, int>> slot_pairs;
        slot_pairs.reserve(slots_array.size());
        for (const auto& s : slots_array) {
            try {
                const std::string ams_str = s.value("ams_id", std::string());
                const std::string slot_str = s.value("slot_id", std::string());
                if (ams_str.empty() || slot_str.empty()) continue;
                slot_pairs.emplace_back(std::stoi(ams_str), std::stoi(slot_str));
            } catch (const std::exception&) {}
        }
        if (slot_pairs.empty()) {
            return MakeResponse(GetModule(), "action", action, 2, _u8L("All selected slots are invalid"), BuildState());
        }
        const int rc = machine_obj->command_multi_slot_ams_change_filament(is_load, slot_pairs);
        if (rc != 0) {
            return MakeResponse(GetModule(), "action", action, 2, _u8L("MQTT publish failed for multi_slot_ams_change_filament"), BuildState());
        }
        m_active_mode = SV::mode::normal;
        return MakeResponse(GetModule(), "action", action, 0, "", BuildState());
    } else {
        return std::nullopt;
    }

    return MakeResponse(GetModule(), "action", action, 0, "", BuildState());
}

}} // namespace Slic3r::GUI
