#ifndef AUTOREFILLREFILLINFOSCHEMA_HPP
#define AUTOREFILLREFILLINFOSCHEMA_HPP

// Schema for the main RefillInfo management page
// (`device_page_auto_refill_refill_info` / submod `state` + `action`).
// Web counterpart:
//   src/slic3r/GUI/DeviceWeb/device_page/src/features/device-page/auto-refill-refill-info/
//
// Conventions (aligned with AmsFilamentHotend/Schema.hpp):
//   * `keys::*`   - JSON field names grouped by object section.
//   * `values::*` - the string enum values those fields may take.
//   * `format::*` - structs mirroring the JSON shape plus `to_json()`.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace AutoRefillRefillInfoSchema {

namespace keys {

namespace root {
    inline constexpr const char* selected_dev_id = "selected_dev_id";
    inline constexpr const char* extruders       = "extruders";
    inline constexpr const char* session_seq     = "session_seq";
    inline constexpr const char* description     = "description";
    inline constexpr const char* editable        = "editable";
    inline constexpr const char* can_add_group   = "can_add_group";
    inline constexpr const char* auto_refill_enabled = "auto_refill_enabled";
}

namespace extruder {
    inline constexpr const char* extruder_id = "extruder_id";
    inline constexpr const char* groups      = "groups";
}

namespace group {
    inline constexpr const char* refill_tag_id    = "refill_tag_id";
    inline constexpr const char* filament_id      = "filament_id";
    inline constexpr const char* filament_type    = "filament_type";
    inline constexpr const char* colors           = "colors";
    inline constexpr const char* color_type       = "color_type";
    inline constexpr const char* nozzle_diameter  = "nozzle_diameter";
    inline constexpr const char* nozzle_flow_type = "nozzle_flow_type";
    inline constexpr const char* slots            = "slots";
}

namespace slot {
    inline constexpr const char* ams_id        = "ams_id";
    inline constexpr const char* slot_id       = "slot_id";
    inline constexpr const char* tray_name     = "tray_name";
    inline constexpr const char* filament_name = "filament_name";
    inline constexpr const char* filament_type = "filament_type";
    inline constexpr const char* sub_brand     = "sub_brand";
    inline constexpr const char* color         = "color";
    inline constexpr const char* colors        = "colors";
    inline constexpr const char* color_type    = "color_type";
    inline constexpr const char* weight_gram   = "weight_gram";
    inline constexpr const char* has_filament  = "has_filament";
    inline constexpr const char* create_type   = "create_type";
}

// Payload fields for `action` submod commands that map to MQTT refill_command.
namespace command {
    inline constexpr const char* group_tag = "group_tag";
    inline constexpr const char* pos       = "pos";
    inline constexpr const char* to_pos    = "to_pos";
    inline constexpr const char* as_head   = "as_head";
    inline constexpr const char* ams_id     = "ams_id";
    inline constexpr const char* slot_id    = "slot_id";
    inline constexpr const char* to_ams_id  = "to_ams_id";
    inline constexpr const char* to_slot_id = "to_slot_id";
}

// Payload field of the `remove_started` / `reset_started` reports.
namespace confirm_remove {
    inline constexpr const char* toast = "toast";
}

} // namespace keys

namespace values {

namespace color_type {
    inline constexpr const char* gradient = "gradient";
    inline constexpr const char* multi    = "multi";
    inline constexpr const char* single   = "single";
}

namespace create_type {
    inline constexpr const char* automatic = "auto";
    inline constexpr const char* manual    = "manual";
    inline constexpr const char* unknown   = "unknown";
}

namespace nozzle_flow_type {
    inline constexpr const char* standard  = "Std";
    inline constexpr const char* high_flow = "HF";
    inline constexpr const char* ultra     = "UHF";
    inline constexpr const char* tough     = "TPU 285";
    inline constexpr const char* elastic   = "EHF";
}

namespace action {
    inline constexpr const char* rendered   = "rendered";
    inline constexpr const char* enable_auto_refill = "enable_auto_refill";
    inline constexpr const char* open_add_group = "open_add_group";
    inline constexpr const char* open_add_filament = "open_add_filament";
    inline constexpr const char* reset      = "reset";
    inline constexpr const char* confirm_reset = "confirm_reset";
    // Reports that settle `confirm_reset`; same asynchronous shape as the
    // removal pair below.
    inline constexpr const char* reset_started   = "reset_started";
    inline constexpr const char* reset_cancelled = "reset_cancelled";
    inline constexpr const char* confirm_remove = "confirm_remove";
    // Reports that settle `confirm_remove`, whose native dialog runs after the
    // bridge request has already been answered.
    inline constexpr const char* remove_started   = "remove_started";
    inline constexpr const char* remove_cancelled = "remove_cancelled";
    inline constexpr const char* remove     = "remove";
    inline constexpr const char* edit       = "edit";
    inline constexpr const char* query      = "query";
    inline constexpr const char* query_next = "query_next";
}

} // namespace values

namespace format {

struct RefillSlot
{
    int         ams_id       = 0;
    int         slot_id      = 0;
    std::string create_type  = values::create_type::unknown;
    bool        has_filament = false;

    std::string              tray_name;
    std::string              filament_name;
    std::string              filament_type;
    std::string              sub_brand;
    std::string              color;
    std::vector<std::string> colors;
    std::string              color_type = values::color_type::single;
    std::optional<int>       weight_gram;
};

struct RefillGroup
{
    // What `action/reset` must send back as `group_tag`; see
    // DevFilaAutoRefillGroup::refill_tag_id.
    std::string              refill_tag_id;
    std::string              filament_id;
    std::string              filament_type;
    std::vector<std::string> colors;
    std::string              color_type = values::color_type::single;
    std::optional<float>     nozzle_diameter;
    std::string              nozzle_flow_type;
    std::vector<RefillSlot>  slots;
};

struct RefillExtruder
{
    int                      extruder_id = 0;
    std::vector<RefillGroup> groups;
};

struct State
{
    std::string                 selected_dev_id;
    std::uint32_t               session_seq = 0;
    std::string                 description;
    bool                        auto_refill_enabled = false;
    bool                        editable      = false;
    bool                        can_add_group = false;
    std::vector<RefillExtruder> extruders;
};

inline void to_json(nlohmann::json& j, const RefillSlot& v)
{
    namespace SK = AutoRefillRefillInfoSchema::keys;
    j = nlohmann::json::object();
    j[SK::slot::ams_id]       = v.ams_id;
    j[SK::slot::slot_id]      = v.slot_id;
    j[SK::slot::create_type]  = v.create_type;
    j[SK::slot::has_filament] = v.has_filament;
    if (!v.has_filament) return;

    j[SK::slot::tray_name]     = v.tray_name;
    j[SK::slot::filament_name] = v.filament_name;
    j[SK::slot::filament_type] = v.filament_type;
    j[SK::slot::sub_brand]     = v.sub_brand;
    j[SK::slot::color]         = v.color;
    j[SK::slot::colors]        = v.colors;
    j[SK::slot::color_type]    = v.color_type;
    if (v.weight_gram.has_value()) j[SK::slot::weight_gram] = *v.weight_gram;
}

inline void to_json(nlohmann::json& j, const RefillGroup& v)
{
    namespace SK = AutoRefillRefillInfoSchema::keys;
    j = {
        {SK::group::refill_tag_id, v.refill_tag_id},
        {SK::group::filament_id,   v.filament_id},
        {SK::group::filament_type, v.filament_type},
        {SK::group::colors,        v.colors},
        {SK::group::color_type,    v.color_type},
        {SK::group::slots,         v.slots}
    };
    if (v.nozzle_diameter.has_value()) j[SK::group::nozzle_diameter] = *v.nozzle_diameter;
    if (!v.nozzle_flow_type.empty())   j[SK::group::nozzle_flow_type] = v.nozzle_flow_type;
}

inline void to_json(nlohmann::json& j, const RefillExtruder& v)
{
    namespace SK = AutoRefillRefillInfoSchema::keys;
    j = {
        {SK::extruder::extruder_id, v.extruder_id},
        {SK::extruder::groups,      v.groups}
    };
}

inline void to_json(nlohmann::json& j, const State& v)
{
    namespace SK = AutoRefillRefillInfoSchema::keys;
    j = {
        {SK::root::session_seq,     v.session_seq},
        {SK::root::selected_dev_id, v.selected_dev_id},
        {SK::root::auto_refill_enabled, v.auto_refill_enabled},
        {SK::root::editable,        v.editable},
        {SK::root::can_add_group,   v.can_add_group},
        {SK::root::extruders,       v.extruders}
    };
    if (!v.description.empty()) j[SK::root::description] = v.description;
}

} // namespace format

}}} // namespace Slic3r::GUI::AutoRefillRefillInfoSchema

#endif // AUTOREFILLREFILLINFOSCHEMA_HPP
