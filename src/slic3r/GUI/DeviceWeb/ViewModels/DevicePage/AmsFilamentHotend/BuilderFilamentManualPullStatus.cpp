#include "BuilderFilamentManualPullStatus.hpp"
#include "Schema.hpp"

#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <string>

namespace Slic3r { namespace GUI {

namespace {

std::string normalize_hex_for_web(const std::string& raw)
{
    if (raw.empty()) return "#D9D9D9";
    std::string hex = raw;
    if (hex[0] != '#') hex = "#" + hex;
    if (hex.size() >= 7) hex = hex.substr(0, 7);
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

} // namespace

nlohmann::json DevicePageFilamentManualPullStatusBuilder::Build(MachineObject* machine_obj)
{
    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    nlohmann::json items = nlohmann::json::array();
    if (!machine_obj || !machine_obj->GetFilaSystem()) {
        return {
            {SK::filament_manual_pull::title,     _u8L("Filament not removed")},
            {SK::filament_manual_pull::items,     items},
            {SK::filament_manual_pull::can_exit,  false},
            {SK::filament_manual_pull::can_close, true}
        };
    }

    auto fila_system = machine_obj->GetFilaSystem();
    auto nozzle_sys = machine_obj->GetNozzleSystem();
    const auto slot_to_display_index = fila_system->GetSlot2HotendDisplayIdx();

    const auto& changing_slots = fila_system->GetFilamentChangingSlots();
    for (size_t i = 0; i < changing_slots.size(); ++i) {
        const auto& s = changing_slots[i];

        int display_idx = static_cast<int>(i) + 1;
        const char* item_state = SV::filament_manual_pull_item_state::success;
        if (auto it = slot_to_display_index.find({ s.first, s.second }); it != slot_to_display_index.end()) {
            display_idx = it->second;
            if (auto nozzle_opt = nozzle_sys->GetNozzleOptByAddrId(display_idx - 1); nozzle_opt.has_value()) {
                item_state = nozzle_opt->HasFilamentAtNozzleInlet() ? SV::filament_manual_pull_item_state::pending : SV::filament_manual_pull_item_state::success;
            } else {
                DEV_ASSERT_ONCE(warning, " Nozzle not found");
                continue;
            }
        } else {
            DEV_ASSERT_ONCE(warning, " Nozzle not found");
            continue;
        }

        std::string filament_name;
        std::string filament_type;
        std::string color = "#D9D9D9";
        std::string color_type = "single";
        const std::string ams_str = std::to_string(s.first);
        const std::string slot_str = std::to_string(s.second);
        if (auto tray = machine_obj->get_tray(ams_str, slot_str); tray.has_value()) {
            filament_name = tray->sub_brands.empty() ? tray->m_fila_type : tray->sub_brands;
            filament_type = tray->m_fila_type;
            color = normalize_hex_for_web(tray->color);
            color_type = color_type_for_web(tray->ctype);
        }

        items.push_back({
            {SK::filament_manual_pull_item::ams_id,        ams_str},
            {SK::filament_manual_pull_item::slot_id,       slot_str},
            {SK::filament_manual_pull_item::display_index, display_idx},
            {SK::filament_manual_pull_item::filament_name, filament_name},
            {SK::filament_manual_pull_item::filament_type, filament_type},
            {SK::filament_manual_pull_item::color,         color},
            {SK::filament_manual_pull_item::color_type,    color_type},
            {SK::filament_manual_pull_item::state,         item_state},
            {SK::filament_manual_pull_item::reason,        ""}
        });
    }

    return {
        {SK::filament_manual_pull::title,     _u8L("Filament not removed")},
        {SK::filament_manual_pull::items,     items},
        {SK::filament_manual_pull::can_exit,  false},
        {SK::filament_manual_pull::can_close, true}
    };
}

}} // namespace Slic3r::GUI
