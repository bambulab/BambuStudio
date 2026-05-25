#ifndef DEVICEPAGEAMSFILAMENTHOTENDBATCHSCHEMA_HPP
#define DEVICEPAGEAMSFILAMENTHOTENDBATCHSCHEMA_HPP

// Schema for the batch-mode JSON payloads exchanged by the same Web tab as
// DevicePageAmsFilamentHotendSchema.hpp. Batch-mode lives in its own header so
// the cheap "regular" state path is never polluted by batch-specific fields
// (per-slot select_action, batch progress, batch.run plumbing, etc.).
//
// Module identity stays the same (`device_page_ams_filament_hotend`); batch
// flows just open a new submod. See AmsHotendBatchSchema::submod below.
//
// Routing summary:
//   request   submod=batch_state    action=get   payload={mode}        ->  filament_page (batch shape)
//   request   submod=batch_run      action=start payload={mode,slots,source}
//   report    submod=batch_progress payload={mode,running_slot,queue,...}
//
// Conventions mirror the regular header:
//   * `keys::*`   — JSON object field names grouped by section.
//   * `values::*` — string enum values used by those fields.
//   * `inline constexpr const char*` everywhere (ODR-safe).
//
// Field-name re-use rule: when a key has the SAME semantic meaning as the
// regular header (e.g. `ams_id`, `slot_id`, `mode`, `message`, `tips`), prefer
// the same string literal here so the Web consumer can share helpers. The
// duplicate literal is intentional — no cross-namespace include between the
// two headers, so editing this file never threatens the regular path.

namespace Slic3r { namespace GUI { namespace AmsHotendBatchSchema {

// Submod / action vocabulary used in the bridge message header.
namespace submod {
    inline constexpr const char* batch_state    = "batch_state";    // get the batch-shaped filament_page
    inline constexpr const char* batch_run      = "batch_run";      // start a batch operation
    inline constexpr const char* batch_progress = "batch_progress"; // C++ -> Web push
}

namespace action {
    inline constexpr const char* get   = "get";    // batch_state.get
    inline constexpr const char* start = "start";  // batch_run.start
    inline constexpr const char* cancel = "cancel"; // batch_run.cancel
}

namespace keys {

// === batch_state.get response ===========================================
// Top-level shape: one root field `filament_page`. We deliberately only
// emit the filament tab (batch mode has no equivalent for the hotend tab).
namespace root {
    inline constexpr const char* filament_page = "filament_page";
}

// state.filament_page (batch shape) — what the selection-mode UI renders.
namespace filament_page {
    inline constexpr const char* mode          = "mode";          // values::mode
    inline constexpr const char* actions       = "actions";        // 4 toolbar entries
    inline constexpr const char* hotend_slots  = "hotend_slots";   // per-slot select_action
    inline constexpr const char* ams_dry_zones = "ams_dry_zones";  // only for mode=dry
}

// state.filament_page.actions[<name>] — describes one toolbar action's
// availability + reason. The four keys are the action names below.
namespace actions {
    inline constexpr const char* can_load_auto     = "can_load_auto";
    inline constexpr const char* can_load_external = "can_load_external";
    inline constexpr const char* can_unload        = "can_unload";
    inline constexpr const char* can_dry           = "can_dry";
}

// Per-action availability object. Distinct from the regular bool fields on
// `state.filament_page.actions` so the batch UI can show a tooltip reason.
namespace action_state {
    inline constexpr const char* enabled = "enabled";
    inline constexpr const char* reason  = "reason";
}

// state.filament_page.hotend_slots[] (batch shape) — minimal slot view.
//
// Only the fields the selection-mode UI actually reads are present; full
// card / line / nozzle render data is reused from the regular path's
// schema (the Web side overlays select_action onto the slot it already
// has cached).
namespace slot {
    inline constexpr const char* nozzle_pos    = "nozzle_pos";
    inline constexpr const char* display_index = "display_index";
    inline constexpr const char* ams_id        = "ams_id";
    inline constexpr const char* slot_id       = "slot_id";
    inline constexpr const char* is_top_tier   = "is_top_tier";
    inline constexpr const char* select_action = "select_action";
}

// slot.select_action — single mode-specific eligibility, not a matrix.
namespace select_action {
    inline constexpr const char* can_select = "can_select";
    inline constexpr const char* reason     = "reason";
    // True for the external-spool slot in load_external mode; the Web puts
    // an "EXT" tag on the card head.
    inline constexpr const char* ext_label  = "ext_label";
}

// === batch_run.start request ============================================
namespace run_request {
    inline constexpr const char* mode   = "mode";    // values::mode
    inline constexpr const char* slots  = "slots";   // array of {ams_id, slot_id}
    inline constexpr const char* source = "source";  // optional, e.g. {ams_id, slot_id} for load_auto
}

namespace run_slot {
    inline constexpr const char* ams_id  = "ams_id";
    inline constexpr const char* slot_id = "slot_id";
}

namespace run_source {
    inline constexpr const char* ams_id  = "ams_id";
    inline constexpr const char* slot_id = "slot_id";
}

// === batch_progress report payload ======================================
namespace progress {
    inline constexpr const char* active            = "active";
    inline constexpr const char* mode              = "mode";              // values::mode
    inline constexpr const char* running_slot      = "running_slot";      // see progress_slot
    inline constexpr const char* queued_slots      = "queued_slots";      // array of progress_slot
    inline constexpr const char* current_step      = "current_step";      // int
    inline constexpr const char* current_step_name = "current_step_name"; // string (translated)
    inline constexpr const char* message           = "message";           // string (translated)
    inline constexpr const char* tips              = "tips";              // string (translated)
}

namespace progress_slot {
    inline constexpr const char* ams_id        = "ams_id";
    inline constexpr const char* slot_id       = "slot_id";
    inline constexpr const char* display_index = "display_index";
}

} // namespace keys

namespace values {

// values for keys::filament_page::mode / keys::progress::mode /
// keys::run_request::mode. Every batch entry point uses the same set so the
// Web has a single source of mode literals.
namespace mode {
    inline constexpr const char* load_auto     = "load_auto";
    inline constexpr const char* load_external = "load_external";
    inline constexpr const char* unload        = "unload";
    inline constexpr const char* dry           = "dry";
}

} // namespace values

}}} // namespace Slic3r::GUI::AmsHotendBatchSchema

#endif // DEVICEPAGEAMSFILAMENTHOTENDBATCHSCHEMA_HPP
