#include "FilamentNozzlePopup.hpp"

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbmp.h>

#include "GUI_App.hpp"
#include "Plater.hpp"
#include "PresetComboBoxes.hpp"
#include "PartPlate.hpp"
#include "FilamentMapDialog.hpp"
#include "FilamentMapPanel.hpp"
#include "Widgets/StaticBox.hpp"
#include "Widgets/Label.hpp"
#include "wxExtensions.hpp"
#include "DeviceCore/DevNozzleSystem.h"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"

namespace Slic3r { namespace GUI {

namespace {

// Whether this slot's filament can actually print with a (diameter, flow) nozzle. Two independent
// conditions must hold: the printer preset must declare that extruder variant so print profiles
// exist, and the filament itself must declare support for that flow (filament_extruder_variant,
// the same is_nozzle_printable_for_filament check PartPlate.cpp validates a slice with).
//
// Deliberately NOT a test of whether the rack carries the nozzle: the list is built from the rack
// now, so every row is installed by construction and this only decides which of those rows are
// selectable rather than greyed out.
//
// Also mirrors the per-extruder filter rules used to populate ExtruderGroup::combo_flow
// (Plater.cpp, update_extruder_variant): E3DHighFlow only applies to 0.4/0.6 nozzles, and
// 0.2 nozzles have no HighFlow variant.
bool filament_supports_nozzle(const std::string &diameter,
                              NozzleVolumeType volume_type,
                              const std::vector<std::string> &filament_variants,
                              bool filament_variants_complete)
{
    if (volume_type == NozzleVolumeType::nvtE3DHighFlow && diameter != "0.4" && diameter != "0.6")
        return false;
    if (diameter == "0.2" && volume_type == NozzleVolumeType::nvtHighFlow)
        return false;
    if (!is_nozzle_printable_for_filament(volume_type, filament_variants, filament_variants_complete))
        return false;

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

FilamentNozzlePopup::FilamentNozzlePopup(wxWindow *parent) : PopupWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS)
{
    SetBackgroundColour(*wxWHITE);

    m_check_bmp = create_scaled_bitmap("checked", this, 16);

    // Rounded white panel per Figma node 7808:13592 (4px radius, #CECECE border);
    // StaticBox already defaults to this exact border color, set explicitly for clarity.
    m_panel = new StaticBox(this);
    m_panel->SetCornerRadius(4);
    m_panel->SetBorderWidth(1);
    m_panel->SetBorderColorNormal(wxColour(0xCE, 0xCE, 0xCE));
    m_panel->SetBackgroundColorNormal(*wxWHITE);
    m_panel->SetBackgroundColour(*wxWHITE);

    m_list_sizer = new wxBoxSizer(wxVERTICAL);
    auto *panel_sizer = new wxBoxSizer(wxVERTICAL);
    panel_sizer->Add(m_list_sizer, 1, wxEXPAND | wxALL, FromDIP(8));
    m_panel->SetSizer(panel_sizer);

    auto *top_sizer = new wxBoxSizer(wxVERTICAL);
    top_sizer->Add(m_panel, 1, wxEXPAND);
    SetSizer(top_sizer);
}

void FilamentNozzlePopup::rebuild_options()
{
    m_options.clear();
    m_selected = -1;

    if (m_filament_idx >= 0) {
        PresetBundle *preset_bundle = wxGetApp().preset_bundle;
        // Diameters this slot's filament has a preset for. Not the list's source any more -- only
        // one of the two things that decide whether a listed nozzle is selectable.
        const std::vector<std::string> supported_diameters = preset_bundle->get_filament_nozzle_diameter_options((size_t) m_filament_idx);

        const auto *selected = preset_bundle->project_config.option<ConfigOptionStrings>("filament_diameter_map");
        std::string current_diameter = (selected != nullptr && m_filament_idx < (int) selected->values.size())
            ? selected->values[m_filament_idx] : std::string();
        // Same fallback the sidebar's nozzle label applies (Sidebar::priv::refresh_filament_nozzle_label):
        // a legacy project, or a validate pass that ran with a single diameter installed, leaves this
        // slot's entry blank. The label still shows a diameter by falling back to the first option, so
        // reading the raw entry here matched no row and left the list with no checkmark at all, even
        // though the chip it was opened from was displaying one of them.
        if (current_diameter.empty() && !supported_diameters.empty())
            current_diameter = supported_diameters.front();

        NozzleVolumeType current_type = NozzleVolumeType::nvtStandard;
        if (PartPlate *plate = wxGetApp().plater()->get_partplate_list().get_curr_plate()) {
            const std::vector<int> volume_maps = plate->get_real_filament_volume_maps(preset_bundle->project_config);
            if (m_filament_idx < (int) volume_maps.size())
                current_type = (NozzleVolumeType) volume_maps[m_filament_idx];
        }

        // Restrict the offered flow types to what this filament itself declares support for
        // (same rule PartPlate.cpp uses to validate printability), not just what the printer's
        // nozzle can physically report -- otherwise e.g. TPU-only flow types would leak into the
        // list for a non-TPU filament, or vice versa.
        std::vector<std::string> filament_variants = {"Direct Drive Standard"};
        bool filament_variants_complete = false;
        if (m_filament_idx < (int) preset_bundle->filament_presets.size()) {
            const Preset *fil_preset = preset_bundle->filaments.find_preset(preset_bundle->filament_presets[m_filament_idx]);
            if (fil_preset != nullptr) {
                if (fil_preset->config.has("filament_extruder_variant"))
                    filament_variants = fil_preset->config.option<ConfigOptionStrings>("filament_extruder_variant")->values;
                filament_variants_complete = preset_bundle->filaments.is_bbl_brand_filament(*fil_preset);
            }
        }

        // The list is the printer's rack: every nozzle physically installed gets a row, whether or
        // not this filament can use it. Hybrid is not a nozzle the user picks, so it stays out.
        std::vector<std::pair<std::string, NozzleVolumeType>> rack;
        for (const auto &nozzle : preset_bundle->get_installed_nozzle_variants()) {
            if (nozzle.second != NozzleVolumeType::nvtHybrid)
                rack.push_back(nozzle);
        }
        // `installed` is keyed on the diameter as a string, so it orders "0.10" before "0.2".
        // Sort on the number instead, then on flow type, to get the natural 0.2/0.4/0.6/0.8 order.
        std::sort(rack.begin(), rack.end(), [](const auto &lhs, const auto &rhs) {
            const double l = atof(lhs.first.c_str()), r = atof(rhs.first.c_str());
            return l != r ? l < r : lhs.second < rhs.second;
        });

        for (const auto &[diameter, type] : rack) {
            const bool diameter_ok = std::find(supported_diameters.begin(), supported_diameters.end(), diameter) !=
                                     supported_diameters.end();
            const bool selectable  = diameter_ok &&
                                     filament_supports_nozzle(diameter, type, filament_variants, filament_variants_complete);
            // Only a selectable row can carry the checkmark: on a greyed one it would read as
            // "this is what you have" next to an entry the user is being told they cannot pick.
            // A slot left on an unusable nozzle shows no checkmark at all, and the red mark on the
            // filament row is what says something needs fixing.
            if (selectable && diameter == current_diameter && type == current_type)
                m_selected = (int) m_options.size();
            m_options.push_back({diameter, type, selectable});
        }
    }

    m_list_sizer->Clear(true);
    const wxSize check_size = m_check_bmp.IsOk() ? m_check_bmp.GetSize() : wxSize(FromDIP(16), FromDIP(16));
    // The checkmark sits flush against the row's left edge; the panel's own 8px padding is already
    // all the inset it needs.
    const int    check_left_margin = 0;

    for (size_t i = 0; i < m_options.size(); ++i) {
        if (i > 0)
            m_list_sizer->AddSpacer(FromDIP(6));

        const Option &opt = m_options[i];
        const bool is_selected = ((int) i == m_selected);

        auto *row = new wxWindow(m_panel, wxID_ANY);
        row->SetBackgroundColour(*wxWHITE);
        if (opt.selectable)
            row->SetCursor(wxCursor(wxCURSOR_HAND));

        auto *check = new wxStaticBitmap(row, wxID_ANY, is_selected ? m_check_bmp : wxNullBitmap, wxDefaultPosition, check_size);

        auto *label = new wxStaticText(row, wxID_ANY, wxString::FromUTF8(opt.diameter) + " " + DevNozzle::GetNozzleVolumeTypeStr(opt.volume_type));
        label->SetFont(Label::Body_14);
        label->SetForegroundColour(opt.selectable ? wxColour("#262E30") : wxColour("#909090"));

        // The checkmark is a real sizer item, and the right side gets a spacer of the same
        // footprint so the label still lands in the middle of the row. Positioning it by hand
        // outside the sizer (as this used to) reserved no width for it: the row measured only as
        // wide as its text, the centred label was laid out straight across the mark, and the
        // opaque label window won the sibling z-order on MSW and hid it -- while OSX, ordering
        // siblings the other way, left it showing and looking misplaced.
        // Left-aligned: the labels start on one vertical line right after the checkmark column, so
        // the list reads as a column of options rather than each row's text drifting with its own
        // width. The trailing stretch spacer is what pushes the leftover row width to the right.
        auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);
        row_sizer->Add(check, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, check_left_margin);
        row_sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));
        row_sizer->AddStretchSpacer(1);
        row_sizer->AddSpacer(FromDIP(6));
        row->SetSizer(row_sizer);

        // Greyed-out rows get no handler at all rather than one that checks and bails: with nothing
        // bound, the click is simply not consumed anywhere in the row.
        if (opt.selectable) {
            auto on_click = [this, i](wxMouseEvent &) { on_item_clicked(i); };
            row->Bind(wxEVT_LEFT_UP, on_click);
            check->Bind(wxEVT_LEFT_UP, on_click);
            label->Bind(wxEVT_LEFT_UP, on_click);
        }

        m_list_sizer->Add(row, 0, wxEXPAND);
    }

    GetSizer()->Layout();
    GetSizer()->SetSizeHints(this);

}

void FilamentNozzlePopup::tryPopup(int filament_idx)
{
    m_filament_idx = filament_idx;
    rebuild_options();
    wxGetApp().UpdateDarkUIWin(this);
    Popup();
}

void FilamentNozzlePopup::on_item_clicked(size_t option_idx)
{
    if (m_filament_idx < 0 || option_idx >= m_options.size())
        return;

    const Option &opt = m_options[option_idx];
    // Greyed-out rows are not wired up at all, so this is belt-and-braces against a later caller.
    if (!opt.selectable)
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

    if (opt.diameter != old_diameter) {
        if (!preset_bundle->set_filament_nozzle_diameter((size_t) m_filament_idx, opt.diameter)) {
            preset_bundle->filament_presets[m_filament_idx] = old_preset;
            selected->values[m_filament_idx] = old_diameter;
            if (m_filament_idx == 0)
                preset_bundle->filaments.select_preset_by_name(old_preset, false, true);
            rebuild_options();
            return;
        }
    }

    auto *global_volume_map = preset_bundle->project_config.option<ConfigOptionInts>("filament_volume_map");
    if (global_volume_map != nullptr) {
        if ((int) global_volume_map->values.size() <= m_filament_idx)
            global_volume_map->values.resize(m_filament_idx + 1, (int) NozzleVolumeType::nvtStandard);
        global_volume_map->values[m_filament_idx] = (int) opt.volume_type;

        for (PartPlate *plate : wxGetApp().plater()->get_partplate_list().get_plate_list()) {
            if (plate == nullptr)
                continue;
            std::vector<int> plate_volume_map = plate->get_filament_volume_maps();
            if (plate_volume_map.empty())
                continue; // no per-plate override: keep falling back to the (just-updated) global default
            if ((int) plate_volume_map.size() <= m_filament_idx)
                plate_volume_map.resize(m_filament_idx + 1, (int) NozzleVolumeType::nvtStandard);
            plate_volume_map[m_filament_idx] = (int) opt.volume_type;
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

    Dismiss();
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

    auto *mode_opt = preset_bundle->project_config.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode");
    if (mode_opt && mode_opt->value != fmmNozzleManual) mode_opt->value = fmmNozzleManual;

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
        plate->set_filament_map_mode(fmmNozzleManual);
        plate->set_filament_maps(filament_map_all_slots);
        plate->set_filament_volume_maps(volume_map_all_slots);
        if (built) plate->set_filament_nozzle_maps(nozzle_map_all_slots);
    }
}

}} // namespace Slic3r::GUI
