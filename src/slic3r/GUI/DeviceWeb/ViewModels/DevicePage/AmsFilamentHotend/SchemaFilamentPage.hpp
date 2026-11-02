#ifndef DEVICEPAGESCHEMA_AMSFILAMENTPAGE_HPP
#define DEVICEPAGESCHEMA_AMSFILAMENTPAGE_HPP

#include "Schema.hpp"
#include "SchemaFilamentChangeStatus.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r { namespace GUI { namespace AmsHotendSchema { namespace format {

struct FilamentActions
{
    bool        can_load = true;
    bool        can_load_external = true;
    bool        can_show_load_auto = true;
    bool        can_unload = true;
    bool        can_dry = false;
    bool        show_ams_settings = false;
    bool        show_auto_refill = false;
    bool        show_read_all = false;
    bool        can_read_all_slots = false;
    std::string tips;
};

struct FilamentPage
{
    FilamentActions      actions;
    FilamentChangeStatus filament_change_status;
    nlohmann::json       ams_dry_zones = nlohmann::json::array();
    nlohmann::json       ams_env_units = nlohmann::json::array();
    bool                 show_ams_env_units = true;
    nlohmann::json       hotend_slots = nlohmann::json::array();
};

inline void to_json(nlohmann::json& j, const FilamentActions& v)
{
    namespace SK = AmsHotendSchema::keys;
    j = {
        {SK::filament_actions::can_load,          v.can_load},
        {SK::filament_actions::can_load_external, v.can_load_external},
        {SK::filament_actions::can_show_load_auto, v.can_show_load_auto},
        {SK::filament_actions::can_unload,        v.can_unload},
        {SK::filament_actions::can_dry,           v.can_dry},
        {SK::filament_actions::show_ams_settings, v.show_ams_settings},
        {SK::filament_actions::show_auto_refill,  v.show_auto_refill},
        {SK::filament_actions::show_read_all,     v.show_read_all},
        {SK::filament_actions::can_read_all_slots, v.can_read_all_slots},
        {SK::filament_actions::tips,              v.tips}
    };
}

inline void to_json(nlohmann::json& j, const FilamentPage& v)
{
    namespace SK = AmsHotendSchema::keys;
    j = {
        {SK::filament_page::actions,                v.actions},
        {SK::filament_page::filament_change_status, v.filament_change_status},
        {SK::filament_page::ams_dry_zones,          v.ams_dry_zones},
        {SK::filament_page::ams_env_units,          v.ams_env_units},
        {SK::filament_page::show_ams_env_units,     v.show_ams_env_units},
        {SK::filament_page::hotend_slots,           v.hotend_slots}
    };
}

}}}} // namespace Slic3r::GUI::AmsHotendSchema::format

#endif // DEVICEPAGESCHEMA_AMSFILAMENTPAGE_HPP
