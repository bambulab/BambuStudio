#include "SyncAmsDialog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <wx/bitmap.h>
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/display.h>
#include <wx/gdicmn.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/wupdlock.h>

#include "CapsuleButton.hpp"
#include "ExtruderHelpers.hpp"          // slot_name, nozzle_loses_detail
#include "ModelPreview.hpp"
#include "GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"        // resources_dir, encode_path for the title icon
#include "I18N.hpp"
#include "PartPlate.hpp"                // PartPlate::get_extruders for a plate's used filaments
#include "Plater.hpp"                   // get_partplate_list
#ifdef __WINDOWS__
#include "dark_mode.hpp"                // NppDarkMode::SetDarkTitleBar
#endif
#include "wxExtensions.hpp"             // create_scaled_bitmap
#include "BitmapCache.hpp"              // parse_color4, to normalise source colours
#include "DeviceManager.hpp"            // MachineObject external spools
#include "DeviceCore/DevManager.h"      // selected machine
#include "DeviceCore/DevMapping.h"      // ams_filament_mapping
#include "DeviceCore/DevFilaSwitch.h"   // filament selector state
#include "DeviceCore/DevFilaSystem.h"   // DevAmsTray::decode_color, for the mapped preview colours
#include "DeviceCore/DevNozzleSystem.h" // DevNozzle flow-type strings, for the source volume type
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/AmsSlotPopup.hpp" // AmsSlotPopup, SlotSelectionState
#include "Widgets/Label.hpp"
#include "Widgets/LinkLabel.hpp"
#include "Widgets/MappingRow.hpp"
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
 * \brief The per-filament nozzle volume types the slicer must honor on a plate, or empty when the
 *        slicer picks every flow itself.
 *
 * Only a manual grouping, or a nozzle-manual grouping in multi-diameter mode, makes
 * filament_volume_map a request. Otherwise it holds a slice result or a selection left over from
 * multi-diameter mode, so it must not bar a slot whose nozzle the slicer can still use.
 *
 * \param plate  the plate to read
 * \param proj   the project config, which a plate without its own mode or map falls back to
 */
std::vector<int> plate_requested_volume_map(const PartPlate &plate, const DynamicPrintConfig &proj)
{
    const FilamentMapMode mode = plate.get_real_filament_map_mode(proj);
    const bool requested       = mode == FilamentMapMode::fmmManual ||
                           (mode == FilamentMapMode::fmmNozzleManual && multi_nozzle_diameter_mode_enabled(proj));
    return requested ? plate.get_real_filament_volume_maps(proj) : std::vector<int>{};
}

/**
 * \brief The per-filament nozzle volume types the slicer must honor in a scope; see
 *        plate_requested_volume_map().
 *
 * The All scope merges every plate that uses a filament: plates without a request add no
 * constraint, and plates that request different flows leave the filament Hybrid, since no single
 * slot can serve them all.
 *
 * \param plate_idx  a plate index; < 0 (the All sentinel) for every plate
 */
std::vector<int> requested_volume_map(int plate_idx)
{
    auto   *bundle = wxGetApp().preset_bundle;
    Plater *plater = wxGetApp().plater();
    if (!bundle || !plater) return {};

    const auto &proj   = bundle->project_config;
    auto       &plates = plater->get_partplate_list();
    if (plate_idx >= 0) {
        PartPlate *plate = plates.get_plate(plate_idx);
        return plate ? plate_requested_volume_map(*plate, proj) : std::vector<int>{};
    }

    // Hybrid marks a filament no plate has requested a flow for yet.
    std::vector<int> merged(bundle->filament_presets.size(), (int) nvtHybrid);
    std::vector<bool> seen(merged.size(), false);
    for (PartPlate *plate : plates.get_plate_list()) {
        if (!plate) continue;
        const std::vector<int> requested = plate_requested_volume_map(*plate, proj);
        for (int e : plate->get_extruders(true)) { // get_extruders returns 1-based ids
            const int id = e - 1;
            if (id < 0 || id >= (int) merged.size() || id >= (int) requested.size()) continue;
            if (!seen[id]) merged[id] = requested[id];
            else if (merged[id] != requested[id]) merged[id] = (int) nvtHybrid;
            seen[id] = true;
        }
    }
    return merged;
}

/**
 * \brief The nozzle volume type ("Standard" / "High Flow" / ...) a project filament requests, or empty
 *        when any flow will do.
 *
 * \param requested  the scope's requested_volume_map()
 * \param extruder   0-based project filament id
 */
std::string source_volume_for(const std::vector<int> &requested, int extruder)
{
    if (extruder < 0 || extruder >= (int) requested.size()) return "";
    const auto vt = (NozzleVolumeType) requested[extruder];
    // Hybrid names no single flow. Treat it as no request, not as a flow no nozzle has.
    if (vt == nvtHybrid) return "";
    return DevNozzle::GetNozzleFlowTypeString(DevNozzle::ToNozzleFlowType(vt));
}

/**
 * \brief A project colour in FilamentInfo's "#RRGGBBAA" form, so every source compares on the same footing.
 *
 * \param colour  the project colour, with or without alpha
 */
std::string normalized_colour(const std::string &colour)
{
    unsigned char rgba[4];
    if (!BitmapCache::parse_color4(colour, rgba)) return colour;
    return wxString::Format("#%02X%02X%02X%02X", rgba[0], rgba[1], rgba[2], rgba[3]).ToStdString();
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
        for (int e : wxGetApp().plater()->get_partplate_list().get_extruders(true))
            ids.push_back(e - 1);
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

    const auto &cfg           = preset_bundle->full_config();
    const auto  requested     = requested_volume_map(plate_idx);
    const auto &project_cfg   = preset_bundle->project_config;
    auto       *is_mixed      = project_cfg.option<ConfigOptionBools>("filament_is_mixed");
    auto       *multi_colours = project_cfg.option<ConfigOptionStrings>("filament_multi_colour");
    auto       *colour_types  = project_cfg.option<ConfigOptionStrings>("filament_colour_type");
    for (int id : ids) {
        if (id < 0 || id >= (int) types.size()) continue;
        if (is_mixed && id < (int) is_mixed->values.size() && is_mixed->values[id]) continue;
        FilamentInfo info;
        info.id                 = id;
        info.type               = types[id];
        info.brand              = brands[id];
        info.filament_id        = filament_ids[id];
        info.color              = normalized_colour(project_cfg.opt_string("filament_colour", (unsigned int) id));
        info.ctype              = DevFilaColorType::CTYPE_SINGLE;
        if (multi_colours && id < (int) multi_colours->values.size()) {
            for (const std::string &colour : Slic3r::split_string(multi_colours->values[id], ' '))
                if (!colour.empty()) info.colors.push_back(normalized_colour(colour));
        }
        if (info.colors.size() > 1) {
            info.ctype = DevFilaColorType::CTYPE_MULTI;
            if (colour_types && id < (int) colour_types->values.size() && colour_types->values[id] == "0")
                info.ctype = DevFilaColorType::CTYPE_GRADIANT;
        }
        info.nozzle_diameter    = source_diameter_for(cfg, id);
        info.nozzle_volume_type = source_volume_for(requested, id);
        out.push_back(info);
    }
    return out;
}

/**
 * \brief Whether the selected printer profile is a single-extruder printer carrying several nozzles.
 *
 * Only these printers match by nozzle as well and let the user pick the matching strategy; every other
 * printer keeps the classic matching.
 */
bool is_multi_nozzle_printer()
{
    auto *bundle = wxGetApp().preset_bundle;
    return bundle && (is_single_ext_multi_nozzle(bundle->printers.get_edited_preset().config) ||
                      multi_nozzle_diameter_mode_enabled(bundle->full_config()));
}

/**
 * \brief How auto-map ranks the candidate slots of one source.
 */
struct MatchStrategy
{
    bool              color_over_subtype{false};               ///< colour outranks the filament sub-type
    NozzleMatchWeight nozzle_weight{NozzleMatchWeight::Ignore}; ///< how much the slot's nozzle counts
};

/**
 * \brief Which of a printer's slots take part in the sync. The only place that decides it.
 *
 * \param obj  the printer to sync from
 */
SyncSlots sync_slots_of(const MachineObject *obj)
{
    SyncSlots slots;
    slots.dual_nozzle         = wxGetApp().preset_bundle->full_config().option<ConfigOptionFloatsNullable>("nozzle_diameter")->values.size() > 1;
    const auto selector       = obj->GetFilaSwitch();
    slots.selector_feeds_both =slots.dual_nozzle && selector && selector->IsInstalled() && selector->IsReady();
    return slots;
}

/**
 * \brief Match every source against the machine's slots in one pass, one result per source.
 *
 * A result whose slot is no longer on the machine comes back unmapped (tray_id < 0); when the matcher
 * fails outright and its result is invalid, every result is unmapped.
 *
 * \param obj       the machine to map onto
 * \param source    the project filaments to match
 * \param strategy  how candidate slots are ranked
 */
std::vector<FilamentInfo> match_filaments(const MachineObject *obj, const std::vector<FilamentInfo> &source, const MatchStrategy &strategy)
{
    const SyncSlots slots   = sync_slots_of(obj);
    const bool      use_ext = !slots.selector_feeds_both;
    // {use_left_ams, use_right_ams, use_left_ext, use_right_ext}
    const std::vector<bool> map_opt = slots.dual_nozzle ? std::vector<bool>{true, true, use_ext, use_ext} : std::vector<bool>{false, true, false, true};
    const bool ignore_ext_if_ams = !wxGetApp().app_config->get_bool("ams_sync_match_full_use_color_dist");

    std::vector<FilamentInfo> result;
    const int rc = DevMappingUtil::ams_filament_mapping(obj, source, result, map_opt, std::vector<int>(), ignore_ext_if_ams,
                                                        strategy.color_over_subtype, strategy.nozzle_weight,
                                                        /*allow_manual_feed*/ true);
    const bool valid = DevMappingUtil::is_valid_mapping_result(obj, result); // unmaps slots that left the machine
    if (rc != 0 && rc != 1 && !valid) {
        for (auto &r : result) {
            r.tray_id  = -1;
            r.distance = 99999;
        }
    }
    return result;
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
 * \brief The printer the sync reads from, or nullptr when none is selected.
 */
MachineObject *selected_machine()
{
    auto *dm = wxGetApp().getDeviceManager();
    return dm ? dm->get_selected_machine() : nullptr;
}

/**
 * \brief The colour each project filament takes after an overwrite sync, keyed by 0-based filament id.
 *
 * Mirrors the overwrite branch of PresetBundle::sync_ams_list(): project filament i takes the colour of the
 * i-th synced tray. Filaments past the synced tray count are left out and keep their project colour.
 *
 * \note Keep the tray filter in step with sync_ams_list() and the skip_ext rule in Plater.
 */
std::map<int, std::array<float, 4>> overwrite_colors()
{
    std::map<int, std::array<float, 4>> colors;
    auto *preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle) return colors;

    MachineObject *obj      = selected_machine();
    const bool     skip_ext = obj && obj->GetFilaSwitch()->IsInstalled() && obj->GetFilaSwitch()->IsReady();
    std::set<std::pair<std::string, std::string>> added;
    int next = 0;
    for (const auto &[tray_id, tray] : preset_bundle->filament_ams_list) {
        const std::string filament_id = tray.opt_string("filament_id", 0u);
        const std::string tray_name   = tray.opt_string("tray_name", 0u);
        if (skip_ext && (tray_name == "Ext" || !added.insert({tray_name, filament_id}).second)) continue;
        if (filament_id.empty()) continue; // an unknown tray is not synced in overwrite mode
        const wxColour c(wxString::FromUTF8(tray.opt_string("filament_colour", 0u)));
        colors[next++] = {c.Red() / 255.f, c.Green() / 255.f, c.Blue() / 255.f, c.Alpha() / 255.f};
    }
    return colors;
}

/**
 * \brief A destination slot's name on the selected printer, e.g. "A1".
 *
 * \param slot  the destination; empty when unmapped or no printer is selected
 */
wxString slot_label(const FilamentInfo &slot)
{
    auto *obj = selected_machine();
    if (!obj || slot.tray_id < 0) return {};
    return wxString::FromUTF8(slot_name(obj, slot.get_ams_id(), std::atoi(slot.slot_id.c_str())));
}

/**
 * \brief Whether the selected printer syncs from external spools only. False when no printer is selected.
 */
bool syncs_ext_spools_only()
{
    auto *obj = selected_machine();
    return obj && !obj->GetFilaSystem()->HasAms();
}

/**
 * \brief The tip that high- and low-temperature filaments are not recommended to be mixed.
 *
 * \param parent  the card to place it on
 * \param bg      the card's background colour
 */
wxSizer *build_mix_temp_tip(wxWindow *parent, const wxColour &bg)
{
    auto *icon = new wxStaticBitmap(parent, wxID_ANY, create_scaled_bitmap("warning", parent, 16));
    icon->SetBackgroundColour(bg);
    auto *text = new ::Label(parent, _L("Mixing high- and low-temperature filaments is not recommended."), LB_AUTO_WRAP);
    text->SetFont(::Label::Body_13);
    text->SetForegroundColour(StateColor::darkModeColorFor(ThemeColor::Warning));
    text->SetBackgroundColour(bg);

    auto *link = new LinkLabel(parent, _L("Wiki for details") + wxT("\u2192"), "https://e.bambulab.com/t?c=obIOA5Zy4e5K3YBL");
    link->getLabel()->SetFont(::Label::Body_13);
    link->SeLinkLabelFColour(StateColor::darkModeColorFor(ThemeColor::BrandGreen));
    link->SeLinkLabelBColour(bg);

    auto *h = new wxBoxSizer(wxHORIZONTAL);
    h->Add(icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(4));
    h->Add(text, 0, wxALIGN_CENTER_VERTICAL);
    h->Add(link, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, parent->FromDIP(4));
    return h;
}

/**
 * \brief The dialog's main column. The preview and the mapping section are flexible; every other item keeps
 *        its min height, so the footer always stays inside the window.
 *
 * Each flexible item has a floor and a natural height (its min size). The mapping section has priority over
 * the preview: extra height grows it first, and a short window shrinks the preview first. From the floor
 * upward, height goes to the mapping section until all its rows show, then to the preview.
 *
 * \note CalcMin() reports the floor, so Fit() sizes a window to the floor; use natural_height() to open it.
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
     * \param floor   the height the preview may shrink to in a short window
     */
    void add_preview(wxSizer *preview, int border, int floor)
    {
        m_preview       = Add(preview, 0, wxEXPAND | wxLEFT | wxRIGHT, border);
        m_preview_floor = floor;
    }

    /**
     * \brief Add the mapping section; a hidden section takes nothing.
     *
     * \param list    the mapping list inside the section; it shrinks to one row and grows to all its rows
     * \param border  left and right inset
     */
    void add_mapping(wxWindow *section, MappingList *list, int border)
    {
        m_mapping = Add(section, 0, wxEXPAND | wxLEFT | wxRIGHT, border);
        m_list    = list;
    }

    /// The column height with both flexible items at their natural height.
    int natural_height() { return wxBoxSizer::CalcMin().y; }

    wxSize CalcMin() override
    {
        wxSize min = wxBoxSizer::CalcMin();
        min.y -= preview_give() + mapping_give();
        return min;
    }

protected:
    void RepositionChildren(const wxSize &min_size) override
    {
        // min_size is the floor; hand the height above it out in priority order.
        int       avail        = std::max(0, m_size.y - min_size.y);
        const int mapping_grow = std::min(avail, mapping_give() + hidden_rows_height());
        avail -= mapping_grow;

        wxPoint pos = m_position;
        for (wxSizerItem *item : GetChildren()) {
            if (!item->IsShown()) continue;
            int h = item->GetMinSizeWithBorder().y;
            if (item == m_mapping)
                h += mapping_grow - mapping_give();
            else if (item == m_preview)
                h += avail - preview_give();
            item->SetDimension(pos, wxSize(m_size.x, h));
            pos.y += h;
        }
    }

private:
    bool mapping_shown() const { return m_mapping && m_mapping->IsShown() && m_list && m_list->GetSizer(); }

    // How far the preview may shrink below its natural height.
    int preview_give() const
    {
        if (!m_preview || !m_preview->IsShown()) return 0;
        return std::max(0, m_preview->GetMinSizeWithBorder().y - m_preview_floor);
    }

    // How far the list may shrink below its natural height: down to one row (rows are uniform).
    int mapping_give() const
    {
        if (!mapping_shown()) return 0;
        const int rows = (int) m_list->GetSizer()->GetItemCount();
        if (rows <= 1) return 0;
        const int one_row = m_list->GetSizer()->CalcMin().y / rows;
        return std::max(0, m_list->GetMinSize().y - one_row);
    }

    // The height of the rows the list scrolls away at its natural height.
    int hidden_rows_height() const
    {
        if (!mapping_shown()) return 0;
        return std::max(0, m_list->GetSizer()->CalcMin().y - m_list->GetMinSize().y);
    }

    wxSizerItem *m_preview{nullptr};
    wxSizerItem *m_mapping{nullptr};
    MappingList *m_list{nullptr};
    int          m_preview_floor{0};
};

} // namespace

SyncAmsDialog::SyncAmsDialog(wxWindow *parent, bool deferrable)
    : DPIDialog(parent,
                wxID_ANY,
                _L("Nozzle and Filament Sync"),
                wxDefaultPosition,
                wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetBackgroundColour(wxGetApp().get_window_default_clr());
    const std::string icon_path = resources_dir() + "/images/BambuStudioTitle.ico";
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    const int inset   = FromDIP(32);
    const int spacing = FromDIP(16);
    auto     *root = new MappingFirstSizer();
    root->AddSpacer(FromDIP(24));
    root->Add(build_header(), 0, wxEXPAND | wxLEFT | wxRIGHT, inset);
    root->AddSpacer(spacing);
    root->add_preview(build_preview(), inset, FromDIP(200));
    root->AddSpacer(spacing);
    wxWindow *mapping = build_mapping_section(); // creates m_mapping_list
    root->add_mapping(mapping, m_mapping_list, inset);
    root->AddSpacer(spacing);
    root->Add(build_footer(deferrable), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, inset);
    SetSizer(root);

    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { EndModal(wxID_CANCEL); });
#ifdef __WINDOWS__
    NppDarkMode::SetDarkExplorerTheme(GetHWND());
    NppDarkMode::SetDarkTitleBar(GetHWND()); // dark title bar so the white title text reads in dark mode
#endif
    // width is fixed by the preview cards and mapping rows, Take that width as the
    // floor, and seed it before the first fit so the auto-wrap description wraps at its final width.
    // The height floor comes from the column and changes with the mode and rows, so relayout() sets it.
    const int content_w = GetBestSize().GetWidth();
    SetClientSize(content_w, GetClientSize().GetHeight());
    SetMinClientSize(wxSize(content_w, -1));
    set_mode(false);
    CentreOnScreen();
}

wxWindow *SyncAmsDialog::build_header()
{
    const wxColour card_bg = StateColor::darkModeColorFor(ThemeColor::Grey200);
    auto          *card    = new RoundedRectangle(this, card_bg, StateColor::darkModeColorFor(ThemeColor::Grey350), FromDIP(8));

    m_mode_map       = new CapsuleButton(card, wxID_ANY, _L("Mapping"), true);
    m_mode_overwrite = new CapsuleButton(card, wxID_ANY, _L("Direct Override"), false);
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
    auto *warn_icon = new wxStaticBitmap(card, wxID_ANY, create_scaled_bitmap("warning_gray", card, 16));
    warn_icon->SetBackgroundColour(card_bg); // the card paints its own fill; the bitmap does not inherit it
    auto *warn_text = new ::Label(card, _L("After modification, the filament list cannot be restored."));
    warn_text->SetFont(::Label::Body_12);
    warn_text->SetForegroundColour(StateColor::darkModeColorFor(ThemeColor::TextMuted));
    warn_text->SetBackgroundColour(card_bg);
    warn->Add(warn_icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    warn->Add(warn_text, 0, wxALIGN_CENTER_VERTICAL);

    auto *v = new wxBoxSizer(wxVERTICAL);
    v->Add(modes, 0, wxALIGN_CENTER_HORIZONTAL);
    v->Add(m_desc, 0, wxEXPAND | wxTOP, FromDIP(8));
    v->Add(warn, 0, wxTOP, FromDIP(4));
    v->Add(build_mix_temp_tip(card, card_bg), 0, wxEXPAND | wxTOP, FromDIP(4));

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
    auto *column = static_cast<MappingFirstSizer *>(GetSizer());
    if (!column) return; // still building; the constructor relays out once the column is set
    wxWindowUpdateLocker freeze(this); // the resize and the relayout paint once, at the final layout
    // The floor changes with the mode and the row count, so refresh it on every relayout.
    const wxSize floor = column->GetMinSize();
    SetMinClientSize(wxSize(std::max(GetMinClientSize().x, floor.x), floor.y));
    const wxSize min = GetMinClientSize();

    // A height other than the one we set last came from a user drag; keep it as an offset on the content height.
    if (m_fitted_h > 0) m_height_offset += GetClientSize().y - m_fitted_h;

    // Fit the natural height, but never taller than the screen; the flexible items absorb the cut.
    // Before the first show the dialog has no position yet, so use the parent's display.
    const wxDisplay display(IsShown() || !GetParent() ? static_cast<wxWindow *>(this) : GetParent());
    const int       frame_h = GetSize().y - GetClientSize().y;
    const int       max_h   = display.GetClientArea().height - frame_h;
    const int       h       = std::min(std::min(column->natural_height(), max_h) + m_height_offset, max_h);
    SetClientSize(wxSize(std::max(GetClientSize().x, min.x), std::max(h, min.y)));
    Layout();
    m_fitted_h = GetClientSize().y;
    // Thaw repaints only the children; the dialog's own surface keeps stale pixels where a child moved or hid.
    Refresh();
}

wxSizer *SyncAmsDialog::build_preview()
{
    auto    *h     = new wxBoxSizer(wxHORIZONTAL);
    ::Label *dummy = nullptr;
    h->Add(build_preview_card(_L("Original"), true, &dummy, &m_preview_orig), 1, wxEXPAND);
    h->Add(build_preview_card(_L("After mapping"), false, &m_after_title, &m_preview_mapped), 1, wxEXPAND | wxLEFT, FromDIP(12));

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
    // Mapping mode recolours each mapped source to its destination slot's colour; unmapped sources keep their
    // project colour. The mapping stays in m_mappings across a mode switch, so returning to mapping
    // re-derives this identically.
    std::map<int, std::array<float, 4>> overrides;
    if (m_overwrite) {
        overrides = overwrite_colors();
    } else {
        for (const auto &[src_id, dst] : m_mappings) {
            if (src_id < 0 || !dst || dst->tray_id < 0) continue;
            const std::string &hex = dst->color.empty() && !dst->colors.empty() ? dst->colors.front() : dst->color;
            const wxColour     c   = DevAmsTray::decode_color(hex);
            overrides[src_id]      = {c.Red() / 255.f, c.Green() / 255.f, c.Blue() / 255.f, c.Alpha() / 255.f};
        }
    }
    m_preview_mapped->set_scene(make_current_plate_scene(current_plate(), overrides), reframe);
}

void SyncAmsDialog::rebuild_mapping() { fill_rows(collect_scope_filaments(-1)); }

void SyncAmsDialog::remap_all()
{
    // The matcher shares slots across all sources, so every project filament is matched together; manual
    // picks are discarded.
    auto_map(collect_scope_filaments(-1));
    rebuild_mapping();
    if (m_reset_all) update_reset_all();
}

void SyncAmsDialog::auto_map(const std::vector<FilamentInfo> &source)
{
    std::vector<FilamentInfo> result;
    auto                     *obj = selected_machine();
    if (obj && !source.empty()) {
        // Multi-nozzle printers follow the strategy combo: Precision first requires a matching nozzle
        // (diameter + flow), Color first only prefers one, and colour outranks the filament sub-type in
        // both. Other printers have no combo and keep the classic ranking.
        MatchStrategy strategy;
        if (m_policy) {
            strategy.color_over_subtype = true;
            strategy.nozzle_weight      = m_policy->GetSelection() == 1 ? NozzleMatchWeight::Require : NozzleMatchWeight::Prefer;
        }
        result = match_filaments(obj, source, strategy);

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
        m_mappings[source[i].id]      = std::make_shared<FilamentInfo>(dst);
        m_auto_mappings[source[i].id] = dst;
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
    // only the natural height: the column grows or shrinks the list around it (see MappingFirstSizer). Width
    // is left to the parent — the list expands to the mapping panel (see build_mapping_section).
    auto         *list_sizer      = m_mapping_list->GetSizer();
    const int     row_count       = (int) list_sizer->GetItemCount();
    const int     content_h       = list_sizer->CalcMin().y;
    constexpr int kMaxVisibleRows = 5;
    const int     list_h          = row_count > kMaxVisibleRows ? content_h * kMaxVisibleRows / row_count : content_h;
    m_mapping_list->SetMinSize(wxSize(-1, list_h));
    update_mapped_preview(/*reframe=*/false); // auto-map/policy change: colours only, keep the camera
    update_detail_loss_hint();
    relayout();
}

void SyncAmsDialog::open_slot_picker(MappingRow *row, const FilamentInfo &source, std::shared_ptr<FilamentInfo> current)
{
    if (!m_slot_popup) m_slot_popup = new AmsSlotPopup(this);

    SlotSelectionState st;
    st.source_id   = source.id;
    st.source_type = source.type;
    st.match_all   = m_policy != nullptr; // multi-nozzle printers may pick across filament types
    if (auto *obj = selected_machine()) st.slots = sync_slots_of(obj);
    for (const auto &[src_id, dst] : m_mappings) {
        st.mapping.push_back(*dst);
        st.mapping.back().id = src_id;
    }
    m_slot_popup->set_state(st);

    // Both callbacks write through the shared `current`, so m_mappings sees the change.
    auto apply = [this, row, current](const FilamentInfo &dest) {
        *current = dest;
        row->set_dest(dest, slot_label(dest));
        row->set_selected(true);
        update_mapped_preview(/*reframe=*/false); // repaint the after-mapping preview with the new colour
        update_reset_all();
        if (update_detail_loss_hint()) relayout();
    };
    // The popup resolves the clicked slot's full identity (incl. nozzle geometry) from the machine's live
    // slots, so a manual pick already reads identically to an auto-matched one — take it as is.
    m_slot_popup->on_pick  = apply;
    m_slot_popup->on_reset = [apply] {
        FilamentInfo unmapped;
        unmapped.tray_id = -1;
        apply(unmapped);
    };
    m_slot_popup->popup_at(row->dest_anchor());
}

void SyncAmsDialog::update_reset_all()
{
    bool edited = false;
    for (const auto &[src_id, dst] : m_mappings) {
        auto it = m_auto_mappings.find(src_id);
        if (it == m_auto_mappings.end() || it->second.ams_id != dst->ams_id || it->second.slot_id != dst->slot_id) {
            edited = true;
            break;
        }
    }
    if (m_reset_all->IsShown() == edited) return;
    m_reset_all->Show(edited);
    m_mapping_panel->Layout();
}

bool SyncAmsDialog::update_detail_loss_hint()
{
    // Every project filament counts, not just the shown plate's, since the sync applies all.
    bool loses_detail = false;
    for (const FilamentInfo &src : collect_scope_filaments(-1)) {
        auto it = m_mappings.find(src.id);
        if (it != m_mappings.end() && it->second->tray_id >= 0 && nozzle_loses_detail(src.nozzle_diameter, it->second->nozzle_diameter)) {
            loses_detail = true;
            break;
        }
    }
    if (m_detail_loss_hint->IsShown() == loses_detail) return false;
    m_detail_loss_hint->Show(loses_detail);
    return true;
}

void SyncAmsDialog::reset_all()
{
    for (auto &[src_id, dst] : m_mappings) {
        auto it = m_auto_mappings.find(src_id);
        if (it != m_auto_mappings.end()) *dst = it->second; // in place: rows hold the same shared_ptr
    }
    rebuild_mapping();
    update_reset_all();
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
    // Each row's dest already carries its slot's nozzle diameter (backfilled at pick / auto-map time). Only
    // multi-nozzle printers hand it on: elsewhere the diameter is the printer's own and must not re-pick
    // the filament's nozzle variant. An unmapped dest leaves empty ids, which the preset bundle reads as unmapped.
    const bool record_diameter = m_policy != nullptr;
    for (const auto &[src_id, dst] : m_mappings) {
        if (src_id < 0)
            continue;
        auto &slot   = m_result.sync_maps[src_id];
        slot.ams_id  = dst->ams_id;
        slot.slot_id = dst->slot_id;
        if (record_diameter && dst->nozzle_diameter > 0.)
            slot.nozzle_diameter = std::to_string(dst->nozzle_diameter);
    }
}

wxWindow *SyncAmsDialog::build_preview_card(const wxString &title, bool with_plate_combo, ::Label **out_title, ModelPreview **preview)
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
    hrow->Add(0, ctrl_h); // reserve the row height so the title centres within it
    hrow->Add(header, 0, wxALIGN_CENTER_VERTICAL);
    if (with_plate_combo) {
        m_plate_combo = new ::ComboBox(card, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(72), ctrl_h), 0, nullptr, wxCB_READONLY);
        m_plate_combo->Append(_L("All"));
        for (int i = 0; i < preview_plate_count(); ++i) m_plate_combo->Append(wxString::Format("%d", i + 1));
        m_plate_combo->SetSelection(preview_plate_count() > 0 ? preview_current_plate_index() + 1 : 0); // default: current plate
        m_plate_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &) {
            on_plate_changed();
        });
        hrow->Add(m_plate_combo, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));
    }

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
    auto *trow = new wxBoxSizer(wxHORIZONTAL);
    trow->Add(title, 0, wxALIGN_CENTER_VERTICAL);
    if (is_multi_nozzle_printer()) {
        m_policy = new ::ComboBox(m_mapping_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(124, 26)), 0, nullptr, wxCB_READONLY);
        m_policy->Append(_L("Color first"));
        m_policy->Append(_L("Precision first"));
        m_policy->SetSelection(1); // default: Precision first
        m_policy->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &) { remap_all(); }); // re-map with the new strategy
        trow->Add(m_policy, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));  // right after the label
    }
    trow->AddStretchSpacer();
    m_reset_all = new ScalableButton(m_mapping_panel, wxID_ANY, "reset_gray", wxEmptyString, wxDefaultSize, wxDefaultPosition,
                                     wxBU_EXACTFIT | wxNO_BORDER, true, 16);
    m_reset_all->SetBackgroundColour(m_mapping_panel->GetBackgroundColour());
    m_reset_all->SetToolTip(_L("Reset all filament mapping"));
    m_reset_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { reset_all(); });
    m_reset_all->Hide(); // shown only while a manual edit differs from the auto-map
    trow->Add(m_reset_all, 0, wxALIGN_CENTER_VERTICAL);

    m_mapping_list = new MappingList(m_mapping_panel);
    m_mapping_list->SetBackgroundColour(m_mapping_panel->GetBackgroundColour()); // set before rows: cells theme off this

    m_detail_loss_hint = new wxPanel(m_mapping_panel);
    m_detail_loss_hint->SetBackgroundColour(m_mapping_panel->GetBackgroundColour());
    auto *hint_icon = new wxStaticBitmap(m_detail_loss_hint, wxID_ANY, create_scaled_bitmap("warning", m_detail_loss_hint, 16));
    auto *hint_text = new ::Label(m_detail_loss_hint, _L("Using a larger nozzle after adapting the settings may reduce fine details in your model. Please proceed with caution."),
                                  LB_AUTO_WRAP);
    hint_text->SetFont(::Label::Body_14);
    hint_text->SetForegroundColour(StateColor::darkModeColorFor(ThemeColor::Warning));
    hint_text->SetMinSize(FromDIP(wxSize(640, -1))); // a long translation wraps instead of widening the dialog
    auto *hint = new wxBoxSizer(wxHORIZONTAL);
    // Centred on the first text line rather than the whole label, so a wrapped hint keeps it beside line one.
    hint->Add(hint_icon, 0, wxTOP, std::max(0, (hint_text->GetCharHeight() - hint_icon->GetBestSize().GetHeight()) / 2));
    hint->AddSpacer(FromDIP(6));
    hint->Add(hint_text, 1);
    m_detail_loss_hint->SetSizer(hint);
    m_detail_loss_hint->Hide(); // remap_all below decides whether it shows

    remap_all(); // fills rows and sizes the list to them (width is left to wxEXPAND below)

    auto *v = new wxBoxSizer(wxVERTICAL);
    v->Add(trow, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
    v->Add(m_mapping_list, 1, wxEXPAND);
    v->Add(m_detail_loss_hint, 0, wxEXPAND | wxTOP, FromDIP(16));
    v->Add(build_advanced(), 0, wxEXPAND | wxTOP, FromDIP(16));
    m_mapping_panel->SetSizer(v);
    return m_mapping_panel;
}

wxSizer *SyncAmsDialog::build_advanced()
{
    m_advanced_icon = new wxStaticBitmap(m_mapping_panel, wxID_ANY, create_scaled_bitmap("advanced_option3", m_mapping_panel, 16));
    auto *expander  = new ::Label(m_mapping_panel, _L("Advanced settings"));
    expander->SetFont(::Label::Body_13);
    expander->SetForegroundColour(StateColor::darkModeColorFor(ThemeColor::BrandGreen));
    auto *row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(expander, 0, wxALIGN_CENTER_VERTICAL);
    row->Add(m_advanced_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(2));
    for (wxWindow *w : {static_cast<wxWindow *>(expander), static_cast<wxWindow *>(m_advanced_icon)}) w->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { toggle_advanced(); });

    m_advanced_body = new wxPanel(m_mapping_panel);
    m_advanced_body->SetBackgroundColour(wxGetApp().get_window_default_clr()); // blend, not a grey box
    auto *bs = new wxBoxSizer(wxVERTICAL);
    // Each option is a persisted app preference that the sync itself reads, so the box mirrors and writes that key directly.
    const std::pair<std::string, wxString> options[] = {
        {"enable_append_color_by_sync_ams",
         syncs_ext_spools_only() ? _L("Add unused filaments to filaments list.") : _L("Add unused AMS filaments to filaments list.")},
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
    m_advanced_icon->SetBitmap(create_scaled_bitmap(m_advanced_open ? "advanced_option4" : "advanced_option3", m_mapping_panel, 16));
    relayout(); // the shown body enlarges the content, so the dialog grows to fit it
}

wxSizer *SyncAmsDialog::build_footer(bool deferrable)
{
    auto *cancel = new ::Button(this, deferrable ? _L("Later") : _L("Cancel"));
    auto *ok     = new ::Button(this, _L("Synchronize now"));
    for (::Button *b : {cancel, ok}) {
        b->SetMinSize(FromDIP(wxSize(96, 30)));
        b->SetCornerRadius(FromDIP(12));
    }
    StateColor ok_bg(std::pair<wxColour, int>(ThemeColor::BrandGreenPressed, StateColor::Pressed), std::pair<wxColour, int>(ThemeColor::BrandGreenHovered, StateColor::Hovered),
                     std::pair<wxColour, int>(ThemeColor::BrandGreen, StateColor::Normal));
    StateColor cancel_bg(std::pair<wxColour, int>(ThemeColor::Grey400, StateColor::Pressed), std::pair<wxColour, int>(ThemeColor::Grey300, StateColor::Hovered),
                         std::pair<wxColour, int>(ThemeColor::White, StateColor::Normal));
    ok->SetBackgroundColor(ok_bg);
    ok->SetBorderColorNormal(StateColor::darkModeColorFor(ThemeColor::BrandGreen));
    ok->SetTextColorNormal(ThemeColor::WhiteFixed);
    cancel->SetBackgroundColor(cancel_bg);
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

void SyncAmsDialog::on_dpi_changed(const wxRect &)
{
    // Both heights were measured at the old DPI: carry the user's offset over at the new scale, and forget the
    // fitted height so the system's DPI resize is not taken for a user drag.
    if (prev_scale_factor() > 0.f) m_height_offset = int(std::lround(m_height_offset * scale_factor() / prev_scale_factor()));
    m_fitted_h = 0;
    Refresh();
}

} // namespace Slic3r::GUI
