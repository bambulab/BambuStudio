#ifndef DEVICEPAGESCHEMA_FILAMENTCHANGESTATUS_HPP
#define DEVICEPAGESCHEMA_FILAMENTCHANGESTATUS_HPP

#include <nlohmann/json.hpp>

#include "SchemaFilamentManualPull.hpp"

#include <string>

namespace Slic3r { namespace GUI { namespace AmsHotendSchema {

namespace keys {

// state.filament_change_status
namespace filament_change {
    inline constexpr const char* active             = "active";
    inline constexpr const char* mode               = "mode";       // values::filament_change_mode::*
    inline constexpr const char* operation          = "operation";  // values::filament_change_operation::*
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
    // Whether the Retry button should be shown. True only while the filament
    inline constexpr const char* can_retry          = "can_retry";
    // Per-slot batch progress info (mode == "batch_running"). Same shape as
    // the pre-run `batch_reserved` payload; Web reuses the same item list type.
    inline constexpr const char* batch_running      = "batch_running";
    inline constexpr const char* manual_pull        = "manual_pull";
}

// state.filament_change_status.steps[] entry.
namespace filament_change_step {
    inline constexpr const char* id         = "id";
    inline constexpr const char* label      = "label";
    inline constexpr const char* step_style = "step_style";
    inline constexpr const char* step_style_text = "step_style_text";
    inline constexpr const char* step_text  = "step_text";
}

// state.filament_change_status.batch_running (and batch_reserved) shape.
namespace batch_progress {
    inline constexpr const char* operation = "operation";
    inline constexpr const char* current   = "current";
    inline constexpr const char* total     = "total";
    inline constexpr const char* items     = "items";
}

// state.filament_change_status.batch_running.items[] entry.
namespace batch_progress_item {
    inline constexpr const char* display_index = "display_index";
    inline constexpr const char* state         = "state";
    inline constexpr const char* reason        = "reason";
}

// state.filament_change_status.running_slot
namespace running_slot {
    inline constexpr const char* ams_id        = "ams_id";
    inline constexpr const char* slot_id       = "slot_id";
    inline constexpr const char* display_index = "display_index";
}

} // namespace keys

namespace values {

namespace filament_change_mode {
    inline constexpr const char* idle           = "idle";
    inline constexpr const char* single         = "single";
    inline constexpr const char* batch_reserved = "batch_reserved";
    inline constexpr const char* batch_running  = "batch_running";
    inline constexpr const char* batch_manual_pull = "batch_manual_pull";
}

namespace filament_change_operation {
    inline constexpr const char* change  = "change";
    // Direction of a single-slot change, reported so the Web layer can name it
    // instead of falling back to a merged "loading/unloading" label.
    inline constexpr const char* load    = "load";
    inline constexpr const char* unload  = "unload";
    inline constexpr const char* unknown = "unknown";
}

namespace filament_change_step_style {
    inline constexpr const char* manual_load   = "manual_load";
    inline constexpr const char* maunal_unload = "maunal_unload";
}

// values for batch_progress_item::state
namespace batch_item_state {
    inline constexpr const char* pending = "pending";
    inline constexpr const char* running = "running";
    inline constexpr const char* success = "success";
    inline constexpr const char* failed  = "failed";
}

} // namespace values

namespace format {

struct FilamentChangeStatus
{
    bool        active = false;
    std::string mode = values::filament_change_mode::idle;
    std::string message;
    std::string tips;
};

inline void to_json(nlohmann::json& j, const FilamentChangeStatus& v)
{
    namespace SK = AmsHotendSchema::keys;
    j = {
        {SK::filament_change::active,  v.active},
        {SK::filament_change::mode,    v.mode},
        {SK::filament_change::message, v.message},
        {SK::filament_change::tips,    v.tips}
    };
}

} // namespace format

}}} // namespace Slic3r::GUI::AmsHotendSchema

#endif // DEVICEPAGESCHEMA_FILAMENTCHANGESTATUS_HPP
