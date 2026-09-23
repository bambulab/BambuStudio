#ifndef DEVICEPAGESCHEMA_FILAMENTSLOT_HPP
#define DEVICEPAGESCHEMA_FILAMENTSLOT_HPP

namespace Slic3r { namespace GUI { namespace AmsHotendSchema { namespace keys {

// state.filament_page.hotend_slots[] - what the Filament tab renders for a slot.
//
// NOTE: `is_on_extruder` and `hotend` are not part of the Filament tab API; the
// current mixed BuildSlot helper still emits them so BuildState can project the
// same source object into the Hotend page. They are stripped from the final
// filament_page payload.
namespace filament_slot {
    // identity / layout
    inline constexpr const char* addr_id             = "addr_id";
    inline constexpr const char* display_index       = "display_index";
    inline constexpr const char* ams_id              = "ams_id";
    inline constexpr const char* slot_id             = "slot_id";
    inline constexpr const char* bind_ams            = "bind_ams";
    inline constexpr const char* bind_ams_count = "bind_ams_count";
    inline constexpr const char* enabled             = "enabled";
    inline constexpr const char* is_external_spool   = "is_external_spool";
    inline constexpr const char* is_top_tier         = "is_top_tier";
    inline constexpr const char* menu_actions        = "menu_actions";
    inline constexpr const char* ams_operation       = "ams_operation";
    // grouped render data
    inline constexpr const char* filament_card       = "filament_card";
    inline constexpr const char* filament_line       = "filament_line";
    inline constexpr const char* filament_nozzle     = "filament_nozzle";
    // Batch-mode-only: per-mode eligibility for the cards' selection checkbox.
    // Absent on normal-mode slots. See keys::select_action below.
    inline constexpr const char* select_action       = "select_action";

    // Internal-only fields kept in the mixed BuildSlot output so BuildState can
    // project them into the Hotend page; never appear on the filament_page
    // payload after projection.
    inline constexpr const char* is_on_extruder = "is_on_extruder";
    inline constexpr const char* hotend         = "hotend";
    inline constexpr const char* hotend_menu_actions = "hotend_menu_actions";
    inline constexpr const char* hotend_detail_dialog = "hotend_detail_dialog";
}

// slot.filament_card - everything the spool card needs to render.
namespace filament_card {
    inline constexpr const char* state         = "state";          // values::card_state
    inline constexpr const char* color         = "color";
    inline constexpr const char* color_type    = "color_type";
    inline constexpr const char* colors        = "colors";
    inline constexpr const char* prefeed_indicator = "prefeed_indicator";
    inline constexpr const char* text          = "text";           // primary card text, e.g. "PLA Basic"
    inline constexpr const char* name          = "name";           // filament name, e.g. "PLA Basic"
    inline constexpr const char* type          = "type";           // filament type,  e.g. "PLA"
    inline constexpr const char* remain        = "remain";         // 0..100
    inline constexpr const char* status_text   = "status_text";
    // True when a newly auto-recorded official filament badge is pending for
    // this slot (see wgtFilaManagerSync::has_pending_badge).
    inline constexpr const char* new_filament_hint = "new_filament_hint";
}

// slot.filament_card.prefeed_indicator - top-right color marker for AMS
// pre-feed while the hotend card itself is currently sourced from Ext.
// `prefeed_fila_type` mirrors the preload tray's m_fila_type; empty string
// means unknown filament (same rule as ResolveCardState) and Web shows "?".
namespace prefeed_indicator {
    inline constexpr const char* visible           = "visible";
    inline constexpr const char* colors            = "colors";
    inline constexpr const char* color_type        = "color_type";
    inline constexpr const char* prefeed_fila_type = "prefeed_fila_type";
}

// slot.filament_line - feed-line state from the spool to the hotend.
namespace filament_line {
    inline constexpr const char* state          = "state";          // values::line_state
    inline constexpr const char* has_filament   = "has_filament";
    inline constexpr const char* filament_color = "filament_color";
}

// slot.filament_nozzle - what the nozzle component needs to render.
namespace filament_nozzle {
    inline constexpr const char* state          = "state";          // values::nozzle_state
    inline constexpr const char* has_filament   = "has_filament";
    inline constexpr const char* filament_color = "filament_color";
    // True when this rack nozzle is the one mounted on the extruder right now.
    inline constexpr const char* on_extruder    = "on_extruder";
    // Tool type in this slot (values::tool_kind). Lets the Filament tab pick a
    // cutter/pen silhouette instead of the default nozzle; combined with
    // on_extruder it selects the "racked" (gripped by the tool changer) variant.
    inline constexpr const char* tool_kind       = "tool_kind";
    inline constexpr const char* tooltip         = "tooltip";

    // Two-line caption under the nozzle silhouette. eg. 0.4 HF
    inline constexpr const char* nozzle_label    = "nozzle_label";
    inline constexpr const char* nozzle_subtext  = "nozzle_subtext";
}

// slot.select_action - per-mode selection eligibility (batch modes only).
// Single object per slot (not a per-mode matrix); the active mode is carried
// by `filament_page.mode`. `ext_label` is true for slots that should show the
// "EXT" badge on the card head (only meaningful in `batch_ext_load`).
namespace select_action {
    inline constexpr const char* can_select = "can_select";
    inline constexpr const char* reason     = "reason";
    inline constexpr const char* ext_label  = "ext_label";
}

// slot.menu_actions
// `read_label` is an i18n key for the Read menu item: values::read_label.
// Unknown filament type -> "Read"; known type -> "Reread". Web renders via t().
namespace menu_actions {
    inline constexpr const char* can_edit          = "can_edit";
    inline constexpr const char* can_load          = "can_load";
    inline constexpr const char* can_unload        = "can_unload";
    inline constexpr const char* can_read          = "can_read";
    inline constexpr const char* read_label        = "read_label";
    inline constexpr const char* can_ams_operation = "can_ams_operation";
}

// slot.ams_operation
namespace ams_operation {
    inline constexpr const char* available = "available";
}

// slot.bind_ams[]
namespace bind_ams {
    inline constexpr const char* ams_id     = "ams_id";
    inline constexpr const char* ams_type   = "ams_type";
    inline constexpr const char* slot_count = "slot_count";
}

}}}} // namespace Slic3r::GUI::AmsHotendSchema::keys

#endif // DEVICEPAGESCHEMA_FILAMENTSLOT_HPP
