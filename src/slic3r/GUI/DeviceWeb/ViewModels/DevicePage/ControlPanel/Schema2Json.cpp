#include "Schema.hpp"

namespace Slic3r { namespace GUI { namespace ControlPanelSchema { namespace format {

namespace {

nlohmann::json optional_int_json(const std::optional<int>& value)
{
    return value.has_value() ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

} // namespace

void to_json(nlohmann::json& j, const TempInputControl& v)
{
    j = {
        {"enabled", v.enabled},
        {"disabled_reason", v.disabled_reason},
        {"max_warning", v.max_warning},
        {"min_warning", v.min_warning},
    };
}

void to_json(nlohmann::json& j, const SpeedOption& v)
{
    j = {{"level", v.level}, {"label", v.label}};
}

void to_json(nlohmann::json& j, const SpeedControl& v)
{
    j = {
        {"selectable", v.selectable},
        {"editable", v.editable},
        {"hint", v.hint},
        {"disabled_reason", v.disabled_reason},
        {"levels", v.levels},
    };
}

void to_json(nlohmann::json& j, const LampControl& v)
{
    j = {{"switchable", v.switchable}, {"disabled_reason", v.disabled_reason}, {"next_lamp_on", v.next_lamp_on}};
}

void to_json(nlohmann::json& j, const FanControl& v)
{
    j = {{"openable", v.openable}, {"disabled_reason", v.disabled_reason}, {"opens_dialog", v.opens_dialog}};
}

void to_json(nlohmann::json& j, const AxisMoveControl& v)
{
    j = {{"movable", v.movable}, {"disabled_reason", v.disabled_reason}, {"step", v.step}};
}

void to_json(nlohmann::json& j, const RackControl& v)
{
    j = {
        {"movable", v.movable},
        {"disabled_reason", v.disabled_reason},
        {"target_pos", v.target_pos},
        {"label", v.label},
        {"tooltip", v.tooltip},
    };
}

void to_json(nlohmann::json& j, const ExtruderSwitchControl& v)
{
    j = {{"switchable", v.switchable}, {"disabled_reason", v.disabled_reason}, {"available_extruder_ids", v.available_extruder_ids}};
}

void to_json(nlohmann::json& j, const ExtruderTempItem& v)
{
    j = {
        {"id", v.id},
        {"label", v.label},
        {"current", optional_int_json(v.current)},
        {"target", optional_int_json(v.target)},
        {"min", v.min},
        {"max", v.max},
        {"editable", v.editable},
        {"temp_status", v.temp_status},
        {"visible", v.visible},
        {"read_only_reason", v.read_only_reason},
        {"show_nozzle", v.show_nozzle},
        {"badge", v.badge},
        {"badge_nozzle_index", v.badge_nozzle_index},
        {"side_badge", v.side_badge},
        {"temp_input", v.temp_input},
    };
}

void to_json(nlohmann::json& j, const ExtruderTempArea& v)
{
    j = {{"visible", v.visible}, {"extruder_count", v.extruder_count}, {"extruders", v.extruders}};
}

void to_json(nlohmann::json& j, const ChamberTempArea& v)
{
    j = {
        {"visible", v.visible},
        {"current", optional_int_json(v.current)},
        {"target", optional_int_json(v.target)},
        {"min", v.min},
        {"max", v.max},
        {"editable", v.editable},
        {"temp_status", v.temp_status},
        {"read_only_reason", v.read_only_reason},
        {"temp_input", v.temp_input},
    };
}

void to_json(nlohmann::json& j, const BedTempArea& v)
{
    j = {
        {"visible", v.visible},
        {"current", optional_int_json(v.current)},
        {"target", optional_int_json(v.target)},
        {"min", v.min},
        {"max", v.max},
        {"editable", v.editable},
        {"temp_status", v.temp_status},
        {"read_only_reason", v.read_only_reason},
        {"temp_input", v.temp_input},
    };
}

void to_json(nlohmann::json& j, const SpeedArea& v)
{
    j = {{"visible", v.visible}, {"speed_text", v.speed_text}, {"speed_level", v.speed_level}, {"speed_control", v.speed_control}};
}

void to_json(nlohmann::json& j, const LampArea& v)
{
    j = {{"visible", v.visible}, {"lamp_on", v.lamp_on}, {"label", v.label}, {"lamp_control", v.lamp_control}};
}

void to_json(nlohmann::json& j, const FanArea& v)
{
    j = {{"fan_visible", v.fan_visible}, {"fan_label", v.fan_label}, {"fan_control", v.fan_control}};
}

void to_json(nlohmann::json& j, const MotionArea& v)
{
    j = {{"z_label", v.z_label}, {"xy_visible", v.xy_visible}, {"z_visible", v.z_visible}, {"e_visible", v.e_visible}, {"axis_moves", v.axis_moves}};
}

void to_json(nlohmann::json& j, const ExtruderView& v)
{
    j = {{"id", v.id}, {"has_filament", v.has_filament}, {"filament_clr", v.filament_clr}, {"extruder_status", v.extruder_status}, {"extruder_icon", v.extruder_icon}};
}

void to_json(nlohmann::json& j, const SingleExtruderView& v)
{
    j = {
        {"has_filament", v.has_filament},
        {"filament_clr", v.filament_clr},
        {"extruder_status", v.extruder_status},
        {"extruder_icon", v.extruder_icon},
    };
}

void to_json(nlohmann::json& j, const DualExtruderView& v)
{
    j = {
        {"current_extruder_id", v.current_extruder_id},
        {"labels", {{"left", v.left_label}, {"right", v.right_label}}},
        {"extruders", v.extruders},
        {"switching_status_text", v.switching_status_text},
        {"switching_failed", v.switching_failed},
        {"can_quit_switching", v.can_quit_switching},
        {"can_retry_switching", v.can_retry_switching},
        {"quit_label", v.quit_label},
        {"retry_label", v.retry_label},
        {"switch_control", v.switch_control},
    };
}

void to_json(nlohmann::json& j, const SingleRackExtruderView& v)
{
    j = {
        {"current_nozzle_id", optional_int_json(v.current_nozzle_id)},
        {"has_filament", v.has_filament},
        {"filament_clr", v.filament_clr},
        {"upper_filament_clr", v.upper_filament_clr},
        {"lower_filament_clr", v.lower_filament_clr},
        {"show_filament_path", v.show_filament_path},
        {"nozzle_diameter", v.nozzle_diameter},
        {"nozzle_flow_type", v.nozzle_flow_type},
        {"extruder_status", v.extruder_status},
        {"rack_toolhead_visual_state", v.rack_toolhead_visual_state},
    };
}

void to_json(nlohmann::json& j, const ExtruderArea& v)
{
    const bool is_single = v.type == values::extruder_area_type::single;
    const bool is_dual = v.type == values::extruder_area_type::dual;
    const bool is_single_rack = v.type == values::extruder_area_type::single_rack;
    j = {
        {"type", v.type},
        {"label", v.label},
        {"single_extruder", is_single ? nlohmann::json(v.single_extruder) : nlohmann::json("")},
        {"dual_extruder", is_dual ? nlohmann::json(v.dual_extruder) : nlohmann::json("")},
        {"single_rack_extruder", is_single_rack ? nlohmann::json(v.single_rack_extruder) : nlohmann::json("")},
    };
}

void to_json(nlohmann::json& j, const HotendRackArea& v)
{
    j = {
        {"visible", v.visible},
        {"title", v.title},
        {"lift", v.lift},
        {"center", v.center},
        {"lower", v.lower},
    };
}

void to_json(nlohmann::json& j, const Display& v)
{
    j = {
        {"visible", v.visible},
        {"extruder_temp", v.extruder_temp},
        {"chamber_temp", v.chamber_temp},
        {"bed_temp", v.bed_temp},
        {"speed", v.speed},
        {"lamp", v.lamp},
        {"fan", v.fan},
        {"motion", v.motion},
        {"extruder", v.extruder},
        {"rack", v.rack},
    };
}

void to_json(nlohmann::json& j, const State& v)
{
    j = {{"display", v.display}};
}

}}}} // namespace Slic3r::GUI::ControlPanelSchema::format
