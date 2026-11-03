#include "ViewModelExtruderStateBuilder.hpp"

#include "slic3r/GUI/DeviceCore/DevConfigUtil.h"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"

namespace Slic3r { namespace GUI { namespace ControlPanel {

namespace Schema = ControlPanelSchema;
namespace Format = ControlPanelSchema::format;

namespace {

std::string normalize_hex(std::string color)
{
    if (color.empty()) return {};
    if (color[0] != '#') color = "#" + color;
    if (color.size() == 9) color = color.substr(0, 7);
    return color;
}

std::string extruder_status(MachineObject* obj, const DevExtder& extder)
{
    auto* extder_system = obj ? obj->GetExtderSystem() : nullptr;
    if (extder_system && extder_system->IsSwitching())
        return Schema::values::extruder_status::switching;
    if (extder.GetExtrusionDirection() == DevExtrusionDirection::Extruding)
        return Schema::values::extruder_status::loading;
    if (extder.GetExtrusionDirection() == DevExtrusionDirection::Retracting)
        return Schema::values::extruder_status::unloading;
    if (extder_system && extder_system->GetCurrentExtderId() == extder.GetExtId())
        return Schema::values::extruder_status::active;
    return Schema::values::extruder_status::idle;
}

std::string single_extruder_icon()
{
    return "monitor_ams_extruder";
}

std::string dual_extruder_icon(const DevExtder& extder, int current_extder_id)
{
    const bool is_left = extder.GetExtId() == DEPUTY_EXTRUDER_ID;
    const bool active = extder.GetExtId() == current_extder_id;
    const bool filled = extder.HasFilamentInExt();
    return std::string(is_left ? "left" : "right") + "_extruder_" +
        (active ? "active" : "unactive") + "_" +
        (filled ? "filled" : "empty");
}

} // namespace

void FillExtruderState(MachineObject* obj, Format::State& state)
{
    auto& area = state.display.extruder;
    auto* extder_system = obj ? obj->GetExtderSystem() : nullptr;
    const int extruder_count = extder_system ? extder_system->GetTotalExtderCount() : 0;
    const bool single_rack = extruder_count == 1 && obj && obj->GetNozzleRack() && obj->GetNozzleRack()->IsSupported() && obj->GetNozzleRack()->IsDirectConnectedRack();
    area.type = extruder_count >= 2 ? Schema::values::extruder_area_type::dual :
                single_rack ? Schema::values::extruder_area_type::single_rack :
                              Schema::values::extruder_area_type::single;

    if (!extder_system) return;

    auto& dual = area.dual_extruder;
    if (area.type == Schema::values::extruder_area_type::dual) {
        dual.current_extruder_id = extder_system->GetCurrentExtderId();
        dual.left_label = DevPrinterConfigUtil::get_toolhead_display_name(obj ? obj->printer_type : std::string(),
            DEPUTY_EXTRUDER_ID, ToolHeadComponent::Extruder, ToolHeadNameCase::TitleCase, true);
        dual.right_label = DevPrinterConfigUtil::get_toolhead_display_name(obj ? obj->printer_type : std::string(),
            MAIN_EXTRUDER_ID, ToolHeadComponent::Extruder, ToolHeadNameCase::TitleCase, true);
    }

    if (const auto current = extder_system->GetCurrentExtder()) {
        auto fill_common_current = [&](std::optional<int>& nozzle_id, std::string& filament_clr, std::string& nozzle_diameter, std::string& nozzle_flow_type) {
            if (current->GetPickedNozzleAddrId().has_value()) {
                nozzle_id = current->GetPickedNozzleAddrId().value() + 1;
            } else {
                const int nz_id = current->GetNozzleId();
                if (nz_id != 0xE) {
                    nozzle_id = nz_id >= 16 ? (nz_id - 16 + 1) : (nz_id + 1);
                }
            }
            const auto& slot = current->GetSlotNow();
            if (!slot.ams_id.empty() && !slot.slot_id.empty()) {
                if (DevAmsTray* tray = obj->get_ams_tray(slot.ams_id, slot.slot_id)) {
                    filament_clr = normalize_hex(tray->color);
                }
            }
            if (filament_clr.empty()) {
                if (DevAmsTray* tray = obj->get_curr_tray())
                    filament_clr = normalize_hex(tray->color);
            }
            if (auto nozzle_sys = obj->GetNozzleSystem()) {
                const auto ext_nozzle = nozzle_sys->GetExtNozzle(current->GetExtId());
                if (!ext_nozzle.IsEmpty() && ext_nozzle.IsNormal()) {
                    nozzle_diameter = ext_nozzle.GetNozzleDiameterStr().ToUTF8().data();
                    nozzle_flow_type = ext_nozzle.GetNozzleFlowTypeStr().ToUTF8().data();
                } else if (!current->HasNozzleInstalled() || ext_nozzle.IsEmpty()) {
                    nozzle_flow_type = _u8L("Unclamped");
                }
                if (filament_clr.empty() && !ext_nozzle.GetFilamentColor().empty()) {
                    filament_clr = normalize_hex(ext_nozzle.GetFilamentColor());
                }
            }
        };

        if (single_rack) {
            auto& view = area.single_rack_extruder;
            fill_common_current(view.current_nozzle_id, view.filament_clr, view.nozzle_diameter, view.nozzle_flow_type);
            view.has_filament = current->HasFilamentInExt();
            view.extruder_status = extruder_status(obj, *current);
            view.rack_toolhead_visual_state = std::string(rack_extruder_visual_state_key(resolve_rack_extruder_visual_state(obj, *current)));
        } else if (extruder_count >= 2) {
            // Dual extruder details are filled below as a pair of ExtruderView entries.
        } else {
            auto& view = area.single_extruder;
            std::optional<int> ignored_nozzle_id;
            std::string ignored_nozzle_diameter;
            std::string ignored_nozzle_flow_type;
            fill_common_current(ignored_nozzle_id, view.filament_clr, ignored_nozzle_diameter, ignored_nozzle_flow_type);
            view.has_filament = current->HasFilamentInExt();
            view.extruder_status = extruder_status(obj, *current);
            view.extruder_icon = single_extruder_icon();
        }
    }

    if (area.type != Schema::values::extruder_area_type::dual) return;

    for (const auto& extder : extder_system->GetExtruders()) {
        Format::ExtruderView view;
        view.id = extder.GetExtId();
        view.has_filament = extder.HasFilamentInExt();
        view.extruder_status = extruder_status(obj, extder);
        if (view.has_filament) {
            const auto& slot = extder.GetSlotNow();
            if (DevAmsTray* tray = obj->get_ams_tray(slot.ams_id, slot.slot_id))
                view.filament_clr = normalize_hex(tray->color);
        }
        view.extruder_icon = dual_extruder_icon(extder, dual.current_extruder_id);
        dual.extruders.push_back(std::move(view));
    }
}

}}} // namespace Slic3r::GUI::ControlPanel
