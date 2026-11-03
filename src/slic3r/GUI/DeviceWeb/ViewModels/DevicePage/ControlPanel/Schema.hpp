#ifndef DEVICEPAGE_CONTROLPANEL_SCHEMA_HPP
#define DEVICEPAGE_CONTROLPANEL_SCHEMA_HPP

#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace ControlPanelSchema {

namespace values {
namespace temp_status {
inline constexpr const char* idle    = "idle";
inline constexpr const char* heating = "heating";
inline constexpr const char* cooling = "cooling";
} // namespace temp_status

namespace extruder_status {
inline constexpr const char* idle      = "idle";
inline constexpr const char* active    = "active";
inline constexpr const char* loading   = "loading";
inline constexpr const char* unloading = "unloading";
inline constexpr const char* switching = "switching";
} // namespace extruder_status

namespace rack_toolhead_visual_state {
inline constexpr const char* empty = "empty";
} // namespace rack_toolhead_visual_state

namespace extruder_area_type {
inline constexpr const char* single      = "single";
inline constexpr const char* dual        = "dual";
inline constexpr const char* single_rack = "single_rack";
} // namespace extruder_area_type

namespace fan_label {
inline constexpr const char* fan           = "Fan";
inline constexpr const char* air_condition = "AirCondition";
} // namespace fan_label
} // namespace values

namespace format {

struct TempInputControl
{
    bool        enabled = false;
    std::string disabled_reason;
};

struct SpeedOption
{
    int         level = 2;
    std::string label;
};

struct SpeedControl
{
    bool                     selectable = false;
    std::string              disabled_reason;
    std::vector<SpeedOption> levels;
};

struct LampControl
{
    bool        switchable = false;
    std::string disabled_reason;
    bool        next_lamp_on = false;
};

struct FanControl
{
    bool        openable = false;
    std::string disabled_reason;
    bool        opens_dialog = true;
};

struct AxisMoveControl
{
    bool        movable = false;
    std::string disabled_reason;
    std::string step;
};

struct RackControl
{
    bool        movable = false;
    std::string disabled_reason;
    std::string target_pos;
};

struct ExtruderSwitchControl
{
    bool             switchable = false;
    std::string      disabled_reason;
    std::vector<int> available_extruder_ids;
};

struct ExtruderTempItem
{
    std::string        id;
    std::string        label;
    std::optional<int> current;
    std::optional<int> target;
    int                min = 0;
    int                max = 0;
    bool               editable = false;
    std::string        temp_status = values::temp_status::idle;
    bool               visible = false;
    std::string        read_only_reason;
    bool               show_nozzle = false;
    bool               badge = false;
    int                badge_nozzle_index = 0;
    TempInputControl   temp_input;
};

struct ExtruderTempArea
{
    bool                          visible = false;
    int                           extruder_count = 0;
    std::vector<ExtruderTempItem> extruders;
};

struct ChamberTempArea
{
    bool               visible = false;
    std::optional<int> current;
    std::optional<int> target;
    int                min = 0;
    int                max = 0;
    bool               editable = false;
    std::string        temp_status = values::temp_status::idle;
    std::string        read_only_reason;
    TempInputControl   temp_input;
};

struct BedTempArea
{
    bool               visible = false;
    std::optional<int> current;
    std::optional<int> target;
    int                min = 0;
    int                max = 0;
    bool               editable = false;
    std::string        temp_status = values::temp_status::idle;
    std::string        read_only_reason;
    TempInputControl   temp_input;
};

struct SpeedArea
{
    bool        visible = false;
    std::string speed_text = "100%";
    int         speed_level = 2;
    SpeedControl speed_control;
};

struct LampArea
{
    bool        visible = false;
    bool        lamp_on = false;
    std::string label;
    LampControl lamp_control;
};

struct FanArea
{
    bool        fan_visible = false;
    std::string fan_label = values::fan_label::fan;
    FanControl  fan_control;
};

struct MotionArea
{
    std::string z_label = "Z";
    bool        xy_visible = false;
    bool        z_visible = false;
    bool        e_visible = false;
    std::map<std::string, AxisMoveControl> axis_moves;
};

struct ExtruderView
{
    int         id = 0;
    bool        has_filament = false;
    std::string filament_clr;
    std::string extruder_status = values::extruder_status::idle;
    std::string extruder_icon;
};

struct SingleExtruderView
{
    bool        has_filament = false;
    std::string filament_clr;
    std::string extruder_status = values::extruder_status::idle;
    std::string extruder_icon;
};

struct DualExtruderView
{
    int                       current_extruder_id = 0;
    std::string               left_label;
    std::string               right_label;
    std::vector<ExtruderView> extruders;
    std::string               switching_status_text;
    ExtruderSwitchControl     switch_control;
};

struct SingleRackExtruderView
{
    std::optional<int> current_nozzle_id;
    bool               has_filament = false;
    std::string        filament_clr;
    std::string        nozzle_diameter;
    std::string        nozzle_flow_type;
    std::string        extruder_status = values::extruder_status::idle;
    std::string        rack_toolhead_visual_state = values::rack_toolhead_visual_state::empty;
};

struct ExtruderArea
{
    std::string            type = values::extruder_area_type::single;
    SingleExtruderView     single_extruder;
    DualExtruderView       dual_extruder;
    SingleRackExtruderView single_rack_extruder;
};

struct HotendRackArea
{
    bool visible = false;
    RackControl lift;
    RackControl center;
    RackControl lower;
};

struct Display
{
    bool             visible = false;
    ExtruderTempArea extruder_temp;
    ChamberTempArea  chamber_temp;
    BedTempArea      bed_temp;
    SpeedArea        speed;
    LampArea         lamp;
    FanArea          fan;
    MotionArea       motion;
    ExtruderArea     extruder;
    HotendRackArea   rack;
};

struct State
{
    Display display;
};

void to_json(nlohmann::json& j, const TempInputControl& v);
void to_json(nlohmann::json& j, const SpeedOption& v);
void to_json(nlohmann::json& j, const SpeedControl& v);
void to_json(nlohmann::json& j, const LampControl& v);
void to_json(nlohmann::json& j, const FanControl& v);
void to_json(nlohmann::json& j, const AxisMoveControl& v);
void to_json(nlohmann::json& j, const RackControl& v);
void to_json(nlohmann::json& j, const ExtruderSwitchControl& v);
void to_json(nlohmann::json& j, const ExtruderTempItem& v);
void to_json(nlohmann::json& j, const ExtruderTempArea& v);
void to_json(nlohmann::json& j, const ChamberTempArea& v);
void to_json(nlohmann::json& j, const BedTempArea& v);
void to_json(nlohmann::json& j, const SpeedArea& v);
void to_json(nlohmann::json& j, const LampArea& v);
void to_json(nlohmann::json& j, const FanArea& v);
void to_json(nlohmann::json& j, const MotionArea& v);
void to_json(nlohmann::json& j, const ExtruderView& v);
void to_json(nlohmann::json& j, const SingleExtruderView& v);
void to_json(nlohmann::json& j, const DualExtruderView& v);
void to_json(nlohmann::json& j, const SingleRackExtruderView& v);
void to_json(nlohmann::json& j, const ExtruderArea& v);
void to_json(nlohmann::json& j, const HotendRackArea& v);
void to_json(nlohmann::json& j, const Display& v);
void to_json(nlohmann::json& j, const State& v);

} // namespace format
}}} // namespace Slic3r::GUI::ControlPanelSchema

#endif // DEVICEPAGE_CONTROLPANEL_SCHEMA_HPP
