#include "ViewModelExtruderStateBuilder.hpp"
#include "ViewModelActions.hpp"

#include "slic3r/GUI/DeviceCore/DevConfigUtil.h"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
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

std::string single_extruder_icon(bool has_filament)
{
    return has_filament ? "monitor_extruder_filled_load" : "monitor_extruder_empty_load";
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

bool is_pen_tool(const DevNozzleToolDefs& tool_defs)
{
    return tool_defs.tool_type == DevNozzleToolType::Cut &&
           tool_defs.cut_info.has_value() &&
           tool_defs.cut_info->type == CutType::Pen;
}

std::string tool_label(const DevNozzleToolDefs& tool_defs)
{
    switch (tool_defs.tool_type) {
    case DevNozzleToolType::Cut:   return is_pen_tool(tool_defs) ? _u8L("Pen") : _u8L("Cutter");
    case DevNozzleToolType::Laser: return _u8L("Laser");
    case DevNozzleToolType::Fan:   return _u8L("Fan");
    case DevNozzleToolType::Nozzle:
    case DevNozzleToolType::None:
    default:                       return {};
    }
}

std::string resolve_rack_visual_key(MachineObject* obj, const DevExtder& current, const DevNozzleToolDefs& tool_defs)
{
    if (tool_defs.tool_type == DevNozzleToolType::Cut) {
        return is_pen_tool(tool_defs) ? Schema::values::rack_toolhead_visual_state::pen
                                      : Schema::values::rack_toolhead_visual_state::cutter;
    }
    if (tool_defs.tool_type != DevNozzleToolType::Nozzle &&
        tool_defs.tool_type != DevNozzleToolType::None) {
        return Schema::values::rack_toolhead_visual_state::default_toolhead;
    }
    const auto state = resolve_rack_extruder_visual_state(obj, current);
    const std::string img = obj ? DevPrinterConfigUtil::get_rack_extruder_img(obj->printer_type, state) : std::string();
    if (img == "dev_rack_toolhead_unclamped.png") return Schema::values::rack_toolhead_visual_state::empty;
    if (img == "dev_rack_toolhead_clamped.png") return Schema::values::rack_toolhead_visual_state::clamped;
    if (img == "dev_rack_toolhead_clamped_no_filament.png") return Schema::values::rack_toolhead_visual_state::clamped_no_filament;
    if (img == "dev_rack_toolhead_picking.png") return Schema::values::rack_toolhead_visual_state::picking;
    if (img == "dev_rack_toolhead_placing.png") return Schema::values::rack_toolhead_visual_state::placing;
    if (img == "dev_rack_toolhead_cutter.png") return Schema::values::rack_toolhead_visual_state::cutter;
    if (img == "dev_rack_toolhead_pen.png") return Schema::values::rack_toolhead_visual_state::pen;
    if (img == "dev_rack_toolhead.png") return Schema::values::rack_toolhead_visual_state::default_toolhead;
    return std::string(rack_extruder_visual_state_key(state));
}

// Same file the native rack panel loads. Printer json selects it per visual state.
std::string resolve_rack_toolhead_image(MachineObject* obj, const std::optional<DevExtder>& current, const DevNozzleToolDefs& tool_defs)
{
    if (tool_defs.tool_type == DevNozzleToolType::Cut) {
        return is_pen_tool(tool_defs) ? "dev_rack_toolhead_pen.png" : "dev_rack_toolhead_cutter.png";
    }
    if (tool_defs.tool_type != DevNozzleToolType::Nozzle &&
        tool_defs.tool_type != DevNozzleToolType::None) {
        return "dev_rack_toolhead.png";
    }

    const auto state = current ? resolve_rack_extruder_visual_state(obj, *current)
                               : DevRackExtruderVisualState::Empty;
    return obj ? DevPrinterConfigUtil::get_rack_extruder_img(obj->printer_type, state)
               : std::string("dev_rack_toolhead.png");
}

std::string feeding_slot_color_hex(MachineObject* obj, const DevExtder& current)
{
    if (!obj) return {};
    const DevAmsSlotInfo& slot = current.GetSlotNow();
    if (slot.ams_id.empty() || slot.slot_id.empty()) return {};
    if (!obj->contains_tray(slot.ams_id, slot.slot_id)) return {};
    const auto tray = obj->get_tray(slot.ams_id, slot.slot_id);
    if (!tray || tray->color.empty()) return {};
    return normalize_hex(tray->color);
}

} // namespace

void FillExtruderState(MachineObject* obj, Format::State& state)
{
    auto& area = state.display.extruder;
    area.label = _u8L("Extruder");

    const bool ready = obj && obj->is_connected() && obj->is_info_ready();
    auto* extder_system = ready ? obj->GetExtderSystem() : nullptr;
    const int extruder_count = extder_system ? extder_system->GetTotalExtderCount() : 1;
    const bool single_rack = extruder_count == 1 && ready && obj->GetNozzleRack() &&
                             obj->GetNozzleRack()->IsSupported() && obj->GetNozzleRack()->IsDirectConnectedRack();
    area.type = extruder_count >= 2 ? Schema::values::extruder_area_type::dual :
                single_rack ? Schema::values::extruder_area_type::single_rack :
                              Schema::values::extruder_area_type::single;

    if (!extder_system) {
        area.single_extruder.has_filament = false;
        area.single_extruder.extruder_icon = single_extruder_icon(false);
        return;
    }

    auto& dual = area.dual_extruder;
    if (area.type == Schema::values::extruder_area_type::dual) {
        dual.current_extruder_id = extder_system->GetCurrentExtderId();
        dual.left_label = _u8L(DevPrinterConfigUtil::get_toolhead_display_name(
            obj ? obj->printer_type : std::string(),
            DEPUTY_EXTRUDER_ID, ToolHeadComponent::Extruder, ToolHeadNameCase::TitleCase, true));
        dual.right_label = _u8L(DevPrinterConfigUtil::get_toolhead_display_name(
            obj ? obj->printer_type : std::string(),
            MAIN_EXTRUDER_ID, ToolHeadComponent::Extruder, ToolHeadNameCase::TitleCase, true));
        dual.quit_label = _CTX_utf8(L_CONTEXT("Quit", "Quit_Switching"), "Quit_Switching");
        dual.retry_label = _u8L("Retry");

        if (!ControlPanelActionHandler::IsSwitchingActionHeld(obj->get_dev_id())) {
            const auto sw_state = extder_system->GetSwitchState();
            if (sw_state == DevExtderSwitchState::ES_SWITCHING) {
                dual.switching_status_text = _u8L("Switching...");
                dual.switching_failed = false;
            } else if (sw_state == DevExtderSwitchState::ES_SWITCHING_FAILED) {
                dual.switching_status_text = _u8L("Switching failed");
                dual.switching_failed = true;
                dual.can_quit_switching = !obj->is_in_printing() && !obj->is_in_printing_pause();
                dual.can_retry_switching = true;
            }
        }
    }

    if (single_rack) {
        auto& view = area.single_rack_extruder;
        const auto current = extder_system->GetCurrentExtder();
        if (current && current->GetPickedNozzleAddrId().has_value()) {
            const int pick_pos_id = current->GetPickedNozzleAddrId().value() + 0x10;
            if (pick_pos_id != 0xE) {
                view.current_nozzle_id = pick_pos_id >= 16 ? (pick_pos_id - 16 + 1) : (pick_pos_id + 1);
            }
        }

        const auto ext_nozzle = obj->GetNozzleSystem()
            ? obj->GetNozzleSystem()->GetExtNozzle(MAIN_EXTRUDER_ID)
            : DevNozzle();
        const auto tool_defs = ext_nozzle.IsEmpty() ? DevNozzleToolDefs() : ext_nozzle.GetToolDefs();
        const bool has_installed_tool = current && current->HasNozzleInstalled() && !ext_nozzle.IsEmpty();

        if (!has_installed_tool) {
            view.nozzle_flow_type = _u8L("Unclamped");
        } else if (tool_defs.tool_type == DevNozzleToolType::Nozzle) {
            if (!ext_nozzle.IsEmpty() && ext_nozzle.IsNormal()) {
                view.nozzle_diameter = ext_nozzle.GetNozzleDiameterStr().ToUTF8().data();
                view.nozzle_flow_type = ext_nozzle.GetNozzleFlowTypeStr().ToUTF8().data();
            } else if (!ext_nozzle.IsEmpty() && ext_nozzle.IsAbnormal()) {
                view.nozzle_flow_type = _u8L("Error");
            } else if (!ext_nozzle.IsEmpty() && ext_nozzle.IsUnknown()) {
                view.nozzle_flow_type = _u8L("Unknown");
            } else {
                view.nozzle_flow_type = _u8L("Unclamped");
            }
        } else if (!ext_nozzle.IsEmpty() && ext_nozzle.IsAbnormal()) {
            view.nozzle_flow_type = _u8L("Error");
        } else if (!ext_nozzle.IsEmpty() && ext_nozzle.IsUnknown()) {
            view.nozzle_flow_type = _u8L("Unknown");
        } else if (!tool_label(tool_defs).empty()) {
            view.nozzle_flow_type = tool_label(tool_defs);
        } else {
            view.nozzle_flow_type = _u8L("Unclamped");
        }

        view.rack_toolhead_image = resolve_rack_toolhead_image(obj, current, tool_defs);
        if (current) {
            view.has_filament = current->HasFilamentInExt();
            view.extruder_status = extruder_status(obj, *current);
            view.rack_toolhead_visual_state = resolve_rack_visual_key(obj, *current, tool_defs);

            if (has_installed_tool &&
                (tool_defs.tool_type == DevNozzleToolType::Nozzle || tool_defs.tool_type == DevNozzleToolType::None)) {
                view.show_filament_path = true;
                const std::string feeding_clr = feeding_slot_color_hex(obj, *current);
                const std::string nozzle_clr = normalize_hex(ext_nozzle.GetFilamentColor());
                view.upper_filament_clr = !feeding_clr.empty() ? feeding_clr : nozzle_clr;
                view.lower_filament_clr = !nozzle_clr.empty() ? nozzle_clr : feeding_clr;
                view.filament_clr = !view.upper_filament_clr.empty() ? view.upper_filament_clr : view.lower_filament_clr;
            }
        }
        return;
    }

    if (area.type == Schema::values::extruder_area_type::single) {
        auto& view = area.single_extruder;
        const bool has_filament = extder_system->GetTotalExtderSize() > 0 && extder_system->HasFilamentInExt(0);
        view.has_filament = has_filament;
        if (const auto current = extder_system->GetCurrentExtder()) {
            view.extruder_status = extruder_status(obj, *current);
            if (has_filament) {
                view.filament_clr = feeding_slot_color_hex(obj, *current);
            }
        }
        view.extruder_icon = single_extruder_icon(has_filament);
        return;
    }

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
