#include "BuilderFilamentChangeStatus.hpp"
#include "Schema.hpp"

#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <boost/log/trivial.hpp>

#include <map>
#include <string>

namespace Slic3r { namespace GUI {

namespace {

int addr_id_to_display_idx(int addr_id) { return addr_id + 1; }

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

} // namespace

nlohmann::json DevicePageFilamentChangeStatusBuilder::Build(MachineObject* machine_obj,
                                                            const nlohmann::json& running_slot,
                                                            const std::string& operation,
                                                            const std::string& message)
{
    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    const bool change_active = IsActive(machine_obj);
    // Stopping a filament change is only safe while the machine is idle. Block
    // it whenever a print job or calibration routine is in progress, so the
    // user cannot abort mid print/calibration.
    const bool can_stop = change_active && machine_obj && !machine_obj->is_in_printing() && !machine_obj->is_in_calibration();

    nlohmann::json status = {
        {SK::filament_change::active,             change_active},
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
        {SK::filament_change::tips,               _u8L("Other filaments are info-only")},
        {SK::filament_change::can_stop,           can_stop},
        {SK::filament_change::can_retry,          false}
    };
    if (!machine_obj || !machine_obj->GetFilaSystem()) return status;

    auto fila_system = machine_obj->GetFilaSystem();
    status[SK::filament_change::can_retry] = change_active && fila_system->GetAmsTaskRunStatus() == AMS_TASK_RUN_ABNORMAL;
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

bool DevicePageFilamentChangeStatusBuilder::IsActive(MachineObject* machine_obj)
{
    if (machine_obj && machine_obj->is_enable_np && machine_obj->GetExtderSystem()->IsBusyLoading()) {
        return true;
    }

    return false;
}

}} // namespace Slic3r::GUI
