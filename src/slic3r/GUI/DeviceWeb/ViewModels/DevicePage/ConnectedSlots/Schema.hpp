#ifndef CONNECTEDSLOTSSCHEMA_HPP
#define CONNECTEDSLOTSSCHEMA_HPP

/*****************************************************************//**
 * \file   Schema.hpp
 * \brief  JSON contract of the `device_page_connected_slots` module.
 *
 * The Web side mirrors the same shape in `types.ts`; keep this header as the
 * single source of truth for field names and enum string values.
 *
 * Layout tree (one slot = one hotend dialog):
 *   top_slots_area.left_slot   = 槽位示意图 + 料盘 + 料线
 *   top_slots_area.right_slots = 外接示意图 + 料盘 + 料线
 *   bottom_hotend              = 下方料线 + 下方喷嘴
 *
 * Conventions (aligned with AmsFilamentHotend/Schema.hpp):
 *   \li `keys::*`   - JSON field names grouped by object section.
 *   \li `values::*` - the string enum values those fields may take.
 *   \li `format::*` - structs mirroring the JSON shape plus `to_json()`.
 *********************************************************************/

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace ConnectedSlotsSchema {

namespace keys {

/// Top-level state payload returned by the ViewModel.
namespace root {
    inline constexpr const char* selected_dev_id = "selected_dev_id";
    /// Monotonically increasing counter bumped on every `Refresh()` call, so the
    /// Web side can tell a fresh dialog-open apart from an in-place data refresh.
    inline constexpr const char* session_seq     = "session_seq";
    inline constexpr const char* slots           = "slots";
}

namespace slot {
    inline constexpr const char* addr_id         = "addr_id";
    inline constexpr const char* display_index   = "display_index";
    inline constexpr const char* top_slots_area  = "top_slots_area";
    inline constexpr const char* bottom_hotend   = "bottom_hotend";
}

namespace top_slots_area {
    inline constexpr const char* left_slot   = "left_slot";
    inline constexpr const char* right_slots = "right_slots";
}

/// One visual column (left slot / right ext).
namespace column {
    inline constexpr const char* schematic = "schematic"; // 1.x.1 示意图
    inline constexpr const char* unit      = "unit";      // 1.x.2 料盘 (ams_unit shape)
    inline constexpr const char* line      = "line";      // 1.x.3 料线 (filament_line shape)
}

namespace bottom_hotend {
    inline constexpr const char* line   = "line";   // 2.1 下方料线 (filament_line)
    inline constexpr const char* nozzle = "nozzle"; // 2.2 下方喷嘴 (filament_nozzle)
}

/// column.schematic - mini diagram above the trays.
/// `kind` is values::source_kind (ams = 槽位示意图, ext = 外接示意图).
namespace schematic {
    inline constexpr const char* kind           = "kind";
    inline constexpr const char* selected       = "selected";
    /// kind==ams: the diagram is a nozzle-rack position indicator, not a filament
    /// preview. `slot_count` is the rack capacity and `selected_index` the 0-based
    /// position this dialog was opened for (-1 when unknown).
    inline constexpr const char* slot_count     = "slot_count";
    inline constexpr const char* selected_index = "selected_index";
    inline constexpr const char* items          = "items";        // kind==ext: Ext 1..N
}

namespace schematic_item {
    inline constexpr const char* ext_index  = "ext_index"; // 1-based; identifies the item in source/select
    inline constexpr const char* ams_id     = "ams_id";    // empty for the Ext (virtual tray) item
    inline constexpr const char* label      = "label";
    inline constexpr const char* color      = "color";     // comma separated, one entry per slot
    inline constexpr const char* slot_count = "slot_count";// picks the single- / four-slot AMS silhouette
    inline constexpr const char* empty      = "empty";
    inline constexpr const char* selected   = "selected";
}

/// Payload of the `source` / `select` command: switch which item of
/// right_slots.schematic.items has its trays expanded below.
namespace source_select {
    inline constexpr const char* ext_index = "ext_index";
}

/// column.unit - same field names as AmsFilamentHotend ams_unit so tray
/// rendering can stay aligned. `unit` is null when the column is empty.
namespace ams_unit {
    inline constexpr const char* ams_id       = "ams_id";
    inline constexpr const char* ams_type     = "ams_type";
    inline constexpr const char* display_name = "display_name"; // C++ product name, e.g. "AMS H3" / "AMS-A"
    inline constexpr const char* slot_count   = "slot_count";
    inline constexpr const char* can_dry      = "can_dry";
    inline constexpr const char* humidity_raw = "humidity_raw";
    inline constexpr const char* humidity_level        = "humidity_level";
    inline constexpr const char* humidity_display_type = "humidity_display_type";
    inline constexpr const char* temp         = "temp";         // celsius; omit or null when unknown
    inline constexpr const char* dry_status   = "dry_status";
    inline constexpr const char* dry_status_text = "dry_status_text"; // translated dry_status; empty when !can_dry
    inline constexpr const char* trays        = "trays";
}

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

/// tray.actions - visibility of the tray context menu items. `read_label` is an
/// i18n key from values::read_label, rendered by the Web through t().
namespace tray_actions {
    inline constexpr const char* can_edit   = "can_edit";
    inline constexpr const char* can_load   = "can_load";
    inline constexpr const char* can_unload = "can_unload";
    inline constexpr const char* can_read   = "can_read";
    inline constexpr const char* read_label = "read_label";
}

/// Payload of the `action` / `edit|load|unload|read` commands.
namespace tray_action {
    inline constexpr const char* ams_id  = "ams_id";
    inline constexpr const char* slot_id = "slot_id";
}

/// Payload of the `action` / `dry` command.
namespace dry_action {
    inline constexpr const char* ams_id = "ams_id";
}

/// column.line / bottom.line - same shape as AmsFilamentHotend filament_line.
namespace filament_line {
    inline constexpr const char* state          = "state";          // values::line_state
    inline constexpr const char* has_filament   = "has_filament";
    inline constexpr const char* filament_color = "filament_color";
}

/// bottom.nozzle - same shape as AmsFilamentHotend filament_nozzle.
namespace filament_nozzle {
    inline constexpr const char* state          = "state";          // values::nozzle_state
    inline constexpr const char* has_filament   = "has_filament";
    inline constexpr const char* filament_color = "filament_color";
    inline constexpr const char* on_extruder    = "on_extruder";
}

} // namespace keys

namespace values {

namespace source_kind {
    inline constexpr const char* ams = "ams";
    inline constexpr const char* ext = "ext";
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
}

namespace tray_state {
    inline constexpr const char* empty     = "empty";
    inline constexpr const char* reading   = "reading";
    inline constexpr const char* ready     = "ready";
    inline constexpr const char* unknown   = "unknown";
    inline constexpr const char* error     = "error";
    /// Transport states: this very slot is the one the firmware is moving.
    inline constexpr const char* loading   = "loading";
    inline constexpr const char* unloading = "unloading";
}

namespace color_type {
    inline constexpr const char* gradient = "gradient";
    inline constexpr const char* multi    = "multi";
    inline constexpr const char* single   = "single";
}

/// mirrors AmsControlWeb: classic-AMS level droplet or N3 percent.
namespace humidity_display_type {
    inline constexpr const char* none    = "none";
    inline constexpr const char* level   = "level";
    inline constexpr const char* percent = "percent";
}

/// i18n keys for tray_actions::read_label: unknown filament -> Read, known -> Reread.
namespace read_label {
    inline constexpr const char* read   = "Read";
    inline constexpr const char* reread = "Reread";
}

} // namespace values

namespace format {

struct FilamentLine
{
    std::string state          = values::line_state::no_filament;
    bool        has_filament   = false;
    std::string filament_color;
};

struct FilamentNozzle
{
    std::string state          = values::nozzle_state::none;
    bool        has_filament   = false;
    std::string filament_color;
    bool        on_extruder    = false;
};

struct SchematicItem
{
    int         ext_index  = 0;
    std::string ams_id;
    std::string label;
    std::string color;
    int         slot_count = 1;
    bool        empty      = true;
    bool        selected   = false;
};

struct Schematic
{
    std::string                kind; // values::source_kind
    bool                       selected       = false;
    int                        slot_count     = 0;  // ams: rack position count
    int                        selected_index = -1; // ams: 0-based current rack position
    std::vector<SchematicItem> items;               // ext: Ext 1..N
};

struct Column
{
    Schematic      schematic;
    nlohmann::json unit = nullptr; // ams_unit shape; null when the column is empty
    FilamentLine   line;
};

struct TopSlotsArea
{
    Column left_slot;
    Column right_slots;
};

struct BottomHotend
{
    FilamentLine   line;
    FilamentNozzle nozzle;
};

struct Slot
{
    int           addr_id       = 0;
    int           display_index = 0;
    TopSlotsArea  top_slots_area;
    BottomHotend  bottom_hotend;
};

struct State
{
    std::string       selected_dev_id;
    std::uint32_t     session_seq = 0;
    std::vector<Slot> slots;
};

inline void to_json(nlohmann::json& j, const FilamentLine& v)
{
    namespace SK = ConnectedSlotsSchema::keys;
    j = {
        {SK::filament_line::state,          v.state},
        {SK::filament_line::has_filament,   v.has_filament},
        {SK::filament_line::filament_color, v.filament_color}
    };
}

inline void to_json(nlohmann::json& j, const FilamentNozzle& v)
{
    namespace SK = ConnectedSlotsSchema::keys;
    j = {
        {SK::filament_nozzle::state,          v.state},
        {SK::filament_nozzle::has_filament,   v.has_filament},
        {SK::filament_nozzle::filament_color, v.filament_color},
        {SK::filament_nozzle::on_extruder,    v.on_extruder}
    };
}

inline void to_json(nlohmann::json& j, const SchematicItem& v)
{
    namespace SK = ConnectedSlotsSchema::keys;
    j = {
        {SK::schematic_item::ext_index,  v.ext_index},
        {SK::schematic_item::ams_id,     v.ams_id},
        {SK::schematic_item::label,      v.label},
        {SK::schematic_item::color,      v.color},
        {SK::schematic_item::slot_count, v.slot_count},
        {SK::schematic_item::empty,      v.empty},
        {SK::schematic_item::selected,   v.selected}
    };
}

inline void to_json(nlohmann::json& j, const Schematic& v)
{
    namespace SK = ConnectedSlotsSchema::keys;
    j = {
        {SK::schematic::kind,     v.kind},
        {SK::schematic::selected, v.selected}
    };
    if (v.kind == values::source_kind::ams) {
        j[SK::schematic::slot_count]     = v.slot_count;
        j[SK::schematic::selected_index] = v.selected_index;
    } else {
        j[SK::schematic::items] = v.items;
    }
}

inline void to_json(nlohmann::json& j, const Column& v)
{
    namespace SK = ConnectedSlotsSchema::keys;
    j = {
        {SK::column::schematic, v.schematic},
        {SK::column::unit,      v.unit},
        {SK::column::line,      v.line}
    };
}

inline void to_json(nlohmann::json& j, const TopSlotsArea& v)
{
    namespace SK = ConnectedSlotsSchema::keys;
    j = {
        {SK::top_slots_area::left_slot,   v.left_slot},
        {SK::top_slots_area::right_slots, v.right_slots}
    };
}

inline void to_json(nlohmann::json& j, const BottomHotend& v)
{
    namespace SK = ConnectedSlotsSchema::keys;
    j = {
        {SK::bottom_hotend::line,   v.line},
        {SK::bottom_hotend::nozzle, v.nozzle}
    };
}

inline void to_json(nlohmann::json& j, const Slot& v)
{
    namespace SK = ConnectedSlotsSchema::keys;
    j = {
        {SK::slot::addr_id,         v.addr_id},
        {SK::slot::display_index,   v.display_index},
        {SK::slot::top_slots_area,  v.top_slots_area},
        {SK::slot::bottom_hotend,   v.bottom_hotend}
    };
}

inline void to_json(nlohmann::json& j, const State& v)
{
    namespace SK = ConnectedSlotsSchema::keys;
    j = {
        {SK::root::session_seq,     v.session_seq},
        {SK::root::selected_dev_id, v.selected_dev_id},
        {SK::root::slots,           v.slots}
    };
}

} // namespace format

}}} // namespace Slic3r::GUI::ConnectedSlotsSchema

#endif // CONNECTEDSLOTSSCHEMA_HPP
