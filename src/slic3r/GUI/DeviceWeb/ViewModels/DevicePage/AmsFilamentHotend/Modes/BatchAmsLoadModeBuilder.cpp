#include "BatchAmsLoadModeBuilder.hpp"
#include "BatchModeCommon.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ViewModel.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Schema.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ResolverSlotState.hpp"

#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <utility>
#include <vector>

namespace Slic3r { namespace GUI {

namespace SK = AmsHotendSchema::keys;
namespace SV = AmsHotendSchema::values;

namespace {

struct PrebuiltNozzleInfo {
    int            addr_id = 0;
    nlohmann::json nozzle  = {
        {SK::filament_nozzle::state,          SV::nozzle_state::normal},
        {SK::filament_nozzle::has_filament,   false},
        {SK::filament_nozzle::filament_color, std::string()},
        {SK::filament_nozzle::on_extruder,    false},
    };
};

PrebuiltNozzleInfo find_prebuilt_nozzle(const nlohmann::json& prebuilt_normal_page, int display_index)
{
    PrebuiltNozzleInfo info;
    const auto slots_it = prebuilt_normal_page.find(SK::filament_page::hotend_slots);
    if (slots_it == prebuilt_normal_page.end() || !slots_it->is_array()) {
        return info;
    }
    for (const auto& slot : *slots_it) {
        if (slot.value(SK::filament_slot::display_index, 0) != display_index) {
            continue;
        }
        info.addr_id = slot.value(SK::filament_slot::addr_id, 0);
        info.nozzle  = slot.value(SK::filament_slot::filament_nozzle, info.nozzle);
        return info;
    }
    return info;
}

nlohmann::json make_select_action(const std::string& card_state, const nlohmann::json& nozzle_section)
{
    const bool is_empty        = (card_state == SV::card_state::empty || card_state == SV::card_state::awaiting_tpu_read);
    const bool is_ready_to_load= (card_state == SV::card_state::ready_to_load);
    const bool is_ready_to_use = (card_state == SV::card_state::ready_to_use);
    const bool is_unknown_type = (card_state == SV::card_state::unknown);
    const bool is_reading      = (card_state == SV::card_state::reading);
    const bool is_loading      = (card_state == SV::card_state::loading);
    const bool is_unloading    = (card_state == SV::card_state::unloading);
    const bool is_error        = (card_state == SV::card_state::error);

    bool can_select = false;
    std::string reason;

    if (auto nozzle_status = BatchModeCommon::classify_nozzle(nozzle_section); nozzle_status.is_blocked()) {
        reason = *nozzle_status.block_reason;
    } else if (is_empty) {
        reason = _u8L("Empty slot");
    } else if (is_reading) {
        reason = _u8L("RFID is being read");
    } else if (is_loading || is_unloading) {
        reason = _u8L("Filament change in progress");
    } else if (is_error) {
        reason = _u8L("Slot is in error state");
    } else if (is_ready_to_use) {
        reason = _u8L("Filament already loaded");
    } else if (is_ready_to_load || is_unknown_type) {
        can_select = true;
    }

    // Auto-load mode does not use the EXT badge (that's only batch_ext_load).
    return BatchModeCommon::make_select_action(can_select, reason);
}

std::string resolve_ams_slot_card_state(MachineObject* machine_obj, const DevAmsTray* tray)
{
    if (!tray || !tray->is_exists) {
        return SV::card_state::empty;
    }

    if (tray->is_reading(machine_obj)) return SV::card_state::reading;
    if (tray->setting_id.empty() || tray->m_fila_type.empty()) return SV::card_state::unknown;
    if (DevicePageFilamentSlotStateResolver::IsTraySlotLoadedInNozzle(machine_obj, tray)) return SV::card_state::ready_to_use;
    return SV::card_state::ready_to_load;
}

std::string resolve_status_text(const std::string& card_state)
{
    if (card_state == SV::card_state::reading) return _u8L("Reading");
    if (card_state == SV::card_state::ready_to_load) return _u8L("Ready to load");
    if (card_state == SV::card_state::ready_to_use) return _u8L("Loaded");
    if (card_state == SV::card_state::empty) return "--";
    if (card_state == SV::card_state::awaiting_tpu_read) return _u8L("TPU pending read");
    if (card_state == SV::card_state::unknown) return _u8L("Unknown filament");
    if (card_state == SV::card_state::error) return _u8L("Error");
    return "";
}

} // namespace

nlohmann::json BatchAmsLoadModeBuilder::BuildFilamentPage(
    const DevicePageAmsFilamentHotendVM& vm,
    MachineObject*                       machine_obj,
    const nlohmann::json&                prebuilt_normal_page) const
{
    nlohmann::json page;
    page[SK::filament_page::mode] = SV::mode::batch_ams_load;
    page[SK::filament_page::show_ams_env_units] = false;

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
        {SK::filament_actions::can_dry,           BatchModeCommon::make_action_state(src_can_dry)},
        {SK::filament_actions::can_show_load_auto, BatchModeCommon::make_action_state(true)},
        {SK::filament_actions::can_load_external, BatchModeCommon::make_action_state(false, other_active)},
        {SK::filament_actions::can_unload,        BatchModeCommon::make_action_state(false, other_active)},
    };

    nlohmann::json slots = nlohmann::json::array();
    auto fila_system = machine_obj ? machine_obj->GetFilaSystem() : nullptr;
    if (!fila_system) {
        page[SK::filament_page::hotend_slots] = std::move(slots);
        return page;
    }

    int fallback_display_index = 1;
    int rack_nozzle_count = machine_obj ? machine_obj->GetNozzleRack()->GetTotalRackNozzleCount() : 0;
    for (const auto& [ams_id, ams_unit] : fila_system->GetAmsList()) {
        if (!ams_unit) continue;
        if (rack_nozzle_count != ams_unit->GetSlotCount()) continue;
        for (const auto& [slot_id, tray] : ams_unit->GetTrays()) {
            if (!tray) continue;

            std::vector<std::string> colors = tray->cols;
            if (colors.empty() && !tray->color.empty()) colors.push_back(tray->color);
            for (auto& color : colors) color = BatchModeCommon::normalize_hex_for_web(color);

            const std::string card_state = resolve_ams_slot_card_state(machine_obj, tray);
            const std::string filament_type = tray->m_fila_type;
            const std::string filament_name = vm.ResolveFilamentCardText(tray, filament_type);
            const std::string color = BatchModeCommon::normalize_hex_for_web(tray->color);
            const int display_index = fallback_display_index++;
            const bool is_top_tier = (display_index % 2) == 0;

            const int display_remain = DevicePageFilamentSlotStateResolver::ResolveDisplayRemain(tray);

            const nlohmann::json card_section = {
                {SK::filament_card::state,       card_state},
                {SK::filament_card::color,       color},
                {SK::filament_card::color_type,  BatchModeCommon::color_type_for_web(tray->ctype)},
                {SK::filament_card::colors,      colors},
                {SK::filament_card::text,        filament_name},
                {SK::filament_card::name,        filament_name},
                {SK::filament_card::type,        filament_type},
                {SK::filament_card::remain,      display_remain},
                {SK::filament_card::status_text, resolve_status_text(card_state)}
            };

            const PrebuiltNozzleInfo prebuilt = find_prebuilt_nozzle(prebuilt_normal_page, display_index);

            slots.push_back({
                {SK::filament_slot::addr_id,           prebuilt.addr_id},
                {SK::filament_slot::display_index,     display_index},
                {SK::filament_slot::ams_id,            ams_id},
                {SK::filament_slot::slot_id,           slot_id},
                {SK::filament_slot::is_top_tier,       is_top_tier},
                {SK::filament_slot::is_external_spool, false},
                {SK::filament_slot::enabled,           tray->is_exists},
                {SK::filament_slot::filament_card,     card_section},
                {SK::filament_slot::filament_line, {
                    {SK::filament_line::state,          SV::line_state::no_filament},
                    {SK::filament_line::has_filament,   false},
                    {SK::filament_line::filament_color, std::string()}
                }},
                {SK::filament_slot::filament_nozzle,   prebuilt.nozzle},
                {SK::filament_slot::select_action,     make_select_action(card_state, prebuilt.nozzle)},
            });
        }
    }
    page[SK::filament_page::hotend_slots] = std::move(slots);

    return page;
}

const char* BatchAmsLoadModeBuilder::ModeName() const
{
    return SV::mode::batch_ams_load;
}

}} // namespace Slic3r::GUI
