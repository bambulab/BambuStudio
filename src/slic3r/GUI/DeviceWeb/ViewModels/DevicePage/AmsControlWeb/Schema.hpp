#ifndef AMSCONTROLWEBSCHEMA_HPP
#define AMSCONTROLWEBSCHEMA_HPP

// Schema for the JSON payload exchanged between DevicePageAmsControlWebVM
// (`device_page_ams_control_web`) and the Web page under
// src/slic3r/GUI/DeviceWeb/device_page/src/features/device-page/ams-control-web/.
//
// Entry point: carries the top-level envelope and pulls in SchemaData.hpp
// (`data`, the device mirror) and SchemaDisplay.hpp (`display`, the render-ready
// view model). Selection and footer actions live here so the list payload stays
// a straight mirror.
//
// Single source of truth for field names; keep types.ts aligned with them.

#include "SchemaData.hpp"
#include "SchemaDisplay.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace AmsControlWebSchema {

namespace keys {
    inline constexpr const char* data             = "data";
    inline constexpr const char* display          = "display";
    inline constexpr const char* selected_ams_id  = "selected_ams_id";
    inline constexpr const char* selected_slot_id = "selected_slot_id";
    inline constexpr const char* actions          = "actions";
} // namespace keys

namespace actions {
    inline constexpr const char* show_auto_refill = "show_auto_refill";
    inline constexpr const char* show_settings    = "show_settings";
    inline constexpr const char* can_load         = "can_load";
    inline constexpr const char* can_unload       = "can_unload";
    // Translated reason the matching button is disabled, empty when enabled.
    inline constexpr const char* load_tips        = "load_tips";
    inline constexpr const char* unload_tips      = "unload_tips";
} // namespace actions

namespace format {

struct Actions
{
    bool        show_auto_refill = false;
    bool        show_settings    = false;
    bool        can_load         = false;
    bool        can_unload       = false;
    std::string load_tips;
    std::string unload_tips;
};

struct State
{
    AmsListData data;
    Display     display;
    std::string selected_ams_id;
    std::string selected_slot_id;
    Actions     actions;
    // The two inputs below stay out of the JSON: the page never reads them back,
    // it reads what they were resolved into.
    // Slot the machine currently has loaded, resolved into `slot_view::loaded`,
    // `slot_link::state` and the extruder colours.
    std::string loaded_ams_id;
    std::string loaded_slot_id;
    // Units the user opened, most recent first. An input to the layout: each
    // column publishes its own choice as `panel_view::active_ams_id`.
    std::vector<std::string> active_ams_ids;
};

inline void to_json(nlohmann::json& j, const Actions& v)
{
    j = {
        {actions::show_auto_refill, v.show_auto_refill},
        {actions::show_settings,    v.show_settings},
        {actions::can_load,         v.can_load},
        {actions::can_unload,       v.can_unload},
        {actions::load_tips,        v.load_tips},
        {actions::unload_tips,      v.unload_tips},
    };
}

inline void to_json(nlohmann::json& j, const State& v)
{
    j = {
        {keys::data,             v.data},
        {keys::display,          v.display},
        {keys::selected_ams_id,  v.selected_ams_id},
        {keys::selected_slot_id, v.selected_slot_id},
        {keys::actions,          v.actions},
    };
}

} // namespace format

}}} // namespace Slic3r::GUI::AmsControlWebSchema

#endif // AMSCONTROLWEBSCHEMA_HPP
