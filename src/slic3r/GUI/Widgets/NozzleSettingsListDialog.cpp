#include "NozzleSettingsListDialog.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/format.hpp>

#include <wx/gdicmn.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>

#include "slic3r/GUI/ExtruderPanel.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/ExtruderHelpers.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/FilamentBitmapUtils.hpp"
#include "slic3r/GUI/Widgets/NozzleAmsRow.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/ComboBox.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "slic3r/GUI/Widgets/StaticBox.hpp"
#include "slic3r/GUI/Widgets/AMSItem.hpp"
#include "slic3r/GUI/Widgets/MultiNozzleSync.hpp"
#include "slic3r/GUI/DeviceCore/DevConfigUtil.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSwitch.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r::GUI {
namespace {

// FromDIP is a wxWindow member; NozzleRow is a plain class, so scale through a window.
inline int    dip(const wxWindow *w, int v) { return w ? w->FromDIP(v) : v; }
inline wxSize dip(const wxWindow *w, int x, int y) { return w ? w->FromDIP(wxSize(x, y)) : wxSize(x, y); }

MachineObject *selected_machine()
{
    auto *dev = wxGetApp().getDeviceManager();
    return dev ? dev->get_selected_machine() : nullptr;
}

// One filament's display data, resolved live from the selected machine's AMS tray. Only the row
// view shows per-slot filament indicators, so this lives here rather than in a shared header.
struct FilamentDisplay
{
    enum class State { Empty, Unknown, Normal };
    State                 state{State::Empty};
    wxString              name;
    std::vector<wxColour> colors;
    bool                  is_gradient{false};
};

// Resolve an <ams_id, slot_id> into display data from the currently selected machine.
// No/absent tray -> Empty; loaded but no filament id -> Unknown; else Normal with sub-brand + colour.
FilamentDisplay resolve_filament_display(const std::pair<int, int> &slot)
{
    FilamentDisplay fd;
    const auto [ams_id, slot_id] = slot;
    if (ams_id < 0 || slot_id < 0) return fd;

    MachineObject *obj = selected_machine();
    if (!obj) return fd;

    auto tray_opt = obj->get_tray(ams_id, slot_id);
    if (!tray_opt.has_value()) return fd; // Empty

    const DevAmsTray &tray   = *tray_opt;
    const bool        is_ext = tray.ams_type == DevAmsType::EXT_SPOOL;
    if (!tray.is_exists && !is_ext) return fd; // Empty

    if (tray.get_filament_id().empty()) {
        fd.state = FilamentDisplay::State::Unknown;
        return fd;
    }

    fd.state = FilamentDisplay::State::Normal;
    fd.name  = !tray.sub_brands.empty() ? wxString::FromUTF8(tray.sub_brands) : wxString::FromUTF8(tray.m_fila_type);
    for (const std::string &hex : tray.cols) fd.colors.push_back(DevAmsTray::decode_color(hex));
    if (fd.colors.empty()) fd.colors.push_back(tray.get_color());
    fd.is_gradient = tray.ctype == DevFilaColorType::CTYPE_GRADIANT && fd.colors.size() >= 2;
    return fd;
}

// Filament switcher icon between the two columns, mirroring the sidebar: shown only while the edits
// still match a connected printer that has a switcher installed; Error until it is calibrated.
CustomSeparator::State filament_switcher_state(bool synced)
{
    if (!synced) return CustomSeparator::State::Hidden;
    MachineObject *obj = selected_machine();
    if (!obj || !obj->is_online()) return CustomSeparator::State::Hidden;
    std::shared_ptr<DevFilaSwitch> fila_switch = obj->GetFilaSwitch();
    if (!fila_switch || !fila_switch->IsInstalled()) return CustomSeparator::State::Hidden;
    return fila_switch->IsReady() ? CustomSeparator::State::Normal : CustomSeparator::State::Error;
}

bool extruder_configs_match(const std::vector<NozzleConfig> &edited, const std::vector<NozzleConfig> &seed)
{
    if (edited.size() != seed.size()) return false;
    for (size_t i = 0; i < edited.size(); ++i) {
        if (edited[i].diameter != seed[i].diameter) return false;
        if (!edited[i].diameter.IsEmpty() && edited[i].volume != seed[i].volume) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// NozzleRow
//
// One nozzle's editable controls, led by "Nozzle N" (its 1-based slot on the extruder), or
// plain "Nozzle" when the extruder has a single slot. One filament view is added per bound
// AMS slot
//
// Layout:
//   [Nozzle N] [diameter v] [flow v] [Error|Unknown] [Filament ● name]...
// ---------------------------------------------------------------------------

class NozzleRow
{
public:
    NozzleRow(wxWindow                       *parent,
              int                             index,
              int                             nozzle_count,
              int                             extruder_index,
              const NozzleConfig             &seed,
              const std::vector<std::string> &diameters,
              std::function<void()>           on_changed,
              std::function<void()>           on_error_click);

    wxSizer     *GetSizer() const { return m_sizer; }
    NozzleConfig GetConfig() const;
    wxString     GetDiameter() const; // parsed value ("" when Empty)
    void         RefreshState();
    void         Rescale();

private:
    // One filament indicator (color swatch + name)
    struct FilamentView
    {
        wxPanel        *panel{nullptr};
        StaticBox      *box{nullptr};
        wxStaticBitmap *swatch{nullptr}; // multi/gradient color bitmap (Normal only)
        Label          *label{nullptr};
        wxStaticText   *question{nullptr};
    };

    void         build_combos(wxWindow *parent);
    void         build_status_label(wxWindow *parent);
    void         rebuild_diameter_combo(NozzleVolumeType volume);
    void         rebuild_flow_combo(const wxString &diameter);
    void         build_filament_views(wxWindow *parent);
    FilamentView build_filament_view(wxWindow *parent, const FilamentDisplay &fd);
    bool         apply_error_state(); // renders Error/Unknown state; true if that state applies

    wxWindow                *m_parent{nullptr};
    int                      m_extruder_index{0};
    std::vector<std::string> m_diameters;
    NozzleConfig             m_seed;
    std::function<void()>    m_on_changed;
    std::function<void()>    m_on_error_click;

    wxBoxSizer *m_sizer{nullptr};

    Label          *m_label{nullptr}; // "Nozzle N"; also the click target of an Error nozzle
    ComboBox       *m_diameter_combo{nullptr};
    ComboBox       *m_flow_combo{nullptr};
    Label          *m_status_label{nullptr}; // "Error" / "Unknown" text, driven by m_status

    std::vector<FilamentView> m_filaments; // one per bound AMS slot

    NozzleStatus                     m_status{NozzleStatus::nsUnknown};
    std::vector<std::pair<int, int>> m_ams_slots;
    bool                             m_error_click_bound{false};
};

NozzleRow::NozzleRow(wxWindow                       *parent,
                     int                             index,
                     int                             nozzle_count,
                     int                             extruder_index,
                     const NozzleConfig             &seed,
                     const std::vector<std::string> &diameters,
                     std::function<void()>           on_changed,
                     std::function<void()>           on_error_click)
    : m_parent(parent)
    , m_extruder_index(extruder_index)
    , m_diameters(diameters)
    , m_seed(seed)
    , m_on_changed(std::move(on_changed))
    , m_on_error_click(std::move(on_error_click))
    , m_status(seed.status)
    , m_ams_slots(seed.ams_slots)
{
    m_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Same 1-based numbering as the sidebar's nozzle tiles; the seeds arrive slot-ordered.
    m_label = new Label(parent, nozzle_count > 1 ? wxString::Format(_L("Nozzle %d "), index + 1) : _L("Nozzle"));
    m_sizer->Add(m_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, dip(parent, 4));

    build_combos(parent);
    build_status_label(parent);
    build_filament_views(parent);
}

void NozzleRow::build_combos(wxWindow *parent)
{
    m_diameter_combo = new ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, dip(parent, 110, 24), 0, nullptr, wxCB_READONLY);
    rebuild_diameter_combo(m_seed.volume);
    m_sizer->Add(m_diameter_combo, 0, wxRESERVE_SPACE_EVEN_IF_HIDDEN | wxALIGN_CENTER_VERTICAL | wxRIGHT, dip(parent, 4));

    m_flow_combo = new ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, dip(parent, 110, 24), 0, nullptr, wxCB_READONLY);
    rebuild_flow_combo(m_seed.diameter);
    m_sizer->Add(m_flow_combo, 0, wxRESERVE_SPACE_EVEN_IF_HIDDEN | wxALIGN_CENTER_VERTICAL | wxRIGHT, dip(parent, 4));

    // The two combos rebuild each other: changing the volume type reshapes the
    // available diameters (0.2 is Standard-only), changing the diameter reshapes flows.
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

void NozzleRow::build_status_label(wxWindow *parent)
{
    m_status_label = new Label(parent, wxEmptyString);
    m_status_label->SetForegroundColour(ThemeColor::Danger);
    m_status_label->Hide();
    m_sizer->Add(m_status_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, dip(parent, 4));
}

// Build one filament indicator (color swatch + name) rendered from `fd`:
// Empty / Unknown (white box + "?") / Normal (sub-brand name + swatch color).
NozzleRow::FilamentView NozzleRow::build_filament_view(wxWindow *parent, const FilamentDisplay &fd)
{
    FilamentView filament;
    filament.panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    filament.panel->SetBackgroundColour(wxColour("#F7F7F7"));
    filament.label = new Label(filament.panel, wxEmptyString);
    filament.box   = new StaticBox(filament.panel, wxID_ANY, wxDefaultPosition, dip(parent, 16, 16), wxBORDER_NONE);
    filament.box->SetCornerRadius(dip(parent, 2));
    filament.box->SetBorderColor(wxColour("#BFBFBF"));
    filament.question = new wxStaticText(filament.box, wxID_ANY, "?");
    filament.question->SetFont(Label::Body_10);
    filament.question->SetForegroundColour(wxColour("#5C5C5C"));
    filament.question->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
    filament.question->Hide();
    filament.swatch = new wxStaticBitmap(filament.box, wxID_ANY, wxNullBitmap);
    filament.swatch->Hide();
    auto *box_sizer = new wxBoxSizer(wxHORIZONTAL);
    box_sizer->AddStretchSpacer();
    box_sizer->Add(filament.question, 0, wxALIGN_CENTER_VERTICAL);
    box_sizer->Add(filament.swatch, 1, wxEXPAND);
    box_sizer->AddStretchSpacer();
    filament.box->SetSizer(box_sizer);

    const wxSize swatch_size = dip(parent, 16, 16);
    switch (fd.state) {
    case FilamentDisplay::State::Empty:
        filament.label->SetLabel(_L("Empty"));
        filament.box->SetBackgroundColour(*wxWHITE);
        filament.box->SetBorderWidth(dip(parent, 1));
        break;
    case FilamentDisplay::State::Unknown:
        filament.label->SetLabel(_L("Unknown Filament"));
        filament.box->SetBackgroundColour(*wxWHITE);
        filament.box->SetBorderWidth(dip(parent, 1));
        filament.question->Show();
        break;
    case FilamentDisplay::State::Normal:
        filament.label->SetLabel(fd.name);
        filament.box->SetBorderWidth(0);
        filament.swatch->SetBitmap(create_filament_bitmap(fd.colors, swatch_size, fd.is_gradient));
        filament.swatch->Show();
        break;
    }

    auto *filament_sizer = new wxBoxSizer(wxHORIZONTAL);
    filament_sizer->Add(filament.box, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, dip(parent, 4));
    filament_sizer->Add(filament.label, 0, wxALIGN_CENTER_VERTICAL);
    filament.panel->SetSizer(filament_sizer);
    return filament;
}

// One view per bound AMS slot. Name/color is resolved live from the selected machine
// at build time via the slot coordinates.
void NozzleRow::build_filament_views(wxWindow *parent)
{
    if (m_ams_slots.empty()) return;

    auto *tt = new Label(parent, _L("Filament"));
    m_sizer->Add(tt, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, dip(parent, 4));

    for (const auto &slot : m_ams_slots) {
        FilamentView filament = build_filament_view(parent, resolve_filament_display(slot));
        m_sizer->Add(filament.panel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, dip(parent, 4));
        m_filaments.push_back(filament);
    }
}

// Rebuild the diameter list for the given volume type. 0.2 nozzle only exists with a
// Standard volume type, so it is removed whenever a non-Standard volume type is selected.
//
// Keep the in-edit diameter across rebuilds. m_seed is the value at dialog open and
// must only be a fallback — otherwise picking a flow type snaps the diameter back.
void NozzleRow::rebuild_diameter_combo(NozzleVolumeType volume_type)
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
        if (d == m_seed.diameter.ToStdString())
            seed_idx = idx;
        if (had_list && d == prev_dia.ToStdString())
            prev_idx = idx;
    }
    m_diameter_combo->Append(_L("Empty"), {});
    const int empty_idx = static_cast<int>(m_diameter_combo->GetCount()) - 1;

    int select = 0;
    if (had_list && prev_idx != -1)
        select = prev_idx;
    else if (had_list && prev == _L("Empty"))
        select = empty_idx;
    else if (m_seed.diameter.IsEmpty())
        // An empty-diameter seed (non-existent / abnormal nozzle) maps to the trailing
        // "Empty" entry, not the first real diameter — otherwise the nozzle is silently
        // persisted as configured.
        select = empty_idx;
    else if (seed_idx != -1)
        select = seed_idx;

    m_diameter_combo->SetSelection(select);
}

// Rebuild the flow list from the printer preset that matches `diameter`
void NozzleRow::rebuild_flow_combo(const wxString &diameter)
{
    auto *preset_bundle      = wxGetApp().preset_bundle;
    auto  nozzle_volumes_def = preset_bundle->project_config.def()->get("nozzle_volume_type");

    // Keep the in-edit flow across rebuilds. m_seed.volume is only the value at
    // dialog open; using it as the sole target snaps the flow back after a diameter
    // change (e.g. 0.8 then High Flow cannot stick).
    int prev_volume = -1;
    {
        const int sel = m_flow_combo->GetSelection();
        if (sel != wxNOT_FOUND)
            if (void *data = m_flow_combo->GetClientData(sel))
                prev_volume = decode_volume(data);
    }
    const bool had_list = m_flow_combo->GetCount() > 0;

    m_flow_combo->Clear();

    auto printer_preset            = preset_bundle->get_similar_printer_preset({}, diameter.ToStdString());
    auto extruder_variants         = printer_preset->config.option<ConfigOptionStrings>("extruder_variant_list");
    auto extruders_def             = printer_preset->config.def()->get("extruder_type");
    auto extruders                 = printer_preset->config.option<ConfigOptionEnumsGeneric>("extruder_type");
    auto extruder_max_nozzle_count = printer_preset->config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");

    const int  idx    = m_extruder_index;
    const auto type   = extruders_def->enum_labels[extruders->values[idx]];
    const int  wanted = (had_list && prev_volume >= 0) ? prev_volume : static_cast<int>(m_seed.volume);
    int        select = -1;
    int        seed_select = -1;

    for (int j = 0; j < (int) nozzle_volumes_def->enum_labels.size(); ++j) {
        const bool variant_ok = boost::algorithm::contains(extruder_variants->values[idx], type + " " + nozzle_volumes_def->enum_labels[j]);
        const bool hybrid_ok  = extruder_max_nozzle_count->values[idx] > 1 && nozzle_volumes_def->enum_keys_map->at(nozzle_volumes_def->enum_values[j]) == nvtHybrid;
        if (!variant_ok && !hybrid_ok) continue;

        auto cur_volume_type = nozzle_volumes_def->enum_keys_map->at(nozzle_volumes_def->enum_values[j]);
        if (cur_volume_type == NozzleVolumeType::nvtE3DHighFlow && diameter.ToStdString() != "0.4" && diameter.ToStdString() != "0.6") continue;
        if (diameter.ToStdString() == "0.2" && cur_volume_type == NozzleVolumeType::nvtHighFlow) continue;
        if (cur_volume_type == NozzleVolumeType::nvtHybrid) continue;

        if (wanted == j) select = m_flow_combo->GetCount();
        if (static_cast<int>(m_seed.volume) == j) seed_select = m_flow_combo->GetCount();
        m_flow_combo->Append(_L(nozzle_volumes_def->enum_labels[j]), {}, encode_volume(j));
    }
    if (select == -1) select = seed_select;
    if (select == -1) select = static_cast<int>(m_flow_combo->GetCount()) - 1;
    m_flow_combo->SetSelection(select);
}

wxString NozzleRow::GetDiameter() const
{
    wxString d = m_diameter_combo->GetValue();
    if (d == _L("Empty")) return wxEmptyString;
    d.Replace(" mm", "");
    return d;
}

// Read current UI state back into a NozzleConfig
NozzleConfig NozzleRow::GetConfig() const
{
    NozzleConfig cfg;

    wxString diameter_text = m_diameter_combo->GetValue();
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

    cfg.status = m_status;
    // AMS slots are read-only in this dialog; pass through. Filament name/color is not
    // stored on the config — it's resolved live from the machine when displayed.
    cfg.ams_slots = m_ams_slots;
    return cfg;
}

// Render the Error/Unknown hotend state (status label, tooltip, and a click-to-update-firmware
// binding for Error). Returns true when that state applies.
bool NozzleRow::apply_error_state()
{
    if (m_status != NozzleStatus::nsError && m_status != NozzleStatus::nsUnknown) return false;

    if (m_status_label) {
        m_status_label->SetLabel(m_status == NozzleStatus::nsError ? _L("Error") : _L("Unknown"));
        m_status_label->Show();
    }
    m_label->SetForegroundColour(ThemeColor::Danger);

    const wxString tip = m_status == NozzleStatus::nsError ? _L("Hotend status is abnormal and not available. Click to update firmware.") :
                                                             _L("Hotend information is unknown. Please refresh nozzle information before use.");
    m_label->SetToolTip(tip);
    if (m_status == NozzleStatus::nsError) {
        m_label->SetCursor(wxCursor(wxCURSOR_HAND));
        if (!m_error_click_bound) {
            m_label->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
                if (m_on_error_click) m_on_error_click();
                e.Skip();
            });
            m_error_click_bound = true;
        }
    }
    return true;
}

void NozzleRow::RefreshState()
{
    if (m_diameter_combo->GetSelection() != -1 && m_flow_combo && m_flow_combo->GetSelection() != -1 && m_status != NozzleStatus::nsNormal) m_status = NozzleStatus::nsNormal;

    if (apply_error_state()) return;

    if (m_status_label) m_status_label->Show(false);
    m_label->UnsetToolTip();
    m_label->SetCursor(wxCursor(wxCURSOR_ARROW));
    m_label->SetForegroundColour(ThemeColor::TextPrimary);
}

void NozzleRow::Rescale()
{
    m_diameter_combo->SetMinSize(dip(m_parent, 110, 24));
    m_flow_combo->SetMinSize(dip(m_parent, 110, 24));

    for (auto &filament : m_filaments)
        if (filament.box) filament.box->SetSize(dip(m_parent, 16, 16));
}

/**
 * rief Green text link with a chevron that folds or unfolds a section.
 *
 * Starts folded. It only reports the new state: the owner shows or hides the section and lays out again.
 */
class DisclosureView : public wxPanel
{
public:
    DisclosureView(wxWindow *parent, const wxString &text, std::function<void(bool expanded)> on_toggle)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
        , m_on_toggle(std::move(on_toggle))
        , m_expanded_icon(this, "drop_down_green", 16)
        , m_collapsed_icon(this, "drop_right_green", 16)
    {
        SetBackgroundColour(parent->GetBackgroundColour());
        m_label = new Label(this, Label::Body_13, text);
        m_label->SetForegroundColour(ThemeColor::BrandGreen);
        m_chevron = new wxStaticBitmap(this, wxID_ANY, m_collapsed_icon.bmp());

        auto *sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(m_label, 0, wxALIGN_CENTER_VERTICAL);
        sizer->Add(m_chevron, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));
        SetSizer(sizer);

        // The whole link is the click target, not only the panel behind the text.
        for (wxWindow *target : std::initializer_list<wxWindow *>{this, m_label, m_chevron}) {
            target->SetCursor(wxCURSOR_HAND);
            target->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) { toggle(); });
        }
        Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent &e) {
            m_expanded_icon.msw_rescale();
            m_collapsed_icon.msw_rescale();
            update_chevron();
            e.Skip();
        });
    }

private:
    void toggle()
    {
        m_expanded = !m_expanded;
        update_chevron();
        if (m_on_toggle) m_on_toggle(m_expanded);
    }

    void update_chevron() { m_chevron->SetBitmap((m_expanded ? m_expanded_icon : m_collapsed_icon).bmp()); }

    std::function<void(bool)> m_on_toggle;
    ScalableBitmap            m_expanded_icon;
    ScalableBitmap            m_collapsed_icon;
    Label                    *m_label{nullptr};
    wxStaticBitmap           *m_chevron{nullptr};
    bool                      m_expanded{false};
};

} // namespace

class NozzleColumn : public wxPanel
{
public:
    NozzleColumn(wxWindow                        *parent,
                 int                              extruder_index,
                 const std::vector<NozzleConfig> &seeds,
                 std::function<void()>            on_changed,
                 std::function<void()>            on_error_click,
                 std::function<void()>            on_ams_count_changed);

    std::vector<NozzleConfig> GetConfigs() const;
    void                      RefreshState();
    void                      Rescale();
    void                      RestoreAms(); // revert this column's AMS-count edits (Cancel)

private:
    int                                     m_extruder_index{0};
    std::vector<std::unique_ptr<NozzleRow>> m_rows;
    std::function<void()>                   m_on_ams_count_changed;
    NozzleAmsRow                           *m_ams_row{nullptr}; // external-AMS attach row, below the nozzle rows
};

NozzleColumn::NozzleColumn(wxWindow                        *parent,
                           int                              extruder_index,
                           const std::vector<NozzleConfig> &seeds,
                           std::function<void()>            on_changed,
                           std::function<void()>            on_error_click,
                           std::function<void()>            on_ams_count_changed)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_extruder_index(extruder_index), m_on_ams_count_changed(std::move(on_ams_count_changed))
{
    SetBackgroundColour(ThemeColor::Grey200);

    auto *sizer = new wxBoxSizer(wxVERTICAL);

    // Title: extruder display name. The left column shows the deputy extruder, the right one the main.
    std::string    pt      = wxGetApp().preset_bundle->printers.get_edited_preset().get_printer_type(wxGetApp().preset_bundle);
    const int      head_id = extruder_index == 0 ? DEPUTY_EXTRUDER_ID : MAIN_EXTRUDER_ID;
    const wxString title   = _L(DevPrinterConfigUtil::get_toolhead_display_name(pt, head_id, ToolHeadComponent::Extruder, ToolHeadNameCase::TitleCase));
    auto          *title_label = new Label(this, title);
    title_label->SetFont(Label::Body_14.Bold());
    sizer->Add(title_label, 0, wxALIGN_LEFT | wxBOTTOM, FromDIP(8));

    auto      diameters    = wxGetApp().preset_bundle->printers.diameters_of_selected_printer();
    const int nozzle_count = static_cast<int>(seeds.size());

    for (int i = 0; i < nozzle_count; ++i) {
        auto row = std::make_unique<NozzleRow>(this, i, nozzle_count, extruder_index, seeds[i], diameters, on_changed, on_error_click);
        sizer->Add(row->GetSizer(), 0, wxALIGN_LEFT | wxBOTTOM, FromDIP(8));
        m_rows.push_back(std::move(row));
    }

    // The AMS row closes the column.
    m_ams_row = new NozzleAmsRow(this, m_extruder_index, _L("AMS"), NozzleAmsRow::Style::Plain, m_on_ams_count_changed);
    sizer->Add(m_ams_row, 0, wxALIGN_LEFT);

    SetSizer(sizer);
    Layout();
}

std::vector<NozzleConfig> NozzleColumn::GetConfigs() const
{
    std::vector<NozzleConfig> configs;
    configs.reserve(m_rows.size());
    for (const auto &row : m_rows) configs.push_back(row->GetConfig());
    return configs;
}

void NozzleColumn::RefreshState()
{
    for (const auto &row : m_rows) row->RefreshState();

    Layout();
    Refresh();
    wxGetApp().UpdateDarkUIWin(this);
}

void NozzleColumn::Rescale()
{
    for (auto &row : m_rows) row->Rescale();
    if (m_ams_row) m_ams_row->Rescale();
    Layout();
}

void NozzleColumn::RestoreAms()
{
    if (m_ams_row) m_ams_row->restore();
}

NozzleSettingsListDialog::NozzleSettingsListDialog(wxWindow                        *parent,
                                               const std::vector<NozzleConfig> &left_configs,
                                               const std::vector<NozzleConfig> &right_configs)
    : DPIDialog(parent, wxID_ANY, _L("Extruder Settings"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    std::vector<NozzleConfig> left_seeds  = left_configs;
    std::vector<NozzleConfig> right_seeds = right_configs;
    if (left_seeds.empty()) left_seeds.emplace_back();

    // What the printer reports right now — the synced badge compares the edits against this
    // Must precede create_layout(), which calls update_dialog_state().
    m_machine_baseline = collect_machine_nozzle_baseline(true);

    SetBackgroundColour(*wxWHITE);
    create_layout(left_seeds, right_seeds);
    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void NozzleSettingsListDialog::create_layout(const std::vector<NozzleConfig> &left_seeds, const std::vector<NozzleConfig> &right_seeds)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % Slic3r::resources_dir()).str();
    SetIcon(wxIcon(Slic3r::encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    auto *main_sizer = new wxBoxSizer(wxVERTICAL);
    build_columns(main_sizer, left_seeds, right_seeds);
    build_printer_status(main_sizer);
    build_button_bar(main_sizer);

    SetSizer(main_sizer);
    Layout();
    Fit();

    update_dialog_state();

    CallAfter([this]() { align_separator_icon(); });
}

void NozzleSettingsListDialog::build_columns(wxBoxSizer *main_sizer, const std::vector<NozzleConfig> &left_seeds, const std::vector<NozzleConfig> &right_seeds)
{
    m_columns_container = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_columns_container->SetBackgroundColour(wxColour("#F7F7F7"));
    auto *container_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto on_changed     = [this]() { update_dialog_state(); };
    auto on_error_click = [this]() {
        MessageDialog dlg(this, _L("Hotend status is abnormal. Please update the firmware."), _L("Update Firmware"), wxCANCEL | wxOK);
        if (dlg.ShowModal() == wxID_OK) {
            Close(); // revert live AMS edits via the close handler, then leave for firmware update
            wxGetApp().mainframe->jump_to_monitor();
            wxGetApp().mainframe->m_monitor->jump_to_Upgrade();
        }
    };
    auto on_ams_changed = [this]() { on_ams_count_changed(); };

    m_left_column = new NozzleColumn(m_columns_container, 0, left_seeds, on_changed, on_error_click, on_ams_changed);
    container_sizer->Add(m_left_column, 1, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, FromDIP(20));
    m_separator = new CustomSeparator(m_columns_container, false);
    container_sizer->Add(m_separator, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(20));
    m_right_column = new NozzleColumn(m_columns_container, 1, right_seeds, on_changed, on_error_click, on_ams_changed);
    container_sizer->Add(m_right_column, 1, wxEXPAND | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(20));

    m_columns_container->SetSizer(container_sizer);
    main_sizer->Add(m_columns_container, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    // Machine-wide synced badge, pinned to the top-right of the columns container.
    m_columns_container->Bind(wxEVT_SIZE, [this](wxSizeEvent &e) {
        layout_badge();
        e.Skip();
    });
}

// Read-only rack of the connected printer, below the columns, folded behind a link until the user
// opens it. Only a printer of the selected preset's model with a nozzle rack qualifies; otherwise
// the section does not exist at all.
void NozzleSettingsListDialog::build_printer_status(wxBoxSizer *main_sizer)
{
    if (!m_machine_baseline.synced) return; // no printer, or a printer of another model
    MachineObject *obj = selected_machine();
    if (!obj || !obj->GetNozzleSystem()) return;
    std::shared_ptr<DevNozzleRack> rack = obj->GetNozzleSystem()->GetNozzleRack();
    if (!rack || !rack->IsSupported()) return;

    auto *status = new MultiNozzleStatusTable(this, _L("Current printer status"));
    status->UpdateRackInfo(rack);
    status->Hide();

    const wxString link_text = wxString::Format(_L("Connected printer %s, view printer status"), from_u8(obj->get_dev_name()));
    auto *disclosure = new DisclosureView(this, link_text, [this, status](bool expanded) {
        status->Show(expanded);
        Layout();
        Fit(); // shrink back as well as grow
        Refresh();
    });

    main_sizer->Add(disclosure, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    main_sizer->Add(status, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
}

void NozzleSettingsListDialog::build_button_bar(wxBoxSizer *main_sizer)
{
    StateColor ok_bg(std::pair<wxColour, int>(ThemeColor::BrandGreenPressed, StateColor::Pressed), std::pair<wxColour, int>(ThemeColor::BrandGreenHovered, StateColor::Hovered),
                     std::pair<wxColour, int>(ThemeColor::BrandGreen, StateColor::Normal));
    StateColor ok_text(std::pair<wxColour, int>(wxColour("#FFFFFE"), StateColor::Normal));
    StateColor cancel_bg(std::pair<wxColour, int>(ThemeColor::Grey400, StateColor::Pressed), std::pair<wxColour, int>(ThemeColor::Grey300, StateColor::Hovered),
                         std::pair<wxColour, int>(ThemeColor::White, StateColor::Normal));
    StateColor cancel_bd(std::pair<wxColour, int>(ThemeColor::TextPrimary, StateColor::Normal));
    StateColor cancel_text(std::pair<wxColour, int>(ThemeColor::TextPrimary, StateColor::Normal));

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
    m_cancel_btn->SetTextColor(cancel_text);
    m_cancel_btn->SetId(wxID_CANCEL);
    m_cancel_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });

    // AMS edits persist to the live registry the moment they are made, so every dismissal other than
    // Confirm must revert them. Funnel the Cancel button through the same close path as the window's
    // X button; the system close reaches EndModal without this handler otherwise, keeping the edits.
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) {
        if (m_left_column) m_left_column->RestoreAms();
        if (m_right_column) m_right_column->RestoreAms();
        EndModal(wxID_CANCEL);
    });

    auto *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();
    btn_sizer->Add(m_confirm_btn, 0, wxRIGHT, FromDIP(12));
    btn_sizer->Add(m_cancel_btn, 0);
    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, FromDIP(20));
}

void NozzleSettingsListDialog::update_dialog_state()
{
    // Not synced with the printer means not synced, full stop: no amount of matching
    // between the edits and the configs we were handed can make it synced.
    bool all_synced = m_machine_baseline.synced; // machine-wide: every extruder must match

    auto scan = [&](NozzleColumn *col, int extruder_idx) {
        if (!col) return;
        col->RefreshState();
        if (all_synced && !extruder_configs_match(col->GetConfigs(), m_machine_baseline.configs[extruder_idx])) all_synced = false;
    };
    scan(m_left_column, 0);
    scan(m_right_column, 1);

    show_synced(all_synced);
    if (m_separator) m_separator->SetState(filament_switcher_state(all_synced));

    Layout();
    Fit();
}

// Machine-wide synced badge, top-right of the columns container.
void NozzleSettingsListDialog::show_synced(bool show)
{
    if (show && !m_badge && m_columns_container) {
        m_badge = new wxStaticBitmap(m_columns_container, wxID_ANY, ScalableBitmap(m_columns_container, "badge", 18).bmp());
        m_badge->SetBackgroundColour(m_columns_container->GetBackgroundColour());
        layout_badge();
    }
    if (m_badge) m_badge->Show(show);
}

void NozzleSettingsListDialog::layout_badge()
{
    if (!m_badge || !m_columns_container) return;
    wxSize csize = m_columns_container->GetSize();
    wxSize bsize = m_badge->GetSize();
    m_badge->SetPosition(wxPoint(csize.x - bsize.x, 0));
    m_badge->Raise();
}

void NozzleSettingsListDialog::align_separator_icon()
{
    wxPoint col_pos = m_right_column->GetScreenPosition();
    wxPoint sep_pos = m_separator->GetScreenPosition();
    int     offset  = (col_pos.y + FromDIP(12)) - sep_pos.y;
    m_separator->SetIconYOffset(offset);
}

// A column's AMS preview count changed — grow the dialog to fit the new previews.
void NozzleSettingsListDialog::on_ams_count_changed()
{
    Layout();
    Fit();
    wxSize cur = GetSize(), min = GetMinSize();
    SetSize(wxSize(std::max(cur.x, min.x), std::max(cur.y, min.y)));
    Refresh();
}

std::vector<NozzleConfig> NozzleSettingsListDialog::GetExtruderConfigs(int index) const
{
    if (index == 0 && m_left_column) return m_left_column->GetConfigs();
    if (index == 1 && m_right_column) return m_right_column->GetConfigs();
    return {};
}

void NozzleSettingsListDialog::on_dpi_changed(const wxRect & /*suggested_rect*/)
{
    if (m_left_column) m_left_column->Rescale();
    if (m_right_column) m_right_column->Rescale();

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
