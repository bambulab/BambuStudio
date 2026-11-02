#include "BatchUnloadModeBuilder.hpp"
#include "BatchModeCommon.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Schema.hpp"
#include "slic3r/GUI/I18N.hpp"

namespace Slic3r { namespace GUI {

namespace SK = AmsHotendSchema::keys;
namespace SV = AmsHotendSchema::values;

namespace {

nlohmann::json make_select_action(const nlohmann::json& slot)
{
    const auto& nozzle = slot.value(SK::filament_slot::filament_nozzle, nlohmann::json::object());
    if (auto nozzle_status = BatchModeCommon::classify_nozzle(nozzle); nozzle_status.is_blocked()) {
        return BatchModeCommon::make_select_action(false, *nozzle_status.block_reason);
    }

    const auto& line = slot.value(SK::filament_slot::filament_line, nlohmann::json::object());
    const std::string line_state = line.value(SK::filament_line::state, std::string());
    const bool can_select = (line_state == SV::line_state::loaded);
    std::string reason;
    if (!can_select) {
        reason = _u8L("Slot not loaded");
    }
    // Unload mode doesn't use the EXT badge.
    return BatchModeCommon::make_select_action(can_select, reason);
}

} // namespace

nlohmann::json BatchUnloadModeBuilder::BuildFilamentPage(
    const DevicePageAmsFilamentHotendVM& /*vm*/,
    MachineObject*                       /*machine_obj*/,
    const nlohmann::json&                prebuilt_normal_page) const
{
    nlohmann::json page;
    page[SK::filament_page::mode] = SV::mode::batch_unload;
    page[SK::filament_page::show_ams_env_units] = false;

    // `can_dry` is mirrored from the normal-mode actions so the side-channel
    // dry dialog remains reachable in batch mode (mirrors BatchAmsLoadModeBuilder).
    const std::string other_active = _u8L("Other batch mode active");
    const auto& src_actions = prebuilt_normal_page.value(SK::filament_page::actions, nlohmann::json::object());
    const bool src_can_dry = src_actions.value(SK::filament_actions::can_dry, false);
    page[SK::filament_page::actions] = {
        {SK::filament_actions::can_dry,           BatchModeCommon::make_action_state(src_can_dry)},
        {SK::filament_actions::can_show_load_auto, BatchModeCommon::make_action_state(false, other_active)},
        {SK::filament_actions::can_load_external, BatchModeCommon::make_action_state(false, other_active)},
        {SK::filament_actions::can_unload,        BatchModeCommon::make_action_state(true)},
    };

    nlohmann::json slots = nlohmann::json::array();
    const auto& src_slots = prebuilt_normal_page.value(SK::filament_page::hotend_slots, nlohmann::json::array());
    // See BatchAmsLoadModeBuilder for the rationale: keep the three render
    // groups so cards still draw normally, drop only the menu / dialog hooks.
    for (const auto& slot : src_slots) {
        slots.push_back({
            {SK::filament_slot::addr_id,           slot.value(SK::filament_slot::addr_id,           0)},
            {SK::filament_slot::display_index,     slot.value(SK::filament_slot::display_index,     0)},
            {SK::filament_slot::ams_id,            slot.value(SK::filament_slot::ams_id,            std::string())},
            {SK::filament_slot::slot_id,           slot.value(SK::filament_slot::slot_id,           std::string())},
            {SK::filament_slot::is_top_tier,       slot.value(SK::filament_slot::is_top_tier,       false)},
            {SK::filament_slot::is_external_spool, slot.value(SK::filament_slot::is_external_spool, false)},
            {SK::filament_slot::enabled,           slot.value(SK::filament_slot::enabled,           false)},
            {SK::filament_slot::filament_card,     slot.value(SK::filament_slot::filament_card,     nlohmann::json::object())},
            {SK::filament_slot::filament_line,     slot.value(SK::filament_slot::filament_line,     nlohmann::json::object())},
            {SK::filament_slot::filament_nozzle,   slot.value(SK::filament_slot::filament_nozzle,   nlohmann::json::object())},
            {SK::filament_slot::select_action,     make_select_action(slot)},
        });
    }
    page[SK::filament_page::hotend_slots] = std::move(slots);

    return page;
}

const char* BatchUnloadModeBuilder::ModeName() const
{
    return SV::mode::batch_unload;
}

}} // namespace Slic3r::GUI
