#include "ViewModelDisplayBuilder.hpp"
#include "ViewModelDataBuilder.hpp"
#include "ViewModelLayoutBuilder.hpp"

#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevInfo.h"

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r { namespace GUI { namespace AmsControlWebDisplay {

namespace SchemaFormat = AmsControlWebSchema::format;
namespace SchemaValues = AmsControlWebSchema::values;

namespace {

std::set<std::pair<std::string, std::string>>& filament_mgr_hint_slots()
{
    static std::set<std::pair<std::string, std::string>> slots;
    return slots;
}

// AMSLib icon rules: no icon without a spool, the eye icon for genuine spools
// and for everything in 2D mode, the pencil icon otherwise.
SchemaFormat::MenuActions build_menu_actions(const SchemaFormat::Tray& tray,
                                             const std::string& slot_state,
                                             bool view_only)
{
    SchemaFormat::MenuActions actions;

    const bool has_spool = slot_state != SchemaValues::slot_state::empty &&
                           slot_state != SchemaValues::slot_state::none;
    if (!has_spool) {
        // Pulling the spool out retires a pending hint.
        filament_mgr_hint_slots().erase({tray.ams_id, tray.slot_id});
        return actions;
    }

    actions.show_read = view_only || slot_state == SchemaValues::slot_state::brand;
    actions.show_edit = !actions.show_read;
    actions.show_filament_mgr_hint =
        filament_mgr_hint_slots().count({tray.ams_id, tray.slot_id}) > 0;
    return actions;
}

SchemaFormat::SlotView build_slot_view(const SchemaFormat::Tray& tray,
                                       const SchemaFormat::State& state,
                                       bool is_ext,
                                       bool view_only)
{
    SchemaFormat::SlotView view;
    view.ams_id    = tray.ams_id;
    view.slot_id   = tray.slot_id;
    view.label     = tray.tray_label;
    view.selected  = tray.ams_id == state.selected_ams_id && tray.slot_id == state.selected_slot_id;
    view.loaded    = tray.ams_id == state.loaded_ams_id && tray.slot_id == state.loaded_slot_id;
    view.reading   = tray.reading;
    view.show_rfid = !is_ext;

    const bool is_bbl = DevFilaSystem::IsBBL_Filament(tray.tag_uid);
    if (!tray.is_exists)
        view.slot_state = SchemaValues::slot_state::empty;
    // parse_ext_info() marks every ext slot as non-genuine, keeping it editable:
    // going genuine would swap the pencil icon for the read-only eye.
    else if (is_ext)
        view.slot_state = SchemaValues::slot_state::third_brand;
    else if (is_bbl && tray.info_ready)
        view.slot_state = SchemaValues::slot_state::brand;
    else
        view.slot_state = SchemaValues::slot_state::third_brand;

    // parse_ams_info / parse_ext_info only copy the spool fields once the tray
    // reports both a colour and a type. Drawing white with a "?" until then keeps
    // a fresh spool from briefly showing the colour of the one it replaced.
    const bool has_spool = view.slot_state != SchemaValues::slot_state::empty;
    if (has_spool && !tray.info_ready) {
        view.show_unknown = true;
        view.color        = "#FFFFFF";
        view.colors       = {view.color};
        view.color_type   = 0;
    } else {
        view.color      = tray.color;
        view.colors     = tray.colors;
        view.color_type = tray.color_type;
        view.fila_type  = tray.fila_type;
    }

    // AMSinfo::parse_ams_info: a remain readout needs a genuine spool,
    // trustworthy tray info and remain detection switched on.
    view.show_remain = !is_ext && is_bbl && tray.info_ready && state.data.detect_remain_enabled;
    view.remain      = (view.show_remain && tray.remain >= 0 && tray.remain <= 100) ? tray.remain : 100;

    view.menu_actions = build_menu_actions(tray, view.slot_state, view_only);
    return view;
}

SchemaFormat::HumidityView build_humidity_view(const SchemaFormat::Unit& unit, DevAms* ams)
{
    SchemaFormat::HumidityView view;
    view.display_type = unit.humidity_display_type;
    view.level        = unit.humidity_level;
    view.percent      = unit.humidity_percent;

    if (view.display_type == SchemaValues::humidity_display_type::level) {
        view.display_idx = (unit.humidity_level > 0 && unit.humidity_level < 6) ? unit.humidity_level : -1;
    } else if (view.display_type == SchemaValues::humidity_display_type::percent) {
        const int percent = unit.humidity_percent;
        if (percent < 0)        view.display_idx = -1;
        else if (percent < 20)  view.display_idx = 5;
        else if (percent < 40)  view.display_idx = 4;
        else if (percent < 60)  view.display_idx = 3;
        else if (percent < 80)  view.display_idx = 2;
        else                    view.display_idx = 1;
    }

    if (ams) {
        view.left_dry_time = ams->GetLeftDryTime();
        view.drying        = ams->AmsIsDrying() ||
                             (!ams->GetDryStatus().has_value() && view.left_dry_time > 0);
    }
    return view;
}

// Either the slot reports its port or the switch names the slot on one of its
// inputs. Both come from the machine, so no port is guessed from the AMS model.
std::string switcher_port_for_slot(const SchemaFormat::FilaSwitchData& fila_switch,
                                   const SchemaFormat::Tray& tray)
{
    if (!fila_switch.installed) return {};
    if (!tray.switcher_port.empty()) return tray.switcher_port;

    auto routed_through = [&tray](const SchemaFormat::FilaSwitchPort& in) {
        return !in.ams_id.empty() && in.ams_id == tray.ams_id && in.slot_id == tray.slot_id;
    };
    if (routed_through(fila_switch.in_a)) return SchemaValues::switcher_port::a;
    if (routed_through(fila_switch.in_b)) return SchemaValues::switcher_port::b;
    return {};
}

// How far a load has run along the line is not reported by the machine, so a
// slot the extruder system is pulling from reads `loading` and a slot already in
// reads `loaded`.
SchemaFormat::SlotLink build_slot_link(const SchemaFormat::Tray& tray,
                                      const SchemaFormat::State& state,
                                      const std::string& loading_ams_id,
                                      const std::string& loading_slot_id)
{
    SchemaFormat::SlotLink link;
    link.ams_id        = tray.ams_id;
    link.slot_id       = tray.slot_id;
    link.switcher_port = switcher_port_for_slot(state.data.fila_switch, tray);
    link.extruder_ids  = tray.binded_extruder_ids;

    if (!loading_ams_id.empty() && tray.ams_id == loading_ams_id && tray.slot_id == loading_slot_id)
        link.state = SchemaValues::link_state::loading;
    else if (tray.ams_id == state.loaded_ams_id && tray.slot_id == state.loaded_slot_id)
        link.state = SchemaValues::link_state::loaded;

    if (link.state != SchemaValues::link_state::idle)
        link.color = tray.color;
    return link;
}

} // namespace

void Build(MachineObject* machine_obj, SchemaFormat::State& state)
{
    const auto& data = state.data;

    // Reaching here means BuildState found a machine with a filament system; the
    // no-printer case keeps the default false and hides the whole panel.
    state.display.visible = true;

    auto fila_system   = machine_obj ? machine_obj->GetFilaSystem() : nullptr;
    auto* extder_system = machine_obj ? machine_obj->GetExtderSystem() : nullptr;
    const int extruder_count = extder_system ? extder_system->GetTotalExtderCount() : 1;

    // 2D (laser / cut) mode turns every spool read-only, same as AMSControl does.
    const bool view_only = machine_obj && machine_obj->GetInfo() && !machine_obj->GetInfo()->IsFdmMode();

    // Both areas share one arrangement, so the strip and the cards stay in step.
    const auto layout = AmsControlWebLayout::Build(machine_obj, data, state.active_ams_ids,
                                                  state.selected_ams_id);
    std::set<std::string> active_ams_ids;
    for (const auto& panel : layout.panels) {
        if (!panel.active_ams_id.empty())
            active_ams_ids.insert(panel.active_ams_id);
    }

    auto& preview = state.display.ams_preview_area;
    auto& ams_ext = state.display.ams_ext_area;

    preview.visible = !data.ams_units.empty();
    preview.layout  = layout;

    ams_ext.visible = !data.ams_units.empty() || !data.ext_slots.empty();
    ams_ext.layout  = layout;

    for (const auto& unit : data.ams_units) {
        const bool active     = active_ams_ids.count(unit.ams_id) > 0;
        const int  slot_count = static_cast<int>(unit.trays.size());

        SchemaFormat::PreviewItem item;
        item.ams_id        = unit.ams_id;
        item.ams_type_name = unit.ams_type_name;
        item.active        = active;
        item.slot_count    = slot_count;
        for (const auto& tray : unit.trays) {
            SchemaFormat::PreviewCube cube;
            cube.color      = tray.color;
            cube.colors     = tray.colors;
            cube.color_type = tray.color_type;
            cube.is_exists  = tray.is_exists;
            item.cubes.push_back(std::move(cube));
        }
        preview.items.push_back(std::move(item));

        SchemaFormat::UnitView unit_view;
        unit_view.ams_id        = unit.ams_id;
        unit_view.ams_type_name = unit.ams_type_name;
        unit_view.active        = active;
        unit_view.slot_count    = slot_count;
        unit_view.humidity      = build_humidity_view(
            unit, fila_system ? fila_system->GetAmsById(unit.ams_id) : nullptr);
        for (const auto& tray : unit.trays)
            unit_view.slots.push_back(build_slot_view(tray, state, /*is_ext=*/false, view_only));
        ams_ext.units.push_back(std::move(unit_view));
    }

    // An external spool owns a cell of its own, so the strip needs an entry for it
    // to be switchable, exactly like AddAms does for the EXT_SPOOL info.
    for (const auto& tray : data.ext_slots) {
        SchemaFormat::PreviewItem item;
        item.ams_id        = tray.ams_id;
        item.ams_type_name = "EXT_SPOOL";
        item.active        = active_ams_ids.count(tray.ams_id) > 0;
        item.slot_count    = 1;

        SchemaFormat::PreviewCube cube;
        cube.color      = tray.color;
        cube.colors     = tray.colors;
        cube.color_type = tray.color_type;
        cube.is_exists  = tray.is_exists;
        item.cubes.push_back(std::move(cube));
        preview.items.push_back(std::move(item));

        ams_ext.ext_slots.push_back(build_slot_view(tray, state, /*is_ext=*/true, view_only));
    }

    // The machine only names a target slot while a load is actually running.
    std::string loading_ams_id;
    std::string loading_slot_id;
    if (extder_system && extder_system->IsBusyLoading()) {
        loading_ams_id  = extder_system->GetTargetAmsId();
        loading_slot_id = extder_system->GetTargetSlotId();
    }

    // Same order as the ext area, so a card and its line match by index too.
    auto& line   = state.display.filament_line_area;
    line.visible = ams_ext.visible;
    for (const auto& unit : data.ams_units) {
        for (const auto& tray : unit.trays)
            line.links.push_back(build_slot_link(tray, state, loading_ams_id, loading_slot_id));
    }
    for (const auto& tray : data.ext_slots)
        line.links.push_back(build_slot_link(tray, state, loading_ams_id, loading_slot_id));

    const auto& fila_switch = data.fila_switch;
    auto& switcher = state.display.switcher_area;
    // AMSControl only shows the switch icon when there are two extruders to
    // multiplex between, while the setup banner does not care.
    switcher.installed       = fila_switch.installed;
    switcher.ready           = fila_switch.ready;
    switcher.visible         = fila_switch.installed && extruder_count >= 2;
    switcher.show_setup_hint = fila_switch.installed && !fila_switch.ready;
    if (switcher.show_setup_hint)
        switcher.setup_hint = _u8L("AMS has not been initialized. Please initialize it before use.");

    auto& extruder   = state.display.extruder_area;
    extruder.visible = true;

    const int current_extruder_id = extder_system ? extder_system->GetCurrentExtderId() : MAIN_EXTRUDER_ID;
    std::optional<int> loading_extruder_id;
    if (extder_system && extder_system->IsBusyLoading())
        loading_extruder_id = extder_system->GetLoadingExtderId();

    auto build_extruder_view = [&](int id, bool has_filament, const std::string& color) {
        SchemaFormat::ExtruderView view;
        view.id           = id;
        view.has_filament = has_filament;
        if (has_filament)
            view.filament_color = color;
        if (loading_extruder_id.has_value() && *loading_extruder_id == id)
            view.state = SchemaValues::extruder_state::loading;
        else if (id == current_extruder_id)
            view.state = SchemaValues::extruder_state::active;
        return view;
    };

    if (extder_system && !extder_system->GetExtruders().empty()) {
        for (const auto& ext : extder_system->GetExtruders()) {
            const auto& slot = ext.GetSlotNow();
            const auto* tray = AmsControlWebData::FindTray(data, slot.ams_id, slot.slot_id);
            extruder.extruders.push_back(build_extruder_view(
                ext.GetExtId(), ext.HasFilamentInExt(), tray ? tray->color : std::string()));
        }
    } else {
        // Nothing to mirror: fall back to the slot resolved as loaded.
        const SchemaFormat::Tray* loaded = state.loaded_ams_id.empty()
            ? nullptr
            : AmsControlWebData::FindTray(data, state.loaded_ams_id, state.loaded_slot_id);
        extruder.extruders.push_back(build_extruder_view(
            MAIN_EXTRUDER_ID, loaded != nullptr, loaded ? loaded->color : std::string()));
    }
}

void NotifyNewRfidFilament(const std::string& ams_id, const std::string& slot_id)
{
    filament_mgr_hint_slots().emplace(ams_id, slot_id);
}

void DismissFilamentMgrHint(const std::string& ams_id, const std::string& slot_id)
{
    filament_mgr_hint_slots().erase({ams_id, slot_id});
}

}}} // namespace Slic3r::GUI::AmsControlWebDisplay
