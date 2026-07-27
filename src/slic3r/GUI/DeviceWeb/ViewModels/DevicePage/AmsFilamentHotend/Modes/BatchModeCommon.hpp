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

// Localized reason why the nozzle cannot take part in a batch operation, or
// `nullopt` when the nozzle is usable. Takes a slot's `filament_nozzle` section
// so both the nozzle state and the tool kind are covered.
std::optional<std::string> nozzle_unavailable_reason(const nlohmann::json& nozzle_section);

// Colors
std::string normalize_hex_for_web(const std::string& raw);
std::string color_type_for_web(DevFilaColorType color_type);

}}} // namespace Slic3r::GUI::BatchModeCommon

#endif // BATCHMODECOMMON_HPP
