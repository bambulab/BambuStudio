#ifndef AUTOREFILLREFILLINFOSCHEMA_HPP
#define AUTOREFILLREFILLINFOSCHEMA_HPP

// Schema for the JSON payload exchanged between the C++ ViewModel
// `device_page_auto_refill_refill_info` and its Web counterpart living under
// src/slic3r/GUI/DeviceWeb/device_page/src/features/device-page/auto-refill-refill-info/.
//
// The Web side mirrors the same shape in `types.ts`; keep this header as the
// single source of truth for field names and enum string values so the C++
// producer and the Web consumer reference exactly the same identifiers.
//
// Conventions (aligned with AmsFilamentHotend/Schema.hpp):
//   * `keys::*`   - JSON field names grouped by object section.
//   * `values::*` - the string enum values those fields may take.
//   * `format::*` - structs mirroring the JSON shape plus `to_json()` so the
//                   ViewModel builds a typed tree instead of hand-writing JSON.
//
// Only the RefillVersion::RefillInfo variant is modelled here; the legacy
// FilaBackup dialog keeps its own native rendering.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace AutoRefillRefillInfoSchema {

namespace keys {

// Top-level state payload returned by the ViewModel.
namespace root {
    inline constexpr const char* selected_dev_id = "selected_dev_id";
    inline constexpr const char* extruders       = "extruders";
    // Monotonically increasing counter bumped on every `Refresh()` call, so the
    // Web side can tell a fresh dialog-open apart from an in-place data refresh.
    inline constexpr const char* session_seq     = "session_seq";
    // Page-level presentation driven by C++ so the Web side stays declarative:
    //   description   - localized explanatory text under the title; empty = hide.
    //   editable      - refill relations can be reordered / added / removed.
    //   can_add_group - the "add material group" entry button is shown.
    inline constexpr const char* description      = "description";
    inline constexpr const char* editable         = "editable";
    inline constexpr const char* can_add_group    = "can_add_group";
}

// One entry per extruder; each carries the refill groups resolved for it.
namespace extruder {
    inline constexpr const char* extruder_id = "extruder_id";
    inline constexpr const char* groups      = "groups";
}

// A refill group = one filament "chain": ordered slots that back each other up.
namespace group {
    inline constexpr const char* filament_id      = "filament_id";
    inline constexpr const char* filament_type    = "filament_type";
    inline constexpr const char* colors           = "colors";
    inline constexpr const char* color_type       = "color_type";      // values::color_type::*
    inline constexpr const char* nozzle_diameter  = "nozzle_diameter"; // omitted when unknown
    inline constexpr const char* nozzle_flow_type = "nozzle_flow_type";// omitted when unknown
    inline constexpr const char* slots            = "slots";
}

// One physical tray inside a group, in refill-priority order.
namespace slot {
    inline constexpr const char* ams_id        = "ams_id";
    inline constexpr const char* slot_id       = "slot_id";
    inline constexpr const char* tray_name     = "tray_name";
    inline constexpr const char* filament_type = "filament_type";
    inline constexpr const char* sub_brand     = "sub_brand";
    inline constexpr const char* color         = "color";
    inline constexpr const char* colors        = "colors";
    inline constexpr const char* color_type    = "color_type";  // values::color_type::*
    inline constexpr const char* weight_gram   = "weight_gram"; // remaining grams; omitted when unknown
    inline constexpr const char* has_filament  = "has_filament";
    inline constexpr const char* create_type   = "create_type"; // values::create_type::*
}

} // namespace keys

namespace values {

// group.color_type / slot.color_type - mirrors DevFilaColorType.
namespace color_type {
    inline constexpr const char* gradient = "gradient";
    inline constexpr const char* multi    = "multi";
    inline constexpr const char* single   = "single";
}

// slot.create_type - how the refill relation for this slot was created.
namespace create_type {
    inline constexpr const char* automatic = "auto";   // system generated
    inline constexpr const char* manual    = "manual"; // user added
    inline constexpr const char* unknown   = "unknown";
}

// group.nozzle_flow_type - short display label shown next to the nozzle
// diameter (e.g. "0.4 Std"); empty when unknown. Values match the shared
// nozzle-flow short names used by AmsFilamentHotend (PageHotend.cpp).
namespace nozzle_flow_type {
    inline constexpr const char* standard  = "Std";
    inline constexpr const char* high_flow = "HF";
    inline constexpr const char* ultra     = "UHF";
    inline constexpr const char* tough     = "TF";
    inline constexpr const char* elastic   = "EHF";
}

} // namespace values

namespace format {

// One tray inside a refill group, in refill-priority order.
struct RefillSlot
{
    int         ams_id       = 0;
    int         slot_id      = 0;
    // values::create_type::* -> "auto" | "manual" | "unknown"
    std::string create_type  = values::create_type::unknown;
    bool        has_filament = false;

    // The fields below are only serialized when `has_filament` is true (the
    // tray is resolvable); an empty slot carries just the identity above.
    std::string              tray_name;
    std::string              filament_type;
    std::string              sub_brand;
    std::string              color;
    std::vector<std::string> colors;
    // values::color_type::* -> "single" | "multi" | "gradient"
    std::string              color_type = values::color_type::single;
    // Remaining filament weight in grams; omitted from JSON when unknown.
    std::optional<int>       weight_gram;
};

// One filament chain: ordered backup slots sharing type / color / nozzle.
struct RefillGroup
{
    std::string              filament_id;
    std::string              filament_type;
    std::vector<std::string> colors;
    // values::color_type::* -> "single" | "multi" | "gradient"
    std::string              color_type = values::color_type::single;
    // Both nozzle fields are optional; omitted from JSON when unset.
    std::optional<float>     nozzle_diameter;
    // values::nozzle_flow_type::* -> "Std" | "HF" | "UHF" | "TF" | "EHF"; empty = omit.
    std::string              nozzle_flow_type;
    std::vector<RefillSlot>  slots;
};

// Refill groups resolved for one extruder.
struct RefillExtruder
{
    int                      extruder_id = 0;
    std::vector<RefillGroup> groups;
};

// Full state payload pushed to the Web dialog.
struct State
{
    std::string                 selected_dev_id;
    std::uint32_t               session_seq = 0;
    // Page-level presentation flags / copy resolved by the ViewModel.
    std::string                 description;         // localized; empty = hide
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
        {SK::root::editable,        v.editable},
        {SK::root::can_add_group,   v.can_add_group},
        {SK::root::extruders,       v.extruders}
    };
    if (!v.description.empty()) j[SK::root::description] = v.description;
}

} // namespace format

}}} // namespace Slic3r::GUI::AutoRefillRefillInfoSchema

#endif // AUTOREFILLREFILLINFOSCHEMA_HPP
