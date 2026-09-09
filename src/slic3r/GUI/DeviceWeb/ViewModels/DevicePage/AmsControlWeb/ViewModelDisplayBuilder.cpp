#include "ViewModelDisplayBuilder.hpp"
#include "ViewModelDataBuilder.hpp"
#include "ViewModelLayoutBuilder.hpp"

#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/DeviceCore/DevCalib.h"
#include "slic3r/GUI/DeviceCore/DevConfig.h"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevInfo.h"
#include "slic3r/Utils/CalibUtils.hpp"

#include <wx/string.h>

#include <cmath>
#include <exception>
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

SchemaFormat::MenuActions build_menu_actions(const SchemaFormat::Tray& tray,
                                             const std::string& slot_state,
                                             bool view_only)
{
    SchemaFormat::MenuActions actions;

    const bool has_spool = slot_state != SchemaValues::slot_state::empty &&
                           slot_state != SchemaValues::slot_state::none;
    if (!has_spool) {
        filament_mgr_hint_slots().erase({tray.ams_id, tray.slot_id});
        return actions;
    }

    actions.show_read = view_only || slot_state == SchemaValues::slot_state::brand;
    actions.show_edit = !actions.show_read;
    actions.show_filament_mgr_hint =
        filament_mgr_hint_slots().count({tray.ams_id, tray.slot_id}) > 0;
    return actions;
}

// Mirrors AMSLib::render_generic_text. Lite cards hide K in the Web view
// the same way m_show_kn is false for AMS_LITE.
void fill_slot_k(SchemaFormat::SlotView& view,
                 const SchemaFormat::Tray& tray,
                 MachineObject* machine_obj,
                 bool show_kn)
{
    if (!show_kn || !machine_obj || view.show_unknown || tray.fila_type.empty())
        return;

    float k = tray.k;
    bool  show_k_value = true;
    bool  k_loading    = false;
    auto* calib = machine_obj->GetCalib();
    auto* cfg   = machine_obj->GetConfig();

    if (tray.cali_idx == -1 ||
        (calib && CalibUtils::get_selected_calib_idx(calib->GetPAHistory(), tray.cali_idx) == -1)) {
        if (cfg && cfg->SupportCalibrationPA_FlowAuto()) {
            show_k_value = false;
        } else if (tray.cali_idx == -1) {
            show_k_value = false;
        } else if (calib && !calib->IsPAHistoryReady()) {
            show_k_value = false;
            k_loading    = true;
        } else {
            float n = tray.n;
            get_default_k_n_value(tray.setting_id, k, n);
        }
    } else if (std::fabs(k) < 1e-4f) {
        show_k_value = false;
    }

    if (show_k_value) {
        view.k_text = wxString::Format("K %1.3f", k).ToStdString();
    } else if (k_loading) {
        view.k_loading      = true;
        view.k_loading_text = _CTX_utf8(L_CONTEXT("loading", "AMS filament"), "AMS filament");
    }
}

SchemaFormat::SlotView build_slot_view(const SchemaFormat::Tray& tray,
                                       const SchemaFormat::State& state,
                                       MachineObject* machine_obj,
                                       bool is_ext,
                                       bool view_only,
                                       bool show_kn)
{
    SchemaFormat::SlotView view;
    view.ams_id    = tray.ams_id;
    view.slot_id   = tray.slot_id;
    view.selected  = tray.ams_id == state.selected_ams_id && tray.slot_id == state.selected_slot_id;
    // Dual-nozzle printers can load one slot per throat; state.loaded_* is only
    // the first of those (selection fallback), not the unique loaded slot.
    view.loaded    = AmsControlWebData::IsSlotLoaded(machine_obj, tray.ams_id, tray.slot_id);
    view.reading   = tray.reading;
    view.show_rfid = !is_ext;

    // Ring caption is transition_tridid(ams*4 + slot), not get_slot_name().
    if (is_ext) {
        view.label = tray.tray_label;
    } else {
        try {
            const int tray_id = std::stoi(tray.ams_id) * 4 + std::stoi(tray.slot_id);
            view.label = wxGetApp().transition_tridid(tray_id).ToStdString();
        } catch (const std::exception&) {
            view.label = tray.tray_label;
        }
    }

    const bool is_bbl = DevFilaSystem::IsBBL_Filament(tray.tag_uid);
    if (!tray.is_exists)
        view.slot_state = SchemaValues::slot_state::empty;
    // Ext slots stay third_brand so they keep the pencil instead of the read-only eye.
    else if (is_ext)
        view.slot_state = SchemaValues::slot_state::third_brand;
    else if (is_bbl && tray.info_ready)
        view.slot_state = SchemaValues::slot_state::brand;
    else
        view.slot_state = SchemaValues::slot_state::third_brand;

    // White "?" until colour and type are both ready, so a fresh spool does not
    // briefly show the colour of the one it replaced.
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

    view.show_remain = !is_ext && is_bbl && tray.info_ready && state.data.detect_remain_enabled;
    view.remain      = (view.show_remain && tray.remain >= 0 && tray.remain <= 100) ? tray.remain : 100;
    view.show_remain_height = false;

    // The capsule remains the default visualization; height fill is controlled
    // independently and stays disabled unless a future caller opts in.
    view.slot_remain_line.show_line      = view.show_remain && has_spool;
    view.slot_remain_line.remain_percent = view.remain >= 5 ? view.remain : 5;// the visual min val is 5

    view.menu_actions = build_menu_actions(tray, view.slot_state, view_only);
    fill_slot_k(view, tray, machine_obj, show_kn);
    return view;
}

// Match AMSinfo: unknown trays paint white, not leftover MQTT colour. Empty
// colour strings do not clear DevAmsTray::color (`UpdateColorFromStr` returns).
void fill_preview_cube(SchemaFormat::PreviewCube& cube, const SchemaFormat::Tray& tray)
{
    cube.is_exists = tray.is_exists;
    if (tray.is_exists && !tray.info_ready) {
        cube.color      = "#FFFFFF";
        cube.colors     = {cube.color};
        cube.color_type = 0;
        return;
    }
    cube.color      = tray.color;
    cube.colors     = tray.colors;
    cube.color_type = tray.color_type;
}

SchemaFormat::HumidityView build_humidity_view(const SchemaFormat::Unit& unit, DevAms* ams)
{
    SchemaFormat::HumidityView view;
    view.display_type = unit.humidity_display_type;
    view.level        = unit.humidity_level;
    view.percent      = unit.humidity_percent;

    view.support_drying = unit.ams_type == static_cast<int>(DevAmsType::N3F) ||
                          unit.ams_type == static_cast<int>(DevAmsType::N3S);

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

SchemaFormat::SlotLink build_slot_link(const SchemaFormat::Tray& tray,
                                      const SchemaFormat::State& state,
                                      MachineObject* machine_obj,
                                      const std::string& loading_ams_id,
                                      const std::string& loading_slot_id,
                                      bool is_ext)
{
    SchemaFormat::SlotLink link;
    link.ams_id        = tray.ams_id;
    link.slot_id       = tray.slot_id;
    link.switcher_port = switcher_port_for_slot(state.data.fila_switch, tray);
    link.extruder_ids  = tray.binded_extruder_ids;

    // AMSControl::ShowRoad: hide Ext roads as soon as the switch is installed.
    // Ready only gates the setup banner; plumbing already goes through the switch.
    if (is_ext && state.data.fila_switch.installed)
        link.extruder_ids.clear();

    if (!loading_ams_id.empty() && tray.ams_id == loading_ams_id && tray.slot_id == loading_slot_id)
        link.state = SchemaValues::link_state::loading;
    else if (AmsControlWebData::IsSlotLoaded(machine_obj, tray.ams_id, tray.slot_id))
        link.state = SchemaValues::link_state::loaded;

    // Colour the backbone from HasFilamentInExt (`loaded`), not IsBusyLoading:
    // snow != star can keep IsBusyLoading true with an empty throat.
    if (link.state == SchemaValues::link_state::loaded ||
        link.state == SchemaValues::link_state::loading ||
        link.state == SchemaValues::link_state::unloading)
        link.color = tray.color;
    return link;
}

// Same ams_mode as StatusPanel::update_ams: first connected AMS type when NP
// is on, otherwise f1 printers are AMS_LITE even with an empty AMS list.
DevAmsType resolve_ams_mode(MachineObject* obj)
{
    DevAmsType ams_mode = DevAmsType::AMS;
    if (!obj) return ams_mode;
    auto fila = obj->GetFilaSystem();
    if ((obj->is_enable_np || obj->is_enable_ams_np) && fila && !fila->GetAmsList().empty()) {
        if (const auto* ams = fila->GetAmsList().begin()->second)
            ams_mode = ams->GetAmsType();
    } else if (obj->get_printer_ams_type() == "f1") {
        ams_mode = DevAmsType::AMS_LITE;
    }
    return ams_mode;
}

// Mirrors AMSextruder::updateNozzleNum: dual uses left/right; single N-series
// uses single_nozzle_n, everything else single_nozzle_xp.
std::string extruder_icon_name(int extruder_count, int extruder_id, const std::string& series_name)
{
    if (extruder_count >= 2)
        return extruder_id == 1 ? SchemaValues::extruder_icon::left_nozzle
                                : SchemaValues::extruder_icon::right_nozzle;
    if (MachineObject::is_series_n(series_name))
        return SchemaValues::extruder_icon::single_nozzle_n;
    return SchemaValues::extruder_icon::single_nozzle_xp;
}

} // namespace

void Build(MachineObject* machine_obj, SchemaFormat::State& state)
{
    const auto& data = state.data;

    state.display.visible = true;

    auto fila_system   = machine_obj ? machine_obj->GetFilaSystem() : nullptr;
    auto* extder_system = machine_obj ? machine_obj->GetExtderSystem() : nullptr;
    const int extruder_count = extder_system ? extder_system->GetTotalExtderCount() : 1;

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

    ams_ext.visible    = !data.ams_units.empty() || !data.ext_slots.empty();
    ams_ext.lite_style = resolve_ams_mode(machine_obj) == DevAmsType::AMS_LITE;
    ams_ext.layout     = layout;

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
            fill_preview_cube(cube, tray);
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
            unit_view.slots.push_back(build_slot_view(
                tray, state, machine_obj, /*is_ext=*/false, view_only,
                /*show_kn=*/unit.ams_type_name != "AMS_LITE"));
        ams_ext.units.push_back(std::move(unit_view));
    }

    for (const auto& tray : data.ext_slots) {
        SchemaFormat::PreviewItem item;
        item.ams_id        = tray.ams_id;
        item.ams_type_name = "EXT_SPOOL";
        item.active        = active_ams_ids.count(tray.ams_id) > 0;
        item.slot_count    = 1;

        SchemaFormat::PreviewCube cube;
        fill_preview_cube(cube, tray);
        item.cubes.push_back(std::move(cube));
        preview.items.push_back(std::move(item));

        ams_ext.ext_slots.push_back(build_slot_view(
            tray, state, machine_obj, /*is_ext=*/true, view_only, /*show_kn=*/true));
    }

    // GetTargetAmsId can stay on the last target (often Ext "255") while the
    // throat is empty; only treat it as loading when that extruder has filament.
    std::string loading_ams_id;
    std::string loading_slot_id;
    if (extder_system && extder_system->IsBusyLoading()) {
        bool filament_in_loading_ext = false;
        if (const auto loading_id = extder_system->GetLoadingExtderId()) {
            if (auto ext = extder_system->GetExtderById(*loading_id))
                filament_in_loading_ext = ext->HasFilamentInExt();
        }
        if (filament_in_loading_ext) {
            loading_ams_id  = extder_system->GetTargetAmsId();
            loading_slot_id = extder_system->GetTargetSlotId();
        }
    }

    auto& line   = state.display.filament_line_area;
    line.visible = ams_ext.visible;
    for (const auto& unit : data.ams_units) {
        for (const auto& tray : unit.trays)
            line.links.push_back(build_slot_link(tray, state, machine_obj, loading_ams_id, loading_slot_id, /*is_ext=*/false));
    }
    for (const auto& tray : data.ext_slots)
        line.links.push_back(build_slot_link(tray, state, machine_obj, loading_ams_id, loading_slot_id, /*is_ext=*/true));

    const auto& fila_switch = data.fila_switch;
    auto& switcher = state.display.switcher_area;
    // Switch icon needs two extruders; the setup banner does not.
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

    const std::string series_name = machine_obj ? machine_obj->get_printer_series_str() : std::string();

    auto build_extruder_view = [&](int id, bool has_filament, const std::string& color) {
        SchemaFormat::ExtruderView view;
        view.id           = id;
        view.has_filament = has_filament;
        view.icon         = extruder_icon_name(extruder_count, id, series_name);
        if (has_filament)
            view.filament_color = color;
        if (has_filament && loading_extruder_id.has_value() && *loading_extruder_id == id)
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
