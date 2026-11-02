#include "BatchExtLoadModeBuilder.hpp"
#include "BatchModeCommon.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ViewModel.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Schema.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ResolverSlotState.hpp"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <optional>
#include <vector>

namespace Slic3r { namespace GUI {

namespace SK = AmsHotendSchema::keys;
namespace SV = AmsHotendSchema::values;

namespace {

nlohmann::json make_select_action(bool can_select, std::string reason)
{
    // Manual external-spool mode shows the EXT badge on every nozzle card
    // to make it clear the visible filament info comes from the virtual tray.
    return BatchModeCommon::make_select_action(can_select, std::move(reason), /*ext_label*/ true);
}

int ext_tray_id_for_addr_id(int addr_id)
{
    return 255 - addr_id;
}

} // namespace

nlohmann::json BatchExtLoadModeBuilder::BuildFilamentPage(
    const DevicePageAmsFilamentHotendVM& vm,
    MachineObject*                       machine_obj,
    const nlohmann::json&                prebuilt_normal_page) const
{
    nlohmann::json page;
    page[SK::filament_page::mode] = SV::mode::batch_ext_load;
    page[SK::filament_page::show_ams_env_units] = false;

    // `can_dry` is mirrored from the normal-mode actions so the side-channel
    // dry dialog remains reachable in batch mode (mirrors BatchAmsLoadModeBuilder).
    const std::string other_active = _u8L("Other batch mode active");
    const auto& src_actions = prebuilt_normal_page.value(SK::filament_page::actions, nlohmann::json::object());
    const bool src_can_dry = src_actions.value(SK::filament_actions::can_dry, false);
    page[SK::filament_page::actions] = {
        {SK::filament_actions::can_dry,           BatchModeCommon::make_action_state(src_can_dry)},
        {SK::filament_actions::can_show_load_auto, BatchModeCommon::make_action_state(false, other_active)},
        {SK::filament_actions::can_load_external, BatchModeCommon::make_action_state(true)},
        {SK::filament_actions::can_unload,        BatchModeCommon::make_action_state(false, other_active)},
    };

    nlohmann::json slots = nlohmann::json::array();
    const auto& src_slots = prebuilt_normal_page.value(SK::filament_page::hotend_slots, nlohmann::json::array());
    auto fila_system = machine_obj ? machine_obj->GetFilaSystem() : nullptr;

    // Manual external-spool mode renders one virtual-tray card for every rack
    // nozzle. It intentionally does NOT reuse the normal AMS-bound slot body:
    // every nozzle must show the filament metadata from its corresponding Ext
    // virtual tray. Empty / never-edited Ext tray info is represented as
    // Unknown ("?") so the Web can force the edit flow before starting the
    // load command.
    for (const auto& slot : src_slots) {
        const int addr_id       = slot.value(SK::filament_slot::addr_id, 0);
        const int display_index = slot.value(SK::filament_slot::display_index, 0);
        const int ext_tray_id   = ext_tray_id_for_addr_id(addr_id);
        const std::string ext_ams_id  = std::to_string(ext_tray_id);
        const std::string ext_slot_id = "0";

        std::optional<DevAmsTray> ext_tray_opt;
        if (fila_system) {
            ext_tray_opt = fila_system->GetVirtualTray(ext_tray_id);
        }
        const bool is_loaded = ext_tray_opt.has_value() && DevicePageFilamentSlotStateResolver::IsTraySlotLoadedInNozzle(machine_obj, &ext_tray_opt.value());

        std::string card_state = SV::card_state::unknown;
        std::string color = "#D9D9D9";
        std::string color_type = "single";
        std::string filament_name;
        std::string filament_type;
        int remain = 100;
        std::vector<std::string> colors;

        if (ext_tray_opt.has_value() && !ext_tray_opt->m_fila_type.empty()) {
            filament_type = ext_tray_opt->m_fila_type;
            filament_name = vm.ResolveFilamentCardText(&*ext_tray_opt, filament_type);
            color = BatchModeCommon::normalize_hex_for_web(ext_tray_opt->color);
            color_type = BatchModeCommon::color_type_for_web(ext_tray_opt->ctype);
            colors = ext_tray_opt->cols;
            if (colors.empty() && !ext_tray_opt->color.empty()) {
                colors.push_back(ext_tray_opt->color);
            }
            for (auto& c : colors) {
                c = BatchModeCommon::normalize_hex_for_web(c);
            }
            remain = DevicePageFilamentSlotStateResolver::ResolveDisplayRemain(&*ext_tray_opt);
            card_state = is_loaded ? SV::card_state::ready_to_use : SV::card_state::ready_to_load;
        }

        const auto& src_nozzle = slot.value(SK::filament_slot::filament_nozzle, nlohmann::json::object());
        const std::string src_nozzle_state = src_nozzle.value(SK::filament_nozzle::state, std::string());
        const auto nozzle_status = BatchModeCommon::classify_nozzle(src_nozzle);

        bool can_select = false;
        std::string reason;
        if (nozzle_status.is_blocked()) {
            reason = *nozzle_status.block_reason;
        } else if (is_loaded) {
            reason = _u8L("External filament already loaded into hotend");
        } else {
            can_select = true;
        }

        const nlohmann::json card_section = {
            {SK::filament_card::state,       card_state},
            {SK::filament_card::color,       color},
            {SK::filament_card::color_type,  color_type},
            {SK::filament_card::colors,      colors},
            {SK::filament_card::text,        filament_name},
            {SK::filament_card::name,        filament_name},
            {SK::filament_card::type,        filament_type},
            {SK::filament_card::remain,      remain},
            {SK::filament_card::status_text, card_state == SV::card_state::unknown ? _u8L("Unknown filament") : std::string()}
        };

        const nlohmann::json line_section = {
            {SK::filament_line::state,          is_loaded ? SV::line_state::loaded : SV::line_state::no_filament},
            {SK::filament_line::has_filament,   is_loaded},
            {SK::filament_line::filament_color, is_loaded ? color : std::string()}
        };

        const std::string nozzle_state = nozzle_status.has_visible_alert
            ? src_nozzle_state
            : (is_loaded ? SV::nozzle_state::active : SV::nozzle_state::normal);
        const bool nozzle_has_filament = is_loaded && !nozzle_status.is_blocked();
        const nlohmann::json nozzle_section = {
            {SK::filament_nozzle::state,          nozzle_state},
            {SK::filament_nozzle::has_filament,   nozzle_has_filament},
            {SK::filament_nozzle::filament_color, nozzle_has_filament ? color : std::string()},
            {SK::filament_nozzle::on_extruder,    src_nozzle.value(SK::filament_nozzle::on_extruder, false)},
            // Keep the tool kind so a non-nozzle tool still renders its own
            // silhouette instead of a plain nozzle while being unselectable.
            {SK::filament_nozzle::tool_kind,      src_nozzle.value(SK::filament_nozzle::tool_kind, std::string(SV::tool_kind::empty))},
            {SK::filament_nozzle::tooltip,        nozzle_status.has_visible_alert ? src_nozzle.value(SK::filament_nozzle::tooltip, std::string()) : std::string()}
        };

        slots.push_back({
            {SK::filament_slot::addr_id,           addr_id},
            {SK::filament_slot::display_index,     display_index},
            {SK::filament_slot::ams_id,            ext_ams_id},
            {SK::filament_slot::slot_id,           ext_slot_id},
            {SK::filament_slot::is_top_tier,       slot.value(SK::filament_slot::is_top_tier,       false)},
            {SK::filament_slot::is_external_spool, true},
            {SK::filament_slot::enabled,           slot.value(SK::filament_slot::enabled,           false)},
            {SK::filament_slot::filament_card,     card_section},
            {SK::filament_slot::filament_line,     line_section},
            {SK::filament_slot::filament_nozzle,   nozzle_section},
            {SK::filament_slot::select_action,     make_select_action(can_select, reason)},
        });
    }
    page[SK::filament_page::hotend_slots] = std::move(slots);

    return page;
}

const char* BatchExtLoadModeBuilder::ModeName() const
{
    return SV::mode::batch_ext_load;
}

}} // namespace Slic3r::GUI
