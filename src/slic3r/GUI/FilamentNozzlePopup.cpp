#include "FilamentNozzlePopup.hpp"

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>

#include <wx/event.h>
#include <wx/sizer.h>

#include "ExtruderHelpers.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Plater.hpp"
#include "PresetComboBoxes.hpp"
#include "PartPlate.hpp"
#include "FilamentMapDialog.hpp"
#include "FilamentMapPanel.hpp"
#include "Widgets/CascadeMenu.hpp"
#include "Widgets/Label.hpp"
#include "wxExtensions.hpp"
#include "DeviceCore/DevNozzleSystem.h"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/LocalesUtils.hpp"

namespace Slic3r { namespace GUI {

namespace {

// The two reasons a listed nozzle cannot be picked. Both render the same way; only the message
// differs, because only one of them is something the user can go and fix.
wxString missing_nozzle_tip() { return _L("To pick this, change the nozzle settings first"); }
wxString unusable_nozzle_tip() { return _L("The filament in this slot cannot print with this nozzle"); }

// Whether the printer is sold with a nozzle of this flow type, going by the preset that carries
// `diameter` -- i.e. whether print profiles exist for that extruder variant at all. Says nothing
// about whether one is installed, nor about whether a given filament can print with it.
bool printer_offers_flow(const std::string &diameter, NozzleVolumeType volume_type)
{
    PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    const Preset *printer_preset = preset_bundle->get_similar_printer_preset({}, diameter);
    if (printer_preset == nullptr)
        return false;

    auto *extruder_variants = printer_preset->config.option<ConfigOptionStrings>("extruder_variant_list");
    auto *extruders_def      = printer_preset->config.def()->get("extruder_type");
    auto *extruders          = printer_preset->config.option<ConfigOptionEnumsGeneric>("extruder_type");
    auto *nozzle_volumes_def = preset_bundle->project_config.def()->get("nozzle_volume_type");
    if (extruder_variants == nullptr || extruders_def == nullptr || extruders == nullptr || nozzle_volumes_def == nullptr)
        return false;

    for (size_t idx = 0; idx < extruder_variants->values.size() && idx < extruders->values.size(); ++idx) {
        const std::string type = extruders_def->enum_labels[extruders->values[idx]];
        for (size_t j = 0; j < nozzle_volumes_def->enum_labels.size(); ++j) {
            if ((NozzleVolumeType) nozzle_volumes_def->enum_keys_map->at(nozzle_volumes_def->enum_values[j]) != volume_type)
                continue;
            if (boost::algorithm::contains(extruder_variants->values[idx], type + " " + nozzle_volumes_def->enum_labels[j]))
                return true;
        }
    }
    return false;
}

} // anonymous namespace

FilamentNozzlePopup::FilamentNozzlePopup(wxWindow *parent) : CascadeMenuPopup(parent)
{
    // One flow menu for the life of this popup, refilled as the pointer moves between diameters,
    // exactly as DropDown reuses its own submenu. Destroying a popup and building a new one on every
    // hover does not work: Destroy() on a top-level window only queues the deletion for idle time,
    // so the outgoing window is usually still alive -- and still holding wx's popup handlers -- when
    // the incoming one pops. Parented on the owning window rather than on this popup, because a
    // transient popup parented on another transient popup goes down with it.
    m_sub_menu = new CascadeMenuPopup(parent);
    m_sub_menu->Hide(); // the guard on this level keys off this, so start it in a known state
    m_sub_menu->SetMainMenu(this);
    SetSubMenu(m_sub_menu);
}

// Everything the row builders need about the slot, read once per popup rather than re-derived per row.
void FilamentNozzlePopup::read_slot_state()
{
    m_printer_diameters.clear();
    m_rack.clear();
    m_filament_diameters.clear();
    m_filament_variants = {"Direct Drive Standard"};
    m_filament_variants_complete = false;
    m_diameter.clear();
    m_volume = NozzleVolumeType::nvtStandard;
    if (m_filament_idx < 0)
        return;

    PresetBundle *preset_bundle = wxGetApp().preset_bundle;

    // Every diameter this printer is sold in, not just the ones on the rack: a diameter the user
    // could install but has not is exactly what the faded rows are there to show.
    m_printer_diameters = preset_bundle->printers.diameters_of_selected_printer();

    // Hybrid is a slicing construct rather than a nozzle anyone installs, so it never reaches a row.
    for (const auto &nozzle : preset_bundle->get_installed_nozzle_variants()) {
        if (nozzle.second != NozzleVolumeType::nvtHybrid)
            m_rack.insert(nozzle);
    }

    // What this slot can actually be set to: the diameters the rack carries, narrowed to those this
    // slot's filament has a preset for. Already a subset of the rack, so a diameter missing from it
    // can mean either reason -- which is why the row builder checks the rack first.
    m_filament_diameters = preset_bundle->get_filament_nozzle_diameter_options((size_t) m_filament_idx);

    const auto *selected = preset_bundle->project_config.option<ConfigOptionStrings>("filament_diameter_map");
    m_diameter = (selected != nullptr && m_filament_idx < (int) selected->values.size())
        ? selected->values[m_filament_idx] : std::string();
    // Same fallback the sidebar's nozzle label applies (Sidebar::priv::refresh_filament_nozzle_label):
    // a legacy project, or a validate pass that ran with a single diameter installed, leaves this
    // slot's entry blank. The label still shows a diameter by falling back to the first option, so
    // reading the raw entry here matched no row and left the list with no checkmark at all, even
    // though the chip it was opened from was displaying one of them.
    if (m_diameter.empty() && !m_filament_diameters.empty())
        m_diameter = m_filament_diameters.front();

    if (PartPlate *plate = wxGetApp().plater()->get_partplate_list().get_curr_plate()) {
        const std::vector<int> volume_maps = plate->get_real_filament_volume_maps(preset_bundle->project_config);
        if (m_filament_idx < (int) volume_maps.size())
            m_volume = (NozzleVolumeType) volume_maps[m_filament_idx];
    }

    // Restrict the offered flow types to what this filament itself declares support for
    // (same rule PartPlate.cpp uses to validate printability), not just what the printer's
    // nozzle can physically report -- otherwise e.g. TPU-only flow types would leak into the
    // list for a non-TPU filament, or vice versa.
    if (m_filament_idx < (int) preset_bundle->filament_presets.size()) {
        const Preset *fil_preset = preset_bundle->filaments.find_preset(preset_bundle->filament_presets[m_filament_idx]);
        if (fil_preset != nullptr) {
            if (fil_preset->config.has("filament_extruder_variant"))
                m_filament_variants = fil_preset->config.option<ConfigOptionStrings>("filament_extruder_variant")->values;
            m_filament_variants_complete = preset_bundle->filaments.is_bbl_brand_filament(*fil_preset);
        }
    }
}

std::vector<FilamentNozzlePopup::FlowRow> FilamentNozzlePopup::flow_rows_for(const std::string &diameter) const
{
    std::vector<FlowRow> rows;

    PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    auto         *def           = preset_bundle->project_config.def()->get("nozzle_volume_type");
    if (def == nullptr)
        return rows;

    for (size_t j = 0; j < def->enum_labels.size(); ++j) {
        const auto volume = (NozzleVolumeType) def->enum_keys_map->at(def->enum_values[j]);
        if (volume == NozzleVolumeType::nvtHybrid)
            continue;
        // Left out rather than greyed: a type the printer is not sold with, and a combination no
        // nozzle is made in, are both things the user can do nothing about, so the row would only
        // be noise -- and worse, it would send them to the nozzle settings to look for something
        // that is not there.
        if (!printer_offers_flow(diameter, volume) || !nozzle_variant_exists(diameter, volume))
            continue;

        FlowRow row;
        row.volume    = volume;
        row.label     = _L(def->enum_labels[j]);
        row.installed = m_rack.count({diameter, volume}) > 0;
        row.usable    = is_nozzle_printable_for_filament(volume, m_filament_variants, m_filament_variants_complete);
        rows.push_back(row);
    }
    return rows;
}

void FilamentNozzlePopup::open_flow_menu(const std::string &diameter, const wxWindow *anchor)
{
    auto *sub = m_sub_menu;
    if (sub == nullptr)
        return;

    // Refill in place. The sizer goes before its items do, so it never holds dead rows.
    sub->SetSizer(nullptr);
    sub->DestroyChildren();
    auto *sub_box = new wxBoxSizer(wxVERTICAL);

    bool first = true;
    for (const auto &flow : flow_rows_for(diameter)) {
        // Only a pickable row can carry the checkmark: on an unavailable one it would read as
        // "this is what you have" next to an entry the user is being told they cannot pick. A slot
        // left on an unusable nozzle shows no checkmark at all, and the red mark on the filament
        // row is what says something needs fixing.
        const bool pickable = flow.installed && flow.usable;
        const bool checked  = pickable && diameter == m_diameter && flow.volume == m_volume;

        // Greyed label only, no fade: a second-level row is just text, so that is the design's
        // rendering of the state here. The message is what tells the two reasons apart.
        auto *item = new CascadeMenuItem(sub, flow.label, pickable, checked, false);
        if (!pickable)
            item->SetTip(flow.installed ? unusable_nozzle_tip() : missing_nozzle_tip());

        if (pickable)
            // apply_pick closes both levels itself, at the point in its write-back sequence where
            // the sidebar expects the menu to be gone.
            item->Bind(wxEVT_BUTTON, [this, diameter, volume = flow.volume](wxCommandEvent &) { apply_pick(diameter, volume); });

        CascadeMenu::AddItem(sub_box, item, first);
        first = false;
    }
    CascadeMenu::Finish(sub, sub_box);

    CascadeMenu::PositionSubMenu(sub, this, anchor);
    if (!sub->IsShown())
        sub->Popup();
    else
        sub->Refresh();
}

// Diameters at the first level, each opening the flow types that diameter is made in.
void FilamentNozzlePopup::build_menu()
{
    // Refilled in place for the same reason the flow menu is: this popup outlives a single gesture.
    SetSizer(nullptr);
    DestroyChildren();
    auto *sizer = new wxBoxSizer(wxVERTICAL);

    bool first = true;
    for (const auto &diameter : m_printer_diameters) {
        // Two reasons a diameter can be unavailable, told apart by what the row says on hover: the
        // nozzle settings not carrying one is something the user can go and change, while a
        // filament with no preset at that diameter is a fact about the filament. The settings are
        // checked first because m_filament_diameters is already narrowed to them, so a diameter
        // missing from that list alone does not say which of the two it is.
        const bool installed = std::any_of(m_rack.begin(), m_rack.end(),
                                           [&diameter](const auto &n) { return n.first == diameter; });
        const bool pickable  = installed && std::find(m_filament_diameters.begin(), m_filament_diameters.end(), diameter) !=
                                                m_filament_diameters.end();

        auto *item = new CascadeMenuItem(this, wxString::FromUTF8(diameter) + " mm", pickable, pickable && diameter == m_diameter, true);
        if (!pickable) {
            // Faded whole-row, the design's rendering for a level that also carries a tick column
            // and a chevron.
            item->SetDimmed(true);
            item->SetTip(installed ? unusable_nozzle_tip() : missing_nozzle_tip());
        }

        // Every diameter the printer supports opens its flow menu, including a faded diameter that
        // is absent from the nozzle settings. Its flow rows remain disabled, but rebuilding the
        // submenu tells the user which nozzle types that diameter offers and, importantly, replaces
        // the stale submenu belonging to the previously hovered diameter.
        auto open = [this, diameter, item] { open_flow_menu(diameter, item); };
        item->Bind(wxEVT_ENTER_WINDOW, [open](wxMouseEvent &e) {
            open();
            e.Skip();
        });
        item->Bind(wxEVT_LEFT_DOWN, [open](wxMouseEvent &) { open(); });

        CascadeMenu::AddItem(sizer, item, first);
        first = false;
    }
    CascadeMenu::Finish(this, sizer);
}

void FilamentNozzlePopup::tryPopup(int filament_idx)
{
    m_filament_idx = filament_idx;
    read_slot_state();
    build_menu();
    // No UpdateDarkUIWin pass here, unlike the flat list this replaced: RoundedPopupWindow paints
    // its own fill from GetBackgroundColour() through the dark-mode map, so letting the generic
    // walker rewrite that colour would map an already-mapped one a second time.
    Popup();
}

// Order matters: this level refuses to dismiss while the flow menu is still up, so release the
// guard and close that one first, then put the guard back for the next gesture.
void FilamentNozzlePopup::close_menus()
{
    SetSubMenu(nullptr);
    if (m_sub_menu)
        m_sub_menu->Dismiss();
    Dismiss();
    SetSubMenu(m_sub_menu);
}

void FilamentNozzlePopup::apply_pick(const std::string &diameter, NozzleVolumeType volume)
{
    if (m_filament_idx < 0)
        return;

    PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    if (m_filament_idx >= (int) preset_bundle->filament_presets.size())
        return;

    auto *selected = preset_bundle->project_config.option<ConfigOptionStrings>("filament_diameter_map");
    if (selected == nullptr)
        return;
    selected->values.resize(preset_bundle->filament_presets.size());

    const std::string old_preset   = preset_bundle->filament_presets[m_filament_idx];
    const std::string old_diameter = selected->values[m_filament_idx];

    if (diameter != old_diameter) {
        if (!preset_bundle->set_filament_nozzle_diameter((size_t) m_filament_idx, diameter)) {
            preset_bundle->filament_presets[m_filament_idx] = old_preset;
            selected->values[m_filament_idx] = old_diameter;
            if (m_filament_idx == 0)
                preset_bundle->filaments.select_preset_by_name(old_preset, false, true);
            // Nothing changed, so leaving the menu up would invite the user to try the same row
            // again. Close it instead and let them reopen against the restored state.
            close_menus();
            return;
        }
    }

    auto *global_volume_map = preset_bundle->project_config.option<ConfigOptionInts>("filament_volume_map");
    if (global_volume_map != nullptr) {
        if ((int) global_volume_map->values.size() <= m_filament_idx)
            global_volume_map->values.resize(m_filament_idx + 1, (int) NozzleVolumeType::nvtStandard);
        global_volume_map->values[m_filament_idx] = (int) volume;

        for (PartPlate *plate : wxGetApp().plater()->get_partplate_list().get_plate_list()) {
            if (plate == nullptr)
                continue;
            std::vector<int> plate_volume_map = plate->get_filament_volume_maps();
            if (plate_volume_map.empty())
                continue; // no per-plate override: keep falling back to the (just-updated) global default
            if ((int) plate_volume_map.size() <= m_filament_idx)
                plate_volume_map.resize(m_filament_idx + 1, (int) NozzleVolumeType::nvtStandard);
            plate_volume_map[m_filament_idx] = (int) volume;
            plate->set_filament_volume_maps(plate_volume_map);
        }
    }

    apply_nozzle_manual_maps_to_plates();

    Plater *plater = wxGetApp().plater();
    plater->sidebar().refresh_filament_diameter_buttons(true);
    plater->update_project_dirty_from_presets();
    preset_bundle->export_selections(*wxGetApp().app_config);
    plater->on_config_change(preset_bundle->full_config());

    // Rebuild this slot's combo so its synced badge is re-derived against the diameter/flow just
    // written above: add_ams_filaments_grouped_by_nozzle compares the tray to the slot's live
    // selection, so the badge drops when the new combination isn't in the rack and comes back if the
    // user returns the slot to one that is.
    auto &combos = plater->sidebar().combos_filament();
    if ((size_t) m_filament_idx < combos.size() && combos[m_filament_idx] != nullptr)
        combos[m_filament_idx]->update();

    close_menus();
    // on_config_change only rebuilds mixed rows on colour/type/support diffs.
    // Diameter/volume edits must refresh chips and broken-diameter marks here.
    plater->sidebar().update_mixed_filament_list(false);
}

void apply_nozzle_manual_maps_to_plates()
{
    PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle == nullptr) return;
    // Gated on is_single_ext_multi_nozzle: the all-1s filament_map / nozzle_map derivation below is
    // only correct there. Dual-extruder machines have their own filament_map semantics handled by
    // FilamentMapDialog, so this must NOT run for them.
    // Also gated on multi_nozzle_diameter_mode: with the toggle off the per-slot selector is hidden
    // and fmmNozzleManual isn't the authoritative mode, so forcing it here would clobber whatever
    // filament_map_mode the user set via FilamentMapDialog for that (non-multi-diameter) state.
    if (!is_single_ext_multi_nozzle(preset_bundle->full_config())) return;
    if (!multi_nozzle_diameter_mode_enabled(preset_bundle->project_config)) return;
    
    //auto *mode_opt = preset_bundle->project_config.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode");
    //if (mode_opt && mode_opt->value != fmmNozzleManual) mode_opt->value = fmmNozzleManual;
    const auto     full_config = preset_bundle->full_config();
    const size_t   n_slots     = preset_bundle->filament_presets.size();
    const auto    *g_vol_map   = preset_bundle->project_config.option<ConfigOptionInts>("filament_volume_map");

    std::vector<int> volume_map_all_slots(n_slots, (int) NozzleVolumeType::nvtStandard);
    if (g_vol_map)
        for (size_t i = 0; i < n_slots && i < g_vol_map->values.size(); ++i)
            volume_map_all_slots[i] = g_vol_map->values[i];

    // Per-slot nozzle_map: match each slot's selected diameter (+ compatible volume) to a specific
    // physical nozzle. Required because at slice time (ToolOrdering.cpp:2278-2282) each filament
    // MUST point to a nozzle whose diameter equals filament_diameter_map[i], else
    // "Nozzle assignment error: per-filament nozzle map points to a nozzle with a different diameter."
    // Simple all-0 (or a coarse AMS-vs-fixed group id) fails this check for slots whose selected
    // diameter isn't 0.4mm on a 6x0.4 + 0.2/0.6/0.8 rack.
    std::vector<int> filament_map_all_slots(n_slots, 1); // single logical extruder -> all 1s
    std::vector<int> nozzle_map_all_slots(n_slots, 0);
    const int   ams_idx = get_effective_ams_nozzle_idx_for_active_diameter(full_config);
    const auto *nd_flat = full_config.option<ConfigOptionFloatsNullable>("extruder_nozzle_diameter");
    const auto *vt_flat = full_config.option<ConfigOptionIntsNullable>("extruder_nozzle_volume_type");
    const auto *sel_diams = preset_bundle->project_config.option<ConfigOptionStrings>("filament_diameter_map");

    auto volume_compatible = [](int nozzle_v, int slot_v) {
        return nozzle_v == (int) nvtHybrid || slot_v == (int) nvtHybrid || nozzle_v == slot_v;
    };

    if (nd_flat && !nd_flat->values.empty()) {
        const size_t phys_cnt = nd_flat->values.size();
        for (size_t i = 0; i < n_slots; ++i) {
            const std::string slot_d = (sel_diams && i < sel_diams->values.size()) ? sel_diams->values[i] : std::string();
            const int         slot_v = (i < volume_map_all_slots.size()) ? volume_map_all_slots[i] : (int) nvtStandard;
            int matched = -1;
            // 1) Prefer the AMS nozzle if this slot's diameter matches it.
            if (ams_idx >= 0 && (size_t) ams_idx < phys_cnt) {
                const std::string phys_d = format_diameter_to_str(nd_flat->values[ams_idx]);
                const int         phys_v = (vt_flat && (size_t) ams_idx < vt_flat->values.size()) ? vt_flat->values[ams_idx] : (int) nvtStandard;
                if (phys_d == slot_d && volume_compatible(phys_v, slot_v))
                    matched = ams_idx;
            }
            // 2) Else pick the first non-AMS physical nozzle whose diameter (+ volume) matches.
            if (matched < 0) {
                for (size_t n = 0; n < phys_cnt; ++n) {
                    if ((int) n == ams_idx) continue;
                    const std::string phys_d = format_diameter_to_str(nd_flat->values[n]);
                    const int         phys_v = (vt_flat && n < vt_flat->values.size()) ? vt_flat->values[n] : (int) nvtStandard;
                    if (phys_d == slot_d && volume_compatible(phys_v, slot_v)) { matched = (int) n; break; }
                }
            }
            // 3) Diameter-only fallback (ignore volume) so we never leave a stale 0 that a slice
            //    would reject as "different diameter" -- even if downstream still rejects the volume.
            if (matched < 0) {
                for (size_t n = 0; n < phys_cnt; ++n) {
                    if (format_diameter_to_str(nd_flat->values[n]) == slot_d) { matched = (int) n; break; }
                }
            }
            nozzle_map_all_slots[i] = matched >= 0 ? matched : 0;
        }
    }
    const bool built = true; // we always populate nozzle_map_all_slots above (may be all 0s if no phys nozzle info)

    // Also sync the project-level maps to filament_presets.size(). validate_filament_nozzle_diameter_selections
    // already handles filament_diameter_map, but filament_volume_map / filament_nozzle_map /
    // filament_map can lag behind when filament slots are added -- persisted project_settings.config
    // would then show shorter arrays than what plate metadata carries, which is confusing when inspecting
    // the 3mf. Resize + rewrite to keep them consistent.
    if (auto *p_fmap = preset_bundle->project_config.option<ConfigOptionInts>("filament_map", true))
        p_fmap->values = filament_map_all_slots;
    if (auto *p_vmap = preset_bundle->project_config.option<ConfigOptionInts>("filament_volume_map", true))
        p_vmap->values = volume_map_all_slots;
    if (built) {
        if (auto *p_nmap = preset_bundle->project_config.option<ConfigOptionInts>("filament_nozzle_map", true))
            p_nmap->values = nozzle_map_all_slots;
    }

    for (PartPlate *plate : wxGetApp().plater()->get_partplate_list().get_plate_list()) {
        if (plate == nullptr) continue;
        // Which flow a slot prints on is not a grouping result -- it is picked in the nozzle popup or
        // taken from the AMS tray during a sync, and automatic grouping never rewrites it. So it is
        // pushed to every plate that carries a per-slot copy, whatever mode the plate is in. Leaving
        // it to the branch below meant a plate in an automatic mode kept a stale flow forever: the
        // sidebar chip reads the per-plate copy while everything else reads the project one, so the
        // row went on naming a nozzle the slot had already moved off.
        if (!plate->get_filament_volume_maps().empty())
            plate->set_filament_volume_maps(volume_map_all_slots);

        // filament_map / nozzle_map are a different matter: in an automatic mode the slicer computes
        // and writes them back (see BackgroundSlicingProcess), so re-pushing them on every sidebar
        // refresh would fight that write-back. Only plates parked in the manual mode take them.
        if (plate->get_filament_map_mode() != fmmNozzleManual) continue;
        plate->set_filament_maps(filament_map_all_slots);
        plate->set_filament_volume_maps(volume_map_all_slots);
        if (built) plate->set_filament_nozzle_maps(nozzle_map_all_slots);
    }
}

void sync_custom_map_mode_with_diameter_mode()
{
    PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    Plater       *plater        = wxGetApp().plater();
    if (preset_bundle == nullptr || plater == nullptr) return;

    // Nozzle Manual is only the spelling of custom grouping on a single-extruder multi-nozzle
    // machine with multi-diameter on. Leaving that machine, or turning the gate off, must spell
    // it Manual again. The maps are left alone: set_filament_map_mode() would clear them.
    const bool multi_diameter = is_single_ext_multi_nozzle(preset_bundle->full_config()) && multi_nozzle_diameter_mode_enabled(preset_bundle->project_config);
    const FilamentMapMode from = multi_diameter ? fmmManual : fmmNozzleManual;
    const FilamentMapMode to   = multi_diameter ? fmmNozzleManual : fmmManual;

    if (auto *mode_opt = preset_bundle->project_config.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode");
        mode_opt != nullptr && mode_opt->value == from)
        mode_opt->value = to;

    for (PartPlate *plate : plater->get_partplate_list().get_plate_list()) {
        if (plate == nullptr) continue;
        auto *mode_opt = plate->config()->option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode");
        if (mode_opt == nullptr || mode_opt->value != from) continue;
        // 不走 set_filament_map_mode()：它发现模式变了会清空三个 map，而这里只是换个写法。
        mode_opt->value = to;
    }
}

}} // namespace Slic3r::GUI
