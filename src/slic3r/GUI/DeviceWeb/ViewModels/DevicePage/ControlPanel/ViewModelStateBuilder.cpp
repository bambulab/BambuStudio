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

constexpr int kTemperatureDeltaThreshold = 2;
constexpr int kExtrudeMinTemperature = 170;
constexpr int kDefaultNozzleMinTemp = 20;
constexpr int kDefaultNozzleMaxTemp = 300;
constexpr int kDefaultBedMinTemp = 20;
constexpr int kDefaultBedMaxTemp = 120;
constexpr int kDefaultChamberMinTemp = 20;
constexpr int kDefaultChamberMaxTemp = 60;

std::string temp_status(std::optional<int> current, std::optional<int> target, bool strict_greater = false)
{
    if (!current.has_value() || !target.has_value())
        return Schema::values::temp_status::idle;
    const int delta = *target - *current;
    if (strict_greater ? (delta > kTemperatureDeltaThreshold) : (delta >= kTemperatureDeltaThreshold))
        return Schema::values::temp_status::heating;
    if (*current - *target >= kTemperatureDeltaThreshold)
        return Schema::values::temp_status::cooling;
    return Schema::values::temp_status::idle;
}

std::string disabled_reason()
{
    return _u8L("This control is currently unavailable.");
}

Format::TempInputControl make_temp_control(bool enabled, int min_temp, int max_temp, const std::string& reason = {})
{
    Format::TempInputControl result;
    result.enabled = enabled;
    result.disabled_reason = enabled ? std::string() : reason;
    result.max_warning = (_L("The maximum temperature cannot exceed ") + wxString::Format("%d", max_temp)).ToUTF8().data();
    result.min_warning = (_L("The minmum temperature should not be less than ") + wxString::Format("%d", min_temp)).ToUTF8().data();
    return result;
}

Format::SpeedControl make_speed_control(bool selectable, bool editable, const std::string& reason = {})
{
    Format::SpeedControl result;
    result.selectable = selectable;
    result.editable = editable;
    result.hint = _u8L("This only takes effect during printing");
    result.disabled_reason = selectable ? std::string() : reason;
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

Format::RackControl make_rack_control(bool enabled,
                                      const std::string& target_pos,
                                      const std::string& label,
                                      const std::string& tooltip,
                                      const std::string& reason = {})
{
    Format::RackControl result;
    result.movable = enabled;
    result.disabled_reason = enabled ? std::string() : reason;
    result.target_pos = target_pos;
    result.label = label;
    result.tooltip = tooltip;
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
    return obj && obj->is_connected() && obj->is_info_ready();
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
    item.visible = extruder_id == MAIN_EXTRUDER_ID;
    item.show_nozzle = true;
    item.min = kDefaultNozzleMinTemp;
    item.max = kDefaultNozzleMaxTemp;

    auto* extder_system = (obj && has_machine(obj)) ? obj->GetExtderSystem() : nullptr;
    if (!obj || !extder_system) {
        item.read_only_reason = _u8L("Please select a printer");
        return item;
    }

    const int total = extder_system->GetTotalExtderCount();
    item.visible = total >= 2 || extruder_id == MAIN_EXTRUDER_ID;
    if (total >= 2) {
        item.side_badge = extruder_id == DEPUTY_EXTRUDER_ID ? "L" : "R";
    }

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
        item.target = ControlPanelActionHandler::ResolveHeldNozzleTemp(
            obj->get_dev_id(), extruder_id, extder->GetTargetTemp());
    }
    item.temp_status = temp_status(item.current, item.target, /*strict_greater=*/true);

    if (total == 1 && direct_rack && extder.has_value() && extder->GetPickedNozzleAddrId().has_value()) {
        item.badge = true;
        item.badge_nozzle_index = extder->GetPickedNozzleAddrId().value() + 1;
    }
    return item;
}

void fill_temperature(MachineObject* obj, Format::State& state)
{
    const bool ready = has_machine(obj);
    auto& area = state.display.extruder_temp;
    auto* extder_system = ready ? obj->GetExtderSystem() : nullptr;
    area.visible = true;
    area.extruder_count = extder_system ? extder_system->GetTotalExtderCount() : 1;
    area.extruders.push_back(build_extruder_temp(obj, MAIN_EXTRUDER_ID, "main",
        _u8L(DevPrinterConfigUtil::get_toolhead_display_name(obj ? obj->printer_type : std::string(),
                                                             MAIN_EXTRUDER_ID, ToolHeadComponent::Nozzle,
                                                             ToolHeadNameCase::TitleCase, true))));
    if (area.extruder_count >= 2) {
        area.extruders.insert(area.extruders.begin(), build_extruder_temp(obj, DEPUTY_EXTRUDER_ID, "deputy",
            _u8L(DevPrinterConfigUtil::get_toolhead_display_name(obj ? obj->printer_type : std::string(),
                                                                 DEPUTY_EXTRUDER_ID, ToolHeadComponent::Nozzle,
                                                                 ToolHeadNameCase::TitleCase, true))));
    }

    auto& bed = state.display.bed_temp;
    bed.visible = true;
    bed.min = kDefaultBedMinTemp;
    bed.max = obj ? obj->get_bed_temperature_limit() : kDefaultBedMaxTemp;
    if (obj && obj->bed_temp_range.size() > 1) {
        bed.min = obj->bed_temp_range[0];
        bed.max = obj->bed_temp_range[1];
    }
    if (ready && obj->GetBed()) {
        bed.current = int_temp(obj->GetBed()->GetBedTemp());
        bed.target = ControlPanelActionHandler::ResolveHeldBedTemp(
            obj->get_dev_id(), int_temp(obj->GetBed()->GetBedTempTarget()));
    }
    bed.editable = temp_control_enabled(obj);
    bed.read_only_reason = bed.editable ? std::string() : (ready ? disabled_reason() : _u8L("Please select a printer"));
    bed.temp_status = temp_status(bed.current, bed.target);

    auto& chamber = state.display.chamber_temp;
    chamber.visible = true;
    chamber.min = kDefaultChamberMinTemp;
    chamber.max = kDefaultChamberMaxTemp;
    if (ready && obj->GetChamber()) {
        auto chamber_ptr = obj->GetChamber();
        if (chamber_ptr->SupportChamberTempDisplay())
            chamber.current = int_temp(chamber_ptr->GetChamberTemp());
        if (chamber_ptr->SupportChamberEdit()) {
            chamber.target = ControlPanelActionHandler::ResolveHeldChamberTemp(
                obj->get_dev_id(), int_temp(chamber_ptr->GetChamberTempTarget()));
            chamber.min = chamber_ptr->GetChamberTempEditMin();
            chamber.max = chamber_ptr->GetChamberTempEditMax();
        }
        chamber.editable = temp_control_enabled(obj) && chamber_ptr->SupportChamberEdit();
        if (int_temp(chamber_ptr->GetChamberTempTarget()).value_or(0) - int_temp(chamber_ptr->GetChamberTemp()).value_or(0) >= kTemperatureDeltaThreshold)
            chamber.temp_status = Schema::values::temp_status::heating;
    }
    chamber.read_only_reason = chamber.editable ? std::string() : (ready ? disabled_reason() : _u8L("Please select a printer"));
}

void fill_misc_and_motion(MachineObject* obj, Format::State& state)
{
    const bool ready = has_machine(obj);
    auto& speed = state.display.speed;
    speed.visible = true;
    const int raw_speed_level = ready ? std::clamp(static_cast<int>(obj->GetPrintingSpeedLevel()), 1, 4) : 2;
    speed.speed_level = ready
        ? ControlPanelActionHandler::ResolveHeldSpeedLevel(obj->get_dev_id(), raw_speed_level)
        : 2;
    speed.speed_text = ready ? (std::to_string(obj->printing_speed_mag) + "%") : _u8L("100%");

    auto& lamp = state.display.lamp;
    lamp.visible = true;
    lamp.label = _u8L("Lamp");
    const bool raw_lamp_on = ready && obj->GetLamp() && obj->GetLamp()->IsChamberLightOn();
    lamp.lamp_on = ready
        ? ControlPanelActionHandler::ResolveHeldLampOn(obj->get_dev_id(), raw_lamp_on)
        : false;

    auto& fan = state.display.fan;
    fan.fan_visible = !ready || (!obj->GetInfo() || obj->GetInfo()->IsFdmMode());
    fan.fan_label = (ready && obj->is_series_o()) ? _u8L("Air Management") : _u8L("Fan");

    auto& motion = state.display.motion;
    motion.xy_visible = true;
    motion.z_visible = true;
    motion.e_visible = true;
    motion.z_label = (!ready || !obj->GetAxis() || obj->GetAxis()->IsArchCoreXY()) ? _u8L("Bed") : "Z";
}

void fill_rack(MachineObject* obj, Format::State& state)
{
    const bool ready = has_machine(obj);
    auto rack = ready ? obj->GetNozzleRack() : nullptr;
    state.display.rack.visible = rack && rack->IsSupported() && rack->IsDirectConnectedRack();
    state.display.rack.title = _u8L("Hotend Rack");
}

void fill_actions(MachineObject* obj, Format::State& state)
{
    const bool has = has_machine(obj);
    const bool temp_enabled = temp_control_enabled(obj);
    const bool motion_enabled = motion_control_enabled(obj);
    const bool force_disabled = wxGetApp().is_monitor_ctrl_force_disabled();
    const std::string reason = has ? disabled_reason() : _u8L("Please select a printer");

    state.display.bed_temp.temp_input = make_temp_control(
        temp_enabled && !force_disabled,
        state.display.bed_temp.min,
        state.display.bed_temp.max,
        reason);
    state.display.chamber_temp.temp_input = make_temp_control(
        state.display.chamber_temp.editable && !force_disabled,
        state.display.chamber_temp.min,
        state.display.chamber_temp.max,
        reason);
    for (auto& item : state.display.extruder_temp.extruders) {
        const bool is_deputy = item.id == "deputy";
        const bool enabled = item.editable && !force_disabled &&
            (!is_deputy || state.display.extruder_temp.extruder_count >= 2);
        item.temp_input = make_temp_control(
            enabled,
            item.min,
            item.max,
            item.read_only_reason.empty() ? reason : item.read_only_reason);
    }

    const bool speed_selectable = has && !force_disabled;
    const bool speed_editable = speed_selectable && obj->is_in_printing();
    state.display.speed.speed_control = make_speed_control(speed_selectable, speed_editable, reason);

    const bool lamp_enabled = has && (!obj->GetInfo() || obj->GetInfo()->IsFdmMode());
    state.display.lamp.lamp_control = make_lamp_control(lamp_enabled, state.display.lamp.lamp_on, reason);
    state.display.fan.fan_control = make_fan_control(has && state.display.fan.fan_visible && !force_disabled, reason);
    if (state.display.extruder.type == Schema::values::extruder_area_type::dual) {
        const bool can_switch = has && !force_disabled &&
            !(obj->is_in_printing() && !obj->is_in_printing_pause()) &&
            obj->ams_status_main != AMS_STATUS_MAIN_FILAMENT_CHANGE &&
            obj->targ_nozzle_id_from_pc == INVALID_EXTRUDER_ID;
        state.display.extruder.dual_extruder.switch_control =
            make_switch_control(can_switch,
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

    const bool rack_base_enabled = state.display.rack.visible && motion_enabled && !force_disabled;
    bool lift_enable = rack_base_enabled;
    bool center_enable = rack_base_enabled;
    bool lower_enable = rack_base_enabled;
    std::string lift_label;
    std::string center_label;
    std::string lower_label;

    if (auto rack = has ? obj->GetNozzleRack() : nullptr) {
        if (rack->GetReadingCount() > 0) {
            center_label = _u8L("Reading");
            lift_enable = center_enable = lower_enable = false;
        } else if (rack->GetStatus() != DevNozzleRack::RACK_STATUS_IDLE &&
                   rack->GetStatus() != DevNozzleRack::RACK_STATUS_UNKNOWN) {
            center_label = _u8L("Running");
            lift_enable = center_enable = lower_enable = false;
        } else if (rack->IsDirectConnectedRack()) {
            switch (rack->GetDirectRackPos()) {
            case DevNozzleRack::DirectRackPos::RACK_POS_UP:
                lift_label = _u8L("Raised");
                lift_enable = false;
                break;
            case DevNozzleRack::DirectRackPos::RACK_POS_DOWN:
                lower_label = _u8L("Lowered");
                lower_enable = false;
                break;
            case DevNozzleRack::DirectRackPos::RACK_POS_CENTRE:
                center_label = _u8L("Centered");
                center_enable = false;
                break;
            default:
                break;
            }
        }
    }

    state.display.rack.lift = make_rack_control(lift_enable, "lift", lift_label, _u8L("Lift Hotend Rack"), reason);
    state.display.rack.center = make_rack_control(center_enable, "center", center_label, _u8L("Recenter Hotend Rack"), reason);
    state.display.rack.lower = make_rack_control(lower_enable, "lower", lower_label, _u8L("Lower Hotend Rack"), reason);
}

} // namespace

Format::State BuildState(MachineObject* machine_obj)
{
    Format::State state;
    state.display.visible = true;

    fill_temperature(machine_obj, state);
    fill_misc_and_motion(machine_obj, state);
    FillExtruderState(machine_obj, state);
    fill_rack(machine_obj, state);
    fill_actions(machine_obj, state);

    return state;
}

}}} // namespace Slic3r::GUI::ControlPanel
