#include "BuilderFilamentChangeStatus.hpp"
#include "BuilderFilamentManualPullStatus.hpp"
#include "ResolverSlotState.hpp"
#include "Schema.hpp"

#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <string>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

struct FilamentStepStyleInfo
{
    std::string style;
    std::string text;
};

int& filament_change_step_index_cache()
{
    static int cache = 0;
    return cache;
}

std::optional<DevAmsSlotId>& filament_change_slot_cache()
{
    static std::optional<DevAmsSlotId> slot;
    return slot;
}

void clear_cached_filament_change_step()
{
    filament_change_step_index_cache() = 0;
    filament_change_slot_cache().reset();
}

int clamp_filament_change_step_index(int current_index, const std::optional<DevAmsSlotId>& changing_slot)
{
    // Keep one active filament-change flow monotonic even if the device briefly
    // reports an older or missing step between pushes.
    int& cache = filament_change_step_index_cache();
    auto& cached_slot = filament_change_slot_cache();
    if (changing_slot.has_value() && cached_slot.has_value() && *changing_slot != *cached_slot) {
        cache = 0;
    }
    if (changing_slot.has_value()) {
        cached_slot = changing_slot;
    }

    if (current_index <= 0) {
        return cache;
    }

    if (current_index < cache) {
        return cache;
    }

    cache = current_index;
    return current_index;
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
    case DevFilamentStep::STEP_BATCH_PULL_FILA:    return _u8L("Batch pull back the filaments");
    case DevFilamentStep::STEP_NESTING_PRINT_TOOL: return _u8L("Nesting PrintTool");
    case DevFilamentStep::STEP_PURGE_FILAMENT:     return _u8L("Purge filament");
    }
    return {};
}

int filament_step_display_index(MachineObject* machine_obj, const DevAmsSlotId& slot)
{
    if (!machine_obj || !machine_obj->GetFilaSystem()) return slot.second + 1;

    DevAmsSlotId key = slot;
    if (machine_obj->GetFilaSystem()->IsVirtualTrayId(std::to_string(key.first))) key.second = 0;
    const auto slot_to_display_index = machine_obj->GetFilaSystem()->GetSlot2HotendDisplayIdx();
    if (auto it = slot_to_display_index.find(key); it != slot_to_display_index.end()) {
        return it->second;
    }

    return slot.second + 1;
}

std::string slot_unload_style_text(int display_index)
{
    return (boost::format(_u8L("Please manually pull out the filament from slot %1%.")) % display_index).str();
}

std::string hotend_unload_style_text(int display_index)
{
    return (boost::format(_u8L("Please manually pull out the filament from hotend %1% on the toolhead.")) % display_index).str();
}

std::string hotend_load_style_text(int display_index)
{
    return (boost::format(_u8L("Please manually insert the filament into the loading port of hotend %1% on the toolhead until it cannot be pushed any further.")) % display_index).str();
}

std::string slot_load_style_text(int display_index)
{
    return (boost::format(_u8L("Please manually insert the filament from slot %1% into the extruder loading port until it cannot be pushed any further.")) % display_index).str();
}

FilamentStepStyleInfo filament_step_style(MachineObject* machine_obj, std::optional<DevAmsSlotId> changing_slot, DevFilamentStep step)
{
    if (!machine_obj || !machine_obj->GetFilaSystem() || !machine_obj->GetNozzleRack() || !changing_slot.has_value()) {
        return {};
    }

    DevAmsType ams_type = DevAmsType::EXT_SPOOL;
    if (auto ams_item = machine_obj->GetFilaSystem()->GetAmsById(std::to_string(changing_slot->first)); ams_item) {
        ams_type = ams_item->GetAmsType();
    }

    namespace SV = AmsHotendSchema::values;
    const auto rack_type = machine_obj->GetNozzleRack()->GetRackType();
    const int display_index = filament_step_display_index(machine_obj, *changing_slot);

    if (step == DevFilamentStep::STEP_PULL_CURR_FILAMENT) {
        if ((rack_type == DevNozzleRack::RackType_A && ams_type == DevAmsType::ZMULTI_A) ||
            (rack_type == DevNozzleRack::RackType_B && ams_type == DevAmsType::ZMULTI_B)) {
            return { SV::filament_change_step_style::maunal_unload, slot_unload_style_text(display_index) };
        }
        if (machine_obj->GetNozzleRack()->IsDirectConnectedRack() && ams_type == DevAmsType::EXT_SPOOL) {
            return { SV::filament_change_step_style::maunal_unload, hotend_unload_style_text(display_index) };
        }
    }

    if (step == DevFilamentStep::STEP_PUSH_NEW_FILAMENT) {
        if (machine_obj->GetNozzleRack()->IsDirectConnectedRack() && ams_type == DevAmsType::EXT_SPOOL) {
            return { SV::filament_change_step_style::manual_load, hotend_load_style_text(display_index) };
        }
        if (rack_type == DevNozzleRack::RackType_B && ams_type == DevAmsType::ZMULTI_B) {
            return { SV::filament_change_step_style::manual_load, slot_load_style_text(display_index) };
        }
    }

    return {};
}

std::vector<DevAmsSlotId> get_filament_changing_slots(MachineObject* machine_obj)
{
    if (machine_obj && machine_obj->GetFilaSystem()) {
        return machine_obj->GetFilaSystem()->GetFilamentChangingSlots();
    }

    return {};
}

std::optional<DevAmsSlotId> get_current_changing_slot(MachineObject* machine_obj)
{
    if (!machine_obj || !machine_obj->GetFilaSystem()) return std::nullopt;
    return machine_obj->GetFilaSystem()->GetCurrentChangingSlot();
}

nlohmann::json build_running_slot_json(MachineObject* machine_obj, const std::optional<DevAmsSlotId>& changing_slot)
{
    if (!machine_obj || !machine_obj->GetFilaSystem() || !changing_slot.has_value()) {
        return nullptr;
    }

    namespace SK = AmsHotendSchema::keys;
    const std::string ams_id = std::to_string(changing_slot->first);
    const std::string slot_id = machine_obj->GetFilaSystem()->IsVirtualTrayId(ams_id) ? std::string("0") : std::to_string(changing_slot->second);
    const int display_index = filament_step_display_index(machine_obj, *changing_slot);

    return {
        {SK::running_slot::ams_id,        ams_id},
        {SK::running_slot::slot_id,       slot_id},
        {SK::running_slot::display_index, display_index}
    };
}

std::string resolve_filament_change_operation(MachineObject* machine_obj, const std::optional<DevAmsSlotId>& changing_slot)
{
    namespace SV = AmsHotendSchema::values;
    return machine_obj && machine_obj->is_target_slot_unload() ? SV::filament_change_operation::unload : SV::filament_change_operation::load;
}

std::string build_filament_change_title(MachineObject* machine_obj,
                                        const std::optional<DevAmsSlotId>& changing_slot,
                                        const std::string& operation)
{
    namespace SV = AmsHotendSchema::values;
    if (!machine_obj || !changing_slot.has_value()) return {};

    const int display_index = filament_step_display_index(machine_obj, *changing_slot);
    if (operation == SV::filament_change_operation::load) {
        return (boost::format(_u8L("Slot %1% filament loading in progress")) % display_index).str();
    }
    if (operation == SV::filament_change_operation::unload) {
        return (boost::format(_u8L("Slot %1% filament unloading in progress")) % display_index).str();
    }
    return {};
}

} // namespace

nlohmann::json DevicePageFilamentChangeStatusBuilder::Build(MachineObject* machine_obj)
{
    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    const bool change_active = IsActive(machine_obj);
    const bool can_stop = change_active && machine_obj && !machine_obj->is_in_printing() && !machine_obj->is_in_calibration();
    const auto changing_slot = change_active ? get_current_changing_slot(machine_obj) : std::nullopt;
    const nlohmann::json running_slot = build_running_slot_json(machine_obj, changing_slot);
    const std::string operation = change_active ? resolve_filament_change_operation(machine_obj, changing_slot) : std::string(SV::filament_change_operation::change);
    const std::string message = change_active ? build_filament_change_title(machine_obj, changing_slot, operation) : std::string();

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

    if (!machine_obj || !machine_obj->GetFilaSystem()) {
        clear_cached_filament_change_step();
        return status;
    }
    if (!change_active) {
        clear_cached_filament_change_step();
        return status;
    }

    auto fila_system = machine_obj->GetFilaSystem();
    status[SK::filament_change::can_retry] = change_active && fila_system->GetAmsTaskRunStatus() == AMS_TASK_RUN_ABNORMAL;

    auto current_step = fila_system->GetCurrentFilamentChangeStep();
    if (current_step.has_value()) {
        // fix the AP problem
        if (current_step.value() == DevFilamentStep::STEP_PULL_CURR_FILAMENT && get_filament_changing_slots(machine_obj).size() == 1) {
            auto steps = fila_system->GetCurrentFilamentChangeSteps();
            if (!steps.empty() && steps.back() == DevFilamentStep::STEP_BATCH_PULL_FILA) {
                current_step = std::optional<DevFilamentStep>(DevFilamentStep::STEP_BATCH_PULL_FILA);
            };
        };
    }

    int current_index = 0;
    auto steps = fila_system->GetCurrentFilamentChangeSteps();
    std::vector<DevFilamentStep> visible_steps;
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto step = steps[i];
        const std::string label = filament_step_label(step);
        if (label.empty()) {
            BOOST_LOG_TRIVIAL(warning) << "Unknown filament change step for web: " << static_cast<int>(step);
            continue;
        }
        visible_steps.push_back(step);
        const FilamentStepStyleInfo style_info = filament_step_style(machine_obj, changing_slot, step);
        status[SK::filament_change::steps].push_back({
            {SK::filament_change_step::id,         static_cast<int>(step)},
            {SK::filament_change_step::label,      label},
            {SK::filament_change_step::step_style, style_info.style},
            {SK::filament_change_step::step_style_text, style_info.text},
            {SK::filament_change_step::step_text,  ""}
        });
        status[SK::filament_change::step_labels].push_back(label);
        if (current_step.has_value() && step == *current_step) {
            current_index = static_cast<int>(status[SK::filament_change::steps].size());
        }
    }
    current_index = clamp_filament_change_step_index(current_index, changing_slot);
    if (current_index > static_cast<int>(visible_steps.size())) {
        current_index = static_cast<int>(visible_steps.size());
    }
    if (current_index > 0 && current_index <= static_cast<int>(visible_steps.size())) {
        current_step = visible_steps[static_cast<size_t>(current_index - 1)];
        status[SK::filament_change::current_step] = static_cast<int>(*current_step);
        status[SK::filament_change::current_step_name] = filament_step_label(*current_step);
    } else if (current_step.has_value()) {
        status[SK::filament_change::current_step] = static_cast<int>(*current_step);
        status[SK::filament_change::current_step_name] = filament_step_label(*current_step);
    }
    status[SK::filament_change::current_step_index] = current_index;
    status[SK::filament_change::current_step_total] = static_cast<int>(status[SK::filament_change::steps].size());

    if (current_step.has_value() && *current_step == DevFilamentStep::STEP_BATCH_PULL_FILA) {
        status[SK::filament_change::mode] = SV::filament_change_mode::batch_manual_pull;
        status[SK::filament_change::manual_pull] = DevicePageFilamentManualPullStatusBuilder::Build(machine_obj);
        return status;
    }

    const auto& changing_slots = get_filament_changing_slots(machine_obj);
    if (changing_slots.size() >= 2) {
        status[SK::filament_change::mode] = SV::filament_change_mode::batch_running;
        const std::string running_ams_id = running_slot.is_object() ? running_slot.value(SK::running_slot::ams_id, std::string()) : std::string();
        const std::string running_slot_id = running_slot.is_object() ? running_slot.value(SK::running_slot::slot_id, std::string()) : std::string();

        nlohmann::json items = nlohmann::json::array();
        int running_index_1based = 0;
        const auto slot_to_display_index = fila_system->GetSlot2HotendDisplayIdx();

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

    clear_cached_filament_change_step();
    return false;
}

}} // namespace Slic3r::GUI
