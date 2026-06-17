#ifndef DEVICEPAGESCHEMA_AMSFILAMENTHOTEND_HPP
#define DEVICEPAGESCHEMA_AMSFILAMENTHOTEND_HPP

// Schema for the JSON payload exchanged between the C++ ViewModel
// `device_page_ams_filament_hotend` and its Web counterpart living under
// src/slic3r/GUI/DeviceWeb/device_page/src/features/device-page/ams-filament-hotend/.
//
// The Web side mirrors the same shape in `types.ts`; whenever the two get out
// of sync UI silently breaks. Keep this header as the single source of truth
// for field names and enum string values so the C++ producer and the Web
// consumer reference exactly the same identifiers.
//
// Conventions:
//   * `keys::*` namespaces collect JSON object field names grouped by section.
//   * `values::*` namespaces collect the string enum values used by those
//     fields (card state, line state, rack mode, ...).
//   * Everything is `inline constexpr const char*` so it can be used in any
//     translation unit without ODR concerns.

namespace Slic3r { namespace GUI { namespace AmsHotendSchema {

namespace keys {

// Top-level state payload — split per page so each tab/component only sees
// the data it actually renders.
namespace root {
    inline constexpr const char* filament_page          = "filament_page";
    inline constexpr const char* hotend_page            = "hotend_page";
    inline constexpr const char* ams_ext_operation_page = "ams_ext_operation_page";
}

// state.filament_page — everything the Filament tab renders.
//
// AMS units used to live under `ams.units`, but every consumer now reads the
// per-slot `hotend_slots[].ams_operation.units` instead, so the top-level
// units array was dropped. Dryer-chamber state is lifted to
// `ams_dry_zones` (a flat array of drying-capable zones only) so the
// Filament tab can map zone → dryer slot without an intermediate object.
//
// `mode` is the UI mode the payload is targeted at — one of values::mode::*.
// Per-mode payload shape is in `design.md` section 7.3: normal mode carries
// full slot render data (filament_card/line/nozzle), batch modes only the
// slot identity + select_action.
namespace filament_page {
    inline constexpr const char* mode                   = "mode";        // values::mode::*
    inline constexpr const char* actions                = "actions";
    inline constexpr const char* filament_change_status = "filament_change_status";
    inline constexpr const char* ams_dry_zones          = "ams_dry_zones";
    inline constexpr const char* hotend_slots           = "hotend_slots";
}

// state.hotend_page — Hotend tab only.
namespace hotend_page {
    inline constexpr const char* actions            = "actions";
    inline constexpr const char* hotend_rack_status = "hotend_rack_status";
    inline constexpr const char* hotend_slots       = "hotend_slots";
}

// state.ams_ext_operation_page — data source for the Ext AMS operation dialog.
// It mirrors the visible hotend slots that can open the dialog, but carries the
// complete AMS-unit payload directly so Web does not need to reconstruct it
// from slot.bind_ams or URL query fallbacks.
namespace ams_ext_operation_page {
    inline constexpr const char* slots = "slots";
}

namespace ams_ext_operation_slot {
    inline constexpr const char* addr_id             = "addr_id";
    inline constexpr const char* display_index       = "display_index";
    inline constexpr const char* ams_id              = "ams_id";
    inline constexpr const char* slot_id             = "slot_id";
    inline constexpr const char* bind_ams            = "bind_ams";
    inline constexpr const char* bind_ams_slot_count = "bind_ams_slot_count";
    inline constexpr const char* units               = "units";
}

// state.filament_page.actions — filament tab toolbar.
namespace filament_actions {
    inline constexpr const char* can_dry           = "can_dry";
    inline constexpr const char* can_unload        = "can_unload";
    inline constexpr const char* can_load          = "can_load";
    inline constexpr const char* can_load_external = "can_load_external";
    inline constexpr const char* can_load_auto     = "can_load_auto";
    inline constexpr const char* show_ams_settings = "show_ams_settings";
    inline constexpr const char* show_auto_refill  = "show_auto_refill";
    inline constexpr const char* tips              = "tips";
}

// Batch-mode shape for filament_page.actions.<name>. In normal mode each
// toolbar action key holds a plain bool; in batch modes it holds this
// `{ enabled, reason }` object so the Web can show a tooltip on hover.
namespace action_state {
    inline constexpr const char* enabled = "enabled";
    inline constexpr const char* reason  = "reason";
}

// state.hotend_page.actions — hotend tab toolbar.
namespace hotend_actions {
    inline constexpr const char* can_read_all_hotends     = "can_read_all_hotends";
    inline constexpr const char* read_all_disabled_reason = "read_all_disabled_reason";
}

// state.hotend_rack_status
namespace rack_status {
    inline constexpr const char* active        = "active";
    inline constexpr const char* mode          = "mode";          // values::rack_mode
    inline constexpr const char* display_index = "display_index";
    inline constexpr const char* current       = "current";
    inline constexpr const char* total         = "total";
    inline constexpr const char* message       = "message";
    inline constexpr const char* tips          = "tips";
    inline constexpr const char* job           = "job";           // values::rack_job
    inline constexpr const char* operation     = "operation";     // values::rack_operation
    inline constexpr const char* state         = "state";         // values::rack_state
    inline constexpr const char* target_display_index = "target_display_index";
    inline constexpr const char* current_step  = "current_step";
    inline constexpr const char* steps         = "steps";
    inline constexpr const char* can_stop      = "can_stop";
    inline constexpr const char* can_retry     = "can_retry";
}

// state.hotend_rack_status.steps[]
namespace rack_step {
    inline constexpr const char* id    = "id";
    inline constexpr const char* label = "label";
    inline constexpr const char* state = "state"; // values::rack_step_state
}

// state.filament_change_status
namespace filament_change {
    inline constexpr const char* active             = "active";
    inline constexpr const char* mode               = "mode";       // "idle" | "single" | "batch_reserved" | "batch_running"
    inline constexpr const char* operation          = "operation";  // "change" | "unknown"
    inline constexpr const char* running_slot       = "running_slot";
    inline constexpr const char* current_step       = "current_step";
    inline constexpr const char* current_step_name  = "current_step_name";
    inline constexpr const char* steps              = "steps";
    inline constexpr const char* step_labels        = "step_labels";
    inline constexpr const char* current_step_index = "current_step_index";
    inline constexpr const char* current_step_total = "current_step_total";
    inline constexpr const char* message            = "message";
    inline constexpr const char* tips               = "tips";
    // Whether the user is allowed to abort the running filament change. Only
    // true while the panel is active AND the printer is neither printing nor
    // calibrating; Web binds the Stop button's disabled state to it.
    inline constexpr const char* can_stop           = "can_stop";
    // Per-slot batch progress info (mode == "batch_running"). Same shape as
    // the pre-run `batch_reserved` payload — Web reuses the same item list
    // type but renders a horizontal step strip instead of the reservation
    // pills. See `batch_progress` below for the field names.
    inline constexpr const char* batch_running      = "batch_running";
}

// state.filament_change_status.batch_running (and batch_reserved) shape.
// `current` is the 1-based index of the slot currently being processed,
// `total` mirrors items.size().
namespace batch_progress {
    inline constexpr const char* operation = "operation";
    inline constexpr const char* current   = "current";
    inline constexpr const char* total     = "total";
    inline constexpr const char* items     = "items";
}

// state.filament_change_status.batch_running.items[] entry.
// `state` enumerates the per-slot completion status. Until the firmware
// `cfs2[]` push is wired in, the C++ producer only emits `running` for the
// single active slot and `pending` for the rest; future firmware integration
// will fill in `done` / `failed`.
namespace batch_progress_item {
    inline constexpr const char* display_index = "display_index";
    inline constexpr const char* state         = "state";
    inline constexpr const char* reason        = "reason";
}

// state.filament_page.hotend_slots[] — what the Filament tab renders for a slot.
//
// NOTE: `is_on_extruder` and `hotend` are not part of the Filament tab API; the
// BuildSlot helper still emits them in its full-slot object so that BuildState
// can project the same source object into the Hotend tab's slot payload too.
// They are stripped from the final filament_page.hotend_slots[] entries.
namespace filament_slot {
    // identity / layout
    inline constexpr const char* addr_id             = "addr_id";
    inline constexpr const char* display_index       = "display_index";
    inline constexpr const char* ams_id              = "ams_id";
    inline constexpr const char* slot_id             = "slot_id";
    inline constexpr const char* bind_ams            = "bind_ams";
    inline constexpr const char* bind_ams_slot_count = "bind_ams_slot_count";
    inline constexpr const char* enabled             = "enabled";
    inline constexpr const char* is_external_spool   = "is_external_spool";
    inline constexpr const char* is_top_tier         = "is_top_tier";
    // Dryer information no longer lives on the slot. The DryerChamber
    // component reads everything (membership, humidity, status, drying flag)
    // from `filament_page.ams_zones[]` instead.
    inline constexpr const char* menu_actions        = "menu_actions";
    inline constexpr const char* ams_operation       = "ams_operation";
    // grouped render data
    inline constexpr const char* filament_card       = "filament_card";
    inline constexpr const char* filament_line       = "filament_line";
    inline constexpr const char* filament_nozzle     = "filament_nozzle";
    // Batch-mode-only: per-mode eligibility for the cards' selection checkbox.
    // Absent on normal-mode slots. See keys::select_action below.
    inline constexpr const char* select_action       = "select_action";

    // Internal-only fields kept in the BuildSlot output so BuildState can
    // project them into the Hotend page; never appear on the filament_page
    // payload after projection.
    inline constexpr const char* is_on_extruder = "is_on_extruder";
    inline constexpr const char* hotend         = "hotend";
    inline constexpr const char* hotend_menu_actions = "hotend_menu_actions";
    inline constexpr const char* hotend_detail_dialog = "hotend_detail_dialog";
}

// state.hotend_page.hotend_slots[] — Hotend tab cards only.
namespace hotend_slot {
    inline constexpr const char* addr_id       = "addr_id";
    inline constexpr const char* display_index = "display_index";
    inline constexpr const char* ams_id        = "ams_id";
    inline constexpr const char* slot_id       = "slot_id";
    inline constexpr const char* enabled       = "enabled";
    // True when this rack nozzle is currently mounted on the extruder; the
    // Hotend tab card shows a "Taken" state instead of the live temperature.
    inline constexpr const char* taken         = "taken";
    inline constexpr const char* menu_actions  = "menu_actions";
    inline constexpr const char* hotend        = "hotend";
    inline constexpr const char* hotend_detail_dialog = "hotend_detail_dialog";
}

// slot.filament_card — everything the spool card needs to render.
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
}

// slot.filament_card.prefeed_indicator — top-right color marker for AMS
// pre-feed while the hotend card itself is currently sourced from Ext.
namespace prefeed_indicator {
    inline constexpr const char* visible    = "visible";
    inline constexpr const char* colors     = "colors";
    inline constexpr const char* color_type = "color_type";
}

// slot.filament_line — feed-line state from the spool to the hotend.
namespace filament_line {
    inline constexpr const char* state          = "state";          // values::line_state
    inline constexpr const char* has_filament   = "has_filament";
    // Filament color currently visible inside the feed tube. Empty string
    // when there is no filament in the line; today mirrors filament_card.color
    // because C++ has no distinct in-tube color signal.
    inline constexpr const char* filament_color = "filament_color";
}

// slot.filament_nozzle — what the nozzle component needs to render.
namespace filament_nozzle {
    inline constexpr const char* state          = "state";          // values::nozzle_state
    inline constexpr const char* has_filament   = "has_filament";
    // Filament color currently inside the nozzle. Empty string when the
    // nozzle holds no filament; today mirrors filament_card.color.
    inline constexpr const char* filament_color = "filament_color";
    // True when this rack nozzle is the one mounted on the extruder right now.
    inline constexpr const char* on_extruder    = "on_extruder";
}

// slot.select_action — per-mode selection eligibility (batch modes only).
// Single object per slot (not a per-mode matrix); the active mode is carried
// by `filament_page.mode`. `ext_label` is true for slots that should show the
// "EXT" badge on the card head (only meaningful in `batch_ext_load`).
namespace select_action {
    inline constexpr const char* can_select = "can_select";
    inline constexpr const char* reason     = "reason";
    inline constexpr const char* ext_label  = "ext_label";
}

// slot.menu_actions
namespace menu_actions {
    inline constexpr const char* can_edit          = "can_edit";
    inline constexpr const char* can_load          = "can_load";
    inline constexpr const char* can_unload        = "can_unload";
    inline constexpr const char* can_read          = "can_read";
    inline constexpr const char* can_ams_operation = "can_ams_operation";
}

// slot.menu_actions for the Hotend tab.
namespace hotend_menu_actions {
    inline constexpr const char* can_read            = "can_read";
    inline constexpr const char* can_detail          = "can_detail";
    inline constexpr const char* can_set_temperature = "can_set_temperature";
    inline constexpr const char* can_take_place      = "can_take_place";
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

// state.ams_ext_operation_page.slots[].units[]
// Web does not consume `exist`, `temp`, the 0-5 `humidity` level, `dry_time`
// or `dry_sub_status`; keep them out of the payload until a UI needs them.
namespace ams_unit {
    inline constexpr const char* ams_id       = "ams_id";
    inline constexpr const char* ams_type     = "ams_type";
    inline constexpr const char* slot_count   = "slot_count";
    inline constexpr const char* humidity_raw = "humidity_raw";
    inline constexpr const char* dry_status   = "dry_status";
    inline constexpr const char* trays        = "trays";
}

// state.ams_ext_operation_page.slots[].units[].trays[]
// `reading` is a duplicate of `tray_state == "reading"`; Web reads tray_state
// instead so it is dropped here.
namespace ams_tray {
    inline constexpr const char* ams_id        = "ams_id";
    inline constexpr const char* slot_id       = "slot_id";
    inline constexpr const char* exists        = "exists";
    inline constexpr const char* tray_state    = "tray_state";   // values::tray_state
    inline constexpr const char* filament_type = "filament_type";
    inline constexpr const char* filament_name = "filament_name";
    inline constexpr const char* color         = "color";
    inline constexpr const char* color_type    = "color_type";
    inline constexpr const char* colors        = "colors";
    inline constexpr const char* remain        = "remain";
    inline constexpr const char* actions       = "actions";
}

// tray.actions (per-tray menu in the AMS Operation dialog)
namespace tray_actions {
    inline constexpr const char* can_load   = "can_load";
    inline constexpr const char* can_unload = "can_unload";
    inline constexpr const char* can_read   = "can_read";
}

// state.filament_page.ams_dry_zones[] — drives the DryerChamber decoration.
//
// Only zones whose physical layout actually supports drying are emitted
// here (today `DevAms::Zone::SupportDrying()` == `zone_slots.size() == 2`).
// Other AMS slot groupings stay out of the payload so the Web does not paint
// a chamber over unrelated columns.
//
// Each zone fully describes a dryer chamber: which slots it contains
// (`zone_slots`, native AMS slot_id values), the live humidity / dry status,
// plus a translated display label. The DryerChamber component matches
// `zone_slots` against filament slots' `(ams_id, slot_id)` to know which
// columns to wrap.
//
// `dry_status` MUST stay an untranslated English enum (Web compares it
// against literal "Idle"/"Off"); `dry_status_text` is the user-visible
// translated label.
namespace dryer_zone {
    inline constexpr const char* ams_id          = "ams_id";
    inline constexpr const char* zone_slots      = "zone_slots";
    inline constexpr const char* temp            = "temp";
    inline constexpr const char* humidity_raw    = "humidity_raw";
    inline constexpr const char* dry_time        = "dry_time";
    inline constexpr const char* dry_status      = "dry_status";       // logical enum
    inline constexpr const char* dry_status_text = "dry_status_text";  // translated label
}

// slot.hotend (the tool side; filament has_filament_in_nozzle lives under
// slot.filament_nozzle.has_filament).
namespace hotend {
    inline constexpr const char* current_temp   = "current_temp";
    inline constexpr const char* target_temp    = "target_temp";
    inline constexpr const char* op_state       = "op_state";   // values::hotend_op_state
    inline constexpr const char* tool_kind      = "tool_kind";  // values::tool_kind
    inline constexpr const char* nozzle_label   = "nozzle_label";
    inline constexpr const char* nozzle_subtext = "nozzle_subtext";
    inline constexpr const char* selected       = "selected";
}

// slot.hotend_detail_dialog — data used only by the hotend detail modal.
namespace hotend_detail_dialog {
    inline constexpr const char* filament_name  = "filament_name";
    inline constexpr const char* filament_color = "filament_color";
    inline constexpr const char* show_filament  = "show_filament";
    inline constexpr const char* tool_image_name = "tool_image_name";
    inline constexpr const char* tool_type      = "tool_type";
    inline constexpr const char* nozzle_type    = "nozzle_type";
    inline constexpr const char* firmware_version = "firmware_version";
    inline constexpr const char* serial_number  = "serial_number";
    inline constexpr const char* used_time_hours = "used_time_hours";
}

// state.filament_change_status.running_slot
namespace running_slot {
    inline constexpr const char* ams_id        = "ams_id";
    inline constexpr const char* slot_id       = "slot_id";
    inline constexpr const char* display_index = "display_index";
}

} // namespace keys

namespace values {

namespace rack_mode {
    inline constexpr const char* idle   = "idle";
    inline constexpr const char* single = "single";
    inline constexpr const char* all    = "all";
}

namespace rack_operation {
    inline constexpr const char* idle        = "idle";
    inline constexpr const char* take        = "take";
    inline constexpr const char* place       = "place";
    inline constexpr const char* exchange    = "exchange";
    inline constexpr const char* read_all    = "read_all";
    inline constexpr const char* read_single = "read_single";
    inline constexpr const char* move        = "move";
}

namespace rack_state {
    inline constexpr const char* idle    = "idle";
    inline constexpr const char* running = "running";
    inline constexpr const char* failed  = "failed";
}

namespace rack_step_state {
    inline constexpr const char* done    = "done";
    inline constexpr const char* active  = "active";
    inline constexpr const char* pending = "pending";
    inline constexpr const char* failed  = "failed";
}

namespace filament_change_mode {
    inline constexpr const char* idle           = "idle";
    inline constexpr const char* single         = "single";
    inline constexpr const char* batch_reserved = "batch_reserved";
    inline constexpr const char* batch_running  = "batch_running";
}

// values for batch_progress_item::state
namespace batch_item_state {
    inline constexpr const char* pending = "pending";
    inline constexpr const char* running = "running";
    inline constexpr const char* success = "success";
    inline constexpr const char* failed  = "failed";
}

// values for filament_page::mode — the four UI modes the page can render.
//   * normal           — default page (IDLE or single-slot 进/退料中)
//   * batch_ams_load   — entered by clicking the toolbar Auto Load button
//   * batch_ext_load   — entered by clicking the toolbar Load External Spool
//   * batch_unload     — entered by clicking the toolbar Unload button
// `Dry` is not a mode — it opens AMSDryCtrWin directly as a single-step dialog.
namespace mode {
    inline constexpr const char* normal         = "normal";
    inline constexpr const char* batch_ams_load = "batch_ams_load";
    inline constexpr const char* batch_ext_load = "batch_ext_load";
    inline constexpr const char* batch_unload   = "batch_unload";
}

namespace filament_change_operation {
    inline constexpr const char* change  = "change";
    inline constexpr const char* unknown = "unknown";
}

namespace card_state {
    inline constexpr const char* empty         = "Empty";
    inline constexpr const char* reading       = "Reading";
    inline constexpr const char* unknown       = "Unknown";
    inline constexpr const char* ready_to_load = "ReadyToLoad";
    inline constexpr const char* ready_to_use  = "ReadyToUse";
    inline constexpr const char* loading       = "Loading";
    inline constexpr const char* unloading     = "Unloading";
    inline constexpr const char* error         = "Error";
}

namespace line_state {
    inline constexpr const char* no_filament = "no_filament";
    inline constexpr const char* loaded      = "loaded";
    inline constexpr const char* loading     = "loading";
    inline constexpr const char* unloading   = "unloading";
    inline constexpr const char* no_hotend   = "no_hotend";
}

namespace nozzle_state {
    inline constexpr const char* normal  = "Normal";
    inline constexpr const char* active  = "Active";
    inline constexpr const char* unknown = "Unknown";
    inline constexpr const char* error   = "Error";
}

namespace tray_state {
    inline constexpr const char* empty   = "empty";
    inline constexpr const char* reading = "reading";
    inline constexpr const char* ready   = "ready";
    inline constexpr const char* unknown = "unknown";
    inline constexpr const char* error   = "error";
}

namespace tool_kind {
    inline constexpr const char* nozzle  = "nozzle";
    inline constexpr const char* cutter  = "cutter";
    inline constexpr const char* laser   = "laser";
    inline constexpr const char* fan     = "fan";
    inline constexpr const char* pen     = "pen";
    inline constexpr const char* unknown = "unknown";
    inline constexpr const char* empty   = "empty";
}

namespace hotend_op_state {
    inline constexpr const char* idle     = "Idle";
    inline constexpr const char* heating  = "Heating";
    inline constexpr const char* cooling  = "Cooling";
    inline constexpr const char* reading  = "Reading";
    inline constexpr const char* taking   = "Taking";
    inline constexpr const char* placing  = "Placing";
    inline constexpr const char* removing = "Removing";
    inline constexpr const char* error    = "Error";
}

} // namespace values

}}} // namespace Slic3r::GUI::AmsHotendSchema

#include "DevicePageSchema_AmsFilamentPage.hpp"
#include "DevicePageSchema_AmsHotendPage.hpp"
#include "DevicePageSchema_AmsExtOperationPage.hpp"

namespace Slic3r { namespace GUI { namespace AmsHotendSchema { namespace format {

struct State
{
    FilamentPage        filament_page;
    HotendPage          hotend_page;
    AmsExtOperationPage ams_ext_operation_page;
};

inline void to_json(nlohmann::json& j, const State& v)
{
    namespace SK = AmsHotendSchema::keys;
    j = {
        {SK::root::filament_page,          v.filament_page},
        {SK::root::hotend_page,            v.hotend_page},
        {SK::root::ams_ext_operation_page, v.ams_ext_operation_page}
    };
}

}}}} // namespace Slic3r::GUI::AmsHotendSchema::format

#endif // DEVICEPAGESCHEMA_AMSFILAMENTHOTEND_HPP
