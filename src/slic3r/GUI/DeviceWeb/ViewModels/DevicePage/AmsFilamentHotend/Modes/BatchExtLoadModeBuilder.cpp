#include "BatchExtLoadModeBuilder.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ViewModel.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Schema.hpp"
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

nlohmann::json make_action_state(bool enabled, std::string reason = "")
{
    return nlohmann::json{
        {SK::action_state::enabled, enabled},
        {SK::action_state::reason,  std::move(reason)},
    };
}

std::string color_type_for_ext_web(DevFilaColorType color_type)
{
    switch (color_type) {
    case DevFilaColorType::CTYPE_MULTI:    return "multi";
    case DevFilaColorType::CTYPE_GRADIANT: return "gradient";
    case DevFilaColorType::CTYPE_SINGLE:   return "single";
    }
    return "single";
}

std::string normalize_ext_hex_for_web(const std::string& raw)
{
    if (raw.empty()) return "#D9D9D9";
    std::string hex = raw;
    if (hex[0] != '#') hex = "#" + hex;
    if (hex.size() >= 7) hex = hex.substr(0, 7);
    return hex;
}

bool is_ext_slot_loaded(MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id)
{
    if (!machine_obj || !machine_obj->GetExtderSystem()) {
        return false;
    }
    for (const auto& ext : machine_obj->GetExtderSystem()->GetExtruders()) {
        if (ext.GetSlotNow().ams_id == ams_id && ext.GetSlotNow().slot_id == slot_id && ext.HasFilamentInExt()) {
            return true;
        }
    }
    return false;
}

nlohmann::json make_select_action(bool can_select, std::string reason)
{
    return nlohmann::json{
        {SK::select_action::can_select, can_select},
        {SK::select_action::reason,     std::move(reason)},
        // Manual external-spool mode shows the EXT badge on every nozzle card
        // to make it clear the visible filament info comes from the virtual tray.
        {SK::select_action::ext_label,  true},
    };
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

    // `can_dry` is mirrored from the normal-mode actions so the side-channel
    // dry dialog remains reachable in batch mode (mirrors BatchAmsLoadModeBuilder).
    const std::string other_active = _u8L("Other batch mode active");
    const auto& src_actions = prebuilt_normal_page.value(SK::filament_page::actions, nlohmann::json::object());
    const bool src_can_dry = src_actions.value(SK::filament_actions::can_dry, false);
    page[SK::filament_page::actions] = {
        {SK::filament_actions::can_dry,           make_action_state(src_can_dry)},
        {SK::filament_actions::can_load_auto,     make_action_state(false, other_active)},
        {SK::filament_actions::can_load_external, make_action_state(true)},
        {SK::filament_actions::can_unload,        make_action_state(false, other_active)},
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
        const bool is_loaded = is_ext_slot_loaded(machine_obj, ext_ams_id, ext_slot_id);

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
            color = normalize_ext_hex_for_web(ext_tray_opt->color);
            color_type = color_type_for_ext_web(ext_tray_opt->ctype);
            colors = ext_tray_opt->cols;
            if (colors.empty() && !ext_tray_opt->color.empty()) {
                colors.push_back(ext_tray_opt->color);
            }
            for (auto& c : colors) {
                c = normalize_ext_hex_for_web(c);
            }
            if (ext_tray_opt->remain >= 0 && ext_tray_opt->remain <= 100) {
                remain = ext_tray_opt->remain;
            }
            card_state = is_loaded ? SV::card_state::ready_to_use : SV::card_state::ready_to_load;
        }

        const bool can_select = !is_loaded;
        std::string reason;
        if (is_loaded) {
            reason = _u8L("External filament already loaded into hotend");
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

        const nlohmann::json nozzle_section = {
            {SK::filament_nozzle::state,          is_loaded ? SV::nozzle_state::active : SV::nozzle_state::normal},
            {SK::filament_nozzle::has_filament,   is_loaded},
            {SK::filament_nozzle::filament_color, is_loaded ? color : std::string()},
            {SK::filament_nozzle::on_extruder,    slot.value(SK::filament_slot::filament_nozzle, nlohmann::json::object()).value(SK::filament_nozzle::on_extruder, false)}
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
