#ifndef DEVICEPAGESCHEMA_FILAMENTMANUALPULL_HPP
#define DEVICEPAGESCHEMA_FILAMENTMANUALPULL_HPP

namespace Slic3r { namespace GUI { namespace AmsHotendSchema {

namespace keys {

// state.filament_change_status.manual_pull
namespace filament_manual_pull {
    inline constexpr const char* title     = "title";
    inline constexpr const char* items     = "items";
    inline constexpr const char* can_exit  = "can_exit";
    inline constexpr const char* can_close = "can_close";
}

// state.filament_change_status.manual_pull.items[]
namespace filament_manual_pull_item {
    inline constexpr const char* ams_id        = "ams_id";
    inline constexpr const char* slot_id       = "slot_id";
    inline constexpr const char* display_index = "display_index";
    inline constexpr const char* filament_name = "filament_name";
    inline constexpr const char* filament_type = "filament_type";
    inline constexpr const char* color         = "color";
    inline constexpr const char* color_type    = "color_type";
    inline constexpr const char* state         = "state";
    inline constexpr const char* reason        = "reason";
}

} // namespace keys

namespace values {

namespace filament_manual_pull_item_state {
    inline constexpr const char* pending = "pending";
    inline constexpr const char* success = "success";
    inline constexpr const char* failed  = "failed";
}

} // namespace values

}}} // namespace Slic3r::GUI::AmsHotendSchema

#endif // DEVICEPAGESCHEMA_FILAMENTMANUALPULL_HPP
