#ifndef DEVICEPAGESCHEMA_HOTENDSLOT_HPP
#define DEVICEPAGESCHEMA_HOTENDSLOT_HPP

namespace Slic3r { namespace GUI { namespace AmsHotendSchema { namespace keys {

// state.hotend_page.hotend_slots[] - Hotend tab cards only.
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
    // Same render group as the Filament tab; the Hotend card only reads
    // has_filament / filament_color to tint the filament inside the nozzle.
    inline constexpr const char* filament_nozzle = "filament_nozzle";
    inline constexpr const char* hotend        = "hotend";
    inline constexpr const char* hotend_detail_dialog = "hotend_detail_dialog";
}

// slot.menu_actions for the Hotend tab.
namespace hotend_menu_actions {
    inline constexpr const char* can_read            = "can_read";
    inline constexpr const char* can_detail          = "can_detail";
    inline constexpr const char* can_set_temperature = "can_set_temperature";
    inline constexpr const char* can_take_place      = "can_take_place";
    inline constexpr const char* disabled_reason     = "disabled_reason";
}

// slot.hotend (the tool side; filament has_filament_in_nozzle lives under
// slot.filament_nozzle.has_filament).
namespace hotend {
    inline constexpr const char* current_temp   = "current_temp";
    inline constexpr const char* target_temp    = "target_temp";
    inline constexpr const char* max_temp       = "max_temp";   // upper bound for target_temp; larger input is clamped
    inline constexpr const char* op_state       = "op_state";   // values::hotend_op_state
    inline constexpr const char* temp_trend     = "temp_trend"; // values::hotend_temp_trend
    inline constexpr const char* tool_kind      = "tool_kind";  // values::tool_kind
    inline constexpr const char* nozzle_label   = "nozzle_label";
    inline constexpr const char* nozzle_subtext = "nozzle_subtext";
    inline constexpr const char* selected       = "selected";
    inline constexpr const char* tooltip        = "tooltip";    // card hover text; empty when none
}

// slot.hotend_detail_dialog - data used only by the hotend detail modal.
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
    inline constexpr const char* switch_count   = "switch_count";
}

}}}} // namespace Slic3r::GUI::AmsHotendSchema::keys

#endif // DEVICEPAGESCHEMA_HOTENDSLOT_HPP
