#ifndef DEVICEPAGEAMSMAPPINGHOTENDSCHEMA_HPP
#define DEVICEPAGEAMSMAPPINGHOTENDSCHEMA_HPP

// Schema for the JSON payload exchanged between the C++ ViewModel
// `device_page_ams_mapping_hotend` and its Web counterpart living under
// src/slic3r/GUI/DeviceWeb/device_page/src/features/device-page/ams-mapping-hotend/.
//
// The Web side mirrors the same shape in `types.ts`; whenever the two get out
// of sync UI silently breaks. Keep this header as the single source of truth
// for field names and enum string values so the C++ producer and the Web
// consumer reference exactly the same identifiers.
//
// Conventions:
//   * `keys::*` namespaces collect JSON object field names grouped by section.
//   * `values::*` namespaces collect the string enum values used by those
//     fields (show_type, slot_state, color_type).
//   * Everything is `inline constexpr const char*` so it can be used in any
//     translation unit without ODR concerns.

namespace Slic3r { namespace GUI { namespace AmsMappingHotendSchema {

namespace keys {

// Top-level state payload returned by BuildState().
namespace root {
    inline constexpr const char* selected_dev_id     = "selected_dev_id";
    inline constexpr const char* current_filament_id = "current_filament_id";
    inline constexpr const char* show_type           = "show_type";
    inline constexpr const char* material_index      = "material_index";
    inline constexpr const char* reset_visible       = "reset_visible";
    inline constexpr const char* is_all_ext          = "is_all_ext";
    inline constexpr const char* sub_ams_groups      = "sub_ams_groups";
    inline constexpr const char* hotend_slots        = "hotend_slots";
    // Monotonically increasing counter bumped on every `Refresh()` call.
    // The Web side resets transient view state (e.g. the sub-AMS sub-page) when
    // the value changes, so re-opening the popup always lands on the main view.
    inline constexpr const char* session_seq         = "session_seq";
    inline constexpr const char* theme               = "theme";        // "dark" | "light"
}

// Per-slot fields — shared between main hotend slots and sub-AMS slots.
// Sub-slots omit `is_top_tier`, `nozzle`, and `sub_ams`.
namespace slot {
    inline constexpr const char* ams_id        = "ams_id";
    inline constexpr const char* slot_id       = "slot_id";
    inline constexpr const char* tray_id       = "tray_id";
    inline constexpr const char* index         = "index";
    inline constexpr const char* is_top_tier   = "is_top_tier";
    inline constexpr const char* ext           = "ext";
    inline constexpr const char* state         = "state";          // values::slot_state::*
    inline constexpr const char* filament_type = "filament_type";
    inline constexpr const char* filament_name = "filament_name";
    inline constexpr const char* color         = "color";
    inline constexpr const char* ctype         = "ctype";
    inline constexpr const char* color_type    = "color_type";     // values::color_type::*
    inline constexpr const char* remain        = "remain";
    inline constexpr const char* has_filament  = "has_filament";
    inline constexpr const char* is_pickable   = "is_pickable";
    inline constexpr const char* can_edit      = "can_edit";
    inline constexpr const char* checked       = "checked";
    inline constexpr const char* multi_colors  = "multi_colors";
    inline constexpr const char* nozzle        = "nozzle";
    inline constexpr const char* sub_ams       = "sub_ams";
    // Main slot only: the block this hotend imposes on its other feed sources
    // because of what occupies it right now. Drives the sub-page banner when
    // it is `ext_spool`.
    inline constexpr const char* hotend_feed_block = "hotend_feed_block";
    // Sub-slot only: why the printer cannot feed this slot on its own. For
    // `ext_spool` it drives both the tooltip and the non-pickable state.
    inline constexpr const char* feed_block        = "feed_block";
}

// slot.nozzle — per-slot nozzle info row.
namespace nozzle {
    inline constexpr const char* dia       = "dia";
    inline constexpr const char* flow_type = "flow_type";
    inline constexpr const char* tool_kind = "tool_kind";   // values::tool_kind::*
}

// slot.sub_ams[] — sub-AMS group entry.
namespace sub_ams_group {
    inline constexpr const char* name       = "name";
    inline constexpr const char* is_preload = "is_preload";
    inline constexpr const char* slots      = "slots";
}

// action/select request payload fields.
namespace select_action {
    inline constexpr const char* r            = "r";
    inline constexpr const char* g            = "g";
    inline constexpr const char* b            = "b";
    inline constexpr const char* a            = "a";
    inline constexpr const char* tray_index   = "tray_index";
    inline constexpr const char* filament_id  = "filament_id";
    // ams_id, slot_id, ctype, multi_colors reuse keys::slot::*
}

} // namespace keys

namespace values {

// root.show_type
namespace show_type {
    inline constexpr const char* left                   = "LEFT";
    inline constexpr const char* right                  = "RIGHT";
    inline constexpr const char* left_and_right         = "LEFT_AND_RIGHT";
    inline constexpr const char* left_and_right_dynamic = "LEFT_AND_RIGHT_DYNAMIC";
}

// slot.state
namespace slot_state {
    inline constexpr const char* normal              = "Normal";
    inline constexpr const char* material_unmatch    = "MaterialUnmatch";
    inline constexpr const char* material_empty      = "MaterialEmpty";
    inline constexpr const char* nozzle_unmatch      = "NozzleUnmatch";
    inline constexpr const char* nozzle_empty        = "NozzleEmpty";
    inline constexpr const char* nozzle_lost         = "NozzleLost";
    inline constexpr const char* nozzle_uncalibrated = "NozzleUncalibrated";
    inline constexpr const char* nozzle_error        = "NozzleError";
    inline constexpr const char* nozzle_unknown      = "NozzleUnknown";
}

// slot.feed_block / slot.hotend_feed_block — mirrors Slic3r::DevSlotFeedBlock.
// Only `ext_spool` makes a slot non-pickable; the others surface as send-page
// pre-print warnings.
namespace feed_block {
    inline constexpr const char* none             = "none";
    inline constexpr const char* need_manual_feed = "need_manual_feed";
    inline constexpr const char* rack_ams         = "rack_ams";
    inline constexpr const char* ext_spool        = "ext_spool";
}

// slot.color_type
namespace color_type {
    inline constexpr const char* single   = "single";
    inline constexpr const char* multi    = "multi";
    inline constexpr const char* gradient = "gradient";
}

// slot.nozzle.tool_kind — kind of tool mounted on the rack position. Only a
// printing nozzle can be matched to a filament; cutter/pen are rendered with
// their own icon and forced into the non-pickable NozzleUnmatch state.
namespace tool_kind {
    inline constexpr const char* nozzle = "nozzle";
    inline constexpr const char* cutter = "cutter";
    inline constexpr const char* pen    = "pen";
}

} // namespace values

}}} // namespace Slic3r::GUI::AmsMappingHotendSchema

#endif // DEVICEPAGEAMSMAPPINGHOTENDSCHEMA_HPP
