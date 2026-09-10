#include "NozzleSettingsCardDialog.hpp"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/format.hpp>

#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/graphics.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/ExtruderHelpers.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/ComboBox.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "slic3r/GUI/Widgets/PopupWindow.hpp"
#include "slic3r/GUI/Widgets/AMSItem.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/Widgets/NozzleAmsRow.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r::GUI {
namespace {

constexpr int CARD_W   = 120;
constexpr int CARD_H   = 100;
constexpr int CARD_GAP = 16;

// Card fill/border/corner as ThemeColor tokens; mapped through StateColor's dark map at paint time.
inline wxColour themed(const wxColour &c) { return wxGetApp().dark_mode() ? StateColor::darkModeColorFor(c) : c; }

// encode_volume / decode_volume live in ExtruderHelpers.hpp; the external-AMS row (previews,
// popups, count edits) is encapsulated in the shared NozzleAmsRow widget.

} // namespace

// ---------------------------------------------------------------------------
// NozzleCard — one nozzle as a vertical card
//   [Nozzle N            ▟]   (title + green corner flag)
//   [ 0.4mm            v ]     diameter dropdown
//   [ Standard         v ]     flow dropdown
// ---------------------------------------------------------------------------
class NozzleCard : public wxPanel
{
public:
    // machine_cfg: what the connected printer reports for this nozzle slot right now, or nullptr when
    // no printer is connected. The synced badge compares the card against this, never against the seed.
    NozzleCard(wxWindow *parent, int number, int extruder_index, const NozzleConfig &seed, const NozzleConfig *machine_cfg, const std::vector<std::string> &diameters, std::function<void()> on_changed);

    NozzleConfig GetConfig() const;
    wxString     GetDiameter() const; // parsed value ("" when Empty)
    // show_diameter_warning is false in multi-diameter mode, where every configured diameter is valid.
    void UpdateWarning(const wxString &selected_diameter, bool show_diameter_warning);
    void Rescale();

private:
    void build_header(wxSizer *v, int number);
    void build_combos(wxSizer *v);
    void rebuild_diameter_combo(NozzleVolumeType volume);
    void rebuild_flow_combo(const wxString &diameter);
    bool is_synced() const; // current diameter/volume matches what the connected printer reports for this slot
    void OnPaint(wxPaintEvent &evt);

    int                              m_extruder_index{0};
    NozzleConfig                     m_seed;
    std::optional<NozzleConfig>      m_machine_cfg; // connected printer's config for this slot; unset when disconnected
    std::vector<std::string>         m_diameters;
    std::function<void()>            m_on_changed;
    NozzleStatus                     m_status{NozzleStatus::nsUnknown};
    std::vector<std::pair<int, int>> m_ams_slots;   // preserved and passed back through GetConfig
    bool                             m_warn{false}; // diameter mismatch -> yellow border (single mode)

    Label          *m_title{nullptr};
    wxStaticBitmap *m_warning_icon{nullptr};
    wxBitmap        m_badge_bmp;     // "badge" (synced check), drawn at the card's top-right corner
    bool            m_synced{false}; // whether this nozzle still matches the machine-seeded config
    ComboBox       *m_diameter_combo{nullptr};
    ComboBox       *m_flow_combo{nullptr};
};

NozzleCard::NozzleCard(wxWindow *parent, int number, int extruder_index, const NozzleConfig &seed, const NozzleConfig *machine_cfg, const std::vector<std::string> &diameters, std::function<void()> on_changed)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_extruder_index(extruder_index)
    , m_seed(seed)
    , m_machine_cfg(machine_cfg ? std::optional<NozzleConfig>(*machine_cfg) : std::nullopt)
    , m_diameters(diameters)
    , m_on_changed(std::move(on_changed))
    , m_status(seed.status)
    , m_ams_slots(seed.ams_slots)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(FromDIP(wxSize(CARD_W, CARD_H)));
    Bind(wxEVT_PAINT, &NozzleCard::OnPaint, this);

    auto *v = new wxBoxSizer(wxVERTICAL);
    build_header(v, number);
    build_combos(v);
    SetSizer(v);
}

void NozzleCard::build_header(wxSizer *v, int number)
{
    auto *row = new wxBoxSizer(wxHORIZONTAL);

    m_title = new Label(this, wxString::Format(_L("Nozzle %d"), number));
    m_title->SetFont(Label::Body_13.Bold());
    m_title->SetBackgroundColour(themed(ThemeColor::Grey250));
    row->Add(m_title, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    m_warning_icon = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("warning_serious", this, 14), wxDefaultPosition, FromDIP(wxSize(14, 14)), 0);
    m_warning_icon->Hide();
    row->Add(m_warning_icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxLEFT, FromDIP(2));

    m_badge_bmp = create_scaled_bitmap("badge", this, 16); // drawn at the card's top-right corner in OnPaint

    v->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
}

void NozzleCard::build_combos(wxSizer *v)
{
    auto width       = CARD_W - 2 * 8;
    m_diameter_combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(width), -1), 0, nullptr, wxCB_READONLY);
    rebuild_diameter_combo(m_seed.volume);
    v->Add(m_diameter_combo, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

    v->AddSpacer(FromDIP(6));

    m_flow_combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(width), -1), 0, nullptr, wxCB_READONLY);
    rebuild_flow_combo(m_seed.diameter);
    v->Add(m_flow_combo, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

    m_diameter_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &e) {
        const int sel      = m_diameter_combo->GetSelection();
        wxString  diameter = (sel == m_diameter_combo->GetCount() - 1) ? _L("Empty") : parse_diameter_value(m_diameter_combo->GetValue());
        rebuild_flow_combo(diameter);
        if (m_on_changed) m_on_changed();
        e.Skip();
    });
    m_flow_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &e) {
        const int sel = m_flow_combo->GetSelection();
        if (sel != wxNOT_FOUND) {
            if (void *data = m_flow_combo->GetClientData(sel)) rebuild_diameter_combo(static_cast<NozzleVolumeType>(decode_volume(data)));
        }
        if (m_on_changed) m_on_changed();
        e.Skip();
    });
}

// Rebuild the diameter list for the given volume type. 0.2 nozzle only exists with a Standard
// volume type. Ported from NozzleSettingsListDialog::NozzleRow.
void NozzleCard::rebuild_diameter_combo(NozzleVolumeType volume_type)
{
    const wxString prev     = m_diameter_combo->GetValue();
    const wxString prev_dia = parse_diameter_value(prev);
    const bool     had_list = m_diameter_combo->GetCount() > 0;
    m_diameter_combo->Clear();

    int seed_idx = -1;
    int prev_idx = -1;
    for (const auto &d : m_diameters) {
        if (d == "0.2" && volume_type != NozzleVolumeType::nvtStandard) continue;
        m_diameter_combo->Append(wxString::FromUTF8(d) + " mm", {});
        const int idx = static_cast<int>(m_diameter_combo->GetCount()) - 1;
        if (d == m_seed.diameter.ToStdString()) seed_idx = idx;
        if (had_list && d == prev_dia.ToStdString()) prev_idx = idx;
    }
    m_diameter_combo->Append(_L("Empty"), {});
    const int empty_idx = static_cast<int>(m_diameter_combo->GetCount()) - 1;

    // Keep the user's in-dialog diameter across a flow-driven rebuild; only fall back to the seed
    // (the value at dialog open) when there is no prior selection to preserve.
    int select = 0;
    if (had_list && prev_idx != -1)
        select = prev_idx;
    else if (had_list && prev == _L("Empty"))
        select = empty_idx;
    else if (m_seed.diameter.IsEmpty())
        select = empty_idx;
    else if (seed_idx != -1)
        select = seed_idx;

    m_diameter_combo->SetSelection(select);
}

// Rebuild the flow list from the printer preset that matches `diameter`
void NozzleCard::rebuild_flow_combo(const wxString &diameter)
{
    auto *preset_bundle      = wxGetApp().preset_bundle;
    auto  nozzle_volumes_def = preset_bundle->project_config.def()->get("nozzle_volume_type");

    m_flow_combo->Clear();

    auto printer_preset            = preset_bundle->get_similar_printer_preset({}, diameter.ToStdString());
    auto extruder_variants         = printer_preset->config.option<ConfigOptionStrings>("extruder_variant_list");
    auto extruders_def             = printer_preset->config.def()->get("extruder_type");
    auto extruders                 = printer_preset->config.option<ConfigOptionEnumsGeneric>("extruder_type");
    auto extruder_max_nozzle_count = printer_preset->config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");

    const int  idx    = std::max(0, std::min(m_extruder_index, static_cast<int>(extruders->values.size()) - 1));
    const auto type   = extruders_def->enum_labels[extruders->values[idx]];
    const int  wanted = static_cast<int>(m_seed.volume);
    int        select = -1;

    for (int j = 0; j < (int) nozzle_volumes_def->enum_labels.size(); ++j) {
        const bool variant_ok = boost::algorithm::contains(extruder_variants->values[idx], type + " " + nozzle_volumes_def->enum_labels[j]);
        const bool hybrid_ok  = extruder_max_nozzle_count->values[idx] > 1 && nozzle_volumes_def->enum_keys_map->at(nozzle_volumes_def->enum_values[j]) == nvtHybrid;
        if (!variant_ok && !hybrid_ok) continue;

        auto cur_volume_type = nozzle_volumes_def->enum_keys_map->at(nozzle_volumes_def->enum_values[j]);
        if (cur_volume_type == NozzleVolumeType::nvtE3DHighFlow && diameter.ToStdString() != "0.4" && diameter.ToStdString() != "0.6") continue;
        if (diameter.ToStdString() == "0.2" && cur_volume_type == NozzleVolumeType::nvtHighFlow) continue;
        if (cur_volume_type == NozzleVolumeType::nvtHybrid) continue;

        if (wanted == j) select = m_flow_combo->GetCount();
        m_flow_combo->Append(_L(nozzle_volumes_def->enum_labels[j]), {}, encode_volume(j));
    }
    if (select == -1) select = m_flow_combo->GetCount() - 1;
    m_flow_combo->SetSelection(select);
}

wxString NozzleCard::GetDiameter() const
{
    wxString d = m_diameter_combo->GetValue();
    if (d == _L("Empty")) return wxEmptyString;
    d.Replace(" mm", "");
    return d;
}

NozzleConfig NozzleCard::GetConfig() const
{
    NozzleConfig cfg;
    wxString     diameter_text = m_diameter_combo->GetValue();
    if (diameter_text == _L("Empty")) {
        cfg.diameter = wxEmptyString;
    } else {
        diameter_text.Replace(" mm", "");
        cfg.diameter = diameter_text;
    }
    const int sel = m_flow_combo->GetSelection();
    if (sel != wxNOT_FOUND) {
        if (void *data = m_flow_combo->GetClientData(sel)) cfg.volume = static_cast<NozzleVolumeType>(decode_volume(data));
    }
    cfg.status    = m_status;
    cfg.ams_slots = m_ams_slots;
    return cfg;
}

// Synced = a printer is connected and this nozzle's diameter/volume matches what that printer reports
// for this slot. No connected printer, or an empty nozzle, is never synced.
bool NozzleCard::is_synced() const
{
    if (!m_machine_cfg) return false;
    const NozzleConfig cur = GetConfig();
    if (cur.diameter.IsEmpty()) return false;
    return cur.diameter == m_machine_cfg->diameter && cur.volume == m_machine_cfg->volume;
}

void NozzleCard::UpdateWarning(const wxString &selected_diameter, bool show_diameter_warning)
{
    if (m_diameter_combo->GetSelection() != -1 && m_flow_combo->GetSelection() != -1 && m_status != NozzleStatus::nsNormal) m_status = NozzleStatus::nsNormal;

    wxString   diameter = GetDiameter();
    const bool empty    = diameter.IsEmpty();
    const bool mismatch = show_diameter_warning && !empty && diameter != selected_diameter;

    if (m_warning_icon) {
        m_warning_icon->SetBitmap(create_scaled_bitmap(empty ? "warning_serious" : "warning", this, 14));
        m_warning_icon->Show(empty || mismatch);
    }
    const wxColour new_title_color = themed((empty || mismatch) ? ThemeColor::Warning : ThemeColor::TextPrimary);
    if (m_title) {
        const wxColour prev_color = m_title->GetForegroundColour();
        m_title->SetForegroundColour(new_title_color);
        if (prev_color != new_title_color) m_title->Refresh(); // title color changed -> repaint it
    }

    const bool synced = is_synced();
    if (m_warn != mismatch || m_synced != synced) { // border color / corner badge changed -> repaint
        m_warn   = mismatch;
        m_synced = synced;
        Refresh();
    }

    // Explain the yellow mark on hover; only single-diameter mode flags a diameter mismatch.
    auto set_tip = [mismatch](wxWindow *w) {
        if (!w) return;
        if (mismatch)
            w->SetToolTip(_L("Incompatible with the slice diameter"));
        else
            w->UnsetToolTip();
    };
    set_tip(this);
    set_tip(m_diameter_combo);
    set_tip(m_flow_combo);

    Layout();
}

void NozzleCard::Rescale()
{
    SetMinSize(FromDIP(wxSize(CARD_W, CARD_H)));
    if (m_diameter_combo) m_diameter_combo->SetMinSize(FromDIP(wxSize(91, 24)));
    if (m_flow_combo) m_flow_combo->SetMinSize(FromDIP(wxSize(91, 24)));
    Layout();
}

// Rounded card: white body. Border is grey normally and yellow (Warning) when the nozzle's diameter
// differs from the selected one (single-diameter mode). The "Nozzle N" pill draws its own background.
void NozzleCard::OnPaint(wxPaintEvent &)
{
    wxPaintDC          pdc(this);
    wxGCDC             dc(pdc);
    wxGraphicsContext *gc = dc.GetGraphicsContext();
    if (gc == nullptr) return;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    const double w  = GetClientSize().GetWidth();
    const double h  = GetClientSize().GetHeight();
    const double r  = FromDIP(6);
    const double bw = FromDIP(1); // border thickness

    const wxColour outside = themed(wxColour("#F7F7F7"));
    const wxColour card_bg = themed(ThemeColor::White);
    const wxColour border  = m_warn ? themed(ThemeColor::Warning) : themed(ThemeColor::Grey300);

    // Fill the corners with the container color so the rounded card sits cleanly on it.
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(outside));
    gc->DrawRectangle(0, 0, w, h);

    // Even border: paint the whole card in the border color, then lay the body on top inset by the
    // border width. The outline is the exposed ring, so its thickness is geometric and identical on
    // every edge — a centered 1px pen splits the half-pixel unevenly under fractional DPI scaling.
    gc->SetBrush(wxBrush(border));
    gc->DrawRoundedRectangle(0, 0, w, h, r);

    gc->SetBrush(wxBrush(card_bg));
    gc->DrawRoundedRectangle(bw, bw, w - 2 * bw, h - 2 * bw, std::max(0.0, r - bw));

    // A rounded grey pill behind the "Nozzle N" label
    if (m_title) {
        const wxRect tr = m_title->GetRect();
        if (tr.GetWidth() > 0) {
            const double px = FromDIP(6);
            const double py = FromDIP(3);
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(themed(ThemeColor::Grey250)));
            gc->DrawRoundedRectangle(tr.GetX() - px, tr.GetY() - py, tr.GetWidth() + 2 * px, tr.GetHeight() + 2 * py, FromDIP(4));
        }
    }

    // Synced badge at the card's top-right corner.
    if (m_synced && m_badge_bmp.IsOk()) {
        const double bw    = FromDIP(16);
        const double inset = FromDIP(4);
        gc->DrawBitmap(m_badge_bmp, w - bw - inset, inset, bw, bw);
    }
}

// ---------------------------------------------------------------------------
// NozzleSettingsCardDialog
// ---------------------------------------------------------------------------
NozzleSettingsCardDialog::NozzleSettingsCardDialog(wxWindow *parent, const wxString &selected_diameter, const std::vector<NozzleConfig> &configs)
    : DPIDialog(parent, wxID_ANY, _L("Nozzle Settings"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_multi_diameter_mode(multi_nozzle_diameter_mode_enabled(wxGetApp().preset_bundle->project_config))
    , m_selected_diameter(selected_diameter)
{
    std::vector<NozzleConfig> seeds = configs;
    if (seeds.empty()) seeds.emplace_back();

    SetBackgroundColour(*wxWHITE);
    create_layout(selected_diameter, seeds);
    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void NozzleSettingsCardDialog::create_layout(const wxString &selected_diameter, const std::vector<NozzleConfig> &seeds)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % Slic3r::resources_dir()).str();
    SetIcon(wxIcon(Slic3r::encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    auto *main_sizer = new wxBoxSizer(wxVERTICAL);
    // Single-diameter mode picks one slicing diameter (selector shown, mismatched cards flagged);
    // multi-diameter mode allows every configured diameter, so the selector is hidden.
    if (!m_multi_diameter_mode) build_diameter_selector(main_sizer, selected_diameter);
    build_card_grid(main_sizer, seeds);
    build_ams_row(main_sizer);
    build_warning_panels(main_sizer);
    build_button_bar(main_sizer);

    SetSizer(main_sizer);
    Layout();
    Fit();

    // The warning panels are hidden here, so the dialog width is driven by the card grid. Wrap the
    // warning messages to that width up front so showing one later (a long single line otherwise
    // stretches the dialog wider than the grid) keeps the dialog at its grid width.
    const int wrap_w = GetClientSize().GetWidth() - FromDIP(60);
    if (m_warning_note_label && wrap_w > 0) m_warning_note_label->Wrap(wrap_w);
    if (m_warning_label && wrap_w > 0) m_warning_label->Wrap(wrap_w);
    if (m_warning_serious_label && wrap_w > 0) m_warning_serious_label->Wrap(wrap_w);

    setup_callbacks();
    update_state();
}

void NozzleSettingsCardDialog::build_diameter_selector(wxBoxSizer *main_sizer, const wxString &selected_diameter)
{
    auto *label = new Label(this, _L("Select Nozzle Diameter"));
    label->SetFont(Label::Body_14.Bold());
    main_sizer->Add(label, 0, wxALIGN_LEFT | wxLEFT | wxTOP, FromDIP(20));

    std::vector<wxString> choices;
    for (const auto &d : wxGetApp().preset_bundle->printers.diameters_of_selected_printer()) choices.push_back(wxString::FromUTF8(d));
    m_diameter_panel = new DiameterButtonPanel(this, choices, false);
    m_diameter_panel->SetSelectedDiameter(selected_diameter);
    main_sizer->Add(m_diameter_panel, 0, wxEXPAND | wxLEFT, FromDIP(20));
}

void NozzleSettingsCardDialog::build_card_grid(wxBoxSizer *main_sizer, const std::vector<NozzleConfig> &seeds)
{
    auto *label = new Label(this, _L("Set your printer's nozzle installation status"));
    label->SetFont(Label::Body_14.Bold());
    main_sizer->Add(label, 0, wxALIGN_LEFT | wxLEFT | wxTOP, FromDIP(20));

    auto *container = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    container->SetBackgroundColour(*wxWHITE);
    auto *grid    = new wxBoxSizer(wxVERTICAL);
    auto *top_row = new wxBoxSizer(wxHORIZONTAL); // even nozzle numbers
    auto *bot_row = new wxBoxSizer(wxHORIZONTAL); // odd nozzle numbers

    auto on_changed = [this]() { update_state(); };
    auto diameters  = wxGetApp().preset_bundle->printers.diameters_of_selected_printer();

    // What the connected printer reports right now; the per-card synced badge compares against this,
    // never against the seed. A disconnected printer leaves `synced` false, so no card is ever synced.
    const MachineNozzleBaseline baseline     = collect_machine_nozzle_baseline(false); // card dialog is single-extruder
    const auto                 &machine_cfgs = baseline.configs[0];

    // Single extruder: nozzles number 1..n and are placed by parity — even on top, odd on the bottom.
    for (int i = 0; i < (int) seeds.size(); ++i) {
        const int           number      = i + 1;
        const NozzleConfig *machine_cfg = (baseline.synced && i < (int) machine_cfgs.size()) ? &machine_cfgs[i] : nullptr;
        auto               *card        = new NozzleCard(container, number, 0, seeds[i], machine_cfg, diameters, on_changed);
        m_cards.push_back(card);
        auto *row = (number % 2 == 0) ? top_row : bot_row;
        row->Add(card, 0, wxRIGHT | wxBOTTOM, FromDIP(CARD_GAP));
    }

    if (top_row->GetItemCount() > 0) grid->Add(top_row, 0, wxLEFT | wxTOP | wxCENTER, FromDIP(CARD_GAP));
    grid->Add(bot_row, 0, wxLEFT | wxTOP | wxCENTER, FromDIP(CARD_GAP));

    container->SetSizer(grid);
    main_sizer->Add(container, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
}

void NozzleSettingsCardDialog::build_warning_panels(wxBoxSizer *main_sizer)
{
    m_warning_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_warning_panel->SetBackgroundColour(*wxWHITE);
    auto *wv = new wxBoxSizer(wxVERTICAL);

    // Top line: why a mismatch matters at all — single-diameter mode slices a single diameter.
    auto *note = new Label(m_warning_panel, _L("Note: Mixed-diameter slicing is only supported in multi-diameter mode."));
    note->SetFont(Label::Body_13);
    note->SetForegroundColour(ThemeColor::Warning);
    wv->Add(note, 0, wxEXPAND);

    // Second line: the warning icon as a legend for the mark shown on mismatched cards.
    auto *ws   = new wxBoxSizer(wxHORIZONTAL);
    auto *icon = new wxStaticBitmap(m_warning_panel, wxID_ANY, create_scaled_bitmap("warning", this, 16), wxDefaultPosition, FromDIP(wxSize(16, 16)), 0);
    auto *l    = new Label(m_warning_panel, _L("marks nozzles whose diameter differs from the slice setting; they cannot be used during slicing/printing."));
    l->SetFont(Label::Body_13);
    l->SetForegroundColour(ThemeColor::Warning);
    ws->Add(icon, 0, wxRIGHT, FromDIP(2));
    ws->Add(l, 1, wxEXPAND);
    wv->Add(ws, 0, wxEXPAND | wxTOP, FromDIP(2));

    m_warning_panel->SetSizer(wv);
    main_sizer->Add(m_warning_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
    m_warning_panel->Hide();
    m_warning_note_label = note;
    m_warning_label      = l;

    m_warning_serious_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_warning_serious_panel->SetBackgroundColour(*wxWHITE);
    auto *ss    = new wxBoxSizer(wxHORIZONTAL);
    auto *sicon = new wxStaticBitmap(m_warning_serious_panel, wxID_ANY, create_scaled_bitmap("warning_serious", this, 16), wxDefaultPosition, FromDIP(wxSize(16, 16)), 0);
    auto *sl    = new Label(m_warning_serious_panel, _L("Empty nozzles cannot be used during slicing/printing."));
    sl->SetFont(Label::Body_13);
    sl->SetForegroundColour(ThemeColor::Danger);
    ss->Add(sicon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    ss->Add(sl, 0, wxALIGN_CENTER_VERTICAL);
    m_warning_serious_panel->SetSizer(ss);
    main_sizer->Add(m_warning_serious_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
    m_warning_serious_panel->Hide();
    m_warning_serious_label = sl;
}

void NozzleSettingsCardDialog::build_button_bar(wxBoxSizer *main_sizer)
{
    StateColor ok_bg(std::pair<wxColour, int>(ThemeColor::BrandGreenPressed, StateColor::Pressed), std::pair<wxColour, int>(ThemeColor::BrandGreenHovered, StateColor::Hovered),
                     std::pair<wxColour, int>(ThemeColor::BrandGreen, StateColor::Normal));
    StateColor ok_text(std::pair<wxColour, int>(ThemeColor::White, StateColor::Normal));
    StateColor cancel_bg(std::pair<wxColour, int>(ThemeColor::Grey400, StateColor::Pressed), std::pair<wxColour, int>(ThemeColor::Grey300, StateColor::Hovered),
                         std::pair<wxColour, int>(ThemeColor::White, StateColor::Normal));
    StateColor cancel_bd(std::pair<wxColour, int>(ThemeColor::TextPrimary, StateColor::Normal));

    m_confirm_btn = new Button(this, _L("Confirm"));
    m_confirm_btn->SetMinSize(wxSize(FromDIP(62), FromDIP(24)));
    m_confirm_btn->SetCornerRadius(FromDIP(12));
    m_confirm_btn->SetBackgroundColor(ok_bg);
    m_confirm_btn->SetFont(Label::Body_12);
    m_confirm_btn->SetBorderColor(ThemeColor::BrandGreen);
    m_confirm_btn->SetTextColor(ok_text);
    m_confirm_btn->SetId(wxID_OK);

    m_cancel_btn = new Button(this, _L("Cancel"));
    m_cancel_btn->SetMinSize(wxSize(FromDIP(62), FromDIP(24)));
    m_cancel_btn->SetCornerRadius(FromDIP(12));
    m_cancel_btn->SetBackgroundColor(cancel_bg);
    m_cancel_btn->SetFont(Label::Body_12);
    m_cancel_btn->SetBorderColor(cancel_bd);
    m_cancel_btn->SetTextColor(cancel_bd);
    m_cancel_btn->SetId(wxID_CANCEL);
    m_cancel_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });

    // AMS edits persist to the live registry the moment they are made, so every dismissal other than
    // Confirm must revert them. Funnel the Cancel button through the same close path as the window's
    // X button; the system close reaches EndModal without this handler otherwise, keeping the edits.
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) {
        if (m_ams_row) m_ams_row->restore();
        EndModal(wxID_CANCEL);
    });

    auto *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();
    btn_sizer->Add(m_confirm_btn, 0, wxRIGHT, FromDIP(12));
    btn_sizer->Add(m_cancel_btn, 0);
    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, FromDIP(20));
}

void NozzleSettingsCardDialog::setup_callbacks()
{
    if (!m_diameter_panel) return;
    m_diameter_panel->SetNozzleQueryCallback([this](const wxString &diameter) -> bool {
        for (auto *c : m_cards)
            if (c->GetDiameter() == diameter) return true;
        return false;
    });
    m_diameter_panel->Bind(wxEVT_BUTTON, [this](wxCommandEvent &e) {
        e.Skip();
        CallAfter([this]() { update_state(); });
    });
}

void NozzleSettingsCardDialog::update_state()
{
    // Diameter mismatch only matters in single-diameter mode (where a selector picks one diameter).
    const bool     show_dia_warning = !m_multi_diameter_mode;
    const wxString selected         = m_diameter_panel ? m_diameter_panel->GetSelectedDiameter() : "";
    bool           has_warning      = false;
    bool           has_empty        = false;

    for (auto *c : m_cards) {
        c->UpdateWarning(selected, show_dia_warning);
        const wxString d = c->GetDiameter();
        if (d.IsEmpty())
            has_empty = true;
        else if (show_dia_warning && d != selected)
            has_warning = true;
    }

    if (m_warning_panel) m_warning_panel->Show(has_warning);
    if (m_warning_serious_panel) m_warning_serious_panel->Show(has_empty);
    if (m_diameter_panel) m_diameter_panel->UpdateButtonStates();

    Layout();
    Fit();
    wxGetApp().UpdateDarkUIWin(this);
}

wxString NozzleSettingsCardDialog::GetSelectedDiameter() const
{
    // Multi-diameter mode has no selector: keep the pre-dialog diameter so the caller's write-back
    // doesn't trigger a spurious diameter switch.
    return m_diameter_panel ? m_diameter_panel->GetSelectedDiameter() : m_selected_diameter;
}

std::vector<NozzleConfig> NozzleSettingsCardDialog::GetExtruderConfigs(int index) const
{
    std::vector<NozzleConfig> configs;
    if (index != 0) return configs; // single extruder
    configs.reserve(m_cards.size());
    for (auto *c : m_cards) configs.push_back(c->GetConfig());
    return configs;
}

// External-AMS row: a standalone Grey200 band carrying the shared NozzleAmsRow widget. wxEXPAND
// stretches the band full width; the widget self-hides when the printer has no AMS attach point.
void NozzleSettingsCardDialog::build_ams_row(wxBoxSizer *main_sizer)
{
    m_ams_row = new NozzleAmsRow(this, 0, _L("External AMS"), NozzleAmsRow::Placement::StandaloneRow, [this] {
        Layout();
        Fit();
    });
    main_sizer->Add(m_ams_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
}

void NozzleSettingsCardDialog::on_dpi_changed(const wxRect & /*suggested_rect*/)
{
    if (m_diameter_panel) m_diameter_panel->Rescale();
    for (auto *c : m_cards) c->Rescale();
    if (m_ams_row) m_ams_row->Rescale();

    auto rescale_btn = [this](Button *btn) {
        if (!btn) return;
        btn->SetMinSize(wxSize(FromDIP(62), FromDIP(24)));
        btn->SetCornerRadius(FromDIP(12));
        btn->Rescale();
    };
    rescale_btn(m_confirm_btn);
    rescale_btn(m_cancel_btn);

    Layout();
    Fit();
    Refresh();
}

} // namespace Slic3r::GUI
