#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI {

class DevicePageControlPanelVM;

namespace ControlPanelActionNames {
inline constexpr const char* set_extruder_temp = "set_extruder_temp";
inline constexpr const char* set_bed_temp      = "set_bed_temp";
inline constexpr const char* set_chamber_temp  = "set_chamber_temp";
inline constexpr const char* set_speed         = "set_speed";
inline constexpr const char* toggle_lamp       = "toggle_lamp";
inline constexpr const char* open_fan          = "open_fan";
inline constexpr const char* axis_move         = "axis_move";
inline constexpr const char* switch_extruder   = "switch_extruder";
} // namespace ControlPanelActionNames

namespace ControlPanelActionOps {
inline constexpr const char* xy_home    = "xy_home";
inline constexpr const char* x_pos_10   = "x_pos_10";
inline constexpr const char* x_neg_10   = "x_neg_10";
inline constexpr const char* x_pos_1    = "x_pos_1";
inline constexpr const char* x_neg_1    = "x_neg_1";
inline constexpr const char* y_pos_10   = "y_pos_10";
inline constexpr const char* y_neg_10   = "y_neg_10";
inline constexpr const char* y_pos_1    = "y_pos_1";
inline constexpr const char* y_neg_1    = "y_neg_1";
inline constexpr const char* z_up_10    = "z_up_10";
inline constexpr const char* z_up_1     = "z_up_1";
inline constexpr const char* z_down_1   = "z_down_1";
inline constexpr const char* z_down_10  = "z_down_10";
inline constexpr const char* e_up_10    = "e_up_10";
inline constexpr const char* e_down_10  = "e_down_10";
inline constexpr const char* rack_lift  = "rack_lift";
inline constexpr const char* rack_center = "rack_center";
inline constexpr const char* rack_lower = "rack_lower";
} // namespace ControlPanelActionOps

namespace ControlPanelActionHandler {

std::optional<nlohmann::json> TryHandle(DevicePageControlPanelVM& vm,
                                        const std::string& action,
                                        const nlohmann::json& payload,
                                        MachineObject* machine_obj);

} // namespace ControlPanelActionHandler

}} // namespace Slic3r::GUI
