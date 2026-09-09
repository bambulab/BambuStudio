#ifndef AMSCONTROLWEBSCHEMA_DISPLAY_HPP
#define AMSCONTROLWEBSCHEMA_DISPLAY_HPP

// The `display` half of the payload: a ready-to-render view model derived from
// the `data` mirror, so every rendering decision is resolved in C++. The areas
// follow the classic AMSControl widget boundaries. Include Schema.hpp unless you
// only need this half.

#include "SchemaData.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace AmsControlWebSchema {

namespace display {
    // False hides the whole panel and leaves the areas below empty.
    inline constexpr const char* visible            = "visible";
    inline constexpr const char* ams_preview_area   = "ams_preview_area";
    inline constexpr const char* ams_ext_area       = "ams_ext_area";
    inline constexpr const char* filament_line_area = "filament_line_area";
    inline constexpr const char* switcher_area      = "switcher_area";
    inline constexpr const char* extruder_area      = "extruder_area";
} // namespace display

// One entry of the AMSPreview strip.
namespace preview_item {
    inline constexpr const char* ams_id        = "ams_id";
    inline constexpr const char* ams_type_name = "ams_type_name";
    inline constexpr const char* active        = "active";
    inline constexpr const char* slot_count    = "slot_count";
    inline constexpr const char* cubes         = "cubes";
} // namespace preview_item

namespace preview_cube {
    inline constexpr const char* color      = "color";
    inline constexpr const char* colors     = "colors";
    inline constexpr const char* color_type = "color_type";
    inline constexpr const char* is_exists  = "is_exists";
} // namespace preview_cube

// Ready-to-render slot card, the AMSLib / Caninfo equivalent.
namespace slot_view {
    inline constexpr const char* ams_id       = "ams_id";
    inline constexpr const char* slot_id      = "slot_id";
    inline constexpr const char* label        = "label";
    inline constexpr const char* slot_state   = "slot_state";
    inline constexpr const char* color        = "color";
    inline constexpr const char* colors       = "colors";
    inline constexpr const char* color_type   = "color_type";
    inline constexpr const char* remain       = "remain";
    inline constexpr const char* show_remain  = "show_remain";
    inline constexpr const char* show_remain_height = "show_remain_height";
    inline constexpr const char* fila_type    = "fila_type";
    inline constexpr const char* selected     = "selected";
    inline constexpr const char* loaded       = "loaded";
    inline constexpr const char* reading      = "reading";
    inline constexpr const char* show_rfid    = "show_rfid";
    inline constexpr const char* menu_actions = "menu_actions";
    // Spool is in but its info is not trustworthy yet: the card shows "?".
    inline constexpr const char* show_unknown    = "show_unknown";
    // PA Factor K on generic AMS / Ext cards. Empty when Lite or C++ would hide it.
    inline constexpr const char* k_text          = "k_text";
    inline constexpr const char* k_loading       = "k_loading";
    inline constexpr const char* k_loading_text  = "k_loading_text";
    // Capsule remain bar drawn above the card. Independent of height fill.
    inline constexpr const char* slot_remain_line = "slot_remain_line";
} // namespace slot_view

// AmsMappingPopup::_DrawRemainArea equivalent: a horizontal remain bar that
// reads the same percentage as `remain`. Height fill is a separate flag.
namespace slot_remain_line {
    inline constexpr const char* show_line      = "show_line";
    inline constexpr const char* remain_percent = "remain_percent";
} // namespace slot_remain_line

namespace menu_actions {
    // Pencil icon: an editable spool (third party / ext) outside view-only mode.
    inline constexpr const char* show_edit              = "show_edit";
    // Eye icon: a genuine spool, or any spool while the printer is in 2D mode.
    inline constexpr const char* show_read              = "show_read";
    // Badge for a newly inserted RFID spool awaiting user confirmation.
    inline constexpr const char* show_filament_mgr_hint = "show_filament_mgr_hint";
} // namespace menu_actions

// AMSHumidity block of one unit.
namespace humidity {
    inline constexpr const char* display_type  = "display_type";
    inline constexpr const char* level         = "level";
    inline constexpr const char* percent       = "percent";
    // 1..5 humidity icon index (5 = driest), -1 when unknown.
    inline constexpr const char* display_idx   = "display_idx";
    inline constexpr const char* drying        = "drying";
    inline constexpr const char* left_dry_time = "left_dry_time";
    inline constexpr const char* support_drying = "support_drying";
} // namespace humidity

namespace unit_view {
    inline constexpr const char* ams_id        = "ams_id";
    inline constexpr const char* ams_type_name = "ams_type_name";
    inline constexpr const char* active        = "active";
    inline constexpr const char* slot_count    = "slot_count";
    inline constexpr const char* humidity      = "humidity";
    inline constexpr const char* slots         = "slots";
} // namespace unit_view

// How the AMS units and ext slots are spread over columns, resolved the way
// AMSControl::CreateAmsSingleNozzle / CreateAmsDoubleNozzle do it. The preview
// strip and the slot cards share one arrangement, so the page never pairs them
// up itself.
namespace panel_layout {
    inline constexpr const char* layout_style = "layout_style";
    inline constexpr const char* panels       = "panels";
} // namespace panel_layout

// One column. `single` layouts have exactly one of these, `left_right` two.
namespace panel_view {
    inline constexpr const char* pos           = "pos";
    inline constexpr const char* visible       = "visible";
    // Unit this column has open, empty when it holds no AMS unit at all.
    inline constexpr const char* active_ams_id = "active_ams_id";
    inline constexpr const char* groups        = "groups";
} // namespace panel_view

// One cell of a column, the classic AmsItem. A four-slot unit fills a cell on
// its own, single-slot units share one two at a time.
namespace panel_group {
    // Resolves against ams_ext_area.units, or its ext_slots for an ext spool.
    inline constexpr const char* ams_ids = "ams_ids";
} // namespace panel_group

namespace preview_area {
    inline constexpr const char* visible = "visible";
    inline constexpr const char* layout  = "layout";
    inline constexpr const char* items   = "items";
} // namespace preview_area

namespace ams_ext_area {
    inline constexpr const char* visible    = "visible";
    // True when StatusPanel would set ext_type to LITE_EXT (AMS_LITE / f1).
    inline constexpr const char* lite_style = "lite_style";
    inline constexpr const char* layout     = "layout";
    inline constexpr const char* units      = "units";
    inline constexpr const char* ext_slots  = "ext_slots";
} // namespace ams_ext_area

namespace line_area {
    inline constexpr const char* visible = "visible";
    inline constexpr const char* links   = "links";
} // namespace line_area

// Where the filament of one slot goes, and whether that line carries filament
// right now. This is the wiring of the machine, not the tube artwork: the page
// decides how to draw a line once it knows the two ends it joins.
namespace slot_link {
    inline constexpr const char* ams_id        = "ams_id";
    inline constexpr const char* slot_id       = "slot_id";
    // Empty when the slot reaches its extruders without passing the switch.
    inline constexpr const char* switcher_port = "switcher_port";
    // Extruders reachable from the slot. Empty means no line to draw at all,
    // which is how an ext spool behaves once the switch owns its path.
    inline constexpr const char* extruder_ids  = "extruder_ids";
    inline constexpr const char* state         = "state";
    // Colour the line carries, empty while the line is idle.
    inline constexpr const char* color         = "color";
} // namespace slot_link

// Classic AMSControl::m_switcher (between DownRoad and the extruder row).
// The uninitialized banner is AMSControl::tipPanel, under the nozzles.
namespace switcher_area {
    inline constexpr const char* visible         = "visible";
    inline constexpr const char* installed       = "installed";
    inline constexpr const char* ready           = "ready";
    // Installed but not calibrated on the printer yet.
    inline constexpr const char* show_setup_hint = "show_setup_hint";
    inline constexpr const char* setup_hint      = "setup_hint";
} // namespace switcher_area

namespace extruder_area {
    inline constexpr const char* visible   = "visible";
    inline constexpr const char* extruders = "extruders";
} // namespace extruder_area

// One extruder of AMSextruder. The id doubles as the side it is drawn on, the
// way AMSPanelPos does: 0 is the main extruder on the right, 1 the deputy one
// on the left.
namespace extruder_view {
    inline constexpr const char* id             = "id";
    inline constexpr const char* state          = "state";
    inline constexpr const char* has_filament   = "has_filament";
    inline constexpr const char* filament_color = "filament_color";
    // Asset key from AMSextruder::updateNozzleNum: left/right or single_n / single_xp.
    inline constexpr const char* icon           = "icon";
} // namespace extruder_view

namespace values {
    // AMSCanType equivalent.
    namespace slot_state {
        inline constexpr const char* none        = "none";
        inline constexpr const char* brand       = "brand";
        inline constexpr const char* third_brand = "third_brand";
        inline constexpr const char* empty       = "empty";
    } // namespace slot_state

    // `unloading` waits for a retract progress source; nothing reports it yet.
    namespace link_state {
        inline constexpr const char* idle      = "idle";
        inline constexpr const char* loaded    = "loaded";
        inline constexpr const char* loading   = "loading";
        inline constexpr const char* unloading = "unloading";
    } // namespace link_state

    // One column on a single extruder machine, one per extruder otherwise.
    namespace layout_style {
        inline constexpr const char* single     = "single";
        inline constexpr const char* left_right = "left_right";
    } // namespace layout_style

    // AMSPanelPos equivalent, plus the lone column of a `single` layout.
    namespace panel_pos {
        inline constexpr const char* single = "single";
        inline constexpr const char* left   = "left";
        inline constexpr const char* right  = "right";
    } // namespace panel_pos

    // `active` is the extruder the machine prints with.
    namespace extruder_state {
        inline constexpr const char* idle    = "idle";
        inline constexpr const char* active  = "active";
        inline constexpr const char* loading = "loading";
    } // namespace extruder_state

    namespace extruder_icon {
        inline constexpr const char* left_nozzle      = "left_nozzle";
        inline constexpr const char* right_nozzle     = "right_nozzle";
        inline constexpr const char* single_nozzle_n  = "single_nozzle_n";
        inline constexpr const char* single_nozzle_xp = "single_nozzle_xp";
    } // namespace extruder_icon
} // namespace values

namespace format {

struct MenuActions
{
    bool show_edit              = false;
    bool show_read              = false;
    bool show_filament_mgr_hint = false;
};

struct SlotRemainLine
{
    bool show_line      = false;
    int  remain_percent = 100;
};

struct SlotView
{
    std::string              ams_id;
    std::string              slot_id;
    std::string              label;
    std::string              slot_state  = values::slot_state::none;
    std::string              color       = "#D9D9D9";
    std::vector<std::string> colors;
    int                      color_type  = 2;
    int                      remain      = 100;
    bool                     show_remain = false;
    bool                     show_remain_height = false;
    std::string              fila_type;
    bool                     selected    = false;
    bool                     loaded      = false;
    bool                     reading      = false;
    bool                     show_rfid    = false;
    bool                     show_unknown = false;
    MenuActions              menu_actions;
    std::string              k_text;
    bool                     k_loading      = false;
    std::string              k_loading_text;
    SlotRemainLine           slot_remain_line;
};

struct HumidityView
{
    std::string display_type   = values::humidity_display_type::none;
    int         level          = -1;
    int         percent        = -1;
    int         display_idx    = -1;
    bool        drying         = false;
    int         left_dry_time  = 0; // minutes
    bool        support_drying = false;
};

struct UnitView
{
    std::string           ams_id;
    std::string           ams_type_name = "AMS";
    bool                  active        = false;
    int                   slot_count    = 0;
    HumidityView          humidity;
    std::vector<SlotView> slots;
};

struct PreviewCube
{
    std::string              color      = "#D9D9D9";
    std::vector<std::string> colors;
    int                      color_type = 2;
    bool                     is_exists  = false;
};

struct PreviewItem
{
    std::string              ams_id;
    std::string              ams_type_name = "AMS";
    bool                     active        = false;
    int                      slot_count    = 0;
    std::vector<PreviewCube> cubes;
};

struct PanelGroup
{
    std::vector<std::string> ams_ids;
};

struct PanelView
{
    std::string             pos     = values::panel_pos::single;
    bool                    visible = false;
    std::string             active_ams_id;
    std::vector<PanelGroup> groups;
};

struct PanelLayout
{
    std::string            layout_style = values::layout_style::single;
    std::vector<PanelView> panels;
};

struct AmsPreviewArea
{
    bool                     visible = false;
    PanelLayout              layout;
    std::vector<PreviewItem> items;
};

struct AmsExtArea
{
    bool                  visible    = false;
    bool                  lite_style = false;
    PanelLayout           layout;
    std::vector<UnitView> units;
    std::vector<SlotView> ext_slots;
};

struct SlotLink
{
    std::string      ams_id;
    std::string      slot_id;
    std::string      switcher_port;
    std::vector<int> extruder_ids;
    std::string      state = values::link_state::idle;
    std::string      color;
};

// One entry per slot the ams_ext_area shows, in the same order.
struct FilamentLineArea
{
    bool                  visible = false;
    std::vector<SlotLink> links;
};

// Per-port routing is not carried here: SlotLink::switcher_port already tells the
// page which input every slot reaches the switch through.
struct SwitcherArea
{
    bool        visible         = false;
    bool        installed       = false;
    bool        ready           = false;
    bool        show_setup_hint = false;
    std::string setup_hint;
};

struct ExtruderView
{
    int         id           = 0;
    std::string state        = values::extruder_state::idle;
    bool        has_filament = false;
    std::string filament_color;
    std::string icon         = values::extruder_icon::single_nozzle_xp;
};

struct ExtruderArea
{
    bool                      visible = false;
    std::vector<ExtruderView> extruders;
};

struct Display
{
    bool             visible = false;
    AmsPreviewArea   ams_preview_area;
    AmsExtArea       ams_ext_area;
    FilamentLineArea filament_line_area;
    SwitcherArea     switcher_area;
    ExtruderArea     extruder_area;
};

inline void to_json(nlohmann::json& j, const MenuActions& v)
{
    j = {
        {menu_actions::show_edit,              v.show_edit},
        {menu_actions::show_read,              v.show_read},
        {menu_actions::show_filament_mgr_hint, v.show_filament_mgr_hint},
    };
}

inline void to_json(nlohmann::json& j, const SlotRemainLine& v)
{
    j = {
        {slot_remain_line::show_line,      v.show_line},
        {slot_remain_line::remain_percent, v.remain_percent},
    };
}

inline void to_json(nlohmann::json& j, const SlotView& v)
{
    j = {
        {slot_view::ams_id,       v.ams_id},
        {slot_view::slot_id,      v.slot_id},
        {slot_view::label,        v.label},
        {slot_view::slot_state,   v.slot_state},
        {slot_view::color,        v.color},
        {slot_view::colors,       v.colors},
        {slot_view::color_type,   v.color_type},
        {slot_view::remain,       v.remain},
        {slot_view::show_remain,  v.show_remain},
        {slot_view::show_remain_height, v.show_remain_height},
        {slot_view::fila_type,    v.fila_type},
        {slot_view::selected,     v.selected},
        {slot_view::loaded,       v.loaded},
        {slot_view::reading,      v.reading},
        {slot_view::show_rfid,    v.show_rfid},
        {slot_view::show_unknown,    v.show_unknown},
        {slot_view::menu_actions,    v.menu_actions},
        {slot_view::k_text,          v.k_text},
        {slot_view::k_loading,       v.k_loading},
        {slot_view::k_loading_text,  v.k_loading_text},
        {slot_view::slot_remain_line, v.slot_remain_line},
    };
}

inline void to_json(nlohmann::json& j, const HumidityView& v)
{
    j = {
        {humidity::display_type,   v.display_type},
        {humidity::level,          v.level},
        {humidity::percent,        v.percent},
        {humidity::display_idx,    v.display_idx},
        {humidity::drying,         v.drying},
        {humidity::left_dry_time,  v.left_dry_time},
        {humidity::support_drying, v.support_drying},
    };
}

inline void to_json(nlohmann::json& j, const UnitView& v)
{
    j = {
        {unit_view::ams_id,        v.ams_id},
        {unit_view::ams_type_name, v.ams_type_name},
        {unit_view::active,        v.active},
        {unit_view::slot_count,    v.slot_count},
        {unit_view::humidity,      v.humidity},
        {unit_view::slots,         v.slots},
    };
}

inline void to_json(nlohmann::json& j, const PreviewCube& v)
{
    j = {
        {preview_cube::color,      v.color},
        {preview_cube::colors,     v.colors},
        {preview_cube::color_type, v.color_type},
        {preview_cube::is_exists,  v.is_exists},
    };
}

inline void to_json(nlohmann::json& j, const PreviewItem& v)
{
    j = {
        {preview_item::ams_id,        v.ams_id},
        {preview_item::ams_type_name, v.ams_type_name},
        {preview_item::active,        v.active},
        {preview_item::slot_count,    v.slot_count},
        {preview_item::cubes,         v.cubes},
    };
}

inline void to_json(nlohmann::json& j, const PanelGroup& v)
{
    j = {
        {panel_group::ams_ids, v.ams_ids},
    };
}

inline void to_json(nlohmann::json& j, const PanelView& v)
{
    j = {
        {panel_view::pos,           v.pos},
        {panel_view::visible,       v.visible},
        {panel_view::active_ams_id, v.active_ams_id},
        {panel_view::groups,        v.groups},
    };
}

inline void to_json(nlohmann::json& j, const PanelLayout& v)
{
    j = {
        {panel_layout::layout_style, v.layout_style},
        {panel_layout::panels,       v.panels},
    };
}

inline void to_json(nlohmann::json& j, const AmsPreviewArea& v)
{
    j = {
        {preview_area::visible, v.visible},
        {preview_area::layout,  v.layout},
        {preview_area::items,   v.items},
    };
}

inline void to_json(nlohmann::json& j, const AmsExtArea& v)
{
    j = {
        {ams_ext_area::visible,    v.visible},
        {ams_ext_area::lite_style, v.lite_style},
        {ams_ext_area::layout,     v.layout},
        {ams_ext_area::units,      v.units},
        {ams_ext_area::ext_slots,  v.ext_slots},
    };
}

inline void to_json(nlohmann::json& j, const SlotLink& v)
{
    j = {
        {slot_link::ams_id,        v.ams_id},
        {slot_link::slot_id,       v.slot_id},
        {slot_link::switcher_port, v.switcher_port},
        {slot_link::extruder_ids,  v.extruder_ids},
        {slot_link::state,         v.state},
        {slot_link::color,         v.color},
    };
}

inline void to_json(nlohmann::json& j, const FilamentLineArea& v)
{
    j = {
        {line_area::visible, v.visible},
        {line_area::links,   v.links},
    };
}

inline void to_json(nlohmann::json& j, const SwitcherArea& v)
{
    j = {
        {switcher_area::visible,         v.visible},
        {switcher_area::installed,       v.installed},
        {switcher_area::ready,           v.ready},
        {switcher_area::show_setup_hint, v.show_setup_hint},
        {switcher_area::setup_hint,      v.setup_hint},
    };
}

inline void to_json(nlohmann::json& j, const ExtruderView& v)
{
    j = {
        {extruder_view::id,             v.id},
        {extruder_view::state,          v.state},
        {extruder_view::has_filament,   v.has_filament},
        {extruder_view::filament_color, v.filament_color},
        {extruder_view::icon,           v.icon},
    };
}

inline void to_json(nlohmann::json& j, const ExtruderArea& v)
{
    j = {
        {extruder_area::visible,   v.visible},
        {extruder_area::extruders, v.extruders},
    };
}

inline void to_json(nlohmann::json& j, const Display& v)
{
    j = {
        {display::visible,            v.visible},
        {display::ams_preview_area,   v.ams_preview_area},
        {display::ams_ext_area,       v.ams_ext_area},
        {display::filament_line_area, v.filament_line_area},
        {display::switcher_area,      v.switcher_area},
        {display::extruder_area,      v.extruder_area},
    };
}

} // namespace format

}}} // namespace Slic3r::GUI::AmsControlWebSchema

#endif // AMSCONTROLWEBSCHEMA_DISPLAY_HPP
