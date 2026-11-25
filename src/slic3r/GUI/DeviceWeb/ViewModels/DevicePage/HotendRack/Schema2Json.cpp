#include "Schema.hpp"

namespace Slic3r { namespace GUI { namespace HotendRackSchema { namespace format {

void to_json(nlohmann::json& j, const ActionControl& v)
{
    j = {{"enabled", v.enabled}, {"disabled_reason", v.disabled_reason}};
}

void to_json(nlohmann::json& j, const HotendSlot& v)
{
    j = {
        {"id", v.id},
        {"display_id", v.display_id},
        {"status", v.status},
        {"diameter", v.diameter},
        {"flow_type", v.flow_type},
        {"filament_color", v.filament_color},
        {"has_error_detail", v.has_error_detail},
    };
}

void to_json(nlohmann::json& j, const HotendRackRow& v)
{
    j = {{"id", v.id}, {"raised", v.raised}, {"slots", v.slots}, {"move_control", v.move_control}};
}

void to_json(nlohmann::json& j, const Toolhead& v)
{
    j = {{"status", v.status}, {"diameter", v.diameter}, {"flow_type", v.flow_type}, {"filament_color", v.filament_color}};
}

void to_json(nlohmann::json& j, const RackArea& v)
{
    j = {{"position", v.position}, {"busy", v.busy}, {"rows", v.rows}};
}

void to_json(nlohmann::json& j, const Reading& v)
{
    j = {{"active", v.active}, {"index", v.index}, {"count", v.count}};
}

void to_json(nlohmann::json& j, const Actions& v)
{
    j = {{"home", v.home}, {"read_all", v.read_all}, {"hotends_info", v.hotends_info}};
}

void to_json(nlohmann::json& j, const Display& v)
{
    j = {
        {"visible", v.visible},
        {"title", v.title},
        {"toolhead", v.toolhead},
        {"rack", v.rack},
        {"reading", v.reading},
        {"actions", v.actions},
    };
}

void to_json(nlohmann::json& j, const State& v)
{
    j = {{"display", v.display}};
}

}}}} // namespace Slic3r::GUI::HotendRackSchema::format
