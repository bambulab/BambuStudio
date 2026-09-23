#ifndef BATCHMODECOMMON_HPP
#define BATCHMODECOMMON_HPP

#include "slic3r/GUI/DeviceCore/DevDefs.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace Slic3r { namespace GUI { namespace BatchModeCommon {

// Wrap a regular `actions.can_<name>` boolean into the batch-shape
// `{ enabled, reason }` object expected by the Web in batch modes.
nlohmann::json make_action_state(bool enabled, std::string reason = "");
nlohmann::json make_select_action(bool can_select, std::string reason, bool ext_label = false);

// Verdict on a slot's `filament_nozzle` for batch-mode purposes.
// - `block_reason`      : non-empty when the user cannot check this slot.
// - `has_visible_alert` : true when the slot carries an alert the user must
//                         see. Batch builders that repaint the nozzle card
//                         use this to keep the source badge and tooltip.
struct BatchNozzleStatus
{
    std::optional<std::string> block_reason;
    bool                       has_visible_alert = false;

    bool is_blocked() const { return block_reason.has_value(); }
};

BatchNozzleStatus classify_nozzle(const nlohmann::json& nozzle_section);

// Colors
std::string normalize_hex_for_web(const std::string& raw);
std::string color_type_for_web(DevFilaColorType color_type);

}}} // namespace Slic3r::GUI::BatchModeCommon

#endif // BATCHMODECOMMON_HPP
