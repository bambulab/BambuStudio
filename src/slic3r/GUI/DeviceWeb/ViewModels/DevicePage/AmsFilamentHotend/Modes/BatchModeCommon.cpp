#include "BatchModeCommon.hpp"

#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Schema.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <utility>

namespace Slic3r { namespace GUI { namespace BatchModeCommon {

namespace SK = AmsHotendSchema::keys;
namespace SV = AmsHotendSchema::values;

nlohmann::json make_action_state(bool enabled, std::string reason)
{
    return nlohmann::json{
        {SK::action_state::enabled, enabled},
        {SK::action_state::reason,  std::move(reason)},
    };
}

nlohmann::json make_select_action(bool can_select, std::string reason, bool ext_label)
{
    return nlohmann::json{
        {SK::select_action::can_select, can_select},
        {SK::select_action::reason,     std::move(reason)},
        {SK::select_action::ext_label,  ext_label},
    };
}

namespace {

bool is_nozzle_none(const std::string& nozzle_state)
{
    return nozzle_state == SV::nozzle_state::none;
}

bool is_nozzle_error(const std::string& nozzle_state)
{
    return nozzle_state == SV::nozzle_state::error;
}

bool is_nozzle_lost(const std::string& nozzle_state)
{
    return nozzle_state == SV::nozzle_state::lost;
}

// Only tools that can feed filament may join a load / unload. An empty or
// unknown kind is treated as a nozzle so slots whose tool type has not been
// reported yet stay operable.
bool is_filament_capable_tool(const std::string& tool_kind)
{
    return !(tool_kind == SV::tool_kind::cutter
          || tool_kind == SV::tool_kind::pen
          || tool_kind == SV::tool_kind::laser
          || tool_kind == SV::tool_kind::fan);
}

} // namespace

BatchNozzleStatus classify_nozzle(const nlohmann::json& nozzle_section)
{
    const std::string nozzle_state = nozzle_section.value(SK::filament_nozzle::state, std::string());
    const std::string tool_kind    = nozzle_section.value(SK::filament_nozzle::tool_kind, std::string());

    BatchNozzleStatus status;
    if (!is_filament_capable_tool(tool_kind)) {
        status.block_reason = _u8L("The current tool is not a nozzle");
    } else if (is_nozzle_none(nozzle_state)) {
        status.block_reason = _u8L("There are no nozzle installed");
    } else if (is_nozzle_error(nozzle_state)) {
        status.block_reason = _u8L("Nozzle is in error state");
    } else if (is_nozzle_lost(nozzle_state)) {
        status.block_reason = _u8L("Nozzle has been detached");
    } else if (nozzle_state == SV::nozzle_state::unknown) {
        status.block_reason = _u8L("Nozzle type is unknown");
    }

    status.has_visible_alert = status.is_blocked();
    return status;
}

std::string normalize_hex_for_web(const std::string& raw)
{
    if (raw.empty()) return "#D9D9D9";
    std::string hex = raw;
    if (hex[0] != '#') hex = "#" + hex;
    if (hex.size() > 9) hex = hex.substr(0, 9);
    return hex;
}

std::string color_type_for_web(DevFilaColorType color_type)
{
    switch (color_type) {
    case DevFilaColorType::CTYPE_MULTI:    return "multi";
    case DevFilaColorType::CTYPE_GRADIANT: return "gradient";
    case DevFilaColorType::CTYPE_SINGLE:   return "single";
    }
    return "single";
}

}}} // namespace Slic3r::GUI::BatchModeCommon
