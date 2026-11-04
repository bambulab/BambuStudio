#include "MixedFilamentDialog.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <wx/image.h>
#include <wx/sizer.h>
#include <wx/dcclient.h>
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/scrolwin.h>
#include <wx/textctrl.h>
#include <wx/wrapsizer.h>
#include <wx/tokenzr.h>
#include <wx/control.h>
#include <wx/display.h>
#ifdef __WINDOWS__
#include <wx/msw/wrapwin.h>
#endif //__WINDOWS__

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/FilamentMixer.hpp"
#include "libslic3r/ColorDecomposeRecipe.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "ColorDecomposeSupport.hpp"
#include "MixingKitsHelpDialog.hpp"
#include "I18N.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GuiColor.hpp"
#include "GradientCurveEditor.hpp"
#include "wxExtensions.hpp"
#include "Tab.hpp"
#include "libslic3r/Preset.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/DropDown.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"

namespace Slic3r {
namespace GUI {

static constexpr int MAX_COMPONENTS = 3;
static constexpr int MIN_COMPONENT_RATIO = 10;
// Shared by the manual preview and the color-matching swatch so both tabs look the same.
static constexpr int kPreviewSwatchSize   = 64;
static constexpr int kPreviewSwatchRadius = 6;

// Lightweight self-painting label used for both dual-color and triple-color
// ratio percentage display.  Hover shows a rounded-rect background; click
// fires wxEVT_LEFT_DOWN which the owning dialog binds to start_ratio_editor.
class RatioLabelPanel : public wxPanel
{
public:
    RatioLabelPanel(wxWindow* parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetCursor(wxCursor(wxCURSOR_HAND));
        SetToolTip(_L("Click to edit ratio"));
        SetFont(::Label::Body_10);

        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& e) { m_hovered = true;  Refresh(); e.Skip(); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) { m_hovered = false; Refresh(); e.Skip(); });
        Bind(wxEVT_PAINT, &RatioLabelPanel::on_paint, this);
    }

    void SetLabel(const wxString& text) override
    {
        if (m_text == text) return;
        m_text = text;
        update_best_size();
        Refresh();
    }
    wxString GetLabel() const override { return m_text; }

private:
    void update_best_size()
    {
        wxClientDC dc(this);
        dc.SetFont(GetFont());
        wxSize ts = dc.GetTextExtent(m_text);
        int pad_x = FromDIP(4), pad_y = FromDIP(3);
        SetMinSize(wxSize(ts.GetWidth() + pad_x * 2, ts.GetHeight() + pad_y * 2));
        InvalidateBestSize();
    }

    void on_paint(wxPaintEvent&)
    {
        wxBufferedPaintDC dc(this);
        wxSize sz = GetClientSize();

        wxColour parent_bg = GetParent() ? GetParent()->GetBackgroundColour()
                                         : StateColor::darkModeColorFor(*wxWHITE);
        dc.SetBrush(wxBrush(parent_bg));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(0, 0, sz.GetWidth(), sz.GetHeight());

        if (m_hovered) {
            dc.SetBrush(wxBrush(StateColor::darkModeColorFor(wxColour("#F8F8F8"))));
            dc.SetPen(wxPen(StateColor::darkModeColorFor(wxColour("#CECECE")), 1));
            dc.DrawRoundedRectangle(0, 0, sz.GetWidth(), sz.GetHeight(), FromDIP(3));
        }

        dc.SetFont(GetFont());
        dc.SetTextForeground(m_hovered ? wxColour("#00AE42")
                                       : StateColor::darkModeColorFor(wxColour("#262E30")));
        wxSize ts = dc.GetTextExtent(m_text);
        int x = (sz.GetWidth()  - ts.GetWidth())  / 2;
        int y = (sz.GetHeight() - ts.GetHeight()) / 2;
        dc.DrawText(m_text, x, y);
    }

    wxString m_text;
    bool     m_hovered{false};
};

static wxColour blend_colors(const wxColour& a, const wxColour& b, double ratio_a)
{
    unsigned char r, g, bl;
    Slic3r::filament_mixer_lerp(a.Red(), a.Green(), a.Blue(),
                                b.Red(), b.Green(), b.Blue(),
                                static_cast<float>(1.0 - ratio_a),
                                &r, &g, &bl);
    return wxColour(r, g, bl);
}

static wxColour blend_n_colors(const std::vector<wxColour>& cols, const std::vector<double>& weights)
{
    std::vector<std::string> hex_colors;
    std::vector<int> int_weights;
    for (size_t i = 0; i < cols.size() && i < weights.size(); ++i) {
        hex_colors.push_back(cols[i].GetAsString(wxC2S_HTML_SYNTAX).ToStdString());
        // Scale double weights (e.g. 0.5) to int (5000) for blend_color_multi;
        // only relative magnitude matters.
        int_weights.push_back(static_cast<int>(std::lround(weights[i] * 10000)));
    }
    std::string hex = Slic3r::blend_color_multi(hex_colors, int_weights);
    return wxColour(hex);
}

// ---- Constructors ----

MixedFilamentDialog::MixedFilamentDialog(wxWindow* parent,
                                         MixedFilamentPhysicalLoader physical_loader,
                                         bool enforce_nozzle_diameter,
                                         const MixedFilamentResult& initial)
    : DPIDialog(parent, wxID_ANY, _L("Add Mixed Filament"), wxDefaultPosition,
                wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_edit_mode(false)
    , m_enforce_nozzle_diameter(enforce_nozzle_diameter)
    , m_physical_loader(std::move(physical_loader))
{
    if (m_physical_loader)
        set_physical_info(m_physical_loader());

    const size_t n_init = initial.components.size();
    bool initial_valid = n_init >= 2 && n_init <= (size_t)MAX_COMPONENTS && initial.ratios.size() == n_init;
    int initial_sum = 0;
    std::set<unsigned int> initial_seen;
    for (size_t i = 0; initial_valid && i < n_init; ++i) {
        const unsigned int c = initial.components[i];
        initial_valid = c >= 1 && c <= m_physical_colors.size() && initial_seen.insert(c).second && initial.ratios[i] >= 0;
        initial_sum += initial.ratios[i];
    }
    if (initial_valid && initial_sum > 0) {
        m_result = initial;
        // Gradient only applies to two-color mixes.
        if (n_init != 2) {
            m_result.gradient_enabled  = false;
            m_result.per_part_gradient = false;
            m_result.gradient_curve.clear();
        }
        if (n_init >= 3) {
            m_tri_wx = (double)m_result.ratios[0] / initial_sum;
            m_tri_wy = (double)m_result.ratios[1] / initial_sum;
            m_tri_wz = (double)m_result.ratios[2] / initial_sum;
        }
    } else {
        m_result.components = {1, (m_physical_colors.size() >= 2) ? 2u : 1u};
        m_result.ratios     = {50, 50};
    }
    build_ui();
    wxGetApp().UpdateDlgDarkUI(this);
    refresh_combo_suffixes();

    wxImage img;
    if (img.LoadFile(from_u8(Slic3r::var("mixed_filament_preview_twocolor.png")), wxBITMAP_TYPE_PNG))
        m_preview_bmp_two = wxBitmap(img);
    if (img.LoadFile(from_u8(Slic3r::var("mixed_filament_preview_threecolor.png")), wxBITMAP_TYPE_PNG))
        m_preview_bmp_three = wxBitmap(img);
}

MixedFilamentDialog::MixedFilamentDialog(wxWindow* parent,
                                         const MixedFilamentResult& existing,
                                         const std::vector<std::string>& physical_colors,
                                         const std::vector<std::string>& physical_names,
                                         const std::vector<std::string>& physical_types,
                                         const std::vector<std::string>& physical_diameters,
                                         const std::vector<std::string>& physical_volume_shorts,
                                         bool enforce_nozzle_diameter)
    : DPIDialog(parent, wxID_ANY, _L("Edit Mixed Filament"), wxDefaultPosition,
                wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_result(existing)
    , m_edit_mode(true)
    , m_physical_colors(physical_colors)
    , m_physical_names(physical_names)
    , m_physical_types(physical_types)
    , m_physical_diameters(physical_diameters)
    , m_physical_volume_shorts(physical_volume_shorts)
    , m_enforce_nozzle_diameter(enforce_nozzle_diameter)
{
    if (m_result.components.size() < 2) {
        m_result.components = {1, (physical_colors.size() >= 2) ? 2u : 1u};
        m_result.ratios     = {50, 50};
    }
    if (m_result.ratios.size() >= 3) {
        int sum = 0;
        for (int r : m_result.ratios) sum += r;
        if (sum > 0) {
            m_tri_wx = (double)m_result.ratios[0] / sum;
            m_tri_wy = (double)m_result.ratios[1] / sum;
            m_tri_wz = (double)m_result.ratios[2] / sum;
        }
    }
    build_ui();
    wxGetApp().UpdateDlgDarkUI(this);
    refresh_combo_suffixes();

    wxImage img;
    if (img.LoadFile(from_u8(Slic3r::var("mixed_filament_preview_twocolor.png")), wxBITMAP_TYPE_PNG))
        m_preview_bmp_two = wxBitmap(img);
    if (img.LoadFile(from_u8(Slic3r::var("mixed_filament_preview_threecolor.png")), wxBITMAP_TYPE_PNG))
        m_preview_bmp_three = wxBitmap(img);
}

void MixedFilamentDialog::on_dpi_changed(const wxRect&)
{
    apply_dialog_size();
    Refresh();
}

wxColour MixedFilamentDialog::comp_colour(size_t i) const
{
    unsigned int c = comp(i);
    if (c >= 1 && c <= m_physical_colors.size())
        return wxColour(m_physical_colors[c - 1]);
    return wxColour("#D9D9D9");
}

static wxBitmap make_alpha_bitmap(int w, int h,
                                  const std::function<void(wxDC& dc)>& draw_fn)
{
    wxBitmap bmp(w, h);
    wxMemoryDC memdc;
#ifdef __WXOSX__
    bmp.UseAlpha();
    memdc.SelectObject(bmp);
#else
    {
        wxImage img(w, h);
        img.InitAlpha();
        memset(img.GetAlpha(), 0, w * h);
        bmp = wxBitmap(std::move(img));
    }
    memdc.SelectObject(bmp);
#endif
    {
#ifdef __WXMSW__
        wxGCDC dc(memdc);
#else
        wxDC& dc = memdc;
#endif
        draw_fn(dc);
    }
    memdc.SelectObject(wxNullBitmap);
    return bmp;
}

wxBitmap MixedFilamentDialog::make_swatch_bitmap(size_t idx)
{
    int swatch_sz = FromDIP(20);
    int pad_left  = FromDIP(2);
    int pad_right = FromDIP(6);
    int bmp_w = pad_left + swatch_sz + pad_right;
    int bmp_h = swatch_sz;

    // Reuse the sidebar clr_picker swatch (get_extruder_color_icon) so the
    // checkerboard (transparent.svg tiling), border and label style match the
    // sidebar exactly, instead of a self-drawn rounded rect / programmatic grid.
    std::string color_hex = "#D9D9D9";
    if (idx < m_physical_colors.size())
        color_hex = m_physical_colors[idx];
    std::string label = std::to_string(idx + 1);

    wxBitmap* icon = get_extruder_color_icon(color_hex, label, swatch_sz, swatch_sz);

    return make_alpha_bitmap(bmp_w, bmp_h, [&](wxDC& dc) {
        if (icon && icon->IsOk())
            dc.DrawBitmap(*icon, pad_left, 0);
    });
}

void MixedFilamentDialog::apply_uniform_label_width(wxStaticText* lbl)
{
    // A material row places the combo right after the label, so the combo x follows the label
    // width and the rows drift apart with fonts that render digits at different advances (which
    // is what macOS does).  Reserve the width of the widest row label on every row instead.
    // The label itself is used as the measuring device on purpose: SetMinSize overrides the
    // control's own best size rather than being merged with it, and on macOS the native cell is
    // wider than the plain text extent, so a wxDC-measured width would clip the text.
    const wxString text = lbl->GetLabel();
    int w = 0;
    for (int i = 1; i <= MAX_COMPONENTS; ++i) {
        lbl->SetLabel(wxString::Format(_L("Filament %d"), i));
        lbl->InvalidateBestSize();
        w = std::max(w, lbl->GetBestSize().x);
    }
    lbl->SetLabel(text);
    lbl->InvalidateBestSize();
    lbl->SetMinSize(wxSize(w, -1));
}

void MixedFilamentDialog::append_material_row()
{
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* lbl = new wxStaticText(this, wxID_ANY,
                                 wxString::Format(_L("Filament %d"), (int)(m_combo_filaments.size() + 1)));
    lbl->SetFont(::Label::Body_12);
    apply_uniform_label_width(lbl);
    row->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    auto* combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxSize(FromDIP(166), FromDIP(24)), 0, nullptr, wxCB_READONLY);
    combo->SetKeepDropArrow(true);
    combo->GetDropDown().SetUseContentWidth(true);
    combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { on_filament_changed(); });
    row->Add(combo, 1, wxALIGN_CENTER_VERTICAL);

    m_combo_filaments.push_back(combo);
    m_combo_to_physical.push_back({});
    m_material_rows_sizer->Add(row, 0, wxEXPAND | wxTOP, FromDIP(9));
}

void MixedFilamentDialog::remove_last_material_row()
{
    if (m_material_rows_sizer && m_material_rows_sizer->GetItemCount() > 0) {
        auto* sizer_item = m_material_rows_sizer->GetItem(m_material_rows_sizer->GetItemCount() - 1);
        if (sizer_item && sizer_item->GetSizer())
            sizer_item->GetSizer()->Clear(true);
        m_material_rows_sizer->Remove(m_material_rows_sizer->GetItemCount() - 1);
    }
    if (!m_combo_filaments.empty())
        m_combo_filaments.pop_back();
    if (!m_combo_to_physical.empty())
        m_combo_to_physical.pop_back();
}

void MixedFilamentDialog::reset_manual_ratio_state()
{
    m_ratio_manual_order.clear();
    if (m_ratio_editor_panel)
        m_ratio_editor_panel->Hide();
    // Restore any label hidden by an in-flight editor so it can never be left
    // permanently invisible if the editor is dismissed without a commit.
    if (m_ratio_editor_anchor) {
        m_ratio_editor_anchor->Show();
        m_ratio_editor_anchor = nullptr;
    }
}

void MixedFilamentDialog::refresh_ratio_labels()
{
    if (m_label_ratio_a)
        m_label_ratio_a->SetLabel(wxString::Format(wxT("%d%%"), ratio(0)));
    if (m_label_ratio_b)
        m_label_ratio_b->SetLabel(wxString::Format(wxT("%d%%"), ratio(1)));
    if (m_ratio_sizer)
        m_ratio_sizer->Layout();
    if (m_triangle_panel)
        m_triangle_panel->Refresh();
}

void MixedFilamentDialog::sync_triangle_weights_from_ratios()
{
    if (m_result.ratios.size() < 3)
        return;

    int sum = 0;
    for (int r : m_result.ratios)
        sum += r;
    if (sum <= 0)
        return;

    m_tri_wx = (double)m_result.ratios[0] / sum;
    m_tri_wy = (double)m_result.ratios[1] / sum;
    m_tri_wz = (double)m_result.ratios[2] / sum;
}

void MixedFilamentDialog::apply_manual_ratio(size_t idx, int value)
{
    const size_t n = num_components();
    if (idx >= n)
        return;
    if (m_result.ratios.size() != n)
        m_result.ratios.assign(n, n > 0 ? 100 / (int)n : 0);
    bool manual_stale = false;
    for (size_t o : m_ratio_manual_order) {
        if (o >= n) { manual_stale = true; break; }
    }
    if (manual_stale)
        reset_manual_ratio_state();

    int max_value = (int)(100 - (n - 1) * MIN_COMPONENT_RATIO);
    value = std::clamp(value, MIN_COMPONENT_RATIO, std::max(MIN_COMPONENT_RATIO, max_value));

    if (n == 2) {
        if (idx == 0) {
            m_result.ratios[0] = value;
            m_result.ratios[1] = 100 - value;
        } else {
            m_result.ratios[1] = value;
            m_result.ratios[0] = 100 - value;
        }
        m_result.ratios[0] = std::clamp(m_result.ratios[0], MIN_COMPONENT_RATIO, 100 - MIN_COMPONENT_RATIO);
        m_result.ratios[1] = 100 - m_result.ratios[0];
    } else if (n >= 3) {
        m_result.ratios[idx] = value;
        int remaining = 100 - value;

        std::vector<size_t> others;
        int others_sum = 0;
        for (size_t i = 0; i < n; ++i) {
            if (i == idx) continue;
            others.push_back(i);
            others_sum += m_result.ratios[i];
        }

        if (!others.empty()) {
            if (others_sum > 0) {
                int assigned = 0;
                for (size_t k = 0; k < others.size(); ++k) {
                    int nv = (int)((double)remaining * m_result.ratios[others[k]] / others_sum + 0.5);
                    nv = std::max(nv, MIN_COMPONENT_RATIO);
                    m_result.ratios[others[k]] = nv;
                    assigned += nv;
                }
                while (assigned != remaining) {
                    if (assigned > remaining) {
                        int pick = -1;
                        for (size_t k = 0; k < others.size(); ++k)
                            if (m_result.ratios[others[k]] > MIN_COMPONENT_RATIO
                                && (pick < 0 || m_result.ratios[others[k]] > m_result.ratios[others[pick]]))
                                pick = (int)k;
                        if (pick < 0) break;
                        --m_result.ratios[others[pick]]; --assigned;
                    } else {
                        int pick = 0;
                        for (size_t k = 1; k < others.size(); ++k)
                            if (m_result.ratios[others[k]] > m_result.ratios[others[pick]])
                                pick = (int)k;
                        ++m_result.ratios[others[pick]]; ++assigned;
                    }
                }
            } else {
                int base = remaining / (int)others.size();
                for (size_t k = 0; k < others.size(); ++k)
                    m_result.ratios[others[k]] = base;
                m_result.ratios[others.back()] += remaining - base * (int)others.size();
            }
        }
    }

    refresh_ratio_labels();
    sync_triangle_weights_from_ratios();
    update_preview();
}

void MixedFilamentDialog::apply_dragged_triangle_ratio(int r0, int r1, int r2)
{
    if (m_result.ratios.size() < 3)
        return;

    int ratios[3] = {
        std::clamp(r0, MIN_COMPONENT_RATIO, 100),
        std::clamp(r1, MIN_COMPONENT_RATIO, 100),
        std::clamp(r2, MIN_COMPONENT_RATIO, 100)
    };

    int sum = ratios[0] + ratios[1] + ratios[2];
    while (sum > 100) {
        int idx = 0;
        for (int i = 1; i < 3; ++i) {
            if (ratios[i] > ratios[idx])
                idx = i;
        }
        if (ratios[idx] <= MIN_COMPONENT_RATIO)
            break;
        --ratios[idx];
        --sum;
    }
    while (sum < 100) {
        int idx = 0;
        for (int i = 1; i < 3; ++i) {
            if (ratios[i] < ratios[idx])
                idx = i;
        }
        ++ratios[idx];
        ++sum;
    }

    m_result.ratios[0] = ratios[0];
    m_result.ratios[1] = ratios[1];
    m_result.ratios[2] = ratios[2];
    sync_triangle_weights_from_ratios();
    reset_manual_ratio_state();
    update_preview();
}

void MixedFilamentDialog::start_ratio_editor(size_t idx, wxWindow* anchor, const wxRect& anchor_rect)
{
    if (!anchor || idx >= m_result.ratios.size())
        return;
    if (m_ratio_editor_panel && m_ratio_editor_panel->IsShown())
        commit_ratio_editor(true);

    if (!m_ratio_editor_panel) {
        wxColour bg = StateColor::darkModeColorFor(wxColour("#F8F8F8"));
        wxColour fg = StateColor::darkModeColorFor(wxColour("#262E30"));

        m_ratio_editor_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition,
                                           wxDefaultSize, wxBORDER_SIMPLE);
        m_ratio_editor_panel->SetBackgroundColour(bg);

        auto* hsizer = new wxBoxSizer(wxHORIZONTAL);

        m_ratio_editor = new wxTextCtrl(m_ratio_editor_panel, wxID_ANY, wxEmptyString,
                                        wxDefaultPosition, wxDefaultSize,
                                        wxTE_PROCESS_ENTER | wxTE_RIGHT | wxBORDER_NONE);
        m_ratio_editor->SetFont(::Label::Body_10);
        m_ratio_editor->SetMaxLength(3);
        m_ratio_editor->SetBackgroundColour(bg);
        m_ratio_editor->SetForegroundColour(fg);
        // Default wxTextCtrl best width (~140px) is too wide for the sizer to
        // shrink, which would push the "%" suffix out of the panel.  Size the
        // editor for the *widest* three digits rather than the largest accepted
        // value: SetMaxLength above lets anything up to "888" be typed, and the
        // macOS system font renders digits at different advances, so "100" is
        // narrower than what the user can actually enter.  GetSizeFromTextSize()
        // then adds the platform's own text field margins; on macOS those margins
        // are what clipped the digits.
        {
            wxClientDC mdc(m_ratio_editor);
            mdc.SetFont(::Label::Body_10);
            int digits_w = mdc.GetTextExtent(wxT("888")).GetWidth();
            m_ratio_editor->SetMinSize(m_ratio_editor->GetSizeFromTextSize(digits_w));
        }

        auto* pct_label = new wxStaticText(m_ratio_editor_panel, wxID_ANY, wxT("%"));
        pct_label->SetFont(::Label::Body_10);
        pct_label->SetForegroundColour(fg);
        pct_label->SetBackgroundColour(bg);
        pct_label->SetMinSize(pct_label->GetBestSize());

        hsizer->Add(m_ratio_editor, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(2));
        hsizer->Add(pct_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(2));
        m_ratio_editor_panel->SetSizer(hsizer);
        m_ratio_editor_panel->Hide();

        m_ratio_editor->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { commit_ratio_editor(true); });
        m_ratio_editor->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
            commit_ratio_editor(true);
            e.Skip();
        });
        m_ratio_editor->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
            if (e.GetKeyCode() == WXK_ESCAPE)
                commit_ratio_editor(false);
            else
                e.Skip();
        });
    }

    m_ratio_editor_idx = idx;

    // Keep the editor in the same window hierarchy as the clicked label so the
    // z-order is reliable and the editor fully covers the anchor (dual-color
    // labels live on the dialog, triple-color labels live on the triangle
    // panel).
    wxWindow* target_parent = anchor->GetParent();
    if (target_parent && m_ratio_editor_panel->GetParent() != target_parent)
        m_ratio_editor_panel->Reparent(target_parent);

    // Hide the label being edited to avoid its (hover-state) text leaking out
    // next to the editor; restored on commit.
    m_ratio_editor_anchor = anchor;
    anchor->Hide();

    wxPoint pos = anchor->GetPosition() + anchor_rect.GetTopLeft();
    // Match the editor to the label (hover box) size so the inline editor and
    // the hover state look identical, but never go below what the digits and
    // the "%" suffix need: the sizer takes any missing width out of the
    // stretchable editor, which would clip the value.
    wxSize needed = m_ratio_editor_panel->ClientToWindowSize(
        m_ratio_editor_panel->GetSizer()->CalcMin());
    wxSize size = anchor->GetSize();
    size.SetWidth(std::max(size.GetWidth(), needed.GetWidth()));
    size.SetHeight(std::max(size.GetHeight(), needed.GetHeight()));
    // An editor wider than the label must still stay inside its parent, or the
    // corner labels of the triangle picker would have it clipped at the edge.
    if (wxWindow* editor_parent = m_ratio_editor_panel->GetParent()) {
        wxSize avail = editor_parent->GetClientSize();
        pos.x = std::clamp(pos.x, 0, std::max(0, avail.GetWidth()  - size.GetWidth()));
        pos.y = std::clamp(pos.y, 0, std::max(0, avail.GetHeight() - size.GetHeight()));
    }
    m_ratio_editor_panel->SetSize(wxRect(pos, size));
    m_ratio_editor_panel->Layout();
    m_ratio_editor->SetValue(wxString::Format(wxT("%d"), ratio(idx)));
    m_ratio_editor_panel->Show();
    m_ratio_editor_panel->Raise();
    m_ratio_editor->SetFocus();
    m_ratio_editor->SelectAll();
    m_ratio_editor_panel->Refresh();
    Update();
}

void MixedFilamentDialog::commit_ratio_editor(bool apply)
{
    if (!m_ratio_editor_panel || !m_ratio_editor_panel->IsShown() || m_ratio_editor_committing)
        return;

    m_ratio_editor_committing = true;

    // Restore the hidden anchor before applying the ratio, so any sizer layout
    // triggered by refresh_ratio_labels() accounts for the visible label.
    m_ratio_editor_panel->Hide();
    if (m_ratio_editor_anchor) {
        m_ratio_editor_anchor->Show();
        m_ratio_editor_anchor = nullptr;
    }

    if (apply) {
        wxString value = m_ratio_editor->GetValue();
        value.Trim(true);
        value.Trim(false);
        if (value.EndsWith(wxT("%")))
            value.RemoveLast();

        long parsed = 0;
        if (value.ToLong(&parsed))
            apply_manual_ratio(m_ratio_editor_idx, (int)parsed);
        else
            refresh_ratio_labels();
    }

    m_ratio_editor_committing = false;
}

void MixedFilamentDialog::commit_ratio_editor_from_background(wxMouseEvent& e)
{
    if (m_ratio_editor_panel && m_ratio_editor_panel->IsShown()) {
        wxPoint mouse_in_panel = m_ratio_editor_panel->ScreenToClient(wxGetMousePosition());
        if (!m_ratio_editor_panel->GetClientRect().Contains(mouse_in_panel))
            commit_ratio_editor(true);
    }
    e.Skip();
}

// ---- UI Construction ----

void MixedFilamentDialog::build_ui()
{
    const wxColour mc_bg       = StateColor::darkModeColorFor(*wxWHITE);
    const wxColour mc_bg_sub   = StateColor::darkModeColorFor(wxColour("#F8F8F8"));
    const wxColour mc_border   = StateColor::darkModeColorFor(wxColour("#CECECE"));
    const wxColour mc_text     = StateColor::darkModeColorFor(wxColour("#262E30"));
    const wxColour mc_dim_text = StateColor::darkModeColorFor(wxColour("#ACACAC"));

    SetBackgroundColour(mc_bg);
    Bind(wxEVT_LEFT_DOWN, &MixedFilamentDialog::commit_ratio_editor_from_background, this);
    SetSize(FromDIP(439), FromDIP(580));

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    if (!m_edit_mode) {
        main_sizer->Add(create_mode_tabs(), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
        main_sizer->Add(create_match_header(), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
    }

    auto* top_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_content_sizer = top_sizer;
    top_sizer->Add(create_preview_panel(), 0, wxALL, FromDIP(20));

    m_right_sizer = new wxBoxSizer(wxVERTICAL);
    m_right_sizer->Add(create_material_selection(), 0, wxEXPAND);
    m_right_sizer->Add(create_gradient_section(), 0, wxEXPAND | wxTOP, FromDIP(7));

    m_ratio_sizer = create_ratio_slider();
    m_right_sizer->Add(m_ratio_sizer, 0, wxEXPAND | wxTOP, FromDIP(7));

    m_triangle_sizer = create_triangle_picker();
    m_right_sizer->Add(m_triangle_sizer, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(7));

    top_sizer->Add(m_right_sizer, 1, wxTOP | wxRIGHT | wxBOTTOM, FromDIP(20));
    main_sizer->Add(top_sizer, 0, wxEXPAND);

    m_recommendation_sizer = create_recommendation_grid();
    main_sizer->Add(m_recommendation_sizer, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(25));

    // Warning panel: red bordered box with exclamation icon + text
    m_warning_sizer = new wxBoxSizer(wxVERTICAL);
    m_warning_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(48)));
    m_warning_panel->SetMinSize(wxSize(-1, FromDIP(48)));
    m_warning_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_warning_panel->Bind(wxEVT_PAINT, &MixedFilamentDialog::paint_warning_panel, this);
    m_warning_panel->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        e.Skip();
        // Wrapping depends on the width, which is only known after layout.
        CallAfter([this]() { update_warning_panel_height(); });
    });
    m_warning_sizer->Add(m_warning_panel, 0, wxEXPAND);
    main_sizer->Add(m_warning_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(25));
    m_warning_panel->Hide();

    if (!m_edit_mode)
        main_sizer->Add(create_replace_target_option(), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    auto* bottom_sizer = new wxBoxSizer(wxHORIZONTAL);
    bottom_sizer->AddStretchSpacer(1);
    bottom_sizer->Add(create_button_panel(), 0, wxALIGN_CENTER_VERTICAL);
    main_sizer->Add(bottom_sizer, 0, wxEXPAND | wxALL, FromDIP(20));

    SetSizer(main_sizer);

    rebuild_all_combos();
    update_mode_ui();
    update_component_count_ui();
    update_preview();
    update_ok_button_state();

    Layout();
    CentreOnParent();
}

static wxColour preview_card_background()
{
    return StateColor::darkModeColorFor(wxColour("#F8F8F8"));
}

// Draws the gray preview card and its caption, then returns the centered swatch rectangle.
static wxRect draw_preview_card_frame(wxDC& dc, wxWindow* window, const wxString& caption)
{
    const wxSize sz = window->GetClientSize();
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(preview_card_background()));
    dc.DrawRectangle(0, 0, sz.GetWidth(), sz.GetHeight());

    dc.SetFont(::Label::Body_12);
    const wxString text = wxControl::Ellipsize(caption, dc, wxELLIPSIZE_END,
                                               std::max(1, sz.GetWidth() - window->FromDIP(8)));
    const wxSize text_sz = dc.GetTextExtent(text);
    const int    sw      = window->FromDIP(kPreviewSwatchSize);
    const int    gap     = window->FromDIP(6);
    const int    top     = std::max(0, (sz.GetHeight() - sw - gap - text_sz.GetHeight()) / 2);

    dc.SetTextForeground(wxColour("#909090"));
    dc.DrawText(text, (sz.GetWidth() - text_sz.GetWidth()) / 2, top + sw + gap);
    return wxRect((sz.GetWidth() - sw) / 2, top, sw, sw);
}

static void draw_match_preview_card(wxDC& dc, wxWindow* window, const wxColour& colour, const wxString& caption)
{
    const wxRect swatch = draw_preview_card_frame(dc, window, caption);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(colour));
    dc.DrawRoundedRectangle(swatch, window->FromDIP(kPreviewSwatchRadius));
}

wxBoxSizer* MixedFilamentDialog::create_preview_panel()
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    m_preview_canvas = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(129), FromDIP(129)));
    m_preview_canvas->SetBackgroundStyle(wxBG_STYLE_PAINT);

    m_preview_canvas->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxBufferedPaintDC dc(m_preview_canvas);
        size_t n = num_components();

        if (m_match_mode) {
            draw_match_preview_card(dc, m_preview_canvas, matched_colour(), _L("Mixed result preview"));
            return;
        }

        const wxRect swatch = draw_preview_card_frame(dc, m_preview_canvas, _L("Effect Preview"));
        if (n == 0) return;

        int swatch_sz = swatch.GetWidth();
        int x0 = swatch.GetX();
        int y0 = swatch.GetY();
        double radius = FromDIP(kPreviewSwatchRadius);

        if (m_result.gradient_enabled && n == 2) {
            Slic3r::GradientCurve curve;
            if (!m_result.gradient_curve.empty()) {
                curve.points = m_result.gradient_curve;
            } else {
                double yStart = (m_result.gradient_direction == 0) ? kGradientMaxRatio : kGradientMinRatio;
                double yEnd   = (m_result.gradient_direction == 0) ? kGradientMinRatio : kGradientMaxRatio;
                curve.points = {{0.0, yStart, NAN, NAN}, {1.0, yEnd, NAN, NAN}};
            }

            wxColour colA = comp_colour(0);
            wxColour colB = comp_colour(1);
            const int bands = std::max(80, swatch_sz);
            double band_h = static_cast<double>(swatch_sz) / bands;
            dc.SetPen(*wxTRANSPARENT_PEN);
            for (int b = 0; b < bands; ++b) {
                double t = 1.0 - (b + 0.5) / bands;
                double r1 = Slic3r::sample_gradient_curve(curve, t);
                double r2 = 1.0 - r1;
                wxColour band_col = blend_n_colors({colA, colB}, {r1, r2});
                dc.SetBrush(wxBrush(band_col));
                int by = y0 + static_cast<int>(b * band_h);
                int bh = static_cast<int>((b + 1) * band_h) - static_cast<int>(b * band_h) + 1;
                dc.DrawRectangle(x0, by, swatch_sz, bh);
            }

            // Mask corners: overdraw a thick background-colored rounded rect frame
            // so the inner edge forms the desired rounded corners.
            // Known limitation: this assumes the panel background equals
            // preview_card_background().  wxGraphicsContext::Clip(path) is not
            // available in our wxWidgets build (only Clip(wxRegion) exists).
            int r = static_cast<int>(radius);
            wxColour bg = preview_card_background();
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxPen(bg, r * 2));
            dc.DrawRoundedRectangle(x0 - r, y0 - r, swatch_sz + r * 2, swatch_sz + r * 2, radius * 2);
        } else {
            std::vector<wxColour> cols;
            std::vector<double>   weights;
            for (size_t i = 0; i < n; ++i) {
                cols.push_back(comp_colour(i));
                weights.push_back(ratio(i) / 100.0);
            }
            wxColour mixed = blend_n_colors(cols, weights);
            dc.SetBrush(wxBrush(mixed));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRoundedRectangle(x0, y0, swatch_sz, swatch_sz, radius);
        }
    });

    sizer->Add(m_preview_canvas, 0, wxALIGN_CENTER);

    return sizer;
}

wxBoxSizer* MixedFilamentDialog::create_material_selection()
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    // Summary panel — draws N components dynamically
    m_summary_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(234), FromDIP(40)));
    m_summary_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_summary_panel->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxBufferedPaintDC dc(m_summary_panel);
        wxSize sz = m_summary_panel->GetClientSize();

        wxColour sum_bg   = StateColor::darkModeColorFor(wxColour("#F8F8F8"));
        wxColour sum_text = StateColor::darkModeColorFor(wxColour("#262E30"));
        dc.SetBrush(wxBrush(sum_bg));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(0, 0, sz.GetWidth(), sz.GetHeight());

        int swatch_sz = FromDIP(20);
        int y_center = (sz.GetHeight() - swatch_sz) / 2;
        int x = FromDIP(13);

        dc.SetFont(::Label::Body_13);

        auto draw_summary_swatch = [&](size_t comp_idx) {
            unsigned int c = comp(comp_idx);
            std::string color_hex = "#D9D9D9";
            if (c >= 1 && c <= m_physical_colors.size())
                color_hex = m_physical_colors[c - 1];
            std::string label = std::to_string(c);
            wxBitmap* icon = get_extruder_color_icon(color_hex, label, swatch_sz, swatch_sz);
            if (icon && icon->IsOk())
                dc.DrawBitmap(*icon, x, y_center);
            x += swatch_sz + FromDIP(4);
        };

        if (m_result.gradient_enabled && num_components() == 2) {
            size_t idx_a = (m_result.gradient_direction == 0) ? 0 : 1;
            size_t idx_b = 1 - idx_a;
            draw_summary_swatch(idx_a);

            dc.SetTextForeground(sum_text);
            wxString arrow = wxT("\u2192");
            wxSize arrow_sz = dc.GetTextExtent(arrow);
            dc.DrawText(arrow, x, y_center + (swatch_sz - arrow_sz.GetHeight()) / 2);
            x += arrow_sz.GetWidth() + FromDIP(4);

            draw_summary_swatch(idx_b);
        } else {
            for (size_t i = 0; i < num_components(); ++i) {
                if (i > 0) {
                    dc.SetTextForeground(sum_text);
                    wxString plus = wxT("+");
                    wxSize plus_sz = dc.GetTextExtent(plus);
                    dc.DrawText(plus, x, y_center + (swatch_sz - plus_sz.GetHeight()) / 2);
                    x += plus_sz.GetWidth() + FromDIP(4);
                }
                draw_summary_swatch(i);

                dc.SetTextForeground(sum_text);
                wxString pct = wxString::Format(wxT("%d%%"), ratio(i));
                wxSize pct_sz = dc.GetTextExtent(pct);
                dc.DrawText(pct, x, y_center + (swatch_sz - pct_sz.GetHeight()) / 2);
                x += pct_sz.GetWidth() + FromDIP(4);
            }
        }
    });
    sizer->Add(m_summary_panel, 0, wxEXPAND);

    auto* sel_label = new wxStaticText(this, wxID_ANY, _L("Select Mixed Materials"));
    sel_label->SetForegroundColour(wxColour("#909090"));
    sel_label->SetFont(::Label::Body_12);
    sizer->Add(sel_label, 0, wxTOP, FromDIP(6));
    m_sel_label = sel_label;

    m_material_rows_sizer = new wxBoxSizer(wxVERTICAL);

    m_combo_filaments.clear();
    m_combo_to_physical.clear();
    for (size_t i = 0; i < m_result.components.size(); ++i)
        append_material_row();

    sizer->Add(m_material_rows_sizer, 0, wxEXPAND);

    auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_btn_add_material = new Button(this, _L("+ Add Filament"));
    m_btn_add_material->SetBackgroundColor(wxColour("#F8F8F8"));
    m_btn_add_material->SetBorderColor(wxColour("#EEEEEE"));
    m_btn_add_material->SetTextColor(wxColour("#262E30"));
    m_btn_add_material->SetMinSize(wxSize(-1, FromDIP(24)));
    m_btn_add_material->SetCornerRadius(0);
    m_btn_add_material->SetCursor(wxCursor(wxCURSOR_HAND));
    m_btn_add_material->EnableTooltipEvenDisabled();
    m_btn_add_material->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_add_material(); });
    btn_sizer->Add(m_btn_add_material, 1, wxRIGHT, FromDIP(6));

    m_btn_remove_material = new Button(this, _L("- Delete Filament"));
    m_btn_remove_material->SetBackgroundColor(wxColour("#F8F8F8"));
    m_btn_remove_material->SetBorderColor(wxColour("#EEEEEE"));
    m_btn_remove_material->SetTextColor(wxColour("#262E30"));
    m_btn_remove_material->SetMinSize(wxSize(-1, FromDIP(24)));
    m_btn_remove_material->SetCornerRadius(0);
    m_btn_remove_material->SetCursor(wxCursor(wxCURSOR_HAND));
    m_btn_remove_material->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_remove_material(); });
    m_btn_remove_material->Hide();
    btn_sizer->Add(m_btn_remove_material, 1, 0, 0);

    sizer->Add(btn_sizer, 0, wxEXPAND | wxTOP, FromDIP(9));

    return sizer;
}

wxBoxSizer* MixedFilamentDialog::create_ratio_slider()
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* ratio_label = new wxStaticText(this, wxID_ANY, _L("Ratio"));
    ratio_label->SetForegroundColour(wxColour("#909090"));
    ratio_label->SetFont(::Label::Body_12);
    sizer->Add(ratio_label, 0, wxBOTTOM, FromDIP(4));

    m_ratio_bar = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(27)));
    m_ratio_bar->SetMinSize(wxSize(-1, FromDIP(27)));
    m_ratio_bar->SetBackgroundStyle(wxBG_STYLE_PAINT);

    m_ratio_bar->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxBufferedPaintDC dc(m_ratio_bar);
        wxSize sz = m_ratio_bar->GetClientSize();

        wxColour col_a = comp_colour(0), col_b = comp_colour(1);

        for (int x = 0; x < sz.GetWidth(); ++x) {
            double t = (double)x / sz.GetWidth();
            wxColour c = blend_colors(col_a, col_b, 1.0 - t);
            dc.SetPen(wxPen(c));
            dc.DrawLine(x, 0, x, sz.GetHeight());
        }

        int div_x = (int)(ratio(1) / 100.0 * sz.GetWidth());
        dc.SetPen(wxPen(StateColor::darkModeColorFor(wxColour(80, 80, 80)), FromDIP(4)));
        dc.DrawLine(div_x, 0, div_x, sz.GetHeight());
        dc.SetPen(wxPen(*wxWHITE, FromDIP(2)));
        dc.DrawLine(div_x, 0, div_x, sz.GetHeight());
    });

    m_ratio_bar->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        if (m_ratio_editor_panel && m_ratio_editor_panel->IsShown())
            commit_ratio_editor(true);
        m_dragging = true;
        m_ratio_bar->CaptureMouse();
        int new_ratio = 100 - (int)(e.GetX() * 100.0 / m_ratio_bar->GetClientSize().GetWidth() + 0.5);
        on_ratio_changed(std::max(MIN_COMPONENT_RATIO, std::min(100 - MIN_COMPONENT_RATIO, new_ratio)));
    });

    m_ratio_bar->Bind(wxEVT_MOTION, [this](wxMouseEvent& e) {
        if (!m_dragging) return;
        int new_ratio = 100 - (int)(e.GetX() * 100.0 / m_ratio_bar->GetClientSize().GetWidth() + 0.5);
        on_ratio_changed(std::max(MIN_COMPONENT_RATIO, std::min(100 - MIN_COMPONENT_RATIO, new_ratio)));
    });

    m_ratio_bar->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
        if (m_dragging) {
            m_dragging = false;
            if (m_ratio_bar->HasCapture())
                m_ratio_bar->ReleaseMouse();
        }
    });

    sizer->Add(m_ratio_bar, 0, wxEXPAND);

    auto* pct_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_label_ratio_a = new RatioLabelPanel(this);
    m_label_ratio_a->SetLabel(wxString::Format(wxT("%d%%"), ratio(0)));
    m_label_ratio_b = new RatioLabelPanel(this);
    m_label_ratio_b->SetLabel(wxString::Format(wxT("%d%%"), ratio(1)));
    auto bind_ratio_click = [this](RatioLabelPanel* label, size_t idx) {
        label->Bind(wxEVT_LEFT_DOWN, [this, label, idx](wxMouseEvent&) {
            wxRect rect(wxPoint(0, 0), label->GetClientSize());
            start_ratio_editor(idx, label, rect);
        });
    };
    bind_ratio_click(m_label_ratio_a, 0);
    bind_ratio_click(m_label_ratio_b, 1);
    pct_sizer->Add(m_label_ratio_a, 0);
    pct_sizer->AddStretchSpacer(1);
    pct_sizer->Add(m_label_ratio_b, 0);
    sizer->Add(pct_sizer, 0, wxEXPAND | wxTOP, FromDIP(2));

    return sizer;
}

// ---- Triangle (ternary) ratio picker ----

// Barycentric coordinate utilities
struct TriPoint { double x, y; };

static double tri_signed_area2(TriPoint a, TriPoint b, TriPoint c)
{
    return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
}

static bool tri_contains(TriPoint p, TriPoint v0, TriPoint v1, TriPoint v2)
{
    double total = tri_signed_area2(v0, v1, v2);
    if (std::abs(total) < 1e-9) return false;
    double s0 = tri_signed_area2(p, v1, v2) / total;
    double s1 = tri_signed_area2(v0, p, v2) / total;
    double s2 = 1.0 - s0 - s1;
    return s0 >= -0.001 && s1 >= -0.001 && s2 >= -0.001;
}

static void tri_barycentric(TriPoint p, TriPoint v0, TriPoint v1, TriPoint v2,
                            double& w0, double& w1, double& w2)
{
    double total = std::abs(tri_signed_area2(v0, v1, v2));
    if (total < 1e-9) { w0 = w1 = w2 = 1.0 / 3.0; return; }
    w0 = std::abs(tri_signed_area2(p, v1, v2)) / total;
    w1 = std::abs(tri_signed_area2(v0, p, v2)) / total;
    w2 = 1.0 - w0 - w1;
    w0 = std::clamp(w0, 0.0, 1.0);
    w1 = std::clamp(w1, 0.0, 1.0);
    w2 = std::clamp(w2, 0.0, 1.0);
    double s = w0 + w1 + w2;
    if (s > 0) { w0 /= s; w1 /= s; w2 /= s; }
}

static TriPoint tri_clamp(TriPoint p, TriPoint v0, TriPoint v1, TriPoint v2)
{
    double w0, w1, w2;
    tri_barycentric(p, v0, v1, v2, w0, w1, w2);
    w0 = std::clamp(w0, 0.0, 1.0);
    w1 = std::clamp(w1, 0.0, 1.0);
    w2 = std::clamp(w2, 0.0, 1.0);
    double s = w0 + w1 + w2;
    if (s > 0) { w0 /= s; w1 /= s; w2 /= s; }
    return {w0 * v0.x + w1 * v1.x + w2 * v2.x,
            w0 * v0.y + w1 * v1.y + w2 * v2.y};
}

wxBoxSizer* MixedFilamentDialog::create_triangle_picker()
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    int panel_w = FromDIP(160);
    int panel_h = FromDIP(160);
    m_triangle_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(panel_w, panel_h));
    m_triangle_panel->SetMinSize(wxSize(panel_w, panel_h));
    m_triangle_panel->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    m_triangle_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);

    auto get_vertices = [this]() -> std::tuple<TriPoint, TriPoint, TriPoint> {
        wxSize sz = m_triangle_panel->GetClientSize();
        double pw = sz.GetWidth(), ph = sz.GetHeight();
        double margin = FromDIP(20);
        double avail = std::min(pw, ph) - 2 * margin;
        double side = avail;
        double tri_h = side * std::sqrt(3.0) / 2.0;
        double cx = pw / 2.0;
        double top_y = (ph - tri_h) / 2.0;
        double bot_y = top_y + tri_h;
        TriPoint v0 = {cx, top_y};                             // top
        TriPoint v1 = {cx - side / 2.0, bot_y};               // bottom-left
        TriPoint v2 = {cx + side / 2.0, bot_y};               // bottom-right
        return {v0, v1, v2};
    };

    m_triangle_panel->Bind(wxEVT_PAINT, [this, get_vertices](wxPaintEvent&) {
        wxBufferedPaintDC dc(m_triangle_panel);
        wxSize sz = m_triangle_panel->GetClientSize();
        auto [v0, v1, v2] = get_vertices();

        wxColour tri_bg = StateColor::darkModeColorFor(*wxWHITE);
        dc.SetBrush(wxBrush(tri_bg));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(0, 0, sz.GetWidth(), sz.GetHeight());

        wxColour c0 = comp_colour(0), c1 = comp_colour(1), c2 = comp_colour(2);

        const bool cache_valid = m_tri_cache_bmp.IsOk() &&
            m_tri_cache_size == sz &&
            m_tri_cache_c0 == c0 && m_tri_cache_c1 == c1 && m_tri_cache_c2 == c2;

        if (!cache_valid) {
            int min_y = (int)std::min({v0.y, v1.y, v2.y});
            int max_y = (int)std::max({v0.y, v1.y, v2.y});
            int min_x = (int)std::min({v0.x, v1.x, v2.x});
            int max_x = (int)std::max({v0.x, v1.x, v2.x});

            m_tri_cache_bmp = wxBitmap(sz.GetWidth(), sz.GetHeight(), 24);
            wxMemoryDC mdc(m_tri_cache_bmp);
            mdc.SetBrush(wxBrush(tri_bg));
            mdc.SetPen(*wxTRANSPARENT_PEN);
            mdc.DrawRectangle(0, 0, sz.GetWidth(), sz.GetHeight());

            for (int py = min_y; py <= max_y; ++py) {
                for (int px = min_x; px <= max_x; ++px) {
                    TriPoint p = {(double)px, (double)py};
                    if (!tri_contains(p, v0, v1, v2)) continue;
                    double w0, w1, w2;
                    tri_barycentric(p, v0, v1, v2, w0, w1, w2);
                    unsigned char mr, mg, mb;
                    if (w0 + w1 > 1e-6) {
                        float t01 = static_cast<float>(w1 / (w0 + w1));
                        Slic3r::filament_mixer_lerp(c0.Red(), c0.Green(), c0.Blue(),
                                                    c1.Red(), c1.Green(), c1.Blue(),
                                                    t01, &mr, &mg, &mb);
                        float t2 = static_cast<float>(w2);
                        Slic3r::filament_mixer_lerp(mr, mg, mb,
                                                    c2.Red(), c2.Green(), c2.Blue(),
                                                    t2, &mr, &mg, &mb);
                    } else {
                        mr = c2.Red(); mg = c2.Green(); mb = c2.Blue();
                    }
                    mdc.SetPen(wxPen(wxColour(mr, mg, mb)));
                    mdc.DrawPoint(px, py);
                }
            }

            mdc.SetPen(wxPen(StateColor::darkModeColorFor(wxColour("#CECECE")), 1));
            mdc.SetBrush(*wxTRANSPARENT_BRUSH);
            wxPoint pts[3] = {{(int)v0.x, (int)v0.y}, {(int)v1.x, (int)v1.y}, {(int)v2.x, (int)v2.y}};
            mdc.DrawPolygon(3, pts);

            mdc.SelectObject(wxNullBitmap);
            m_tri_cache_c0 = c0; m_tri_cache_c1 = c1; m_tri_cache_c2 = c2;
            m_tri_cache_size = sz;
        }

        dc.DrawBitmap(m_tri_cache_bmp, 0, 0);

        // Drag handle (always redrawn on top of cached bitmap)
        double hx = m_tri_wx * v0.x + m_tri_wy * v1.x + m_tri_wz * v2.x;
        double hy = m_tri_wx * v0.y + m_tri_wy * v1.y + m_tri_wz * v2.y;
        int handle_r = FromDIP(5);
        dc.SetBrush(*wxWHITE_BRUSH);
        dc.SetPen(wxPen(wxColour("#262E30"), FromDIP(2)));
        dc.DrawCircle((int)hx, (int)hy, handle_r);

        if (m_result.ratios.size() >= 3) {
            dc.SetFont(::Label::Body_10);
            wxSize ts0 = dc.GetTextExtent(wxString::Format(wxT("%d%%"), m_result.ratios[0]));
            int top_label_y = std::max(0, (int)(v0.y - ts0.GetHeight() - FromDIP(4)));

            dc.SetFont(::Label::Body_12);
            dc.SetTextForeground(wxColour("#909090"));
            dc.DrawText(_L("Ratio"), FromDIP(2), top_label_y);

            // Position the real RatioLabelPanel children
            for (int i = 0; i < 3 && i < (int)m_triangle_ratio_labels.size(); ++i) {
                if (!m_triangle_ratio_labels[i]) continue;
                m_triangle_ratio_labels[i]->SetLabel(
                    wxString::Format(wxT("%d%%"), m_result.ratios[i]));
                wxSize lsz = m_triangle_ratio_labels[i]->GetMinSize();
                int lx = 0, ly = 0;
                if (i == 0) {
                    lx = (int)(v0.x - lsz.GetWidth() / 2);
                    ly = top_label_y;
                } else if (i == 1) {
                    lx = (int)(v1.x - lsz.GetWidth() / 2);
                    ly = (int)(v1.y + FromDIP(3));
                } else {
                    lx = (int)(v2.x - lsz.GetWidth() / 2);
                    ly = (int)(v2.y + FromDIP(3));
                }
                m_triangle_ratio_labels[i]->SetSize(lx, ly, lsz.GetWidth(), lsz.GetHeight());
            }
        }
    });

    auto handle_mouse = [this, get_vertices](wxMouseEvent& e, bool is_down) {
        auto [v0, v1, v2] = get_vertices();
        TriPoint p = {(double)e.GetX(), (double)e.GetY()};

        if (is_down) {
            // Only start dragging when the press lands inside the triangle;
            // clicks outside the triangle must not change the mix ratio.
            if (!tri_contains(p, v0, v1, v2))
                return;
            m_dragging = true;
            m_triangle_panel->CaptureMouse();
        }

        if (!m_dragging) return;

        TriPoint clamped = tri_clamp(p, v0, v1, v2);
        tri_barycentric(clamped, v0, v1, v2, m_tri_wx, m_tri_wy, m_tri_wz);

        int r0 = (int)(m_tri_wx * 100 + 0.5);
        int r1 = (int)(m_tri_wy * 100 + 0.5);
        int r2 = 100 - r0 - r1;
        r0 = std::clamp(r0, 0, 100);
        r1 = std::clamp(r1, 0, 100);
        r2 = std::clamp(r2, 0, 100);

        apply_dragged_triangle_ratio(r0, r1, r2);
    };

    // Create 3 RatioLabelPanel children on the triangle panel
    m_triangle_ratio_labels.fill(nullptr);
    for (int i = 0; i < 3; ++i) {
        auto* lbl = new RatioLabelPanel(m_triangle_panel);
        lbl->SetLabel(wxString::Format(wxT("%d%%"),
                      (i < (int)m_result.ratios.size()) ? m_result.ratios[i] : 33));
        size_t idx = (size_t)i;
        lbl->Bind(wxEVT_LEFT_DOWN, [this, lbl, idx](wxMouseEvent&) {
            wxRect rect(wxPoint(0, 0), lbl->GetClientSize());
            start_ratio_editor(idx, lbl, rect);
        });
        m_triangle_ratio_labels[i] = lbl;
    }

    m_triangle_panel->Bind(wxEVT_LEFT_DOWN, [this, handle_mouse](wxMouseEvent& e) {
        if (m_ratio_editor_panel && m_ratio_editor_panel->IsShown())
            commit_ratio_editor(true);
        handle_mouse(e, true);
    });
    m_triangle_panel->Bind(wxEVT_MOTION, [this, handle_mouse](wxMouseEvent& e) {
        if (m_dragging)
            handle_mouse(e, false);
    });
    m_triangle_panel->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
        if (m_dragging) {
            m_dragging = false;
            if (m_triangle_panel->HasCapture())
                m_triangle_panel->ReleaseMouse();
        }
    });

    sizer->Add(m_triangle_panel, 0);

    return sizer;
}

wxBoxSizer* MixedFilamentDialog::create_gradient_section()
{
    m_gradient_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_chk_gradient = new ::CheckBox(this);
    m_chk_gradient->SetValue(m_result.gradient_enabled);
    m_chk_gradient->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& e) { e.Skip(); on_gradient_toggled(); });
    m_gradient_sizer->Add(m_chk_gradient, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, FromDIP(4));

    m_label_gradient = new wxStaticText(this, wxID_ANY, _L("Gradient Effect"));
    m_label_gradient->SetFont(::Label::Body_13);
    m_gradient_sizer->Add(m_label_gradient, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    m_combo_gradient_dir = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                        wxSize(FromDIP(152), FromDIP(24)), 0, nullptr, wxCB_READONLY);
    m_combo_gradient_dir->SetKeepDropArrow(true);
    update_gradient_direction_items();
    m_combo_gradient_dir->SetSelection(m_result.gradient_direction);
    m_combo_gradient_dir->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { on_gradient_direction_changed(); });
    m_combo_gradient_dir->Show(m_result.gradient_enabled);

    m_gradient_sizer->Add(m_combo_gradient_dir, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(m_gradient_sizer, 0, wxEXPAND);

    // Custom curve editor: visible only when gradient is on and exactly 2 components are mixed.
    m_curve_sizer = new wxBoxSizer(wxVERTICAL);
    m_curve_editor = new GradientCurveEditor(this, comp_colour(0), comp_colour(1));
    if (!m_result.gradient_curve.empty())
        m_curve_editor->set_points(m_result.gradient_curve);
    else
        m_curve_editor->reset_to_linear((m_result.gradient_direction == 0) ? 0.9 : 0.1,
                                        (m_result.gradient_direction == 0) ? 0.1 : 0.9);
    m_curve_editor->Bind(wxEVT_GRADIENT_CURVE_CHANGED,
        [this](wxCommandEvent&) { on_gradient_curve_changed(); });
    m_curve_sizer->Add(m_curve_editor, 0, wxEXPAND | wxTOP, FromDIP(4));

    outer->Add(m_curve_sizer, 0, wxEXPAND | wxTOP, FromDIP(6));
    const bool curve_visible = m_result.gradient_enabled && num_components() == 2;
    m_curve_sizer->ShowItems(curve_visible);

    // Per-part gradient toggle sits BELOW the curve editor.
    m_per_part_gradient_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_chk_per_part_gradient = new ::CheckBox(this);
    m_chk_per_part_gradient->SetValue(m_result.per_part_gradient);
    m_chk_per_part_gradient->Bind(wxEVT_TOGGLEBUTTON,
        [this](wxCommandEvent& e) { e.Skip(); on_per_part_gradient_toggled(); });
    m_per_part_gradient_sizer->Add(m_chk_per_part_gradient, 0,
        wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, FromDIP(4));

    m_label_per_part_gradient = new wxStaticText(this, wxID_ANY, _L("Enable per-part gradient effect"));
    m_label_per_part_gradient->SetFont(::Label::Body_13);
    m_per_part_gradient_sizer->Add(m_label_per_part_gradient, 0,
        wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    outer->Add(m_per_part_gradient_sizer, 0, wxEXPAND | wxTOP, FromDIP(2));
    m_per_part_gradient_sizer->ShowItems(m_result.gradient_enabled);

    return outer;
}

wxBoxSizer* MixedFilamentDialog::create_recommendation_grid()
{
    auto* outer = new wxBoxSizer(wxVERTICAL);

    auto* title_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto* rec_label = new wxStaticText(this, wxID_ANY, _L("Mixing Recommendations"));
    rec_label->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#ACACAC")));
    rec_label->SetFont(::Label::Body_10);
    title_sizer->Add(rec_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));

    auto* rec_line = new wxPanel(this, wxID_ANY);
    rec_line->SetMinSize(wxSize(-1, 1));
    rec_line->SetBackgroundColour(StateColor::darkModeColorFor(ThemeColor::Grey400));
    title_sizer->Add(rec_line, 1, wxALIGN_CENTER_VERTICAL);

    outer->Add(title_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

    m_recommendation_scroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(116)));
    m_recommendation_scroll->SetScrollRate(0, 5);
    m_recommendation_scroll->SetBackgroundColour(StateColor::darkModeColorFor(wxColour("#F8F8F8")));

    m_recommendation_grid = new wxWrapSizer(wxHORIZONTAL, wxWRAPSIZER_DEFAULT_FLAGS);
    auto* scroll_inner_sizer = new wxBoxSizer(wxVERTICAL);
    scroll_inner_sizer->Add(m_recommendation_grid, 1, wxEXPAND | wxLEFT | wxTOP, FromDIP(8));
    m_recommendation_scroll->SetSizer(scroll_inner_sizer);

    rebuild_recommendation_items();

    outer->Add(m_recommendation_scroll, 1, wxEXPAND | wxTOP, FromDIP(4));
    return outer;
}

void MixedFilamentDialog::rebuild_recommendation_items()
{
    if (!m_recommendation_scroll || !m_recommendation_grid)
        return;

    static constexpr int MAX_RECOMMENDATIONS = 100;

    // Each swatch is a child window, so rebuilding up to 100 of them is slow and visibly
    // repaints mid tab switch. The grid is hidden on the match tab, and unchanged inputs
    // give the same swatches.
    if (m_match_mode)
        return;
    std::string key = (num_components() >= 3 ? "3" : "2") + std::string(m_enforce_nozzle_diameter ? "d" : "-");
    for (size_t i = 0; i < m_physical_colors.size(); ++i) {
        key += '|' + m_physical_colors[i];
        key += '|' + (i < m_physical_types.size() ? m_physical_types[i] : std::string());
        key += '|' + (i < m_physical_diameters.size() ? m_physical_diameters[i] : std::string());
        key += '|' + (i < m_physical_names.size() ? m_physical_names[i] : std::string());
    }
    if (key == m_recommendation_key)
        return;
    m_recommendation_key = std::move(key);

    m_recommendation_scroll->Freeze();
    m_recommendation_grid->Clear(true);

    size_t n = m_physical_colors.size();
    int count = 0;

    // Same type is always required. Nozzle diameter is only a mix constraint
    // while multi-diameter mode is on (m_enforce_nozzle_diameter).
    std::map<std::pair<std::string, std::string>, std::vector<size_t>> type_groups;
    for (size_t i = 0; i < n; ++i) {
        std::string t = (i < m_physical_types.size()) ? m_physical_types[i] : "PLA";
        // Skip support filaments (type ends with "-S")
        if (t.size() >= 2 && t.compare(t.size() - 2, 2, "-S") == 0)
            continue;
        const std::string diameter = (m_enforce_nozzle_diameter && i < m_physical_diameters.size())
            ? m_physical_diameters[i] : std::string();
        type_groups[{t, diameter}].push_back(i);
    }

    if (num_components() >= 3) {
        // Three-color: C(g,3) x 3 variants per same-type group
        for (auto& group : type_groups) {
            auto& indices = group.second;
            if (count >= MAX_RECOMMENDATIONS) break;
            size_t g = indices.size();
            for (size_t ai = 0; ai < g && count < MAX_RECOMMENDATIONS; ++ai) {
                for (size_t bi = ai + 1; bi < g && count < MAX_RECOMMENDATIONS; ++bi) {
                    for (size_t ci = bi + 1; ci < g && count < MAX_RECOMMENDATIONS; ++ci) {
                        size_t idx[3] = {indices[ai], indices[bi], indices[ci]};
                        // 3 variants: each filament takes the 50% role in turn
                        for (int dominant = 0; dominant < 3 && count < MAX_RECOMMENDATIONS; ++dominant) {
                            size_t i0 = idx[(dominant + 1) % 3]; // 25%
                            size_t i1 = idx[(dominant + 2) % 3]; // 25%
                            size_t i2 = idx[dominant];           // 50%

                            wxColour ca(m_physical_colors[i0]);
                            wxColour cb(m_physical_colors[i1]);
                            wxColour cc(m_physical_colors[i2]);
                            wxColour mixed = blend_n_colors({ca, cb, cc}, {0.25, 0.25, 0.50});

                            auto* item = new wxPanel(m_recommendation_scroll, wxID_ANY,
                                                     wxDefaultPosition, wxSize(FromDIP(20), FromDIP(20)));
                            item->SetBackgroundColour(mixed);
                            item->SetCursor(wxCursor(wxCURSOR_HAND));

                            unsigned int ca_1 = (unsigned int)(i0 + 1);
                            unsigned int cb_1 = (unsigned int)(i1 + 1);
                            unsigned int cc_1 = (unsigned int)(i2 + 1);
                            item->Bind(wxEVT_LEFT_UP, [this, ca_1, cb_1, cc_1](wxMouseEvent&) {
                                on_recommendation_clicked_triple(ca_1, cb_1, cc_1);
                            });
                            item->SetToolTip(wxString::Format(wxT("%s + %s + %s"),
                                wxString::FromUTF8(m_physical_names[i0]),
                                wxString::FromUTF8(m_physical_names[i1]),
                                wxString::FromUTF8(m_physical_names[i2])));

                            m_recommendation_grid->Add(item, 0, wxRIGHT | wxBOTTOM, FromDIP(6));
                            ++count;
                        }
                    }
                }
            }
        }
    } else {
        // Two-color: C(g,2) per same-type group
        for (auto& group : type_groups) {
            auto& indices = group.second;
            if (count >= MAX_RECOMMENDATIONS) break;
            size_t g = indices.size();
            for (size_t ai = 0; ai < g && count < MAX_RECOMMENDATIONS; ++ai) {
                for (size_t bi = ai + 1; bi < g && count < MAX_RECOMMENDATIONS; ++bi) {
                    size_t i = indices[ai];
                    size_t j = indices[bi];

                    wxColour ca(m_physical_colors[i]);
                    wxColour cb(m_physical_colors[j]);
                    wxColour mixed = blend_colors(ca, cb, 0.5);

                    auto* item = new wxPanel(m_recommendation_scroll, wxID_ANY,
                                             wxDefaultPosition, wxSize(FromDIP(20), FromDIP(20)));
                    item->SetBackgroundColour(mixed);
                    item->SetCursor(wxCursor(wxCURSOR_HAND));

                    unsigned int comp_a = (unsigned int)(i + 1);
                    unsigned int comp_b = (unsigned int)(j + 1);
                    item->Bind(wxEVT_LEFT_UP, [this, comp_a, comp_b](wxMouseEvent&) {
                        on_recommendation_clicked(comp_a, comp_b);
                    });
                    item->SetToolTip(wxString::Format(wxT("%s + %s"),
                        wxString::FromUTF8(m_physical_names[i]),
                        wxString::FromUTF8(m_physical_names[j])));

                    m_recommendation_grid->Add(item, 0, wxRIGHT | wxBOTTOM, FromDIP(6));
                    ++count;
                }
            }
        }
    }

    m_recommendation_scroll->SetScrollbars(0, FromDIP(20), 0, 1);
    m_recommendation_scroll->FitInside();
    m_recommendation_scroll->Layout();
    m_recommendation_scroll->Thaw();
}

wxBoxSizer* MixedFilamentDialog::create_button_panel()
{
    auto* sizer = new wxBoxSizer(wxHORIZONTAL);

    m_btn_cancel = new Button(this, _L("Cancel"));
    m_btn_cancel->SetBackgroundColor(*wxWHITE);
    m_btn_cancel->SetBorderColor(wxColour("#CECECE"));
    m_btn_cancel->SetTextColor(wxColour("#262E30"));
    m_btn_cancel->SetMinSize(wxSize(FromDIP(55), FromDIP(24)));
    m_btn_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });

    m_btn_ok = new Button(this, _L("OK"));
    m_btn_ok->SetBackgroundColor(wxColour("#00AE42"));
    m_btn_ok->SetBorderColor(wxColour("#00AE42"));
    // #FFFFFE is not remapped by dark mode, keeping the text white on the green button.
    m_btn_ok->SetTextColor(wxColour("#FFFFFE"));
    m_btn_ok->SetMinSize(wxSize(FromDIP(55), FromDIP(24)));
    m_btn_ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (!m_can_confirm)
            return;
        EndModal(wxID_OK);
    });

    sizer->Add(m_btn_cancel, 0, wxRIGHT, FromDIP(12));
    sizer->Add(m_btn_ok, 0);

    return sizer;
}

void MixedFilamentDialog::rebuild_all_combos()
{
    m_combo_to_physical.resize(m_combo_filaments.size());

    for (size_t i = 0; i < m_combo_filaments.size(); ++i) {
        std::set<unsigned int> others_selected;
        std::set<std::string>  others_types;
        std::set<std::string>  others_diameters;
        for (size_t k = 0; k < m_result.components.size(); ++k) {
            if (k == i) continue;
            unsigned int phys = m_result.components[k];
            others_selected.insert(phys);
            if (phys >= 1 && phys <= m_physical_types.size())
                others_types.insert(m_physical_types[phys - 1]);
            if (m_enforce_nozzle_diameter && phys >= 1 && phys <= m_physical_diameters.size())
                others_diameters.insert(m_physical_diameters[phys - 1]);
        }

        auto* combo = m_combo_filaments[i];
        combo->Clear();
        m_combo_to_physical[i].clear();

        int restore_sel = -1;
        unsigned int cur_phys = (i < m_result.components.size()) ? m_result.components[i] : 0;
        const bool has_match_target = m_match_mode && m_match_target >= 0
                                      && size_t(m_match_target) < m_physical_colors.size();
        const std::string target_hex = has_match_target
            ? decompose_normalize_color_hex(m_physical_colors[m_match_target]) : std::string();

        if (cur_phys == 0) {
            combo->Append(_L("-- Select --"));
            m_combo_to_physical[i].push_back(0);
            restore_sel = 0;
        }

        for (size_t j = 0; j < m_physical_names.size(); ++j) {
            unsigned int phys_1based = (unsigned int)(j + 1);

            if (others_selected.count(phys_1based))
                continue;
            // Same-color filaments cannot be mix components for the target, matching run_color_match().
            if (has_match_target && (int(j) == m_match_target
                                     || (j < m_physical_colors.size()
                                         && decompose_normalize_color_hex(m_physical_colors[j]) == target_hex)))
                continue;

            int style = 0;
            if (!others_types.empty() && !m_physical_types.empty()) {
                std::string this_type = (j < m_physical_types.size()) ? m_physical_types[j] : "PLA";
                if (others_types.find(this_type) == others_types.end())
                    style = DD_ITEM_STYLE_DIMMED;
            }
            if (m_enforce_nozzle_diameter && !others_diameters.empty() && !m_physical_diameters.empty()) {
                const std::string this_diameter = j < m_physical_diameters.size() ? m_physical_diameters[j] : std::string();
                if (others_diameters.find(this_diameter) == others_diameters.end())
                    style = DD_ITEM_STYLE_DIMMED;
            }

            int idx = combo->Append(combo_item_name(j), make_swatch_bitmap(j), style);
            combo->SetItemSuffix(idx, combo_item_suffix(j));
            m_combo_to_physical[i].push_back(phys_1based);

            if (phys_1based == cur_phys)
                restore_sel = idx;
        }

        if (restore_sel >= 0)
            combo->SetSelection(restore_sel);
        else if (combo->GetCount() > 0)
            combo->SetSelection(0);
    }
    refresh_combo_suffixes();
}

void MixedFilamentDialog::refresh_curve_editor_colors()
{
    if (m_curve_editor)
        m_curve_editor->set_colors(comp_colour(0), comp_colour(1));
}

// ---- Event Handlers ----

void MixedFilamentDialog::on_filament_changed()
{
    for (size_t i = 0; i < m_combo_filaments.size() && i < m_result.components.size(); ++i) {
        int sel = m_combo_filaments[i]->GetSelection();
        if (sel >= 0 && i < m_combo_to_physical.size() && sel < (int)m_combo_to_physical[i].size())
            m_result.components[i] = m_combo_to_physical[i][sel];
    }

    refresh_curve_editor_colors();
    rebuild_all_combos();
    update_gradient_direction_items();
    update_preview();
    update_ok_button_state();
}

void MixedFilamentDialog::on_ratio_changed(int new_ratio_a)
{
    if (m_result.ratios.size() < 2) return;
    m_result.ratios[0] = new_ratio_a;
    m_result.ratios[1] = 100 - new_ratio_a;

    reset_manual_ratio_state();
    refresh_ratio_labels();

    update_preview();
}

void MixedFilamentDialog::on_gradient_toggled()
{
    bool checked = m_chk_gradient->GetValue();

    if (checked) {
        auto& print_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        if (!print_config.opt_bool("enable_mixed_color_sublayer")) {
            wxMessageDialog dlg(this,
                _L("Gradient effect requires 'Mixed color sublayer' to be enabled. Enable it now?"),
                _L("Mixed Color Sublayer"),
                wxYES_NO | wxICON_QUESTION);
            if (dlg.ShowModal() == wxID_YES) {
                DynamicPrintConfig new_conf;
                new_conf.set_key_value("enable_mixed_color_sublayer", new ConfigOptionBool(true));
                wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(new_conf);
            } else {
                m_chk_gradient->SetValue(false);
                return;
            }
        }
    }

    m_result.gradient_enabled = m_chk_gradient->GetValue();

    if (m_ratio_sizer)
        m_ratio_sizer->ShowItems(!m_result.gradient_enabled && num_components() == 2);
    if (m_combo_gradient_dir)
        m_combo_gradient_dir->Show(m_result.gradient_enabled);
    if (m_per_part_gradient_sizer)
        m_per_part_gradient_sizer->ShowItems(m_result.gradient_enabled);
    if (m_curve_sizer)
        m_curve_sizer->ShowItems(m_result.gradient_enabled && num_components() == 2);
    if (!m_result.gradient_enabled) {
        m_result.per_part_gradient = false;
        if (m_chk_per_part_gradient) m_chk_per_part_gradient->SetValue(false);
    }

    // Toggling the curve editor changes the right column height (and width when
    // turning gradient on), so the dialog must follow or the recommendation list
    // gets squeezed off-screen. Same trick as 2-color -> 3-color switching.
    apply_dialog_size();

    Layout();
    Refresh();
}

void MixedFilamentDialog::on_gradient_direction_changed()
{
    if (!m_combo_gradient_dir) return;
    m_result.gradient_direction = m_combo_gradient_dir->GetSelection();

    // Mirror the user's custom curve around y=0.5 instead of resetting it, so
    // shape work (added anchors, bent segments) survives a direction toggle.
    // reverse() flips y and tangent signs consistently; default two-point
    // linear curves end up matching the new direction exactly (0.9->0.1 <-> 0.1->0.9).
    if (m_curve_editor) {
        m_curve_editor->reverse();
        m_result.gradient_curve = m_curve_editor->get_points();
    }
    update_preview();
}

void MixedFilamentDialog::on_gradient_curve_changed()
{
    if (m_curve_editor)
        m_result.gradient_curve = m_curve_editor->get_points();
    update_preview();
}

void MixedFilamentDialog::on_per_part_gradient_toggled()
{
    if (m_chk_per_part_gradient)
        m_result.per_part_gradient = m_chk_per_part_gradient->GetValue();
}

void MixedFilamentDialog::on_add_material()
{
    size_t n = num_components();
    if (n >= (size_t)MAX_COMPONENTS) return;

    std::set<std::string> selected_types;
    std::set<std::string> selected_diameters;
    for (unsigned int phys : m_result.components) {
        if (phys >= 1 && phys <= m_physical_types.size())
            selected_types.insert(m_physical_types[phys - 1]);
        if (m_enforce_nozzle_diameter && phys >= 1 && phys <= m_physical_diameters.size())
            selected_diameters.insert(m_physical_diameters[phys - 1]);
    }
    const bool has_match_target = m_match_mode && m_match_target >= 0
                                  && size_t(m_match_target) < m_physical_colors.size();
    const std::string target_hex = has_match_target
        ? decompose_normalize_color_hex(m_physical_colors[m_match_target]) : std::string();

    // Prefer a compatible item; otherwise add the first combo item and let the existing warning explain it.
    unsigned int new_comp = 0;
    unsigned int fallback_comp = 0;
    for (size_t j = 0; j < m_physical_names.size(); ++j) {
        unsigned int candidate = (unsigned int)(j + 1);
        if (std::find(m_result.components.begin(), m_result.components.end(), candidate) != m_result.components.end())
            continue;
        if (has_match_target && (int(j) == m_match_target
                                 || (j < m_physical_colors.size()
                                     && decompose_normalize_color_hex(m_physical_colors[j]) == target_hex)))
            continue;
        if (fallback_comp == 0)
            fallback_comp = candidate;
        if (m_match_mode && (j >= m_match_target_allowed.size() || !m_match_target_allowed[j]))
            continue;
        if (!selected_types.empty()) {
            const std::string type = j < m_physical_types.size() ? m_physical_types[j] : std::string("PLA");
            if (selected_types.count(type) == 0)
                continue;
        }
        if (m_enforce_nozzle_diameter && !selected_diameters.empty()) {
            const std::string diameter = j < m_physical_diameters.size() ? m_physical_diameters[j] : std::string();
            if (selected_diameters.count(diameter) == 0)
                continue;
        }
        new_comp = candidate;
        break;
    }
    if (new_comp == 0)
        new_comp = fallback_comp;
    if (new_comp == 0) return;
    m_result.components.push_back(new_comp);

    int each = 100 / (int)(n + 1);
    m_result.ratios.clear();
    int assigned = 0;
    for (size_t i = 0; i < n; ++i) {
        m_result.ratios.push_back(each);
        assigned += each;
    }
    m_result.ratios.push_back(100 - assigned);

    if (m_result.ratios.size() >= 3) {
        int sum = 0;
        for (int r : m_result.ratios) sum += r;
        if (sum > 0) {
            m_tri_wx = (double)m_result.ratios[0] / sum;
            m_tri_wy = (double)m_result.ratios[1] / sum;
            m_tri_wz = (double)m_result.ratios[2] / sum;
        }
    }
    reset_manual_ratio_state();

    append_material_row();

    rebuild_all_combos();
    refresh_curve_editor_colors();
    update_component_count_ui();
    update_preview();
    update_ok_button_state();
    rebuild_recommendation_items();

    Layout();
    Refresh();
}

void MixedFilamentDialog::on_remove_material()
{
    if (num_components() <= 2)
        return;

    m_result.components.resize(2);
    m_result.ratios = {50, 50};
    m_tri_wx = 0.5;
    m_tri_wy = 0.5;
    m_tri_wz = 0.0;

    reset_manual_ratio_state();
    refresh_ratio_labels();

    if (m_material_rows_sizer && m_material_rows_sizer->GetItemCount() > 2) {
        auto* sizer_item = m_material_rows_sizer->GetItem(m_material_rows_sizer->GetItemCount() - 1);
        if (sizer_item && sizer_item->GetSizer())
            sizer_item->GetSizer()->Clear(true);
        m_material_rows_sizer->Remove(m_material_rows_sizer->GetItemCount() - 1);
    }

    if (m_combo_filaments.size() > 2)
        m_combo_filaments.pop_back();
    if (m_combo_to_physical.size() > 2)
        m_combo_to_physical.pop_back();

    rebuild_all_combos();
    refresh_curve_editor_colors();
    update_component_count_ui();
    update_preview();
    update_ok_button_state();
    rebuild_recommendation_items();
    Layout();
    Refresh();
}

void MixedFilamentDialog::on_recommendation_clicked(unsigned int comp_a, unsigned int comp_b)
{
    while (m_material_rows_sizer->GetItemCount() > 2) {
        auto* sizer_item = m_material_rows_sizer->GetItem(m_material_rows_sizer->GetItemCount() - 1);
        if (sizer_item && sizer_item->GetSizer())
            sizer_item->GetSizer()->Clear(true);
        m_material_rows_sizer->Remove(m_material_rows_sizer->GetItemCount() - 1);
    }

    while (m_combo_filaments.size() > 2)
        m_combo_filaments.pop_back();
    while (m_combo_to_physical.size() > 2)
        m_combo_to_physical.pop_back();

    m_result.components = {comp_a, comp_b};
    m_result.ratios = {50, 50};

    reset_manual_ratio_state();
    refresh_ratio_labels();

    rebuild_all_combos();
    refresh_curve_editor_colors();
    update_gradient_direction_items();
    update_component_count_ui();
    update_preview();
    update_ok_button_state();
    Layout();
    Refresh();
}

void MixedFilamentDialog::on_recommendation_clicked_triple(unsigned int a, unsigned int b, unsigned int c)
{
    // Ensure we have exactly 3 combo rows
    if (num_components() < 3) {
        // Need to add a 3rd combo row
        while (m_combo_filaments.size() < 3)
            append_material_row();
    } else if (num_components() > 3) {
        while (m_material_rows_sizer->GetItemCount() > 3) {
            auto* sizer_item = m_material_rows_sizer->GetItem(m_material_rows_sizer->GetItemCount() - 1);
            if (sizer_item && sizer_item->GetSizer())
                sizer_item->GetSizer()->Clear(true);
            m_material_rows_sizer->Remove(m_material_rows_sizer->GetItemCount() - 1);
        }
        while (m_combo_filaments.size() > 3)
            m_combo_filaments.pop_back();
        while (m_combo_to_physical.size() > 3)
            m_combo_to_physical.pop_back();
    }

    m_result.components = {a, b, c};
    m_result.ratios = {25, 25, 50};
    m_tri_wx = 0.25;
    m_tri_wy = 0.25;
    m_tri_wz = 0.50;
    reset_manual_ratio_state();

    rebuild_all_combos();
    refresh_curve_editor_colors();
    update_gradient_direction_items();
    update_component_count_ui();
    update_preview();
    update_ok_button_state();
    Layout();
    Refresh();
}

void MixedFilamentDialog::update_preview()
{
    if (m_preview_canvas)  m_preview_canvas->Refresh();
    if (m_summary_panel)   m_summary_panel->Refresh();
    if (m_ratio_bar)       m_ratio_bar->Refresh();
    if (m_triangle_panel)  m_triangle_panel->Refresh();
    update_rematch_button();
}

void MixedFilamentDialog::update_rematch_button()
{
    if (!m_btn_rematch)
        return;
    // Only offer "match again" once the user has edited the recommended recipe.
    const bool modified = m_match_mode && m_match_valid
        && (m_result.components != m_match_recommended.components || m_result.ratios != m_match_recommended.ratios);
    if (m_btn_rematch->IsShown() != modified) {
        m_btn_rematch->Show(modified);
        Layout();
    }
}

// Wraps text to max_width. Breaks at spaces when possible, otherwise between characters so
// text without spaces (Chinese, Japanese) still fits.
static wxArrayString wrap_warning_text(wxDC& dc, const wxString& text, int max_width)
{
    wxArrayString lines;
    wxString      line;
    int           last_space = -1;
    for (wxString::const_iterator it = text.begin(); it != text.end(); ++it) {
        const wxUniChar ch = *it;
        if (ch == '\n') {
            lines.Add(line);
            line.clear();
            last_space = -1;
            continue;
        }
        line += ch;
        if (ch == ' ')
            last_space = int(line.length()) - 1;
        if (line.length() > 1 && dc.GetTextExtent(line).GetWidth() > max_width) {
            if (last_space > 0) {
                lines.Add(line.Left(last_space));
                line = line.Mid(last_space + 1);
            } else {
                lines.Add(line.Left(line.length() - 1));
                line = line.Right(1);
            }
            last_space = line.Find(' ', true);
        }
    }
    if (!line.empty() || lines.empty())
        lines.Add(line);
    return lines;
}

wxString MixedFilamentDialog::current_warning_message() const
{
    return (m_match_mode && !m_match_valid && !m_match_error_msg.empty()) ? m_match_error_msg : m_type_mismatch_msg;
}

void MixedFilamentDialog::update_warning_panel_height()
{
    if (!m_warning_panel)
        return;
    int height = FromDIP(48);
    const wxString msg = current_warning_message();
    const int text_w = m_warning_panel->GetClientSize().GetWidth() - FromDIP(10 + 14 + 6 + 10);
    if (!msg.empty() && text_w > 0) {
        wxClientDC dc(m_warning_panel);
        dc.SetFont(::Label::Body_12);
        const int line_h = dc.GetTextExtent(wxT("Mg")).GetHeight();
        const int lines  = int(wrap_warning_text(dc, msg, text_w).size());
        height = std::max(height, lines * line_h + FromDIP(16));
    }
    if (m_warning_panel->GetMinSize().GetHeight() != height) {
        m_warning_panel->SetMinSize(wxSize(-1, height));
        // Color matching follows the content height, so resize the dialog
        // instead of pushing the bottom controls out of view.
        if (m_match_mode && m_warning_panel->IsShown())
            apply_dialog_size(true);
        else
            Layout();
    }
}

void MixedFilamentDialog::paint_warning_panel(wxPaintEvent&)
{
    wxBufferedPaintDC dc(m_warning_panel);
    wxSize sz = m_warning_panel->GetClientSize();

    dc.SetBrush(wxBrush(StateColor::darkModeColorFor(*wxWHITE)));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(0, 0, sz.GetWidth(), sz.GetHeight());

    // Dark mode uses a dark red panel with a red outlined icon instead of the light pink panel.
    const bool     is_dark       = wxGetApp().dark_mode();
    const wxColour warning_bg    = is_dark ? wxColour("#45272B") : wxColour(255, 245, 245);
    const wxColour warning_color = is_dark ? wxColour("#E85A5A") : wxColour("#E84C4C");
    dc.SetBrush(wxBrush(warning_bg));
    dc.SetPen(wxPen(is_dark ? wxColour("#D84A4A") : wxColour("#E84C4C"), 1));
    dc.DrawRoundedRectangle(0, 0, sz.GetWidth(), sz.GetHeight(), FromDIP(4));

    const wxString msg = current_warning_message();
    dc.SetFont(::Label::Body_12);
    const int           text_x  = FromDIP(10 + 14 + 6);
    const int           avail_w = sz.GetWidth() - text_x - FromDIP(10);
    const wxArrayString lines   = msg.empty() ? wxArrayString() : wrap_warning_text(dc, msg, std::max(1, avail_w));
    const int           line_h  = dc.GetTextExtent(wxT("Mg")).GetHeight();
    const int           y0      = (sz.GetHeight() - (int)lines.size() * line_h) / 2;

    int x = FromDIP(10);
    // Center the icon on the first text line so it stays aligned when the message wraps.
    int cy = lines.empty() ? sz.GetHeight() / 2 : y0 + line_h / 2;

    int icon_r = FromDIP(7);
    dc.SetBrush(is_dark ? *wxTRANSPARENT_BRUSH : wxBrush(warning_color));
    dc.SetPen(is_dark ? wxPen(warning_color, 1) : *wxTRANSPARENT_PEN);
    dc.DrawCircle(x + icon_r, cy, icon_r);
    dc.SetFont(::Label::Body_10);
    dc.SetTextForeground(is_dark ? warning_color : *wxWHITE);
    wxSize ex = dc.GetTextExtent(wxT("!"));
    dc.DrawText(wxT("!"), x + icon_r - ex.GetWidth() / 2, cy - ex.GetHeight() / 2);
    if (lines.empty()) return;

    dc.SetFont(::Label::Body_12);
    dc.SetTextForeground(warning_color);
    for (size_t l = 0; l < lines.size(); ++l)
        dc.DrawText(lines[l], text_x, y0 + (int)l * line_h);
}

void MixedFilamentDialog::update_ok_button_state()
{
    if (!m_btn_ok) return;

    bool has_type_mismatch = false;
    bool has_diameter_mismatch = false;
    if (!m_physical_types.empty() && m_result.components.size() >= 2) {
        std::map<std::string, std::vector<unsigned int>> type_groups;
        for (size_t i = 0; i < m_result.components.size(); ++i) {
            unsigned int phys = m_result.components[i];
            if (phys < 1 || phys > m_physical_types.size()) continue;
            type_groups[m_physical_types[phys - 1]].push_back(phys);
        }
        has_type_mismatch = type_groups.size() > 1;
        if (has_type_mismatch) {
            wxString parts;
            for (auto it = type_groups.begin(); it != type_groups.end(); ++it) {
                if (!parts.empty())
                    parts += _L(" and ");
                wxString slots;
                for (size_t j = 0; j < it->second.size(); ++j) {
                    if (!slots.empty()) slots += ", ";
                    slots += std::to_string(it->second[j]);
                }
                parts += wxString::Format(_L("Slot %s (%s)"), slots, wxString::FromUTF8(it->first));
            }
            m_type_mismatch_msg = parts + " " + _L("cannot be mixed. Please select the same filament type.");
        } else {
            m_type_mismatch_msg.clear();
        }
    } else {
        m_type_mismatch_msg.clear();
    }

    if (m_enforce_nozzle_diameter && !m_physical_diameters.empty() && m_result.components.size() >= 2) {
        std::map<std::string, std::vector<unsigned int>> diameter_groups;
        for (unsigned int phys : m_result.components) {
            if (phys < 1 || phys > m_physical_diameters.size())
                continue;
            const std::string &diameter = m_physical_diameters[phys - 1];
            // Empty map entries are not a known conflict; skip until a real
            // diameter is available. Do not label them as "unknown nozzle".
            if (diameter.empty())
                continue;
            diameter_groups[diameter].push_back(phys);
        }
        has_diameter_mismatch = diameter_groups.size() > 1;
        if (has_diameter_mismatch) {
            wxString parts;
            for (const auto &group : diameter_groups) {
                if (!parts.empty())
                    parts += _L(" and ");
                wxString slots;
                for (unsigned int slot : group.second) {
                    if (!slots.empty())
                        slots += ", ";
                    slots += std::to_string(slot);
                }
                const wxString diameter = wxString::FromUTF8(group.first) + " mm";
                parts += wxString::Format(_L("Slot %s (%s)"), slots, diameter);
            }
            m_type_mismatch_msg = parts + " " + _L("cannot be mixed. Please select filaments using the same nozzle diameter.");
        }
    }

    bool has_unselected = false;
    for (unsigned int c : m_result.components) {
        if (c == 0) { has_unselected = true; break; }
    }

    const bool no_match = m_match_mode && !m_match_valid;

    bool can_confirm = !has_type_mismatch && !has_diameter_mismatch && !has_unselected && !no_match;
    m_can_confirm = can_confirm;
    m_btn_ok->Enable(can_confirm);
    if (no_match) {
        m_btn_ok->SetBackgroundColor(wxColour("#CECECE"));
        m_btn_ok->SetBorderColor(wxColour("#CECECE"));
        m_btn_ok->SetToolTip(_L("No mix is available for the target color"));
    } else if (has_unselected) {
        m_btn_ok->SetBackgroundColor(wxColour("#CECECE"));
        m_btn_ok->SetBorderColor(wxColour("#CECECE"));
        m_btn_ok->SetToolTip(_L("Please select a filament for all components"));
    } else if (has_type_mismatch) {
        m_btn_ok->SetBackgroundColor(wxColour("#CECECE"));
        m_btn_ok->SetBorderColor(wxColour("#CECECE"));
        m_btn_ok->SetToolTip(_L("Cannot mix different filament types"));
    } else if (has_diameter_mismatch) {
        m_btn_ok->SetBackgroundColor(wxColour("#CECECE"));
        m_btn_ok->SetBorderColor(wxColour("#CECECE"));
        m_btn_ok->SetToolTip(_L("Cannot mix filaments using different nozzle diameters"));
    } else {
        m_btn_ok->SetBackgroundColor(wxColour("#00AE42"));
        m_btn_ok->SetBorderColor(wxColour("#00AE42"));
        m_btn_ok->SetToolTip(wxEmptyString);
    }

    if (m_chk_replace_target)
        m_chk_replace_target->Enable(!no_match);
    if (m_lbl_replace_target) {
        // Only gray the text: a disabled wxStaticText is drawn embossed on MSW, which looks doubled.
        m_lbl_replace_target->SetForegroundColour(no_match ? ThemeColor::Grey500
                                                           : StateColor::darkModeColorFor(wxColour("#262E30")));
        m_lbl_replace_target->SetCursor(wxCursor(no_match ? wxCURSOR_ARROW : wxCURSOR_HAND));
        m_lbl_replace_target->Refresh();
    }

    if (m_warning_panel) {
        const bool was_shown = m_warning_panel->IsShown();
        m_warning_panel->Show((no_match && !m_match_error_msg.empty())
                              || (!no_match && (has_type_mismatch || has_diameter_mismatch)));
        // Force a repaint: when the panel is already visible and only the
        // mismatch text changes (e.g. PETG -> ABS), Show()/Layout() do not
        // generate a paint event, so paint_warning_panel keeps the stale text.
        update_warning_panel_height();
        m_warning_panel->Refresh();
        if (m_match_mode && was_shown != m_warning_panel->IsShown())
            apply_dialog_size(true);
        else
            Layout();
    }
}

void MixedFilamentDialog::update_gradient_direction_items()
{
    if (!m_combo_gradient_dir) return;

    int prev_sel = m_combo_gradient_dir->GetSelection();
    m_combo_gradient_dir->Clear();

    if (num_components() < 2) return;

    auto make_direction_bitmap = [this](size_t idx_from, size_t idx_to) -> wxBitmap {
        int swatch_sz = FromDIP(20);
        int arrow_w   = FromDIP(16);
        int gap       = FromDIP(4);
        int bmp_w = swatch_sz + gap + arrow_w + gap + swatch_sz;
        int bmp_h = swatch_sz;

        wxColour dir_text = StateColor::darkModeColorFor(wxColour("#262E30"));

        return make_alpha_bitmap(bmp_w, bmp_h, [&](wxDC& dc) {
            dc.SetFont(::Label::Body_13);

            auto draw_swatch = [&](int x, size_t idx) {
                std::string color_hex = "#D9D9D9";
                if (idx < m_physical_colors.size())
                    color_hex = m_physical_colors[idx];
                std::string label = std::to_string(idx + 1);
                wxBitmap* icon = get_extruder_color_icon(color_hex, label, swatch_sz, swatch_sz);
                if (icon && icon->IsOk())
                    dc.DrawBitmap(*icon, x, 0);
            };

            int x = 0;
            draw_swatch(x, idx_from);
            x += swatch_sz + gap;

            dc.SetTextForeground(dir_text);
            wxString arrow = wxT("\u2192");
            wxSize arrow_sz = dc.GetTextExtent(arrow);
            dc.DrawText(arrow, x + (arrow_w - arrow_sz.GetWidth()) / 2,
                               (bmp_h - arrow_sz.GetHeight()) / 2);
            x += arrow_w + gap;

            draw_swatch(x, idx_to);
        });
    };

    size_t idx_a = (comp(0) >= 1) ? comp(0) - 1 : 0;
    size_t idx_b = (comp(1) >= 1) ? comp(1) - 1 : 1;

    m_combo_gradient_dir->Append(wxT(" "), make_direction_bitmap(idx_a, idx_b));
    m_combo_gradient_dir->Append(wxT(" "), make_direction_bitmap(idx_b, idx_a));

    if (prev_sel >= 0 && prev_sel < (int)m_combo_gradient_dir->GetCount())
        m_combo_gradient_dir->SetSelection(prev_sel);
    else
        m_combo_gradient_dir->SetSelection(0);
}

wxString MixedFilamentDialog::combo_item_name(size_t physical_idx) const
{
    if (physical_idx >= m_physical_names.size())
        return {};
    return wxString::FromUTF8(m_physical_names[physical_idx]);
}

wxString MixedFilamentDialog::combo_item_suffix(size_t physical_idx) const
{
    if (!m_enforce_nozzle_diameter)
        return {};
    if (physical_idx >= m_physical_diameters.size() || m_physical_diameters[physical_idx].empty())
        return {};
    wxString suffix = "| " + wxString::FromUTF8(m_physical_diameters[physical_idx]);
    if (physical_idx < m_physical_volume_shorts.size()
        && !m_physical_volume_shorts[physical_idx].empty())
        suffix += " " + wxString::FromUTF8(m_physical_volume_shorts[physical_idx]);
    return suffix;
}

int MixedFilamentDialog::measure_filament_label_width() const
{
    wxClientDC dc(const_cast<MixedFilamentDialog*>(this));
    dc.SetFont(Label::Body_12);
    int w = 0;
    for (int i = 1; i <= MAX_COMPONENTS; ++i)
        w = std::max(w, dc.GetTextExtent(wxString::Format(_L("Filament %d"), i)).GetWidth());
    // Native static text is wider than a plain DC extent on some platforms.
    return w + FromDIP(12);
}

int MixedFilamentDialog::measure_suffix_column_width() const
{
    if (!m_enforce_nozzle_diameter)
        return 0;
    wxClientDC dc(const_cast<MixedFilamentDialog*>(this));
    dc.SetFont(::Label::Body_13);
    int w = 0;
    for (size_t j = 0; j < m_physical_names.size(); ++j) {
        const wxString suffix = combo_item_suffix(j);
        if (!suffix.IsEmpty())
            w = std::max(w, dc.GetTextExtent(suffix).GetWidth());
    }
    return w;
}

void MixedFilamentDialog::refresh_combo_suffixes()
{
    for (size_t i = 0; i < m_combo_filaments.size(); ++i) {
        ComboBox* combo = m_combo_filaments[i];
        if (!combo)
            continue;

        wxString name = _L("-- Select --");
        wxString suffix;
        const int sel = combo->GetSelection();
        unsigned int phys = 0;
        if (sel >= 0 && i < m_combo_to_physical.size() && sel < (int)m_combo_to_physical[i].size())
            phys = m_combo_to_physical[i][sel];
        if (phys >= 1 && phys <= m_physical_names.size()) {
            name   = combo_item_name(phys - 1);
            suffix = combo_item_suffix(phys - 1);
        }

        combo->SetTextLabel(name);
        combo->SetRightSuffix(suffix);
    }
}

int MixedFilamentDialog::measure_needed_combo_width() const
{
    wxClientDC dc(const_cast<MixedFilamentDialog*>(this));
    dc.SetFont(Label::Body_14);
    int name_w = dc.GetTextExtent(_L("-- Select --")).GetWidth();
    for (size_t j = 0; j < m_physical_names.size(); ++j)
        name_w = std::max(name_w, dc.GetTextExtent(combo_item_name(j)).GetWidth());
    // Long preset names are ellipsized in the closed combo; keep the dialog close to a common official name width.
    name_w = std::min(name_w, dc.GetTextExtent(wxString::FromUTF8("Bambu PLA Palette")).GetWidth() + FromDIP(24));

    const int suffix_w = measure_suffix_column_width();
    const int suffix_gap = suffix_w > 0 ? FromDIP(8) : 0;
    // Closed ComboBox chrome: left pad, drop icon, swatch (icon_1), ellipsize pad.
    const int chrome = FromDIP(5) + FromDIP(20) + FromDIP(4)
                     + FromDIP(28) + FromDIP(7) + FromDIP(8);
    return std::max(FromDIP(166), name_w + suffix_w + suffix_gap + chrome);
}

wxSize MixedFilamentDialog::compute_dialog_size() const
{
    const bool is_three      = (num_components() >= 3);
    const bool curve_visible = !is_three && m_result.gradient_enabled;

    int w = FromDIP(439);
    int h = FromDIP(580);
    if (is_three) {
        h = FromDIP(680);
    } else if (curve_visible) {
        // Wider so the gradient editor can show "Material Ratio" intact;
        // +40 over the 3-color height to fit the curve editor while keeping the
        // recommendation list visible (it can still scroll if needed).
        w = FromDIP(470);
        h = FromDIP(720);
    }
    if (m_mode_tabs_sizer)
        h += FromDIP(52);
    if (m_match_mode) {
        // No stretchable recommendation list here, so follow the content height.
        // Width stays the same as manual mixing so switching tabs does not resize horizontally.
        if (wxSizer* sizer = GetSizer()) {
            const wxSize content = ClientToWindowSize(sizer->CalcMin());
            // A failed match only shows the empty state, so do not pad it with blank space.
            h = m_match_valid ? std::max(FromDIP(520), content.GetHeight()) : content.GetHeight();
        }
    }

    const int combo_needed = measure_needed_combo_width();
    const int label_w      = measure_filament_label_width();
    const int layout_w     = FromDIP(20) + FromDIP(129) + FromDIP(20)
                           + label_w + FromDIP(8) + combo_needed
                           + FromDIP(20);
    w = std::max(w, layout_w);

    int cap = FromDIP(720);
    const int display_idx = wxDisplay::GetFromWindow(const_cast<MixedFilamentDialog*>(this));
    if (display_idx != wxNOT_FOUND) {
        const int work_w = wxDisplay(display_idx).GetClientArea().GetWidth();
        cap = std::min(cap, work_w * 4 / 5);
    }
    if (w > cap)
        w = cap;

    return wxSize(w, h);
}

void MixedFilamentDialog::apply_dialog_size(bool keep_top)
{
    if (m_defer_dialog_resize) {
        m_dialog_resize_pending = true;
        return;
    }

    const wxSize new_size      = compute_dialog_size();
    const int    combo_needed  = measure_needed_combo_width();
    const int    label_w       = measure_filament_label_width();
    const int    combo_fit     = new_size.GetWidth()
                               - FromDIP(20) - FromDIP(129) - FromDIP(20)
                               - label_w - FromDIP(8) - FromDIP(20);
    const int    combo_min     = std::max(FromDIP(166),
                                          std::min(combo_needed, std::max(combo_fit, FromDIP(166))));
    for (auto* combo : m_combo_filaments) {
        if (!combo) continue;
        combo->SetMinSize(wxSize(combo_min, FromDIP(24)));
    }

    const wxRect  old_rect = GetRect();
    const wxPoint center(old_rect.x + old_rect.width / 2,
                         old_rect.y + old_rect.height / 2);
    SetSize(new_size);
    if (old_rect.width > 0 && old_rect.height > 0) {
        wxPoint pos(center.x - new_size.x / 2, keep_top ? old_rect.y : center.y - new_size.y / 2);
        if (keep_top) {
            const int display_idx = wxDisplay::GetFromWindow(this);
            if (display_idx != wxNOT_FOUND) {
                const wxRect work_area = wxDisplay(display_idx).GetClientArea();
                if (pos.y + new_size.y > work_area.GetBottom() + 1)
                    pos.y = std::max(work_area.GetTop(), work_area.GetBottom() + 1 - new_size.y);
            }
        }
        SetPosition(pos);
    }
    Layout();
    refresh_combo_suffixes();
}

void MixedFilamentDialog::update_component_count_ui()
{
    bool is_two   = (num_components() == 2);
    bool is_three = (num_components() >= 3);

    // Toggle ratio slider vs triangle picker
    if (m_ratio_sizer)
        m_ratio_sizer->ShowItems(is_two && !m_result.gradient_enabled);
    if (m_triangle_sizer)
        m_triangle_sizer->ShowItems(is_three);

    // 3-color: hide gradient entirely, force off. Color matching never produces a gradient.
    if (m_gradient_sizer) {
        bool show_gradient = is_two && !m_match_mode;
        m_chk_gradient->Show(show_gradient);
        if (m_label_gradient) m_label_gradient->Show(show_gradient);
        m_combo_gradient_dir->Show(show_gradient && m_result.gradient_enabled);
        if (m_per_part_gradient_sizer)
            m_per_part_gradient_sizer->ShowItems(show_gradient && m_result.gradient_enabled);
        if (m_curve_sizer)
            m_curve_sizer->ShowItems(show_gradient && m_result.gradient_enabled);
    }
    if (is_three || m_match_mode) {
        m_result.gradient_enabled = false;
        if (m_chk_gradient) m_chk_gradient->SetValue(false);
        m_result.per_part_gradient = false;
        if (m_chk_per_part_gradient) m_chk_per_part_gradient->SetValue(false);
    }

    if (m_btn_add_material) {
        bool can_add = (num_components() < (size_t)MAX_COMPONENTS && m_physical_colors.size() > num_components());
        m_btn_add_material->Enable(can_add);
        if (can_add) {
            m_btn_add_material->SetTextColor(wxColour("#262E30"));
            m_btn_add_material->SetBorderColor(wxColour("#EEEEEE"));
            m_btn_add_material->SetToolTip(wxEmptyString);
        } else {
            m_btn_add_material->SetTextColor(wxColour("#CECECE"));
            m_btn_add_material->SetBorderColor(wxColour("#EEEEEE"));
            m_btn_add_material->SetToolTip(is_three ? _L("Maximum 3 materials for mixing") : _L("Maximum number of components reached"));
        }
    }

    if (m_btn_remove_material) {
        m_btn_remove_material->Show(is_three);
        m_btn_remove_material->Enable(is_three);
        m_btn_remove_material->SetToolTip(is_three ? _L("Remove the third material") : wxString());
    }

    apply_dialog_size();
}

// ---- Color matching tab ----

int MixedFilamentDialog::match_target_config_index() const
{
    if (!m_match_mode || m_match_target < 0 || (size_t)m_match_target >= m_physical_config_indices.size())
        return -1;
    return (int)m_physical_config_indices[m_match_target];
}

bool MixedFilamentDialog::replace_target_on_model() const
{
    return m_match_mode && m_match_valid && m_chk_replace_target && m_chk_replace_target->GetValue()
        && match_target_config_index() >= 0;
}

void MixedFilamentDialog::set_physical_info(const MixedFilamentPhysicalInfo& info)
{
    m_physical_colors         = info.colors;
    m_physical_names          = info.names;
    m_physical_types          = info.types;
    m_physical_diameters      = info.diameters;
    m_physical_volume_shorts  = info.volume_shorts;
    m_physical_config_indices = info.config_indices;
    if (m_physical_config_indices.size() != m_physical_colors.size()) {
        m_physical_config_indices.resize(m_physical_colors.size());
        for (size_t i = 0; i < m_physical_config_indices.size(); ++i)
            m_physical_config_indices[i] = i;
    }

    m_match_types.clear();
    m_match_target_allowed.clear();
    m_match_target_hidden.clear();
    PresetBundle* bundle = wxGetApp().preset_bundle;
    for (size_t i = 0; i < m_physical_colors.size(); ++i) {
        const size_t cfg_idx = m_physical_config_indices[i];
        Preset* preset = (bundle && cfg_idx < bundle->filament_presets.size())
            ? bundle->filaments.find_preset(bundle->filament_presets[cfg_idx]) : nullptr;
        m_match_types.push_back(filament_type_for_color_decompose(preset));

        const std::string type = i < m_physical_types.size() ? m_physical_types[i] : std::string();
        const bool is_support = type.size() >= 2 && type.compare(type.size() - 2, 2, "-S") == 0;
        m_match_target_hidden.push_back(is_support);
        m_match_target_allowed.push_back(!is_support &&
            decompose_color_block_reason((int)cfg_idx) == DecomposeColorBlockReason::None);
    }
}

wxBoxSizer* MixedFilamentDialog::create_mode_tabs()
{
    m_mode_tabs_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto make_tab = [this](const wxString& label, bool match_mode) {
        auto* btn = new Button(this, label);
        btn->SetFont(::Label::Body_14);
        btn->SetMinSize(wxSize(FromDIP(130), FromDIP(36)));
        btn->SetCornerRadius(FromDIP(6));
        btn->SetCursor(wxCursor(wxCURSOR_HAND));
        btn->Bind(wxEVT_BUTTON, [this, match_mode](wxCommandEvent&) { select_mode(match_mode); });
        return btn;
    };
    m_btn_tab_manual = make_tab(_L("Manual Mixing"), false);
    m_btn_tab_match  = make_tab(_L("Color Matching"), true);

    m_lbl_wiki = new wxStaticText(this, wxID_ANY, _L("Wiki"));
    m_lbl_wiki->SetForegroundColour(wxColour(0, 174, 66));
    m_lbl_wiki->SetFont(::Label::Body_13.Underlined());
    m_lbl_wiki->SetCursor(wxCursor(wxCURSOR_HAND));
    m_lbl_wiki->Bind(wxEVT_LEFT_DOWN, [](wxMouseEvent&) {
        const std::string language = wxGetApp().app_config ? wxGetApp().app_config->get("language") : std::string();
        const wxString    region   = language.find("zh") == 0 ? "zh" : "en";
        wxGetApp().open_browser_with_warning_dialog(
            wxString::Format("https://wiki.bambulab.com/%s/bambu-studio/color-mixing", region));
    });

    // Reserve the link width on both sides so the mode tabs stay centered.
    const int wiki_w = m_lbl_wiki->GetBestSize().GetWidth();
    m_mode_tabs_sizer->Add(wiki_w, 0, 0);
    m_mode_tabs_sizer->AddStretchSpacer(1);
    m_mode_tabs_sizer->Add(m_btn_tab_manual, 0, wxRIGHT, FromDIP(20));
    m_mode_tabs_sizer->Add(m_btn_tab_match, 0);
    m_mode_tabs_sizer->AddStretchSpacer(1);
    m_mode_tabs_sizer->Add(m_lbl_wiki, 0, wxALIGN_CENTER_VERTICAL);
    update_tab_styles();
    return m_mode_tabs_sizer;
}

wxBoxSizer* MixedFilamentDialog::create_match_header()
{
    m_match_header_sizer = new wxBoxSizer(wxVERTICAL);

    auto make_section_title = [this](wxBoxSizer* row, const wxString& title) {
        auto* marker = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(3), FromDIP(16)));
        marker->SetMinSize(wxSize(FromDIP(3), FromDIP(16)));
        marker->SetBackgroundColour(wxColour("#00AE42"));
        row->Add(marker, 0, wxALIGN_CENTER_VERTICAL);

        auto* label = new wxStaticText(this, wxID_ANY, title);
        label->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#262E30")));
        label->SetFont(::Label::Head_14);
        row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    };

    auto* target_title_row = new wxBoxSizer(wxHORIZONTAL);
    make_section_title(target_title_row, _L("Target Filament"));
    const wxString target_hint_text = _L("(The system will mix 2-3 filaments to approximate this color)");
    auto* target_hint = new wxStaticText(this, wxID_ANY, target_hint_text, wxDefaultPosition, wxDefaultSize,
                                         wxST_ELLIPSIZE_END);
    // Long translations are ellipsized instead of widening the dialog.
    target_hint->SetMinSize(wxSize(FromDIP(1), -1));
    target_hint->SetToolTip(target_hint_text);
    target_hint->SetForegroundColour(wxColour("#909090"));
    target_hint->SetFont(::Label::Body_12);
    target_title_row->Add(target_hint, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    m_match_header_sizer->Add(target_title_row, 0, wxEXPAND);

    auto* target_row = new wxBoxSizer(wxHORIZONTAL);
    m_target_preview_canvas = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(129), FromDIP(129)));
    m_target_preview_canvas->SetMinSize(wxSize(FromDIP(129), FromDIP(129)));
    m_target_preview_canvas->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_target_preview_canvas->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxBufferedPaintDC dc(m_target_preview_canvas);
        draw_match_preview_card(dc, m_target_preview_canvas, match_target_colour(), _L("Target color filament"));
    });
    target_row->Add(m_target_preview_canvas, 0);

    auto* target_select_sizer = new wxBoxSizer(wxVERTICAL);
    auto* target_label = new wxStaticText(this, wxID_ANY, _L("Select Target Color"));
    target_label->SetForegroundColour(wxColour("#909090"));
    target_label->SetFont(::Label::Body_13);
    target_select_sizer->Add(target_label, 0);

    m_combo_target = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                  wxSize(-1, FromDIP(32)), 0, nullptr, wxCB_READONLY);
    m_combo_target->SetKeepDropArrow(true);
    m_combo_target->SetBackgroundColor(StateColor(
        std::make_pair(0xF0F0F1, (int) StateColor::Disabled),
        std::make_pair(0xEDFAF2, (int) StateColor::Focused),
        std::make_pair(StateColor::darkModeColorFor(wxColour("#F8F8F8")), (int) StateColor::Normal)));
    m_combo_target->GetDropDown().SetUseContentWidth(true);
    m_combo_target->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { on_target_changed(); });
    target_select_sizer->Add(m_combo_target, 0, wxEXPAND | wxTOP, FromDIP(6));
    target_row->Add(target_select_sizer, 1, wxLEFT, FromDIP(20));
    m_match_header_sizer->Add(target_row, 0, wxEXPAND | wxTOP, FromDIP(12));

    auto* divider = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    divider->SetMinSize(wxSize(-1, 1));
    divider->SetBackgroundColour(StateColor::darkModeColorFor(wxColour("#EEEEEE")));
    m_match_header_sizer->Add(divider, 0, wxEXPAND | wxTOP, FromDIP(14));

    auto* result_row = new wxBoxSizer(wxHORIZONTAL);
    make_section_title(result_row, _L("Mixed Result"));

    m_btn_rematch = new ScalableButton(this, wxID_ANY, "tree_reset", wxEmptyString, wxDefaultSize,
                                       wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, false, 16);
    m_btn_rematch->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    m_btn_rematch->SetToolTip(_L("Discard changes and match again"));
    m_btn_rematch->SetCursor(wxCursor(wxCURSOR_HAND));
    m_btn_rematch->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { run_color_match(); });
    result_row->Add(m_btn_rematch, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
    // Reserve the button's height so showing / hiding it does not shift the rows below.
    result_row->SetMinSize(wxSize(-1, m_btn_rematch->GetBestSize().GetHeight()));
    result_row->AddStretchSpacer(1);

    m_lbl_official_kit = new wxStaticText(this, wxID_ANY, _L("Use official mixing kits →"));
    m_lbl_official_kit->SetForegroundColour(wxColour(0, 174, 66));
    m_lbl_official_kit->SetFont(::Label::Body_13);
    m_lbl_official_kit->SetCursor(wxCursor(wxCURSOR_HAND));
    m_lbl_official_kit->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        CallAfter([this]() { on_use_official_kit(); });
    });
    result_row->Add(m_lbl_official_kit, 0, wxALIGN_CENTER_VERTICAL);
    m_match_header_sizer->Add(result_row, 0, wxEXPAND | wxTOP, FromDIP(14));

    // Empty state shown instead of the recipe when no mix can be matched.
    m_match_empty_sizer = new wxBoxSizer(wxVERTICAL);
    // preset_empty(_dark) provides the illustration for both themes.
    auto* empty_bmp = new wxStaticBitmap(this, wxID_ANY,
        create_scaled_bitmap(wxGetApp().dark_mode() ? "preset_empty_dark" : "preset_empty", this, 90));
    m_match_empty_sizer->Add(empty_bmp, 0, wxALIGN_CENTER_HORIZONTAL);
    m_match_status = new wxStaticText(this, wxID_ANY, _L("No results"), wxDefaultPosition, wxDefaultSize,
                                      wxALIGN_CENTER_HORIZONTAL);
    m_match_status->SetForegroundColour(wxColour("#909090"));
    m_match_status->SetFont(::Label::Body_13);
    m_match_empty_sizer->Add(m_match_status, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(8));
    m_match_header_sizer->Add(m_match_empty_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(30));

    rebuild_target_combo();
    m_match_header_sizer->ShowItems(false);
    return m_match_header_sizer;
}

wxBoxSizer* MixedFilamentDialog::create_replace_target_option()
{
    m_replace_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_chk_replace_target = new ::CheckBox(this);
    m_chk_replace_target->SetValue(false);
    m_replace_sizer->Add(m_chk_replace_target, 0, wxALIGN_CENTER_VERTICAL);

    auto* label = new wxStaticText(this, wxID_ANY, _L("Replace the target color on models with the mixed result"));
    m_lbl_replace_target = label;
    label->SetFont(::Label::Body_13);
    // Wrap within the base dialog width so this option never widens the dialog.
    label->Wrap(FromDIP(439 - 20 * 2 - 24));
    label->SetCursor(wxCursor(wxCURSOR_HAND));
    label->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        if (!m_chk_replace_target->IsEnabled())
            return;
        m_chk_replace_target->SetValue(!m_chk_replace_target->GetValue());
    });
    m_replace_sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    m_replace_sizer->ShowItems(false);
    return m_replace_sizer;
}

void MixedFilamentDialog::update_tab_styles()
{
    auto style = [](Button* btn, bool selected) {
        if (!btn)
            return;
        const wxColour selected_bg = wxGetApp().dark_mode() ? StateColor::darkModeColorFor(*wxWHITE)
                                                            : wxColour("#E5F7EC");
        btn->SetBackgroundColor(selected ? selected_bg : wxColour("#FFFFFF"));
        btn->SetBorderColor(selected ? wxColour("#00AE42") : wxColour("#CECECE"));
        btn->SetTextColor(selected ? wxColour("#00AE42") : wxColour("#262E30"));
        btn->Refresh();
    };
    style(m_btn_tab_manual, !m_match_mode);
    style(m_btn_tab_match, m_match_mode);
}

void MixedFilamentDialog::update_mode_ui()
{
    update_tab_styles();
    const bool match_failed = m_match_mode && !m_match_valid;
    if (m_match_header_sizer)
        m_match_header_sizer->ShowItems(m_match_mode);
    if (m_match_empty_sizer)
        m_match_empty_sizer->ShowItems(match_failed);
    // ShowItems(true) also reveals children that are normally hidden (ratio slider vs.
    // triangle, gradient rows); the caller restores them via update_component_count_ui().
    if (m_content_sizer)
        m_content_sizer->ShowItems(!match_failed);
    if (m_recommendation_sizer)
        m_recommendation_sizer->ShowItems(!m_match_mode);
    if (m_sel_label)
        m_sel_label->Show(!m_match_mode);
    if (m_replace_sizer)
        m_replace_sizer->ShowItems(m_match_mode);
    update_rematch_button();
    if (m_preview_canvas)
        m_preview_canvas->Refresh();
    if (m_target_preview_canvas)
        m_target_preview_canvas->Refresh();
}

void MixedFilamentDialog::select_mode(bool match_mode)
{
    if (m_edit_mode || match_mode == m_match_mode)
        return;

    // Freeze until the target page is fully laid out, and resize only once at the end:
    // resizing a frozen top-level window still exposes its stale bitmap.
#ifdef __WINDOWS__
    // wx never freezes a top-level window itself on MSW, only its children shown at that time,
    // so the dialog background and newly created / shown controls would still paint mid switch.
    ::SendMessage((HWND)GetHWND(), WM_SETREDRAW, FALSE, 0);
#endif //__WINDOWS__
    Freeze();
    m_defer_dialog_resize   = true;
    m_dialog_resize_pending = false;
    commit_ratio_editor(true);
    if (match_mode) {
        m_manual_result = m_result;
        m_match_mode    = true;
        if (m_has_match_result && m_match_valid) {
            update_mode_ui();
            apply_result(m_match_result);
        } else {
            run_color_match();
        }
    } else {
        if (m_match_valid)
            m_match_result = m_result;
        m_has_match_result = m_match_valid;
        m_match_mode       = false;
        update_mode_ui();
        apply_result(m_manual_result);
    }

    m_defer_dialog_resize = false;
    if (m_dialog_resize_pending)
        apply_dialog_size(true);
    Thaw();
#ifdef __WINDOWS__
    ::SendMessage((HWND)GetHWND(), WM_SETREDRAW, TRUE, 0);
    ::RedrawWindow((HWND)GetHWND(), nullptr, nullptr,
                   RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
#else
    Update();
#endif //__WINDOWS__
}

void MixedFilamentDialog::rebuild_target_combo()
{
    if (!m_combo_target)
        return;

    m_combo_target->Clear();
    m_target_combo_to_physical.clear();
    int sel = -1;
    int first_allowed = -1;
    for (size_t i = 0; i < m_physical_colors.size(); ++i) {
        if (i >= m_match_target_hidden.size() || m_match_target_hidden[i])
            continue;
        const bool allowed = i < m_match_target_allowed.size() && m_match_target_allowed[i];
        const int idx = m_combo_target->Append(combo_item_name(i), make_swatch_bitmap(i), allowed ? 0 : DD_ITEM_STYLE_DISABLED);
        if (!allowed) {
            const size_t cfg_idx = i < m_physical_config_indices.size() ? m_physical_config_indices[i] : i;
            switch (decompose_color_block_reason((int)cfg_idx)) {
            case DecomposeColorBlockReason::Transparent:
                m_combo_target->SetItemTooltip(idx, _L("Transparent filaments are not supported for color matching."));
                break;
            case DecomposeColorBlockReason::Gradient:
                m_combo_target->SetItemTooltip(idx, _L("Gradient filaments are not supported for color matching."));
                break;
            case DecomposeColorBlockReason::MultiColor:
                m_combo_target->SetItemTooltip(idx, _L("Multicolor filaments are not supported for color matching."));
                break;
            default:
                break;
            }
        }
        m_target_combo_to_physical.push_back((int)i);
        if (allowed && first_allowed < 0)
            first_allowed = idx;
        if (allowed && (int)i == m_match_target)
            sel = idx;
    }
    if (sel < 0)
        sel = first_allowed;
    m_match_target = sel >= 0 ? m_target_combo_to_physical[sel] : -1;
    if (sel >= 0)
        m_combo_target->SetSelection(sel);
    if (m_target_preview_canvas)
        m_target_preview_canvas->Refresh();
}

void MixedFilamentDialog::on_target_changed()
{
    const int sel = m_combo_target ? m_combo_target->GetSelection() : -1;
    if (sel < 0 || sel >= (int)m_target_combo_to_physical.size())
        return;
    const int physical = m_target_combo_to_physical[sel];
    if (physical < 0 || physical >= (int)m_match_target_allowed.size() || !m_match_target_allowed[physical]) {
        int restore_sel = -1;
        for (size_t i = 0; i < m_target_combo_to_physical.size(); ++i) {
            if (m_target_combo_to_physical[i] == m_match_target) {
                restore_sel = (int)i;
                break;
            }
        }
        m_combo_target->SetSelection(restore_sel);
        return;
    }
    if (physical == m_match_target)
        return;
    m_match_target = physical;
    run_color_match();
}

void MixedFilamentDialog::on_use_official_kit()
{
    std::set<size_t> protected_indices;
    auto protect_physical = [&](unsigned int physical_1based) {
        if (physical_1based >= 1 && physical_1based <= m_physical_config_indices.size())
            protected_indices.insert(m_physical_config_indices[physical_1based - 1]);
    };
    auto protect_result = [&](const MixedFilamentResult& result) {
        for (unsigned int comp : result.components)
            protect_physical(comp);
    };
    if (m_match_target >= 0)
        protect_physical(unsigned(m_match_target + 1));
    protect_result(m_result);
    protect_result(m_manual_result);
    protect_result(m_match_result);
    protect_result(m_match_recommended);

    // Keep one slot for the mixed filament this dialog creates on OK.
    const OfficialMixingKitAddPlan plan = plan_official_mixing_kit_addition(1, protected_indices);
    MixingKitsHelpDialog dlg(this, plan);
    if (dlg.ShowModal() != wxID_APPLY)
        return;

    const std::vector<size_t> old_config_indices = m_physical_config_indices;
    if (!add_official_mixing_kit_to_project(this, plan, 1))
        return;

    // Deleted filaments shift later physical slots. Remap every stored 1-based component.
    std::set<size_t> deleted(plan.delete_indices.begin(), plan.delete_indices.end());
    std::vector<unsigned int> physical_remap(old_config_indices.size() + 1, 0);
    unsigned int next_physical = 1;
    for (size_t i = 0; i < old_config_indices.size(); ++i) {
        if (deleted.count(old_config_indices[i]) == 0)
            physical_remap[i + 1] = next_physical++;
    }
    auto remap_result = [&](MixedFilamentResult& result) {
        for (unsigned int& comp : result.components)
            comp = comp < physical_remap.size() ? physical_remap[comp] : 0;
    };
    remap_result(m_result);
    remap_result(m_manual_result);
    remap_result(m_match_result);
    remap_result(m_match_recommended);
    if (m_match_target >= 0 && size_t(m_match_target + 1) < physical_remap.size())
        m_match_target = int(physical_remap[m_match_target + 1]) - 1;

    if (m_physical_loader)
        set_physical_info(m_physical_loader());
    rebuild_target_combo();
    run_color_match();
}

void MixedFilamentDialog::set_match_status(const wxString& text)
{
    // The empty state always says "No results"; the specific reason is shown in the warning panel.
    m_match_error_msg = text;
    update_warning_panel_height();
    if (m_warning_panel)
        m_warning_panel->Refresh();
}

void MixedFilamentDialog::run_color_match()
{
    commit_ratio_editor(false);

    MixedFilamentResult result;
    wxString error;

    const int target = m_match_target;
    if (target < 0 || target >= (int)m_physical_colors.size()) {
        error = _L("No filament can be used as the target color. Gradient, multi-color, transparent and support filaments are not supported.");
    } else {
        const wxColour target_col(m_physical_colors[target]);
        Slic3r::ColorDecomposeRgb target_rgb;
        target_rgb.r = target_col.Red();
        target_rgb.g = target_col.Green();
        target_rgb.b = target_col.Blue();

        const std::string target_hex = decompose_normalize_color_hex(m_physical_colors[target]);
        // Filaments with the target color would be returned as a single-filament exact match, so match with the remaining colors.
        auto excluded_from_match = [&](size_t i) {
            if ((int)i == target)
                return true;
            return decompose_normalize_color_hex(m_physical_colors[i]) == target_hex;
        };

        // Use the measured recipe table when a complete, same-diameter official PLA Palette CMYKW kit exists.
        const std::vector<DecomposeOfficialComponent> official_components = pla_palette_cmykw_official_components();
        std::set<size_t> excluded_config_indices;
        for (size_t i = 0; i < m_physical_colors.size() && i < m_physical_config_indices.size(); ++i) {
            if (excluded_from_match(i) || i >= m_match_target_allowed.size() || !m_match_target_allowed[i])
                excluded_config_indices.insert(m_physical_config_indices[i]);
        }
        const std::vector<OfficialMixingKitGroup> kit_groups = collect_official_mixing_kit_groups(excluded_config_indices);
        const OfficialMixingKitGroup* kit_group = find_complete_official_mixing_kit(kit_groups);
        auto physical_of_config = [this](int config_idx) {
            if (config_idx < 0)
                return -1;
            const auto it = std::find(m_physical_config_indices.begin(), m_physical_config_indices.end(), size_t(config_idx));
            return it == m_physical_config_indices.end() ? -1 : int(it - m_physical_config_indices.begin());
        };

        if (kit_group) {
            const Slic3r::ColorDecomposeRecipeResult recipe = Slic3r::lookup_standard_recipe(
                target_rgb, Slic3r::ColorDecomposeRecipeMode::CMYKW, kDecomposePlaPaletteType);
            if (recipe.valid) {
                for (const auto& comp : recipe.components) {
                    const std::string color_hex = decompose_normalize_color_hex(comp.color_hex);
                    const auto component = std::find_if(official_components.begin(), official_components.end(),
                        [&](const DecomposeOfficialComponent& official) { return official.color_hex == color_hex; });
                    if (component == official_components.end()) {
                        result = {};
                        break;
                    }
                    const int phys = physical_of_config(kit_group->config_indices[size_t(component - official_components.begin())]);
                    if (phys < 0) {
                        result = {};
                        break;
                    }
                    result.components.push_back((unsigned int)(phys + 1));
                    result.ratios.push_back(comp.ratio);
                }
            }
            if (result.components.size() < 2 || result.ratios.size() != result.components.size())
                result = {};
        }

        if (result.components.size() < 2) {
            struct MatchGroup {
                std::string type;
                std::string diameter;
                std::vector<Slic3r::ColorDecomposePhysicalFilament> filaments;
            };
            std::vector<MatchGroup> groups;
            for (size_t i = 0; i < m_physical_colors.size(); ++i) {
                if (excluded_from_match(i) || i >= m_match_types.size()
                    || i >= m_match_target_allowed.size() || !m_match_target_allowed[i])
                    continue;
                const std::string diameter = (m_enforce_nozzle_diameter && i < m_physical_diameters.size())
                    ? m_physical_diameters[i] : std::string();
                auto group = std::find_if(groups.begin(), groups.end(), [&](const MatchGroup& g) {
                    return g.diameter == diameter && Slic3r::color_decompose_material_family_matches(g.type, m_match_types[i]);
                });
                if (group == groups.end()) {
                    groups.push_back({m_match_types[i], diameter, {}});
                    group = std::prev(groups.end());
                }
                Slic3r::ColorDecomposePhysicalFilament filament;
                filament.color_hex      = m_physical_colors[i];
                filament.name           = i < m_physical_names.size() ? m_physical_names[i] : std::string();
                filament.type           = m_match_types[i];
                filament.filament_index = (unsigned int)(i + 1);
                group->filaments.push_back(std::move(filament));
            }

            // Mix only filaments from one material family and diameter, then take the closest color.
            Slic3r::ColorDecomposeRecipeResult best;
            float best_distance = 0.f;
            for (const MatchGroup& group : groups) {
                if (group.filaments.size() < 2)
                    continue;
                const Slic3r::ColorDecomposeRecipeResult recipe =
                    Slic3r::recommend_from_physical_filaments(target_rgb, group.filaments, std::string());
                if (!recipe.valid || recipe.components.size() < 2)
                    continue;
                const float distance = calc_color_distance(target_col, wxColour(recipe.matched_color_hex));
                if (!best.valid || distance < best_distance) {
                    best          = recipe;
                    best_distance = distance;
                }
            }
            if (best.valid) {
                for (const auto& comp : best.components) {
                    result.components.push_back(comp.filament_index);
                    result.ratios.push_back(comp.ratio);
                }
            } else {
                error = _L("No available mixing recipe was found.");
            }
        }
    }

    if (error.IsEmpty() && (result.components.size() < 2 || result.ratios.size() != result.components.size()))
        error = _L("No available mixing recipe was found.");

    m_match_valid = error.IsEmpty();
    set_match_status(error);
    update_mode_ui();
    if (!m_match_valid) {
        // Keep the recipe rows hidden: apply_result() would re-show the ratio slider.
        m_has_match_result = false;
        update_ok_button_state();
        apply_dialog_size();
        Refresh();
        return;
    }

    int sum = 0;
    for (int r : result.ratios)
        sum += r;
    if (sum > 0 && sum != 100) {
        int assigned = 0;
        for (size_t i = 0; i + 1 < result.ratios.size(); ++i) {
            result.ratios[i] = (int)std::lround(100.0 * result.ratios[i] / sum);
            assigned += result.ratios[i];
        }
        result.ratios.back() = 100 - assigned;
    }

    m_has_match_result = true;
    apply_result(result);
    m_match_recommended = m_result;
    update_rematch_button();
}

void MixedFilamentDialog::apply_result(const MixedFilamentResult& result)
{
    commit_ratio_editor(false);

    m_result = result;
    if (m_result.components.size() < 2) {
        m_result.components = {1, (m_physical_colors.size() >= 2) ? 2u : 1u};
        m_result.ratios     = {50, 50};
    }
    if (m_result.components.size() > (size_t)MAX_COMPONENTS)
        m_result.components.resize(MAX_COMPONENTS);
    const size_t n = m_result.components.size();
    if (m_result.ratios.size() != n) {
        m_result.ratios.assign(n, 100 / (int)n);
        m_result.ratios[0] += 100 - (100 / (int)n) * (int)n;
    }

    while (m_combo_filaments.size() < n)
        append_material_row();
    while (m_combo_filaments.size() > n)
        remove_last_material_row();

    if (n >= 3) {
        sync_triangle_weights_from_ratios();
    } else {
        m_tri_wx = 0.5;
        m_tri_wy = 0.5;
        m_tri_wz = 0.0;
    }
    reset_manual_ratio_state();
    refresh_ratio_labels();

    if (m_chk_gradient)
        m_chk_gradient->SetValue(m_result.gradient_enabled);
    if (m_chk_per_part_gradient)
        m_chk_per_part_gradient->SetValue(m_result.per_part_gradient);
    if (m_curve_editor) {
        const std::vector<GradientAnchor> curve = m_result.gradient_curve;
        if (!curve.empty())
            m_curve_editor->set_points(curve);
        else
            m_curve_editor->reset_to_linear((m_result.gradient_direction == 0) ? 0.9 : 0.1,
                                            (m_result.gradient_direction == 0) ? 0.1 : 0.9);
        m_result.gradient_curve = curve;
    }

    rebuild_all_combos();
    refresh_curve_editor_colors();
    update_gradient_direction_items();
    if (m_combo_gradient_dir && m_result.gradient_direction < (int)m_combo_gradient_dir->GetCount())
        m_combo_gradient_dir->SetSelection(m_result.gradient_direction);
    update_component_count_ui();
    rebuild_recommendation_items();
    update_preview();
    update_ok_button_state();
    Layout();
    Refresh();
}

wxColour MixedFilamentDialog::match_target_colour() const
{
    if (m_match_target >= 0 && m_match_target < (int)m_physical_colors.size())
        return wxColour(m_physical_colors[m_match_target]);
    return wxColour("#D9D9D9");
}

wxColour MixedFilamentDialog::matched_colour() const
{
    std::vector<std::string> hexes;
    std::vector<int>         ratios;
    std::vector<wxColour>    cols;
    std::vector<double>      weights;
    for (size_t i = 0; i < num_components(); ++i) {
        const wxColour c = comp_colour(i);
        cols.push_back(c);
        weights.push_back(ratio(i) / 100.0);
        hexes.push_back(c.GetAsString(wxC2S_HTML_SYNTAX).ToStdString());
        ratios.push_back(ratio(i));
    }
    // Official kit blends were measured; prefer them over the mixing model.
    const std::string measured = Slic3r::lookup_measured_blend_color(hexes, ratios);
    if (!measured.empty()) {
        const wxColour measured_col(measured);
        if (measured_col.IsOk())
            return measured_col;
    }
    return blend_n_colors(cols, weights);
}

} // namespace GUI
} // namespace Slic3r
