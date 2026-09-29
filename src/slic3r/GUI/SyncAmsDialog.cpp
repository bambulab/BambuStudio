#include "SyncAmsDialog.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <wx/bitmap.h>
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/gdicmn.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>

#include "CapsuleButton.hpp"
#include "ExtruderHelpers.hpp"          // slot_name
#include "ModelPreview.hpp"
#include "GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"
#include "I18N.hpp"
#include "PartPlate.hpp"                // PartPlate::get_extruders for a plate's used filaments
#include "Plater.hpp"                   // get_partplate_list
#ifdef __WINDOWS__
#include "dark_mode.hpp"                // NppDarkMode::SetDarkTitleBar
#endif
#include "wxExtensions.hpp"             // create_scaled_bitmap
#include "DeviceCore/DevManager.h"      // selected machine
#include "DeviceCore/DevMapping.h"      // ams_filament_mapping
#include "DeviceCore/DevFilaSystem.h"   // DevAmsTray::decode_color, for the mapped preview colours
#include "DeviceCore/DevNozzleSystem.h" // DevNozzle flow-type strings, for the source volume type
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/AmsSlotPopup.hpp" // AmsSlotPopup, SlotSelectionState
#include "Widgets/Label.hpp"
#include "Widgets/MappingRow.hpp"
#include "Widgets/PillView.hpp"
#include "Widgets/RoundedRectangle.hpp" // shared fill+border card surface
#include "Widgets/StateColor.hpp"
#include "libslic3r/PresetBundle.hpp" // project filaments + config, for the source list
#include "libslic3r/PrintConfig.hpp"  // nozzle diameter/volume options + primary_nozzle_diameter
#include "libslic3r/ProjectTask.hpp"  // FilamentInfo

namespace Slic3r::GUI {

namespace {

/**
 * \brief The number of plates, or 0 when unavailable.
 */
int preview_plate_count()
{
    Plater *plater = wxGetApp().plater();
    return plater ? plater->get_partplate_list().get_plate_count() : 0;
}

/**
 * \brief The editor's current plate index, or 0 when unavailable.
 */
int preview_current_plate_index()
{
    Plater *plater = wxGetApp().plater();
    return plater ? plater->get_partplate_list().get_curr_plate_index() : 0;
}

/**
 * \brief The nozzle diameter a project filament prints with: its own selected diameter in
 *        multi-diameter mode, otherwise the machine's single nozzle diameter.
 *
 * \param cfg       the full print config
 * \param extruder  0-based project filament id
 */
double source_diameter_for(const DynamicPrintConfig &cfg, int extruder)
{
    if (auto *sel = cfg.option<ConfigOptionStrings>("filament_diameter_map")) {
        if (extruder >= 0 && extruder < (int) sel->values.size()) {
            const std::string &s = sel->values[extruder];
            if (!s.empty() && s.find(';') == std::string::npos) {
                const double d = std::atof(s.c_str());
                if (d > 0.0) return d;
            }
        }
    }
    if (auto *nd = cfg.option<ConfigOptionFloatsNullable>("nozzle_diameter"))
        if (!nd->values.empty()) return nd->values.front();
    return primary_nozzle_diameter(cfg);
}

/**
 * \brief The nozzle volume type ("Standard" / "High Flow") a project filament prints with, or empty
 *        when unresolved.
 *
 * \param cfg       the full print config
 * \param extruder  0-based project filament id
 */
std::string source_volume_for(const DynamicPrintConfig &cfg, int extruder)
{
    if (auto *fvm = cfg.option<ConfigOptionInts>("filament_volume_map")) {
        if (extruder >= 0 && extruder < (int) fvm->values.size()) {
            const auto vt = (NozzleVolumeType) fvm->values[extruder];
            return DevNozzle::GetNozzleFlowTypeString(DevNozzle::ToNozzleFlowType(vt));
        }
    }
    auto *nvt = cfg.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
    if (!nvt || nvt->values.empty()) return "";
    const double dia = source_diameter_for(cfg, extruder);
    auto        *nd  = cfg.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    size_t       idx = 0;
    if (nd && nd->values.size() == nvt->values.size())
        for (size_t i = 0; i < nd->values.size(); ++i)
            if (std::fabs(nd->values[i] - dia) < 1e-3) {
                idx = i;
                break;
            }
    const auto vt = (NozzleVolumeType) nvt->values[idx];
    return DevNozzle::GetNozzleFlowTypeString(DevNozzle::ToNozzleFlowType(vt));
}

/**
 * \brief The mapping source list: one FilamentInfo per project filament the scope uses, with its
 *        nozzle diameter and volume type. Mixed filaments are skipped, since they have no physical spool.
 *
 * \param plate_idx  a plate index; < 0 (the All sentinel) for every project filament
 */
std::vector<FilamentInfo> collect_scope_filaments(int plate_idx)
{
    std::vector<FilamentInfo> out;
    auto                     *preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle) return out;

    // Per-project-filament metadata, indexed by 0-based project filament id.
    std::vector<std::string> types, brands, filament_ids;
    for (auto &filament_name : preset_bundle->filament_presets) {
        for (int i = 0; i < (int) preset_bundle->filaments.size(); ++i) {
            Preset *preset = &preset_bundle->filaments.preset(i);
            if (!preset || filament_name.compare(preset->name) != 0) continue;
            std::string display_type;
            types.push_back(preset->config.get_filament_type(display_type));
            filament_ids.push_back(preset->filament_id);
            auto             *vendor = dynamic_cast<ConfigOptionStrings *>(preset->config.option("filament_vendor"));
            const std::string brand  = (vendor && !vendor->values.empty()) ? vendor->values[0] : std::string();
            brands.push_back(brand);
        }
    }

    // Every project filament for the All sentinel, otherwise the ones the given plate actually uses.
    std::vector<int> ids;
    if (plate_idx < 0) {
        for (int i = 0; i < (int) types.size(); ++i) ids.push_back(i);
    } else {
        auto      &plate_list = wxGetApp().plater()->get_partplate_list();
        PartPlate *plate      = plate_list.get_plate(plate_idx);
        if (plate) {
            for (int e : plate->get_extruders(true)) // get_extruders returns 1-based ids
                ids.push_back(e - 1);
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        }
    }

    const auto &cfg      = preset_bundle->full_config();
    auto       *is_mixed = preset_bundle->project_config.option<ConfigOptionBools>("filament_is_mixed");
    for (int id : ids) {
        if (id < 0 || id >= (int) types.size()) continue;
        if (is_mixed && id < (int) is_mixed->values.size() && is_mixed->values[id]) continue;
        FilamentInfo info;
        info.id                 = id;
        info.type               = types[id];
        info.brand              = brands[id];
        info.filament_id        = filament_ids[id];
        info.color              = preset_bundle->project_config.opt_string("filament_colour", (unsigned int) id);
        info.nozzle_diameter    = source_diameter_for(cfg, id);
        info.nozzle_volume_type = source_volume_for(cfg, id);
        out.push_back(info);
    }
    return out;
}

/**
 * \brief Copy a mapped destination's nozzle diameter and volume type from the machine's matching slot.
 *
 * \param slots  the machine's slots
 * \param dst    the destination to fill; left unchanged when unmapped or no slot matches
 */
void backfill_nozzle_geometry(const std::vector<FilamentInfo> &slots, FilamentInfo &dst)
{
    if (dst.tray_id < 0) return;

    for (const auto &s : slots) {
        if (s.ams_id == dst.ams_id && s.slot_id == dst.slot_id) {
            dst.nozzle_diameter    = s.nozzle_diameter;
            dst.nozzle_volume_type = s.nozzle_volume_type;
            return;
        }
    }
}

/**
 * \brief A destination slot's name on the selected printer, e.g. "A1".
 *
 * \param slot  the destination; empty when unmapped or no printer is selected
 */
wxString slot_label(const FilamentInfo &slot)
{
    auto *dm  = wxGetApp().getDeviceManager();
    auto *obj = dm ? dm->get_selected_machine() : nullptr;
    if (!obj || slot.tray_id < 0) return {};
    return wxString::FromUTF8(slot_name(obj, slot.get_ams_id(), std::atoi(slot.slot_id.c_str())));
}

/**
 * \brief The dialog's main column. Extra height first grows the mapping section until all its rows show,
 *        then goes to the preview; every other item keeps its min height.
 *
 * \note Items are laid out at full width, so alignment flags are ignored.
 */
class MappingFirstSizer : public wxBoxSizer
{
public:
    MappingFirstSizer() : wxBoxSizer(wxVERTICAL) {}

    /**
     * \brief Add the preview, which takes the height the mapping section leaves over.
     *
     * \param border  left and right inset
     */
    void add_preview(wxSizer *preview, int border) { m_preview = Add(preview, 0, wxEXPAND | wxLEFT | wxRIGHT, border); }

    /**
     * \brief Add the mapping section, which grows first; a hidden section takes nothing.
     *
     * \param list    the mapping list inside the section; its rows bound the section's growth
     * \param border  left and right inset
     */
    void add_mapping(wxWindow *section, MappingList *list, int border)
    {
        m_mapping = Add(section, 0, wxEXPAND | wxLEFT | wxRIGHT, border);
        m_list    = list;
    }

protected:
    void RepositionChildren(const wxSize &min_size) override
    {
        // The height of the rows the list scrolls away at its min height.
        int hidden = 0;
        if (m_mapping && m_mapping->IsShown() && m_list && m_list->GetSizer())
            hidden = std::max(0, m_list->GetSizer()->CalcMin().y - m_list->GetMinSize().y);
        const int slack        = std::max(0, m_size.y - min_size.y);
        const int mapping_grow = std::min(slack, hidden);

        wxPoint pos = m_position;
        for (wxSizerItem *item : GetChildren()) {
            if (!item->IsShown()) continue;
            int h = item->GetMinSizeWithBorder().y;
            if (item == m_mapping)
                h += mapping_grow;
            else if (item == m_preview)
                h += slack - mapping_grow;
            item->SetDimension(pos, wxSize(m_size.x, h));
            pos.y += h;
        }
    }

private:
    wxSizerItem *m_preview{nullptr};
    wxSizerItem *m_mapping{nullptr};
    MappingList *m_list{nullptr};
};

} // namespace

SyncAmsDialog::SyncAmsDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("Synchronize AMS Filament Information"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetBackgroundColour(wxGetApp().get_window_default_clr());

    const int inset   = FromDIP(32);
    const int spacing = FromDIP(16);
    auto     *root = new MappingFirstSizer();
    root->AddSpacer(FromDIP(24));
    root->Add(build_header(), 0, wxEXPAND | wxLEFT | wxRIGHT, inset);
    root->AddSpacer(spacing);
    root->add_preview(build_preview(), inset);
    root->AddSpacer(spacing);
    wxWindow *mapping = build_mapping_section(); // creates m_mapping_list
    root->add_mapping(mapping, m_mapping_list, inset);
    root->AddSpacer(spacing);
    root->Add(build_advanced(), 0, wxEXPAND | wxLEFT | wxRIGHT, inset);
    root->AddSpacer(spacing);
    root->Add(build_footer(), 0, wxEXPAND | wxALL, inset);
    SetSizer(root);

    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { EndModal(wxID_CANCEL); });
#ifdef __WINDOWS__
    NppDarkMode::SetDarkExplorerTheme(GetHWND());
    NppDarkMode::SetDarkTitleBar(GetHWND()); // dark title bar so the white title text reads in dark mode
#endif
    // width is fixed by the preview cards and mapping rows, Take that width as the
    // floor, and seed it before the first fit so the auto-wrap description wraps at its final width
    // Height stays free to shrink; the list absorbs it as scroll.
    const int content_w = GetBestSize().GetWidth();
    SetClientSize(content_w, GetClientSize().GetHeight());
    // Floor the width to the content and the height to a fixed minimum; Fit can grow past it but never below.
    SetMinClientSize(wxSize(content_w, FromDIP(650)));
    set_mode(false);
    CentreOnScreen();
}

wxWindow *SyncAmsDialog::build_header()
{
    const wxColour card_bg = StateColor::darkModeColorFor(ThemeColor::Grey200);
    auto          *card    = new RoundedRectangle(this, card_bg, StateColor::darkModeColorFor(ThemeColor::Grey350), FromDIP(8));

    m_mode_map       = new CapsuleButton(card, wxID_ANY, _L("Mapping"), true);
    m_mode_overwrite = new CapsuleButton(card, wxID_ANY, _L("Overwriting"), false);
    m_mode_map->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { set_mode(false); });
    m_mode_overwrite->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { set_mode(true); });
    auto *modes = new wxBoxSizer(wxHORIZONTAL);
    modes->Add(m_mode_map, 0, wxALIGN_CENTER_VERTICAL);
    modes->Add(m_mode_overwrite, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    m_desc = new ::Label(card, wxEmptyString, LB_AUTO_WRAP);
    m_desc->SetFont(::Label::Body_12);
    m_desc->SetForegroundColour(StateColor::darkModeColorFor(ThemeColor::TextPrimary));
    m_desc->SetBackgroundColour(card_bg);
    m_desc->SetMinSize(FromDIP(wxSize(640, -1)));

    auto *warn      = new wxBoxSizer(wxHORIZONTAL);
    auto *warn_icon = new wxStaticBitmap(card, wxID_ANY, create_scaled_bitmap("warning", card, 16));
    auto *warn_text = new ::Label(card, _L("After modification, the filament list cannot be restored."));
    warn_text->SetFont(::Label::Body_12);
    warn_text->SetForegroundColour(StateColor::darkModeColorFor(ThemeColor::TextMuted));
    warn_text->SetBackgroundColour(card_bg);
    warn->Add(warn_icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    warn->Add(warn_text, 0, wxALIGN_CENTER_VERTICAL);

    auto *v = new wxBoxSizer(wxVERTICAL);
    v->Add(modes, 0, wxALIGN_CENTER_HORIZONTAL);
    v->Add(m_desc, 0, wxEXPAND | wxTOP, FromDIP(10));
    v->Add(warn, 0, wxTOP, FromDIP(8));

    auto *pad = new wxBoxSizer(wxVERTICAL);
    pad->Add(v, 1, wxEXPAND | wxALL, FromDIP(12));
    card->SetSizer(pad);
    return card;
}

void SyncAmsDialog::set_mode(bool overwrite)
{
    m_overwrite = overwrite;
    if (m_mode_map) m_mode_map->Select(!overwrite);
    if (m_mode_overwrite) m_mode_overwrite->Select(overwrite);

    m_desc->SetLabel(overwrite ? _L("Overwrite the project filaments list with the filaments in the printer.") :
                                 _L("Match the project filaments with the filaments in the printer according to "
                                    "the matching mode. After syncing, the mapped results will be used."));
    m_after_title->SetLabel(overwrite ? _L("After overwriting") : _L("After mapping"));
    m_mapping_panel->Show(!overwrite);
    update_mapped_preview(/*reframe=*/false); // right card tracks the mode: mapped colours vs the plain plate
    relayout();
}

void SyncAmsDialog::relayout()
{
    Layout();
    Fit();
}

wxSizer *SyncAmsDialog::build_preview()
{
    auto    *h     = new wxBoxSizer(wxHORIZONTAL);
    ::Label *dummy = nullptr;
    h->Add(build_preview_card(_L("Original"), "ORIGINAL", true, &dummy, &m_preview_orig), 1, wxEXPAND);
    h->Add(build_preview_card(_L("After mapping"), "SYNCED", false, &m_after_title, &m_preview_mapped), 1, wxEXPAND | wxLEFT, FromDIP(12));

    // sync the two preview
    m_preview_orig->set_camera_changed([this](const Camera &c) { m_preview_mapped->sync_camera(c); });
    m_preview_mapped->set_camera_changed([this](const Camera &c) { m_preview_orig->sync_camera(c); });

    on_plate_changed();
    return h;
}

int SyncAmsDialog::current_plate()
{
    const int sel       = m_plate_combo ? m_plate_combo->GetSelection() : 0;
    const int plate_idx = sel <= 0 ? -1 : sel - 1; // entry 0 = "All"; entry i = plate i-1

    return plate_idx;
}

void SyncAmsDialog::on_plate_changed()
{
    m_preview_orig->set_scene(make_current_plate_scene(current_plate()));
    update_mapped_preview(true); // scope switch may change the plate geometry
}

void SyncAmsDialog::update_mapped_preview(bool reframe)
{
    // Overwrite mode replaces the whole project list, so there is no per-source recolour to show — the card
    // draws the plate in its own colours (the default scene, empty overrides). Mapping mode recolours each
    // mapped source to its destination slot's colour; unmapped sources keep their project colour. The
    // mapping stays in m_mappings across a mode switch, so returning to mapping re-derives this identically.
    std::map<int, std::array<float, 4>> overrides;
    if (!m_overwrite) {
        for (const auto &[src_id, dst] : m_mappings) {
            if (src_id < 0 || !dst || dst->tray_id < 0) continue;
            const std::string &hex = dst->color.empty() && !dst->colors.empty() ? dst->colors.front() : dst->color;
            const wxColour     c   = DevAmsTray::decode_color(hex);
            overrides[src_id]      = {c.Red() / 255.f, c.Green() / 255.f, c.Blue() / 255.f, c.Alpha() / 255.f};
        }
    }
    m_preview_mapped->set_scene(make_current_plate_scene(current_plate(), overrides), reframe);
}

void SyncAmsDialog::rebuild_mapping()
{
    // Filaments already mapped while another plate was shown keep their result (auto or picked); only the
    // ones this scope brings in for the first time are matched.
    const std::vector<FilamentInfo> source = collect_scope_filaments(current_plate());
    std::vector<FilamentInfo>       unseen;
    for (const auto &s : source)
        if (m_mappings.find(s.id) == m_mappings.end()) unseen.push_back(s);
    auto_map(unseen);
    fill_rows(source);
}

void SyncAmsDialog::remap_all()
{
    // A strategy change re-matches every filament mapped so far, not only the shown scope, so the result
    // never mixes two strategies; manual picks are discarded.
    std::vector<FilamentInfo> known;
    for (const auto &s : collect_scope_filaments(-1))
        if (m_mappings.find(s.id) != m_mappings.end()) known.push_back(s);
    auto_map(known);
    fill_rows(collect_scope_filaments(current_plate()));
}

void SyncAmsDialog::auto_map(const std::vector<FilamentInfo> &source)
{
    std::vector<FilamentInfo> result;
    auto                     *dm  = wxGetApp().getDeviceManager();
    auto                     *obj = dm ? dm->get_selected_machine() : nullptr;
    if (obj && !source.empty()) {
        const auto  &full_config = wxGetApp().preset_bundle->full_config();
        const size_t nozzle_nums = full_config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values.size();
        // {use_left_ams, use_right_ams, use_left_ext, use_right_ext}, mirroring SyncAmsInfoDialog::do_ams_mapping.
        std::vector<bool> map_opt = nozzle_nums > 1 ? std::vector<bool>{true, true, true, true} : std::vector<bool>{false, true, false, true};
        // Auto-map strategy from the policy combo: Precision-first requires a matching nozzle (diameter + flow),
        // Color-first only prefers one. Colour always outranks filament sub-type in either strategy.
        const bool              precision_first = m_policy && m_policy->GetSelection() == 1;
        const NozzleMatchWeight nozzle_weight   = precision_first ? NozzleMatchWeight::Require : NozzleMatchWeight::Prefer;
        DevMappingUtil::ams_filament_mapping(obj, source, result, map_opt, std::vector<int>(), /*nozzle_has_ams_then_ignore_ext*/ true,
                                             /*color_over_subtype*/ true, nozzle_weight);

        // The matcher copies the slot's ids / colour / type but not its nozzle geometry — backfill it from
        // the machine's real slots (the same join the manual-pick path uses).
        const std::vector<FilamentInfo> slots = DevMappingUtil::enumerate_dest_slots(obj);
        for (auto &r : result) backfill_nozzle_geometry(slots, r);
    }

    // The matcher appends one result per source (tray_id < 0 when unmatched); the explicit fallback covers
    // the no-machine case where result is empty, so every source still ends up "Unmapped".
    for (size_t i = 0; i < source.size(); ++i) {
        FilamentInfo dst;
        if (i < result.size())
            dst = result[i];
        else
            dst.tray_id = -1;
        m_mappings[source[i].id] = std::make_shared<FilamentInfo>(dst);
    }
}

void SyncAmsDialog::fill_rows(const std::vector<FilamentInfo> &source)
{
    m_mapping_list->clear();
    for (const auto &src : source) {
        auto it = m_mappings.find(src.id);
        if (it == m_mappings.end()) continue;

        auto *row = m_mapping_list->add_row();
        row->set_source(src);
        row->set_dest(*it->second, slot_label(*it->second));

        // Clicking the destination opens the slot picker; the pick is written through the shared
        // `current`, so m_mappings (read by deal_ok and by later plate switches) sees it.
        std::shared_ptr<FilamentInfo> current = it->second;
        row->on_dest_activate                 = [this, row, src, current] { open_slot_picker(row, src, current); };
    }

    // A scrolled window reports its best height as its min height, not its content, so it only gets row
    // room if we hand it one. Fit the rows up to a cap, then scroll past that; the rows are uniform, so the
    // capped height is an exact fraction of the native content height (no per-row measurement). The cap is
    // only the floor: a taller dialog grows the list toward all its rows (see MappingFirstSizer). Width is
    // left to the parent — the list expands to the mapping panel (see build_mapping_section).
    auto         *list_sizer      = m_mapping_list->GetSizer();
    const int     row_count       = (int) list_sizer->GetItemCount();
    const int     content_h       = list_sizer->CalcMin().y;
    constexpr int kMaxVisibleRows = 5;
    const int     list_h          = row_count > kMaxVisibleRows ? content_h * kMaxVisibleRows / row_count : content_h;
    m_mapping_list->SetMinSize(wxSize(-1, list_h));
    update_mapped_preview(/*reframe=*/false); // auto-map/policy change: colours only, keep the camera
    relayout();
}

void SyncAmsDialog::open_slot_picker(MappingRow *row, const FilamentInfo &source, std::shared_ptr<FilamentInfo> current)
{
    if (!m_slot_popup) m_slot_popup = new AmsSlotPopup(this);

    SlotSelectionState st;
    st.source_id   = source.id;
    st.source_type = source.type;
    st.match_all   = true; // TODO: drive from the advanced "match all" option once wired
    m_slot_popup->set_state(st);
    m_slot_popup->set_current_slot(*current);
    m_slot_popup->on_pick = [this, row, current](const FilamentInfo &chosen) {
        // The popup resolves the clicked slot's full identity (incl. nozzle geometry) from the machine's
        // live slots, so a manual pick already reads identically to an auto-matched one — take it as is.
        *current = chosen;
        row->set_dest(chosen, slot_label(chosen));
        row->set_selected(true);
        update_mapped_preview(/*reframe=*/false); // repaint the after-mapping preview with the new colour
    };
    m_slot_popup->popup_at(row->dest_anchor());
}

void SyncAmsDialog::deal_ok()
{
    // Printer identity is enforced upstream (is_same_printer_for_connected_and_selected), so this path
    // never runs against a mismatched printer.
    m_result.is_same_printer = true;

    if (m_overwrite) {
        m_result.direct_sync = true; // overwrite the project filaments directly, no per-slot mapping
        return;
    }

    m_result.direct_sync = false;
    m_result.sync_maps.clear();
    // Each row's dest already carries its slot's nozzle diameter (backfilled at pick / auto-map time),
    // so record it straight. An unmapped dest leaves empty ids, which the preset bundle reads as unmapped.
    for (const auto &[src_id, dst] : m_mappings) {
        if (src_id < 0)
            continue;
        auto &slot   = m_result.sync_maps[src_id];
        slot.ams_id  = dst->ams_id;
        slot.slot_id = dst->slot_id;
        if (dst->nozzle_diameter > 0.)
            slot.nozzle_diameter = std::to_string(dst->nozzle_diameter);
    }
}

wxWindow *SyncAmsDialog::build_preview_card(const wxString &title, const wxString &tag, bool with_plate_combo, ::Label **out_title, ModelPreview **preview)
{
    const wxColour card_bg = StateColor::darkModeColorFor(ThemeColor::Grey200); // light card surface
    auto          *card    = new RoundedRectangle(this, card_bg, StateColor::darkModeColorFor(ThemeColor::Grey350), FromDIP(8));
    card->SetMinSize(FromDIP(wxSize(350, 350)));

    // Header row sits directly on the card so the rounded corners stay clear. Reserve the combobox
    // height on the row itself (a zero-width spacer), not on the title's box — both cards' header rows
    // then match whether or not this card carries the combobox, and every item still centres within it.
    const int ctrl_h = FromDIP(24);
    auto     *header = new ::Label(card, title);
    header->SetFont(::Label::Body_13);
    header->SetForegroundColour(StateColor::darkModeColorFor(ThemeColor::TextPrimary));
    header->SetBackgroundColour(card_bg);
    if (out_title) *out_title = header;
    auto *hrow = new wxBoxSizer(wxHORIZONTAL);
    hrow->Add(0, ctrl_h); // reserve the row height so the title/tag centre within it
    hrow->Add(header, 0, wxALIGN_CENTER_VERTICAL);
    if (with_plate_combo) {
        m_plate_combo = new ::ComboBox(card, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(72), ctrl_h), 0, nullptr, wxCB_READONLY);
        m_plate_combo->Append(_L("All"));
        for (int i = 0; i < preview_plate_count(); ++i) m_plate_combo->Append(wxString::Format("%d", i + 1));
        m_plate_combo->SetSelection(preview_plate_count() > 0 ? preview_current_plate_index() + 1 : 0); // default: current plate
        m_plate_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &) {
            on_plate_changed();
            rebuild_mapping();
        });
        hrow->Add(m_plate_combo, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));
    }
    hrow->AddStretchSpacer();
    auto *tag_lbl = new PillView(card, card_bg);
    tag_lbl->set_text(tag);
    tag_lbl->set_colors(StateColor::darkModeColorFor(ThemeColor::BrandGreen), StateColor::darkModeColorFor(ThemeColor::LightGreen), wxTransparentColour);
    hrow->Add(tag_lbl, 0, wxALIGN_CENTER_VERTICAL);

    // Hairline under the header, inset so it never reaches the rounded corners.
    auto *divider = new wxWindow(card, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(1)));
    divider->SetBackgroundColour(StateColor::darkModeColorFor(ThemeColor::Grey350));

    // body — the live 3D preview, inset so the card's rounded corners / border stay visible around it.
    *preview = new ModelPreview(card);
    (*preview)->SetBackgroundColour(card_bg);

    auto *v = new wxBoxSizer(wxVERTICAL);
    v->Add(hrow, 0, wxEXPAND | wxALL, FromDIP(8));
    v->Add(divider, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));
    v->Add(*preview, 1, wxEXPAND | wxALL, FromDIP(8));
    card->SetSizer(v);
    return card;
}

wxWindow *SyncAmsDialog::build_mapping_section()
{
    m_mapping_panel = new wxPanel(this);
    m_mapping_panel->SetBackgroundColour(wxGetApp().get_window_default_clr());

    auto *title = new ::Label(m_mapping_panel, _L("Filament matching"));
    title->SetFont(::Label::Head_14);
    title->SetForegroundColour(StateColor::darkModeColorFor(ThemeColor::TextPrimary));
    m_policy = new ::ComboBox(m_mapping_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(124, 26)), 0, nullptr, wxCB_READONLY);
    m_policy->Append(_L("Color first")); // matches the existing dialog's scheme text
    m_policy->Append(_L("Precision first"));
    m_policy->SetSelection(0);
    m_policy->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &) { remap_all(); }); // re-map with the new strategy
    auto *trow = new wxBoxSizer(wxHORIZONTAL);
    trow->Add(title, 0, wxALIGN_CENTER_VERTICAL);
    trow->Add(m_policy, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12)); // right after the label
    trow->AddStretchSpacer();

    m_mapping_list = new MappingList(m_mapping_panel);
    m_mapping_list->SetBackgroundColour(m_mapping_panel->GetBackgroundColour()); // set before rows: cells theme off this
    rebuild_mapping();                                                           // fills rows and sizes the list to them (width is left to wxEXPAND below)

    auto *v = new wxBoxSizer(wxVERTICAL);
    v->Add(trow, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
    v->Add(m_mapping_list, 1, wxEXPAND);
    m_mapping_panel->SetSizer(v);
    return m_mapping_panel;
}

wxSizer *SyncAmsDialog::build_advanced()
{
    m_advanced_icon = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("advanced_option3", this, 16));
    auto *expander  = new ::Label(this, _L("Advanced settings"));
    expander->SetFont(::Label::Body_13);
    expander->SetForegroundColour(StateColor::darkModeColorFor(ThemeColor::BrandGreen));
    auto *row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(expander, 0, wxALIGN_CENTER_VERTICAL);
    row->Add(m_advanced_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(2));
    for (wxWindow *w : {static_cast<wxWindow *>(expander), static_cast<wxWindow *>(m_advanced_icon)}) w->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { toggle_advanced(); });

    m_advanced_body = new wxPanel(this);
    m_advanced_body->SetBackgroundColour(wxGetApp().get_window_default_clr()); // blend, not a grey box
    auto *bs = new wxBoxSizer(wxVERTICAL);
    // Each option is a persisted app preference that the sync itself reads, so the box mirrors and writes that key directly.
    const std::pair<std::string, wxString> options[] = {
        {"enable_append_color_by_sync_ams", _L("Add unused AMS filaments to filaments list.")},
        {"enable_merge_color_by_sync_ams", _L("Automatically merge the same colors in the model after mapping.")},
    };
    for (const auto &[key, text] : options) {
        auto *cb = new ::CheckBox(m_advanced_body);
        cb->SetValue(wxGetApp().app_config->get_bool(key));
        cb->Bind(wxEVT_TOGGLEBUTTON, [cb, key = key](wxCommandEvent &e) {
            wxGetApp().app_config->set_bool(key, cb->GetValue());
            e.Skip();
        });
        auto *lbl = new ::Label(m_advanced_body, text);
        lbl->SetFont(::Label::Body_12);
        lbl->SetForegroundColour(StateColor::darkModeColorFor(ThemeColor::TextPrimary));
        auto *hr = new wxBoxSizer(wxHORIZONTAL);
        hr->Add(cb, 0, wxALIGN_CENTER_VERTICAL);
        hr->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
        bs->Add(hr, 0, wxTOP, FromDIP(8));
    }
    m_advanced_body->SetSizer(bs);
    m_advanced_body->Hide();

    auto *v = new wxBoxSizer(wxVERTICAL);
    v->Add(row, 0);
    v->Add(m_advanced_body, 0, wxTOP, FromDIP(4));
    return v;
}

void SyncAmsDialog::toggle_advanced()
{
    m_advanced_open = !m_advanced_open;
    m_advanced_body->Show(m_advanced_open);
    m_advanced_icon->SetBitmap(create_scaled_bitmap(m_advanced_open ? "advanced_option4" : "advanced_option3", this, 16));
    relayout(); // the shown body enlarges the content, so Fit grows the dialog to fit it
}

wxSizer *SyncAmsDialog::build_footer()
{
    auto *cancel = new ::Button(this, _L("Cancel"));
    auto *ok     = new ::Button(this, _L("Synchronize now"));
    for (::Button *b : {cancel, ok}) {
        b->SetMinSize(FromDIP(wxSize(96, 30)));
        b->SetCornerRadius(FromDIP(12));
    }
    ok->SetBackgroundColorNormal(StateColor::darkModeColorFor(ThemeColor::BrandGreen));
    ok->SetBorderColorNormal(StateColor::darkModeColorFor(ThemeColor::BrandGreen));
    ok->SetTextColorNormal(*wxWHITE);
    cancel->SetBackgroundColorNormal(StateColor::darkModeColorFor(ThemeColor::White));
    cancel->SetBorderColorNormal(StateColor::darkModeColorFor(ThemeColor::Grey400));
    cancel->SetTextColorNormal(StateColor::darkModeColorFor(ThemeColor::TextPrimary));

    cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CANCEL); });
    ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { deal_ok(); EndModal(wxID_OK); });

    auto *h = new wxBoxSizer(wxHORIZONTAL);
    h->AddStretchSpacer();
    h->Add(cancel, 0, wxRIGHT, FromDIP(8));
    h->Add(ok, 0);
    return h;
}

void SyncAmsDialog::on_dpi_changed(const wxRect &) { Refresh(); }

} // namespace Slic3r::GUI
