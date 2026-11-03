#include "ViewModelStateBuilder.hpp"
#include "ViewModelActions.hpp"
#include "ViewModelExtruderStateBuilder.hpp"

#include "slic3r/GUI/DeviceCore/DevBed.h"
#include "slic3r/GUI/DeviceCore/DevChamber.h"
#include "slic3r/GUI/DeviceCore/DevConfigUtil.h"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFan.h"
#include "slic3r/GUI/DeviceCore/DevInfo.h"
#include "slic3r/GUI/DeviceCore/DevLamp.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceCore/DevAxis.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Slic3r { namespace GUI { namespace ControlPanel {

namespace Schema = ControlPanelSchema;
namespace Format = ControlPanelSchema::format;

namespace {

namespace ActionOps = ControlPanelActionOps;

constexpr int kTemperatureDeltaThreshold = 1;
constexpr int kExtrudeMinTemperature = 170;
constexpr int kDefaultNozzleMinTemp = 20;
constexpr int kDefaultNozzleMaxTemp = 300;
constexpr int kDefaultBedMinTemp = 0;
constexpr int kDefaultBedMaxTemp = 120;
constexpr int kDefaultChamberMinTemp = 0;
constexpr int kDefaultChamberMaxTemp = 80;

std::string temp_status(std::optional<int> current, std::optional<int> target)
{
    if (!current.has_value() || !target.has_value())
        return Schema::values::temp_status::idle;
    if (*target - *current >= kTemperatureDeltaThreshold)
        return Schema::values::temp_status::heating;
    if (*current - *target >= kTemperatureDeltaThreshold)
        return Schema::values::temp_status::cooling;
    return Schema::values::temp_status::idle;
}

std::string disabled_reason()
{
    return _u8L("This control is currently unavailable.");
}

Format::TempInputControl make_temp_control(bool enabled, const std::string& reason = {})
{
    Format::TempInputControl result;
    result.enabled = enabled;
    result.disabled_reason = enabled ? std::string() : reason;
    return result;
}

Format::SpeedControl make_speed_control(bool enabled, const std::string& reason = {})
{
    Format::SpeedControl result;
    result.selectable = enabled;
    result.disabled_reason = enabled ? std::string() : reason;
    result.levels = {
        {1, _u8L("Silent")},
        {2, _u8L("Standard")},
        {3, _u8L("Sport")},
        {4, _u8L("Ludicrous")},
    };
    return result;
}

Format::LampControl make_lamp_control(bool enabled, bool lamp_on, const std::string& reason = {})
{
    Format::LampControl result;
    result.switchable = enabled;
    result.disabled_reason = enabled ? std::string() : reason;
    result.next_lamp_on = !lamp_on;
    return result;
}

Format::FanControl make_fan_control(bool enabled, const std::string& reason = {})
{
    Format::FanControl result;
    result.openable = enabled;
    result.disabled_reason = enabled ? std::string() : reason;
    result.opens_dialog = true;
    return result;
}

Format::AxisMoveControl make_axis_control(bool enabled, const std::string& step, const std::string& reason = {})
{
    Format::AxisMoveControl result;
    result.movable = enabled;
    result.disabled_reason = enabled ? std::string() : reason;
    result.step = step;
    return result;
}

Format::RackControl make_rack_control(bool enabled, const std::string& target_pos, const std::string& reason = {})
{
    Format::RackControl result;
    result.movable = enabled;
    result.disabled_reason = enabled ? std::string() : reason;
    result.target_pos = target_pos;
    return result;
}

Format::ExtruderSwitchControl make_switch_control(bool enabled, int extruder_count, const std::string& reason = {})
{
    Format::ExtruderSwitchControl result;
    result.switchable = enabled;
    result.disabled_reason = enabled ? std::string() : reason;
    for (int id = 0; id < extruder_count; ++id)
        result.available_extruder_ids.push_back(id);
    return result;
}

bool has_machine(MachineObject* obj)
{
    return obj && obj->is_connected();
}

bool temp_control_enabled(MachineObject* obj)
{
    return has_machine(obj) && (!obj->GetInfo() || obj->GetInfo()->IsFdmMode());
}

bool motion_control_enabled(MachineObject* obj)
{
    if (!has_machine(obj)) return false;
    if (obj->is_in_printing() && !obj->can_resume()) return false;
    return true;
}

std::optional<int> int_temp(float value)
{
    if (!std::isfinite(value)) return std::nullopt;
    return static_cast<int>(std::lround(value));
}

Format::ExtruderTempItem build_extruder_temp(MachineObject* obj, int extruder_id, const std::string& id, const std::string& label)
{
    Format::ExtruderTempItem item;
    item.id = id;
    item.label = label;
    item.visible = false;
    item.min = kDefaultNozzleMinTemp;
    item.max = kDefaultNozzleMaxTemp;

    auto* extder_system = obj ? obj->GetExtderSystem() : nullptr;
    if (!obj || !extder_system)
        return item;

    const int total = extder_system->GetTotalExtderCount();
    item.visible = total >= 2 || extruder_id == MAIN_EXTRUDER_ID;

    if (obj->nozzle_temp_range.size() >= 2) {
        item.min = obj->nozzle_temp_range[0];
        item.max = obj->nozzle_temp_range[1];
    }

    const auto extder = extder_system->GetExtderById(extruder_id);
    const bool direct_rack = obj->GetNozzleRack() && obj->GetNozzleRack()->IsDirectConnectedRack();
    const bool unclamped = total == 1 && direct_rack && extder.has_value() && !extder->GetPickedNozzleAddrId().has_value();

    item.show_nozzle = !unclamped;
    item.editable = temp_control_enabled(obj) && extder.has_value() && extder->HasNozzleInstalled() && !unclamped;
    item.read_only_reason = item.editable ? std::string() : (unclamped ? _u8L("Unclamped") : disabled_reason());

    if (extder.has_value() && !unclamped) {
        item.current = extder->GetCurrentTemp();
        item.target = extder->GetTargetTemp();
    }
    item.temp_status = temp_status(item.current, item.target);

    if (direct_rack && extder.has_value() && extder->GetPickedNozzleAddrId().has_value()) {
        item.badge = true;
        item.badge_nozzle_index = extder->GetPickedNozzleAddrId().value() + 1;
    }
    return item;
}

void fill_temperature(MachineObject* obj, Format::State& state)
{
    auto& area = state.display.extruder_temp;
    auto* extder_system = obj ? obj->GetExtderSystem() : nullptr;
    area.visible = has_machine(obj);
    area.extruder_count = extder_system ? extder_system->GetTotalExtderCount() : 0;
    area.extruders.push_back(build_extruder_temp(obj, MAIN_EXTRUDER_ID, "main",
        DevPrinterConfigUtil::get_toolhead_display_name(obj ? obj->printer_type : std::string(),
                                                        MAIN_EXTRUDER_ID, ToolHeadComponent::Nozzle,
                                                        ToolHeadNameCase::TitleCase, true)));
    if (area.extruder_count >= 2) {
        area.extruders.insert(area.extruders.begin(), build_extruder_temp(obj, DEPUTY_EXTRUDER_ID, "deputy",
            DevPrinterConfigUtil::get_toolhead_display_name(obj ? obj->printer_type : std::string(),
                                                            DEPUTY_EXTRUDER_ID, ToolHeadComponent::Nozzle,
                                                            ToolHeadNameCase::TitleCase, true)));
    }

    auto& bed = state.display.bed_temp;
    bed.visible = has_machine(obj);
    bed.min = kDefaultBedMinTemp;
    bed.max = obj ? obj->get_bed_temperature_limit() : kDefaultBedMaxTemp;
    if (obj && obj->bed_temp_range.size() > 1) bed.max = obj->bed_temp_range[1];
    if (obj && obj->GetBed()) {
        bed.current = int_temp(obj->GetBed()->GetBedTemp());
        bed.target = int_temp(obj->GetBed()->GetBedTempTarget());
    }
    bed.editable = temp_control_enabled(obj);
    bed.read_only_reason = bed.editable ? std::string() : disabled_reason();
    bed.temp_status = temp_status(bed.current, bed.target);

    auto& chamber = state.display.chamber_temp;
    chamber.visible = has_machine(obj);
    chamber.min = kDefaultChamberMinTemp;
    chamber.max = kDefaultChamberMaxTemp;
    if (obj && obj->GetChamber()) {
        auto chamber_ptr = obj->GetChamber();
        chamber.visible = has_machine(obj) && chamber_ptr->SupportChamberTempDisplay();
        if (chamber_ptr->SupportChamberTempDisplay())
            chamber.current = int_temp(chamber_ptr->GetChamberTemp());
        if (chamber_ptr->SupportChamberEdit()) {
            chamber.target = int_temp(chamber_ptr->GetChamberTempTarget());
            chamber.min = chamber_ptr->GetChamberTempEditMin();
            chamber.max = chamber_ptr->GetChamberTempEditMax();
        }
        chamber.editable = temp_control_enabled(obj) && chamber_ptr->SupportChamberEdit();
    }
    chamber.read_only_reason = chamber.editable ? std::string() : disabled_reason();
    chamber.temp_status = temp_status(chamber.current, chamber.target);
}

void fill_misc_and_motion(MachineObject* obj, Format::State& state)
{
    auto& speed = state.display.speed;
    speed.visible = has_machine(obj);
    speed.speed_level = obj ? std::clamp(static_cast<int>(obj->GetPrintingSpeedLevel()), 1, 4) : 2;
    speed.speed_text = obj ? (std::to_string(obj->printing_speed_mag) + "%") : "100%";

    auto& lamp = state.display.lamp;
    lamp.visible = has_machine(obj);
    lamp.label = _u8L("Lamp");
    lamp.lamp_on = obj && obj->GetLamp() && obj->GetLamp()->IsChamberLightOn();

    auto& fan = state.display.fan;
    fan.fan_visible = has_machine(obj) && obj->GetInfo() && obj->GetInfo()->IsFdmMode();
    fan.fan_label = obj && obj->is_series_o() ? _u8L("Air Management") : _u8L("Fan");

    auto& motion = state.display.motion;
    motion.xy_visible = has_machine(obj);
    motion.z_visible = has_machine(obj);
    motion.e_visible = has_machine(obj);
    motion.z_label = obj && obj->GetAxis() && obj->GetAxis()->IsArchCoreXY() ? _u8L("Bed") : "Z";
}

void fill_rack(MachineObject* obj, Format::State& state)
{
    auto rack = obj ? obj->GetNozzleRack() : nullptr;
    state.display.rack.visible = rack && rack->IsSupported() && rack->IsDirectConnectedRack();
}

void fill_actions(MachineObject* obj, Format::State& state)
{
    const bool has = has_machine(obj);
    const bool temp_enabled = temp_control_enabled(obj);
    const bool motion_enabled = motion_control_enabled(obj);
    const bool force_disabled = wxGetApp().is_monitor_ctrl_force_disabled();
    const std::string reason = has ? disabled_reason() : _u8L("Please select a printer.");

    state.display.bed_temp.temp_input = make_temp_control(temp_enabled && !force_disabled, reason);
    state.display.chamber_temp.temp_input = make_temp_control(state.display.chamber_temp.editable && !force_disabled, reason);
    for (auto& item : state.display.extruder_temp.extruders) {
        const bool is_deputy = item.id == "deputy";
        const bool enabled = temp_enabled && !force_disabled &&
            (!is_deputy || state.display.extruder_temp.extruder_count >= 2);
        item.temp_input = make_temp_control(enabled, reason);
    }

    state.display.speed.speed_control = make_speed_control(has && !force_disabled, reason);
    state.display.lamp.lamp_control = make_lamp_control(has, state.display.lamp.lamp_on, reason);
    state.display.fan.fan_control = make_fan_control(has && state.display.fan.fan_visible && !force_disabled, reason);
    if (state.display.extruder.type == Schema::values::extruder_area_type::dual) {
        state.display.extruder.dual_extruder.switch_control =
            make_switch_control(has && motion_enabled && !force_disabled,
                                static_cast<int>(state.display.extruder.dual_extruder.extruders.size()),
                                reason);
    }

    state.display.motion.axis_moves[ActionOps::xy_home] = make_axis_control(motion_enabled && !force_disabled, "home", reason);
    state.display.motion.axis_moves[ActionOps::x_pos_10] = make_axis_control(motion_enabled && !force_disabled, "10", reason);
    state.display.motion.axis_moves[ActionOps::x_neg_10] = make_axis_control(motion_enabled && !force_disabled, "10", reason);
    state.display.motion.axis_moves[ActionOps::x_pos_1] = make_axis_control(motion_enabled && !force_disabled, "1", reason);
    state.display.motion.axis_moves[ActionOps::x_neg_1] = make_axis_control(motion_enabled && !force_disabled, "1", reason);
    state.display.motion.axis_moves[ActionOps::y_pos_10] = make_axis_control(motion_enabled && !force_disabled, "10", reason);
    state.display.motion.axis_moves[ActionOps::y_neg_10] = make_axis_control(motion_enabled && !force_disabled, "10", reason);
    state.display.motion.axis_moves[ActionOps::y_pos_1] = make_axis_control(motion_enabled && !force_disabled, "1", reason);
    state.display.motion.axis_moves[ActionOps::y_neg_1] = make_axis_control(motion_enabled && !force_disabled, "1", reason);
    state.display.motion.axis_moves[ActionOps::z_up_10] = make_axis_control(motion_enabled && !force_disabled, "10", reason);
    state.display.motion.axis_moves[ActionOps::z_up_1] = make_axis_control(motion_enabled && !force_disabled, "1", reason);
    state.display.motion.axis_moves[ActionOps::z_down_1] = make_axis_control(motion_enabled && !force_disabled, "1", reason);
    state.display.motion.axis_moves[ActionOps::z_down_10] = make_axis_control(motion_enabled && !force_disabled, "10", reason);
    state.display.motion.axis_moves[ActionOps::e_up_10] = make_axis_control(motion_enabled && !force_disabled, "10", reason);
    state.display.motion.axis_moves[ActionOps::e_down_10] = make_axis_control(motion_enabled && !force_disabled, "10", reason);

    const bool rack_enabled = state.display.rack.visible && motion_enabled && !force_disabled;
    state.display.rack.lift = make_rack_control(rack_enabled, "lift", reason);
    state.display.rack.center = make_rack_control(rack_enabled, "center", reason);
    state.display.rack.lower = make_rack_control(rack_enabled, "lower", reason);
}

} // namespace

Format::State BuildState(MachineObject* machine_obj)
{
    Format::State state;
    state.display.visible = has_machine(machine_obj);

    fill_temperature(machine_obj, state);
    fill_misc_and_motion(machine_obj, state);
    FillExtruderState(machine_obj, state);
    fill_rack(machine_obj, state);
    fill_actions(machine_obj, state);

    return state;
}

}}} // namespace Slic3r::GUI::ControlPanel
