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

// Top-level state payload - split per page so each tab/component only sees
// the data it actually renders.
namespace root {
    inline constexpr const char* filament_page          = "filament_page";
    inline constexpr const char* hotend_page            = "hotend_page";
    inline constexpr const char* ams_ext_operation_page = "ams_ext_operation_page";
}

// state.filament_page - everything the Filament tab renders.
//
// AMS units used to live under `ams.units`, but every consumer now reads the
// per-slot `hotend_slots[].ams_operation.units` instead, so the top-level
// units array was dropped. Dryer-chamber state is lifted to
// `ams_dry_zones` (a flat array of drying-capable zones only) so the
// Filament tab can map zone to dryer slot without an intermediate object.
//
// `mode` is the UI mode the payload is targeted at - one of values::mode::*.
// Per-mode payload shape is in `design.md` section 7.3: normal mode carries
// full slot render data (filament_card/line/nozzle), batch modes only the
// slot identity + select_action.
namespace filament_page {
    inline constexpr const char* mode                   = "mode";        // values::mode::*
    inline constexpr const char* actions                = "actions";
    inline constexpr const char* filament_change_status = "filament_change_status";
    inline constexpr const char* ams_dry_zones          = "ams_dry_zones";
    inline constexpr const char* ams_env_units          = "ams_env_units";
    inline constexpr const char* show_ams_env_units     = "show_ams_env_units";
    inline constexpr const char* hotend_slots           = "hotend_slots";
}

// state.hotend_page - Hotend tab only.
namespace hotend_page {
    inline constexpr const char* actions            = "actions";
    inline constexpr const char* hotend_rack_status = "hotend_rack_status";
    inline constexpr const char* hotend_slots       = "hotend_slots";
}

// state.ams_ext_operation_page - Ext AMS operation dialog.
namespace ams_ext_operation_page {
    inline constexpr const char* slots = "slots";
}

namespace ams_ext_operation_slot {
    inline constexpr const char* addr_id             = "addr_id";
    inline constexpr const char* display_index       = "display_index";
    inline constexpr const char* ams_id              = "ams_id";
    inline constexpr const char* slot_id             = "slot_id";
    inline constexpr const char* bind_ams            = "bind_ams";
    inline constexpr const char* bind_ams_count = "bind_ams_count";
    inline constexpr const char* units               = "units";
}

// state.filament_page.actions - filament tab toolbar.
namespace filament_actions {
    inline constexpr const char* can_dry           = "can_dry";
    inline constexpr const char* can_unload        = "can_unload";
    inline constexpr const char* can_load          = "can_load";
    inline constexpr const char* can_load_external = "can_load_external";
    inline constexpr const char* can_show_load_auto = "can_show_load_auto";
    inline constexpr const char* show_ams_settings = "show_ams_settings";
    inline constexpr const char* show_auto_refill  = "show_auto_refill";
    // Whether the toolbar's "read all" icon button is shown. Rack-B only
    // (N1x ZMULTI_B); Rack-A never exposes it. Enabled state still comes from
    // hotend_actions::can_read_all_hotends.
    inline constexpr const char* show_read_all     = "show_read_all";
    // Enabled state of the Rack-B "read all" icon: a connected ZMULTI_B AMS
    // exists and the printer is neither printing nor upgrading.
    inline constexpr const char* can_read_all_slots = "can_read_all_slots";
    inline constexpr const char* tips              = "tips";
}

// Batch-mode shape for filament_page.actions.<name>. In normal mode each
// toolbar action key holds a plain bool; in batch modes it holds this
// `{ enabled, reason }` object so the Web can show a tooltip on hover.
namespace action_state {
    inline constexpr const char* enabled = "enabled";
    inline constexpr const char* reason  = "reason";
}

// state.hotend_page.actions - hotend tab toolbar.
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
    // Whether the Retry button should be shown (not merely enabled).
    inline constexpr const char* can_retry     = "can_retry";
}

// state.hotend_rack_status.steps[]
namespace rack_step {
    inline constexpr const char* id    = "id";
    inline constexpr const char* label = "label";
    inline constexpr const char* state = "state"; // values::rack_step_state
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
// `read_label` mirrors menu_actions::read_label (values::read_label).
namespace tray_actions {
    inline constexpr const char* can_load   = "can_load";
    inline constexpr const char* can_unload = "can_unload";
    inline constexpr const char* can_read   = "can_read";
    inline constexpr const char* read_label = "read_label";
}

// state.filament_page.ams_dry_zones[] - drives the DryerChamber decoration.
//
// Only zones whose physical layout actually supports drying are emitted
// here (today `DevAms::Zone::SupportDrying()` == `zone_slots.size() == 2`).
// Other AMS slot groupings stay out of the payload so the Web does not paint
// a chamber over unrelated columns.
//
// Each zone fully describes a dryer chamber: which slots it contains
// (`zone_slots`, native AMS slot_id values; `zone_addrs`, the rack positions
// they sit in), the live humidity / dry status, plus a translated display
// label. The DryerChamber component wraps the columns whose `addr_id` is in
// `zone_addrs`, falling back to `zone_slots` vs `(ams_id, slot_id)`.
//
// `dry_status` MUST stay an untranslated English enum (Web compares it
// against literal "Idle"/"Off"); `dry_status_text` is the user-visible
// translated label.
namespace dryer_zone {
    inline constexpr const char* ams_id          = "ams_id";
    inline constexpr const char* zone_slots      = "zone_slots";
    // A slot reports whatever is feeding it, so an external spool in a dryer
    // bay hides the AMS slot behind it; the rack position stays put. Empty
    // when DevNozzle::GetRelatedAmsSlot() cannot resolve the rack.
    inline constexpr const char* zone_addrs      = "zone_addrs";
    inline constexpr const char* temp            = "temp";
    inline constexpr const char* humidity_raw    = "humidity_raw";
    inline constexpr const char* dry_time        = "dry_time";
    inline constexpr const char* dry_status      = "dry_status";       // logical enum
    inline constexpr const char* dry_status_text = "dry_status_text";  // translated label
}

// state.filament_page.ams_env_units[] - AMS-unit level temperature / humidity.
//
// Unlike `ams_dry_zones`, which describes one dryer chamber each, an entry
// here covers a whole AMS unit (or its non-drying zone) and carries the
// readings the unit itself reports (`DevAms::GetHumidityPercent()` /
// `GetCurrentTemperature()`), with no dry state. The Filament tab renders it
// as a bare readout anchored to the leftmost column of `zone_addrs` (falling
// back to `ams_id` when `zone_addrs` is omitted).
//
// Emitted only for units that report a real humidity percentage
// (`DevAms::SupportHumidityPercent()`) AND have an area outside a dryer
// chamber - a unit that is entirely a chamber already shows its readings
// through `ams_dry_zones`.
//
// `temp` is null when no valid temperature has been reported;
// `humidity_raw` uses -1 for the same case.
namespace env_unit {
    inline constexpr const char* ams_id       = "ams_id";
    inline constexpr const char* zone_addrs   = "zone_addrs";
    inline constexpr const char* temp         = "temp";
    inline constexpr const char* humidity_raw = "humidity_raw";
}

} // namespace keys

namespace values {

// values for filament_page::mode - the four UI modes the page can render.
//   * normal           - default page (IDLE or single-slot filament change running)
//   * batch_ams_load   - entered by clicking the toolbar Auto Load button
//   * batch_ext_load   - entered by clicking the toolbar Manual Loading
//   * batch_unload     - entered by clicking the toolbar Unload button
// `Dry` is not a mode - it opens AMSDryCtrWin directly as a single-step dialog.
namespace mode {
    inline constexpr const char* normal         = "normal";
    inline constexpr const char* batch_ams_load = "batch_ams_load";
    inline constexpr const char* batch_ext_load = "batch_ext_load";
    inline constexpr const char* batch_unload   = "batch_unload";
}

namespace card_state {
    inline constexpr const char* empty             = "Empty";
    inline constexpr const char* awaiting_tpu_read = "AwaitingTpuRead";
    inline constexpr const char* reading           = "Reading";
    inline constexpr const char* unknown           = "Unknown";
    inline constexpr const char* ready_to_load = "ReadyToLoad";
    inline constexpr const char* ready_to_use  = "ReadyToUse";
    inline constexpr const char* loading       = "Loading";
    inline constexpr const char* unloading     = "Unloading";
    inline constexpr const char* error         = "Error";
}

// i18n keys for menu_actions / tray_actions `read_label`. Web calls t(read_label).
// Unknown filament type -> Read; known type -> Reread.
namespace read_label {
    inline constexpr const char* read   = "Read";
    inline constexpr const char* reread = "Reread";
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
    inline constexpr const char* none    = "None";
    inline constexpr const char* error   = "Error";
    inline constexpr const char* lost    = "Lost";
    inline constexpr const char* feed_suspect = "FeedSuspect";
    inline constexpr const char* unknown      = "Unknown";
}

namespace tray_state {
    inline constexpr const char* empty   = "empty";
    inline constexpr const char* reading = "reading";
    inline constexpr const char* ready   = "ready";
    inline constexpr const char* unknown = "unknown";
    inline constexpr const char* error   = "error";
}

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

namespace tool_kind {
    inline constexpr const char* nozzle  = "nozzle";
    inline constexpr const char* cutter  = "cutter";
    inline constexpr const char* laser   = "laser";
    inline constexpr const char* fan     = "fan";
    inline constexpr const char* pen     = "pen";
    inline constexpr const char* unknown = "unknown";
    inline constexpr const char* empty   = "empty";
}

// state.hotend_page.hotend_slots[].hotend.op_state
namespace hotend_op_state {
    // Health - mirrors values::nozzle_state.
    inline constexpr const char* normal       = "Normal";
    inline constexpr const char* active       = "Active";
    inline constexpr const char* none         = "None";
    inline constexpr const char* error        = "Error";
    inline constexpr const char* lost_in_holder = "Lost";
    inline constexpr const char* feed_suspect = "FeedSuspect";
    inline constexpr const char* unknown      = "Unknown";

    // Rack jobs - hotend only, override the health state.
    inline constexpr const char* reading      = "Reading";
    inline constexpr const char* taking       = "Taking";
    inline constexpr const char* placing      = "Placing";

    // Declared for the Web "Removing" readout; no backend signal drives it yet.
    inline constexpr const char* removing     = "Removing";
}

// state.hotend_page.hotend_slots[].hotend.temp_trend - split out of op_state so
// the heating / cooling arrows survive alongside a fault state.
namespace hotend_temp_trend {
    inline constexpr const char* steady  = "Steady";
    inline constexpr const char* heating = "Heating";
    inline constexpr const char* cooling = "Cooling";
}

} // namespace values

}}} // namespace Slic3r::GUI::AmsHotendSchema

#include "SchemaFilamentSlot.hpp"
#include "SchemaHotendSlot.hpp"
#include "SchemaFilamentChangeStatus.hpp"
#include "SchemaFilamentPage.hpp"
#include "SchemaHotendPage.hpp"
#include "SchemaExtOperationPage.hpp"

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
