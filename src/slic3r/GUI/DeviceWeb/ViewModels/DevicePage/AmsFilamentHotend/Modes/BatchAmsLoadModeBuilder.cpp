#include "BatchAmsLoadModeBuilder.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/DevicePageSchema_AmsFilamentHotend.hpp"
#include "slic3r/GUI/I18N.hpp"

namespace Slic3r { namespace GUI {

namespace SK = AmsHotendSchema::keys;
namespace SV = AmsHotendSchema::values;

namespace {

// Build a per-slot select_action block for the batch_ams_load mode.
// `slot` is one entry of prebuilt_normal_page.hotend_slots[]; we read
// card state + identity to decide whether the user can tick this card.
//
// Eligibility (per product spec for "Auto Load → push filament to hotend"):
//  - slot must be bound to an AMS (any real ams_id, including the Ext slot)
//  - slot must hold filament that is NOT already loaded into the hotend
//    (`ReadyToLoad` / `Unknown` qualify; `ReadyToUse` does NOT — that
//     filament is currently fed to the extruder)
//  - transient states (`Reading` / `Loading` / `Unloading`) and `Error`
//    are rejected with a specific reason for the tooltip
nlohmann::json make_select_action(const nlohmann::json& slot)
{
    const auto& card = slot.value(SK::filament_slot::filament_card, nlohmann::json::object());
    const std::string card_state = card.value(SK::filament_card::state, std::string());
    const std::string ams_id     = slot.value(SK::filament_slot::ams_id, std::string());

    const bool slot_has_ams    = !ams_id.empty();
    const bool is_empty        = (card_state == SV::card_state::empty);
    const bool is_ready_to_use = (card_state == SV::card_state::ready_to_use);
    const bool is_ready_to_load= (card_state == SV::card_state::ready_to_load);
    const bool is_unknown_type = (card_state == SV::card_state::unknown);
    const bool is_reading      = (card_state == SV::card_state::reading);
    const bool is_loading      = (card_state == SV::card_state::loading);
    const bool is_unloading    = (card_state == SV::card_state::unloading);
    const bool is_error        = (card_state == SV::card_state::error);

    bool can_select = false;
    std::string reason;

    if (!slot_has_ams) {
        reason = _u8L("This rack position is not bound to any AMS");
    } else if (is_empty) {
        reason = _u8L("Empty slot");
    } else if (is_ready_to_use) {
        reason = _u8L("Filament already loaded into hotend");
    } else if (is_reading) {
        reason = _u8L("RFID is being read");
    } else if (is_loading || is_unloading) {
        reason = _u8L("Filament change in progress");
    } else if (is_error) {
        reason = _u8L("Slot is in error state");
    } else if (is_ready_to_load || is_unknown_type) {
        can_select = true;
    }

    return nlohmann::json{
        {SK::select_action::can_select, can_select},
        {SK::select_action::reason,     reason},
        // Auto-load mode does not use the EXT badge (that's only batch_ext_load).
        {SK::select_action::ext_label,  false},
    };
}

// Wrap a regular `actions.can_<name>` boolean into the batch-shape
// `{ enabled, reason }` object expected by the Web in batch modes.
nlohmann::json make_action_state(bool enabled, std::string reason = "")
{
    return nlohmann::json{
        {SK::action_state::enabled, enabled},
        {SK::action_state::reason,  std::move(reason)},
    };
}

} // namespace

nlohmann::json BatchAmsLoadModeBuilder::BuildFilamentPage(
    const DevicePageAmsFilamentHotendVM& /*vm*/,
    MachineObject*                       /*machine_obj*/,
    const nlohmann::json&                prebuilt_normal_page) const
{
    // Batch payload preserves the three rendering groups (filament_card,
    // filament_line, filament_nozzle) so each card can keep drawing its
    // spool disc, colour, status text, feed line and nozzle while the page
    // is in batch mode. menu_actions / ams_operation are still dropped: the
    // 3-dot menu and AMS Operation dialog are not reachable in batch mode,
    // and the per-slot select_action below replaces them as the only
    // interactive surface.
    nlohmann::json page;
    page[SK::filament_page::mode] = SV::mode::batch_ams_load;

    // In batch mode every toolbar action carries `{ enabled, reason }`. The
    // currently-active mode's own button stays enabled; the other two are
    // disabled with a generic "other batch mode active" reason. The Web's
    // PageToolbar interprets these objects when the page mode != "normal".
    //
    // `can_dry` is a side-channel action (opens AMSDryCtrWin) that should
    // remain reachable from batch mode too, so we mirror its normal-mode
    // boolean as an enabled `{ enabled, reason }` object when truthy.
    const std::string other_active = _u8L("Other batch mode active");
    const auto& src_actions = prebuilt_normal_page.value(SK::filament_page::actions, nlohmann::json::object());
    const bool src_can_dry = src_actions.value(SK::filament_actions::can_dry, false);
    page[SK::filament_page::actions] = {
        {SK::filament_actions::can_dry,           make_action_state(src_can_dry)},
        {SK::filament_actions::can_load_auto,     make_action_state(true)},
        {SK::filament_actions::can_load_external, make_action_state(false, other_active)},
        {SK::filament_actions::can_unload,        make_action_state(false, other_active)},
    };

    // Project each prebuilt slot to its batch shape.
    nlohmann::json slots = nlohmann::json::array();
    const auto& src_slots = prebuilt_normal_page.value(SK::filament_page::hotend_slots, nlohmann::json::array());
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

const char* BatchAmsLoadModeBuilder::ModeName() const
{
    return SV::mode::batch_ams_load;
}

}} // namespace Slic3r::GUI
