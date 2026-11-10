#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace HotendRackSchema {

namespace values {
namespace slot_status {
inline constexpr const char* empty   = "empty";
inline constexpr const char* normal  = "normal";
inline constexpr const char* unknown = "unknown";
inline constexpr const char* error   = "error";
} // namespace slot_status

namespace row_id {
inline constexpr const char* a = "A";
inline constexpr const char* b = "B";
} // namespace row_id

namespace rack_position {
inline constexpr const char* a_top   = "a_top";
inline constexpr const char* b_top   = "b_top";
inline constexpr const char* center  = "center";
inline constexpr const char* unknown = "unknown";
} // namespace rack_position
} // namespace values

namespace format {

struct ActionControl
{
    bool        enabled = false;
    std::string disabled_reason;
};

struct HotendSlot
{
    int         id = 0;
    std::string display_id;
    std::string status = values::slot_status::empty;
    std::string diameter;
    std::string flow_type;
    std::string filament_color;
    bool        has_error_detail = false;
};

struct HotendRackRow
{
    std::string             id = values::row_id::a;
    bool                    raised = false;
    std::vector<HotendSlot> slots;
    ActionControl           move_control;
};

struct Toolhead
{
    std::string status = values::slot_status::empty;
    std::string diameter;
    std::string flow_type;
    std::string filament_color;
};

struct RackArea
{
    std::string                 position = values::rack_position::unknown;
    bool                        busy = false;
    std::vector<HotendRackRow>  rows;
};

struct Reading
{
    bool        active = false;
    int         index = 0;
    int         count = 0;
};

struct Actions
{
    ActionControl home;
    ActionControl read_all;
    ActionControl hotends_info;
};

struct Display
{
    bool        visible = false;
    std::string title;
    Toolhead    toolhead;
    RackArea    rack;
    Reading     reading;
    Actions     actions;
};

struct State
{
    Display display;
};

void to_json(nlohmann::json& j, const ActionControl& v);
void to_json(nlohmann::json& j, const HotendSlot& v);
void to_json(nlohmann::json& j, const HotendRackRow& v);
void to_json(nlohmann::json& j, const Toolhead& v);
void to_json(nlohmann::json& j, const RackArea& v);
void to_json(nlohmann::json& j, const Reading& v);
void to_json(nlohmann::json& j, const Actions& v);
void to_json(nlohmann::json& j, const Display& v);
void to_json(nlohmann::json& j, const State& v);

} // namespace format
}}} // namespace Slic3r::GUI::HotendRackSchema
