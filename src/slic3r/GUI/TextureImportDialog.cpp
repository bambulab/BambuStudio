#include <GL/glew.h>

#include "TextureImportDialog.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "Widgets/StateColor.hpp"
#include "Widgets/StaticLine.hpp"
#include "libslic3r/Win10ModelRepair.hpp"

#include <wx/button.h>
#include <wx/colour.h>
#include <wx/colordlg.h>
#include <wx/dcclient.h>
#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/evtloop.h>
#include <wx/statline.h>
#include <wx/scrolwin.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>
#include <wx/valtext.h>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <map>
#include <set>

#include <boost/log/trivial.hpp>

static constexpr const char* DEFAULT_VIRTUAL_FILAMENT_NAME = "Bambu PLA Basic";

static bool is_dark() { return Slic3r::GUI::wxGetApp().dark_mode(); }

static wxColour dark_or(const wxColour& light, const wxColour& dark)
{
    return is_dark() ? dark : light;
}

static wxColour texture_import_gray9000()
{
    return wxColour(38, 46, 48);
}

static wxColour texture_import_text_colour()
{
    return StateColor::darkModeColorFor(texture_import_gray9000());
}

static wxColour texture_import_separator_colour()
{
    return StateColor::darkModeColorFor(wxColour("#CECECE"));
}

static wxFont texture_import_section_title_font(wxWindow* win)
{
    wxFont font = win ? win->GetFont() : wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    font.MakeBold();
    return font;
}

static wxSize gl_viewport_size(wxWindow* win, const wxSize& logical_size)
{
    wxSize viewport_size = logical_size;
#ifdef __APPLE__
    const double scale = win ? win->GetContentScaleFactor() : 1.0;
    if (scale > 0.0) {
        viewport_size.x = std::max(1, (int)std::round(viewport_size.x * scale));
        viewport_size.y = std::max(1, (int)std::round(viewport_size.y * scale));
    }
#else
    (void)win;
#endif
    return viewport_size;
}

class ScopedInteractiveBusyCursorSuspender
{
public:
    ScopedInteractiveBusyCursorSuspender()
    {
#if defined(__WXMSW__) || defined(__APPLE__)
        while (wxIsBusy()) {
            wxEndBusyCursor();
            ++m_suspended_count;
        }
#endif
    }

    ~ScopedInteractiveBusyCursorSuspender()
    {
#if defined(__WXMSW__) || defined(__APPLE__)
        for (int i = 0; i < m_suspended_count; ++i)
            wxBeginBusyCursor();
#endif
    }

private:
    int m_suspended_count = 0;
};

static bool needs_filament_swatch_border(const wxColour& colour)
{
    if (is_dark())
        return colour.Red() < 45 && colour.Green() < 45 && colour.Blue() < 45;
    return colour.Red() > 224 && colour.Green() > 224 && colour.Blue() > 224;
}

static wxColour filament_swatch_border_colour()
{
    return is_dark() ? wxColour(207, 207, 207) : wxColour(130, 130, 128);
}

static void draw_filament_swatch_border(wxDC& dc, const wxColour& colour,
                                        int x, int y, int w, int h, int radius = 0)
{
    if (!needs_filament_swatch_border(colour))
        return;

    dc.SetPen(wxPen(filament_swatch_border_colour(), 1));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    if (radius > 0)
        dc.DrawRoundedRectangle(x, y, w, h, radius);
    else
        dc.DrawRectangle(x, y, w, h);
}

static void draw_filament_swatch_ellipse_border(wxDC& dc, const wxColour& colour,
                                                int x, int y, int w, int h)
{
    if (!needs_filament_swatch_border(colour))
        return;

    dc.SetPen(wxPen(filament_swatch_border_colour(), 1));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawEllipse(x, y, w, h);
}

static wxString ellipsize_text(wxDC& dc, wxString text, int max_width)
{
    if (max_width <= 0)
        return wxEmptyString;
    if (dc.GetTextExtent(text).x <= max_width)
        return text;

    const wxString ellipsis = "...";
    while (!text.empty() && dc.GetTextExtent(text + ellipsis).x > max_width)
        text.RemoveLast();
    if (text.empty() && dc.GetTextExtent(ellipsis).x > max_width)
        return wxString();
    return text + ellipsis;
}

static int draw_brand_icon_and_strip(wxDC& dc, wxWindow* win, wxString& name, int x, int cy)
{
    int icon_sz = win->FromDIP(16);
    if (name.StartsWith("Bambu ")) {
        name = name.Mid(6);
        wxBitmap bmp = create_scaled_bitmap("BambuStudioBlack", win, 16);
        if (bmp.IsOk())
            dc.DrawBitmap(bmp, x, cy - icon_sz / 2, true);
        x += icon_sz + win->FromDIP(4);
    }
    return x;
}

// ============================================================
// GreenSlider — thin track + green triangle thumb
// ============================================================

class GreenSlider : public wxPanel {
public:
    GreenSlider(wxWindow* parent, int value, int minVal, int maxVal,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize);
    int  GetValue() const;
    void SetValue(int val);
    bool Enable(bool enable = true) override;
private:
    void OnPaint(wxPaintEvent&);
    void OnMouse(wxMouseEvent&);
    int  xFromValue() const;
    int  valueFromX(int x) const;
    int  m_value, m_min, m_max;
    bool m_dragging = false;
};

GreenSlider::GreenSlider(wxWindow* parent, int value, int minVal, int maxVal,
                         const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, wxID_ANY, pos, size.IsFullySpecified() ? size : wxSize(-1, parent->FromDIP(24)),
              wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE)
    , m_value(std::clamp(value, minVal, maxVal)), m_min(minVal), m_max(maxVal)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(-1, FromDIP(24)));

    Bind(wxEVT_PAINT,     &GreenSlider::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) {
        evt.Skip();
        Refresh();
    });
    Bind(wxEVT_LEFT_DOWN, &GreenSlider::OnMouse, this);
    Bind(wxEVT_LEFT_UP,   &GreenSlider::OnMouse, this);
    Bind(wxEVT_MOTION,    &GreenSlider::OnMouse, this);
}

int GreenSlider::GetValue() const { return m_value; }

void GreenSlider::SetValue(int val)
{
    val = std::clamp(val, m_min, m_max);
    if (val != m_value) { m_value = val; Refresh(); }
}

bool GreenSlider::Enable(bool enable)
{
    bool ok = wxPanel::Enable(enable);
    Refresh();
    return ok;
}

int GreenSlider::xFromValue() const
{
    wxSize sz = GetClientSize();
    int margin = FromDIP(6);
    int track_w = sz.x - 2 * margin;
    if (m_max <= m_min || track_w <= 0) return margin;
    return margin + (m_value - m_min) * track_w / (m_max - m_min);
}

int GreenSlider::valueFromX(int x) const
{
    wxSize sz = GetClientSize();
    int margin = FromDIP(6);
    int track_w = sz.x - 2 * margin;
    if (track_w <= 0 || m_max <= m_min) return m_min;
    int val = m_min + (x - margin) * (m_max - m_min) / track_w;
    return std::clamp(val, m_min, m_max);
}

void GreenSlider::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    wxSize sz = GetClientSize();

    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();

    int margin = FromDIP(6);
    int track_y = sz.y / 2;
    int ts = FromDIP(8);
    int pen_w = FromDIP(2);

    wxColour greenClr = IsEnabled() ? wxColour(0, 174, 66)
                                    : dark_or(wxColour(180, 180, 180), wxColour(90, 90, 96));
    wxColour grayClr  = IsEnabled() ? dark_or(wxColour(200, 200, 200), wxColour(90, 90, 96))
                                    : dark_or(wxColour(220, 220, 220), wxColour(70, 70, 76));

    int tx = xFromValue();

    dc.SetPen(wxPen(greenClr, pen_w));
    dc.DrawLine(margin, track_y, tx, track_y);

    dc.SetPen(wxPen(grayClr, pen_w));
    dc.DrawLine(tx, track_y, sz.x - margin, track_y);

    wxPoint tri[3] = {
        {tx,          track_y + FromDIP(1)},
        {tx - ts / 2, track_y + FromDIP(1) + ts},
        {tx + ts / 2, track_y + FromDIP(1) + ts}
    };
    dc.SetBrush(wxBrush(greenClr));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawPolygon(3, tri);
}

void GreenSlider::OnMouse(wxMouseEvent& evt)
{
    if (!IsEnabled()) return;

    auto update = [&](int x) {
        int nv = valueFromX(x);
        if (nv != m_value) {
            m_value = nv;
            Refresh();
            wxCommandEvent e(wxEVT_SLIDER, GetId());
            e.SetEventObject(this);
            ProcessWindowEvent(e);
        }
    };

    if (evt.LeftDown()) {
        m_dragging = true;
        CaptureMouse();
        update(evt.GetX());
    } else if (evt.LeftUp()) {
        m_dragging = false;
        if (HasCapture()) ReleaseMouse();
    } else if (evt.Dragging() && m_dragging) {
        update(evt.GetX());
    }
}

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_TEXTURE_COMPUTE_DONE, wxCommandEvent);
wxDEFINE_EVENT(EVT_TEXTURE_COMPUTE_PROGRESS, wxCommandEvent);
wxDEFINE_EVENT(EVT_TEXTURE_COMPUTE_ERROR, wxCommandEvent);
wxDEFINE_EVENT(EVT_TEXTURE_MESH_REPAIR_DECISION, wxCommandEvent);

static std::array<float, 4> parse_color_string(const std::string& hex)
{
    std::array<float, 4> c = {1.f, 1.f, 1.f, 1.f};
    if (hex.size() >= 7 && hex[0] == '#') {
        unsigned long val = std::strtoul(hex.c_str() + 1, nullptr, 16);
        c[0] = ((val >> 16) & 0xFF) / 255.f;
        c[1] = ((val >>  8) & 0xFF) / 255.f;
        c[2] = ((val      ) & 0xFF) / 255.f;
    }
    return c;
}

static wxString rgb_to_hex(const std::array<std::size_t, 3>& c)
{
    return wxString::Format("#%02X%02X%02X",
        (unsigned)c[0], (unsigned)c[1], (unsigned)c[2]);
}

static wxString filament_name_to_wx_string(const std::string& name)
{
    wxString utf8_name = wxString::FromUTF8(name.c_str());
    if (!utf8_name.empty() || name.empty())
        return utf8_name;
    return wxString(name);
}

static bool starts_with_preset_name(const std::string& name, const char* prefix)
{
    const size_t prefix_len = std::strlen(prefix);
    return name.size() >= prefix_len && name.compare(0, prefix_len, prefix) == 0;
}

static std::string resolve_default_virtual_filament_preset_name()
{
    auto* preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle)
        return {};

    auto valid_preset_name = [preset_bundle](const std::string& name) -> bool {
        return !name.empty() && preset_bundle->filaments.find_preset(name, false) != nullptr;
    };

    const auto* default_profiles = preset_bundle->printers.get_selected_preset()
        .config.option<ConfigOptionStrings>("default_filament_profile");
    if (default_profiles) {
        for (const std::string& name : default_profiles->values) {
            if (starts_with_preset_name(name, DEFAULT_VIRTUAL_FILAMENT_NAME) && valid_preset_name(name))
                return name;
        }
    }

    for (const Preset& preset : preset_bundle->filaments.get_presets()) {
        if (preset.is_system && preset.is_visible && preset.is_compatible &&
            starts_with_preset_name(preset.name, DEFAULT_VIRTUAL_FILAMENT_NAME)) {
            return preset.name;
        }
    }

    for (const Preset& preset : preset_bundle->filaments.get_presets()) {
        if (preset.is_visible && preset.is_compatible &&
            starts_with_preset_name(preset.name, DEFAULT_VIRTUAL_FILAMENT_NAME)) {
            return preset.name;
        }
    }

    std::string selected = preset_bundle->filaments.get_selected_preset_name();
    return valid_preset_name(selected) ? selected : std::string();
}

static wxPoint constrained_dialog_position(wxWindow* anchor, const wxSize& dialog_size)
{
    if (!anchor)
        return wxDefaultPosition;

    wxSize size = dialog_size;
    if (size.x <= 0 || size.y <= 0)
        size = wxSize(anchor->FromDIP(450), anchor->FromDIP(350));

    wxPoint pos = anchor->ClientToScreen(wxPoint(0, anchor->GetSize().y));
    wxRect display_rect;
    int display_idx = wxDisplay::GetFromPoint(pos);
    if (display_idx != wxNOT_FOUND)
        display_rect = wxDisplay(display_idx).GetClientArea();
    else
        display_rect = wxDisplay().GetClientArea();

    pos.x = std::clamp(pos.x, display_rect.GetLeft(),
                       std::max(display_rect.GetLeft(), display_rect.GetRight() - size.x));
    pos.y = std::clamp(pos.y, display_rect.GetTop(),
                       std::max(display_rect.GetTop(), display_rect.GetBottom() - size.y));
    return pos;
}

// ============================================================
// FilamentSelectPopup
// ============================================================

class FilamentSelectPopup : public PopupWindow
{
public:
    FilamentSelectPopup(wxWindow* parent,
                        const std::vector<std::array<float, 4>>& colors_rgba,
                        const std::vector<std::string>&          names,
                        size_t                                   existing_count,
                        int                                      popup_width,
                        wxWindow*                                dialog_anchor,
                        std::function<void(int)>                 on_select,
                        std::function<void(wxColour)>            on_add_filament,
                        std::function<bool()>                    can_add_filament,
                        std::function<void(bool)>                on_close)
        : PopupWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS)
        , m_colors_rgba(colors_rgba)
        , m_names(names)
        , m_existing_count(existing_count)
        , m_dialog_anchor(dialog_anchor)
        , m_on_select(std::move(on_select))
        , m_on_add_filament(std::move(on_add_filament))
        , m_can_add_filament(std::move(can_add_filament))
        , m_on_close(std::move(on_close))
    {
        wxColour pop_bg = dark_or(*wxWHITE, wxColour(0x2D, 0x2D, 0x31));
        SetBackgroundColour(pop_bg);

        m_content = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
        m_content->SetBackgroundColour(pop_bg);
        m_content->SetScrollRate(0, FromDIP(5));
        auto* outer = new wxBoxSizer(wxVERTICAL);

        const int pop_w   = std::max(FromDIP(213), popup_width);
        const int row_h   = FromDIP(32);
        const int pad     = FromDIP(8);
        const int max_visible_rows = 10;
        const wxColour header_clr = dark_or(wxColour(0xAC, 0xAC, 0xAC), wxColour(0x81, 0x81, 0x83));

        auto add_section_header = [&](const wxString& label) {
            auto* hdr = new wxStaticText(m_content, wxID_ANY, label);
            wxFont hf = hdr->GetFont();
            hf.SetPointSize(9);
            hdr->SetFont(hf);
            hdr->SetForegroundColour(header_clr);
            outer->Add(hdr, 0, wxLEFT | wxRIGHT | wxTOP, pad);
            auto* line = new StaticLine(m_content);
            line->SetLineColour(texture_import_separator_colour());
            outer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, pad);
        };

        // Section 1: project filaments
        add_section_header(_L("Project Filaments"));
        for (size_t i = 0; i < m_colors_rgba.size(); ++i) {
            if (i == m_existing_count && m_existing_count < m_colors_rgba.size()) {
                add_section_header(_L("New Filaments"));
            }
            wxPanel* row = create_item_row(i, row_h);
            outer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);
        }

        auto* add_label = new wxStaticText(this, wxID_ANY, _L("Add Material"));
        wxFont af = add_label->GetFont();
        af.SetPointSize(10);
        add_label->SetFont(af);
        const bool add_enabled = !m_can_add_filament || m_can_add_filament();
        add_label->SetForegroundColour(add_enabled ? wxColour(0x00, 0xAE, 0x42) : header_clr);
        add_label->SetCursor(wxCursor(add_enabled ? wxCURSOR_HAND : wxCURSOR_ARROW));
        if (!add_enabled)
            add_label->SetToolTip(wxString::Format(
                _L("The project supports up to %d filaments. Extra filaments will be discarded."),
                (int)EnforcerBlockerType::ExtruderMax));
        add_label->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
            if (m_can_add_filament && !m_can_add_filament()) {
                return;
            }
            auto on_add_filament = m_on_add_filament;
            wxWindow* popup_parent = GetParent();
            wxWindow* color_anchor = m_dialog_anchor ? m_dialog_anchor : popup_parent;
            m_closing_from_action = true;
            Dismiss();
            wxColourData cd;
            cd.SetChooseFull(true);
            wxColourDialog dlg(popup_parent, &cd);
            auto move_color_dialog = [&dlg, color_anchor]() {
                dlg.Move(constrained_dialog_position(color_anchor, dlg.GetBestSize()));
            };
            dlg.Bind(wxEVT_SHOW, [move_color_dialog](wxShowEvent& e) mutable {
                e.Skip();
                if (e.IsShown())
                    move_color_dialog();
            });
            move_color_dialog();
            if (dlg.ShowModal() == wxID_OK) {
                wxColour clr = dlg.GetColourData().GetColour();
                if (on_add_filament) on_add_filament(clr);
            }
        });

        m_content->SetSizer(outer);
        m_content->FitInside();

        auto* top_sizer = new wxBoxSizer(wxVERTICAL);
        int list_h = outer->GetMinSize().y;
        if (m_colors_rgba.size() > max_visible_rows)
            list_h -= ((int)m_colors_rgba.size() - max_visible_rows) * row_h;
        m_content->SetMinSize(wxSize(pop_w, list_h));
        m_content->SetMaxSize(wxSize(pop_w, list_h));
        top_sizer->Add(m_content, 0, wxEXPAND);

        top_sizer->AddSpacer(FromDIP(4));
        auto* sep_line = new StaticLine(this);
        sep_line->SetLineColour(texture_import_separator_colour());
        top_sizer->Add(sep_line, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);
        top_sizer->Add(add_label, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, pad);
        SetSizerAndFit(top_sizer);

        SetSize(pop_w, top_sizer->GetMinSize().y);
    }

private:
    void OnDismiss() override
    {
        restore_cursor_state();
        if (m_on_close) m_on_close(m_closing_from_action);
        m_closing_from_action = false;
        wxPopupTransientWindow::OnDismiss();
        schedule_destroy();
    }

    void restore_cursor_state()
    {
        SetCursor(wxNullCursor);
        if (m_content)
            m_content->SetCursor(wxNullCursor);
        if (m_dialog_anchor)
            m_dialog_anchor->SetCursor(wxCursor(wxCURSOR_HAND));
        wxSetCursor(wxNullCursor);
    }

    void schedule_destroy()
    {
        if (m_destroy_scheduled)
            return;
        m_destroy_scheduled = true;
        CallAfter([this]() { Destroy(); });
    }

    wxPanel* create_item_row(size_t idx, int row_h)
    {
        wxColour row_bg    = dark_or(*wxWHITE, wxColour(0x2D, 0x2D, 0x31));
        wxColour hover_bg  = dark_or(wxColour(245, 245, 245), wxColour(0x3C, 0x3C, 0x42));
        wxColour name_fg   = texture_import_text_colour();

        wxPanel* row = new wxPanel(m_content, wxID_ANY, wxDefaultPosition, wxSize(-1, row_h));
        row->SetBackgroundColour(row_bg);
        row->SetBackgroundStyle(wxBG_STYLE_PAINT);
        row->SetCursor(wxCursor(wxCURSOR_HAND));

        const int sq     = row->FromDIP(24);
        const int sq_r   = row->FromDIP(2);
        const int sq_x   = row->FromDIP(4);
        const int gap1   = row->FromDIP(8);

        wxColour fil_clr = idx < m_colors_rgba.size()
            ? wxColour((unsigned char)(m_colors_rgba[idx][0] * 255.f),
                       (unsigned char)(m_colors_rgba[idx][1] * 255.f),
                       (unsigned char)(m_colors_rgba[idx][2] * 255.f))
            : wxColour(128, 128, 128);

        wxString name_str = (idx < m_names.size()) ? filament_name_to_wx_string(m_names[idx])
                                                    : wxString::Format("Filament %d", (int)(idx + 1));
        row->SetToolTip(name_str);

        row->Bind(wxEVT_PAINT, [this, idx, sq, sq_r, sq_x, gap1, fil_clr, name_str, row_bg, hover_bg, name_fg](wxPaintEvent& e) {
            auto* p = static_cast<wxPanel*>(e.GetEventObject());
            wxAutoBufferedPaintDC dc(p);
            wxSize sz = p->GetClientSize();

            bool hovered = (m_hover_idx == (int)idx);
            dc.SetBrush(wxBrush(hovered ? hover_bg : row_bg));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, sz.x, sz.y);

            int sq_y = (sz.y - sq) / 2;
            wxColour paint_clr = fil_clr;
            if (idx < m_colors_rgba.size()) {
                paint_clr = wxColour((unsigned char)(m_colors_rgba[idx][0] * 255.f),
                                     (unsigned char)(m_colors_rgba[idx][1] * 255.f),
                                     (unsigned char)(m_colors_rgba[idx][2] * 255.f));
            }
            dc.SetBrush(wxBrush(paint_clr));
            dc.DrawRoundedRectangle(sq_x, sq_y, sq, sq, sq_r);
            draw_filament_swatch_border(dc, paint_clr, sq_x, sq_y, sq, sq, sq_r);

            {
                wxFont nf = p->GetFont();
                nf.SetPointSize(9);
                dc.SetFont(nf);
                dc.SetTextForeground(paint_clr.GetLuminance() < 0.6 ? *wxWHITE : texture_import_gray9000());
                wxString ns = wxString::Format("%d", (int)(idx + 1));
                wxSize tsz = dc.GetTextExtent(ns);
                dc.DrawText(ns, sq_x + (sq - tsz.x) / 2, sq_y + (sq - tsz.y) / 2);
            }

            // Brand icon + material name
            {
                wxFont mf = p->GetFont();
                mf.SetPointSize(10);
                dc.SetFont(mf);
                dc.SetTextForeground(name_fg);
                wxString display = name_str;
                int tx = draw_brand_icon_and_strip(dc, p, display, sq_x + sq + gap1, sz.y / 2);
                display = ellipsize_text(dc, display, sz.x - tx - p->FromDIP(4));
                wxSize tsz = dc.GetTextExtent(display);
                if (!display.empty())
                    dc.DrawText(display, tx, (sz.y - tsz.y) / 2);
            }
        });

        row->Bind(wxEVT_MOTION, [this, idx](wxMouseEvent& evt) {
            if (m_hover_idx != (int)idx) {
                m_hover_idx = (int)idx;
                m_content->Refresh();
            }
            evt.Skip();
        });
        row->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& evt) {
            if (m_hover_idx != -1) {
                m_hover_idx = -1;
                m_content->Refresh();
            }
            evt.Skip();
        });

        row->Bind(wxEVT_LEFT_DOWN, [this, idx](wxMouseEvent&) {
            if (m_on_select) m_on_select((int)idx);
            m_closing_from_action = true;
            Dismiss();
        });

        return row;
    }

    wxScrolledWindow*                         m_content = nullptr;
    std::vector<std::array<float, 4>>          m_colors_rgba;
    std::vector<std::string>                   m_names;
    size_t                                     m_existing_count = 0;
    wxWindow*                                  m_dialog_anchor = nullptr;
    std::function<void(int)>                   m_on_select;
    std::function<void(wxColour)>              m_on_add_filament;
    std::function<bool()>                      m_can_add_filament;
    std::function<void(bool)>                  m_on_close;
    int                                        m_hover_idx = -1;
    bool                                       m_closing_from_action = false;
    bool                                       m_destroy_scheduled = false;
};

// ============================================================
// TexturePreviewCanvas
// ============================================================

TexturePreviewCanvas::TexturePreviewCanvas(wxWindow* parent, const wxGLAttributes& attrs)
    : wxGLCanvas(parent, attrs, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE)
{
    m_context = new wxGLContext(this);

    Bind(wxEVT_PAINT, &TexturePreviewCanvas::on_paint, this);
    Bind(wxEVT_SIZE,  &TexturePreviewCanvas::on_size,  this);
    Bind(wxEVT_MOUSEWHEEL,  &TexturePreviewCanvas::on_mouse, this);
    Bind(wxEVT_LEFT_DOWN,   &TexturePreviewCanvas::on_mouse, this);
    Bind(wxEVT_LEFT_UP,     &TexturePreviewCanvas::on_mouse, this);
    Bind(wxEVT_RIGHT_DOWN,  &TexturePreviewCanvas::on_mouse, this);
    Bind(wxEVT_RIGHT_UP,    &TexturePreviewCanvas::on_mouse, this);
    Bind(wxEVT_MIDDLE_DOWN, &TexturePreviewCanvas::on_mouse, this);
    Bind(wxEVT_MIDDLE_UP,   &TexturePreviewCanvas::on_mouse, this);
    Bind(wxEVT_MOTION,      &TexturePreviewCanvas::on_mouse, this);
    Bind(wxEVT_LEAVE_WINDOW, &TexturePreviewCanvas::on_mouse, this);
}

TexturePreviewCanvas::~TexturePreviewCanvas()
{
    if (m_context) {
        SetCurrent(*m_context);
        if (m_tex_id)
            glDeleteTextures(1, &m_tex_id);
        for (unsigned int id : m_gl_tex_ids)
            if (id) glDeleteTextures(1, &id);
        for (unsigned int id : {m_reset_icon_tex, m_reset_icon_hover_tex,
                                m_reset_icon_dark_tex, m_reset_icon_dark_hover_tex})
            if (id) glDeleteTextures(1, &id);
        delete m_context;
    }
}

void TexturePreviewCanvas::set_mesh_data(
    const std::vector<std::array<float, 3>>& vertices,
    const std::vector<std::array<int, 3>>&   indices)
{
    m_vertices = vertices;
    m_indices  = indices;
    update_bounding_box();
    compute_smooth_normals();
    Refresh();
}

void TexturePreviewCanvas::compute_smooth_normals()
{
    m_vertex_normals.clear();
    if (m_vertices.empty() || m_indices.empty()) return;

    m_vertex_normals.resize(m_vertices.size(), {0.f, 0.f, 0.f});

    for (const auto& face : m_indices) {
        int i0 = face[0], i1 = face[1], i2 = face[2];
        if (i0 < 0 || i0 >= (int)m_vertices.size() ||
            i1 < 0 || i1 >= (int)m_vertices.size() ||
            i2 < 0 || i2 >= (int)m_vertices.size())
            continue;

        const auto& v0 = m_vertices[i0];
        const auto& v1 = m_vertices[i1];
        const auto& v2 = m_vertices[i2];

        float nx = (v1[1]-v0[1])*(v2[2]-v0[2]) - (v1[2]-v0[2])*(v2[1]-v0[1]);
        float ny = (v1[2]-v0[2])*(v2[0]-v0[0]) - (v1[0]-v0[0])*(v2[2]-v0[2]);
        float nz = (v1[0]-v0[0])*(v2[1]-v0[1]) - (v1[1]-v0[1])*(v2[0]-v0[0]);

        m_vertex_normals[i0][0] += nx; m_vertex_normals[i0][1] += ny; m_vertex_normals[i0][2] += nz;
        m_vertex_normals[i1][0] += nx; m_vertex_normals[i1][1] += ny; m_vertex_normals[i1][2] += nz;
        m_vertex_normals[i2][0] += nx; m_vertex_normals[i2][1] += ny; m_vertex_normals[i2][2] += nz;
    }

    for (auto& n : m_vertex_normals) {
        float len = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        if (len > 1e-8f) { n[0] /= len; n[1] /= len; n[2] /= len; }
    }
}

void TexturePreviewCanvas::set_texture_data(
    const std::vector<std::array<float, 2>>& uvs,
    const unsigned char* tex_data, int tex_w, int tex_h, int tex_channels)
{
    m_uvs = uvs;
    m_tex_w = tex_w;
    m_tex_h = tex_h;
    m_tex_channels = tex_channels;
    m_tex_dirty = true;

    size_t sz = (size_t)tex_w * tex_h * tex_channels;
    m_tex_data.assign(tex_data, tex_data + sz);
    Refresh();
}

void TexturePreviewCanvas::set_texture_render_data(
    const std::vector<std::vector<unsigned char>>& tex_pixels_rgb,
    const std::vector<int>& tex_widths,
    const std::vector<int>& tex_heights,
    const std::vector<std::array<std::array<float,2>, 3>>& face_uvs,
    const std::vector<int>& face_tex_ids)
{
    m_tex_pixels_rgb = tex_pixels_rgb;
    m_tex_widths     = tex_widths;
    m_tex_heights    = tex_heights;
    m_face_uvs       = face_uvs;
    m_face_tex_ids   = face_tex_ids;
    m_multi_tex_dirty = true;
    Refresh();
}

void TexturePreviewCanvas::upload_textures()
{
    if (!m_multi_tex_dirty) return;
    m_multi_tex_dirty = false;

    for (unsigned int id : m_gl_tex_ids)
        if (id) glDeleteTextures(1, &id);
    m_gl_tex_ids.clear();

    m_gl_tex_ids.resize(m_tex_pixels_rgb.size(), 0);
    for (size_t i = 0; i < m_tex_pixels_rgb.size(); ++i) {
        if (m_tex_pixels_rgb[i].empty() || m_tex_widths[i] <= 0 || m_tex_heights[i] <= 0)
            continue;
        GLuint tex_id = 0;
        glGenTextures(1, &tex_id);
        glBindTexture(GL_TEXTURE_2D, tex_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_tex_widths[i], m_tex_heights[i],
                     0, GL_RGB, GL_UNSIGNED_BYTE, m_tex_pixels_rgb[i].data());
        m_gl_tex_ids[i] = tex_id;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TexturePreviewCanvas::set_painted_mesh_data(
    const std::vector<std::array<float, 3>>& vertices,
    const std::vector<std::array<int, 3>>&   indices)
{
    m_painted_vertices = vertices;
    m_painted_indices  = indices;
    Refresh();
}

static void convert_face_colors(const std::vector<std::array<std::size_t, 3>>& src,
                                std::vector<std::array<float, 3>>& dst)
{
    dst.resize(src.size());
    for (size_t i = 0; i < src.size(); ++i)
        dst[i] = { src[i][0] / 255.f, src[i][1] / 255.f, src[i][2] / 255.f };
}

void TexturePreviewCanvas::set_face_colors(const std::vector<std::array<std::size_t, 3>>& face_colors)
{
    convert_face_colors(face_colors, m_face_colors_rgb);
    Refresh();
}

void TexturePreviewCanvas::set_original_face_colors(const std::vector<std::array<std::size_t, 3>>& face_colors)
{
    convert_face_colors(face_colors, m_original_face_colors_rgb);
    Refresh();
}

void TexturePreviewCanvas::set_filament_color_map(
    const std::map<std::array<std::size_t, 3>, std::array<float, 3>>& color_map)
{
    m_color_map = color_map;
    m_filament_colors_rgb.resize(m_face_colors_rgb.size());
    for (size_t i = 0; i < m_face_colors_rgb.size(); ++i) {
        std::array<std::size_t, 3> key = {
            (std::size_t)(m_face_colors_rgb[i][0] * 255.f + 0.5f),
            (std::size_t)(m_face_colors_rgb[i][1] * 255.f + 0.5f),
            (std::size_t)(m_face_colors_rgb[i][2] * 255.f + 0.5f)
        };
        auto it = color_map.find(key);
        if (it != color_map.end())
            m_filament_colors_rgb[i] = it->second;
        else
            m_filament_colors_rgb[i] = m_face_colors_rgb[i];
    }
    Refresh();
}

void TexturePreviewCanvas::set_render_mode(RenderMode mode)
{
    if (m_mode != mode) {
        m_mode = mode;
        Refresh();
    }
}

void TexturePreviewCanvas::set_computing_overlay(bool /*show*/)
{
    Refresh();
}

void TexturePreviewCanvas::reset_view()
{
    m_zoom  = 1.0f;
    m_rot_x = -30.0f;
    m_rot_y = 30.0f;
    m_pan_x = 0.0f;
    m_pan_y = 0.0f;
    Refresh();
}

wxRect TexturePreviewCanvas::reset_overlay_rect() const
{
    wxSize sz = GetClientSize();
    const int button_size = FromDIP(40);
    const int margin = FromDIP(20);
    return wxRect(
        std::max(margin, sz.x - button_size - margin),
        std::max(margin, sz.y - button_size - margin),
        button_size,
        button_size);
}

unsigned int TexturePreviewCanvas::upload_reset_icon_texture(const std::string& icon_name)
{
    wxBitmap bmp = create_scaled_bitmap(icon_name, this, 40);
    if (!bmp.IsOk())
        return 0;

    wxImage image = bmp.ConvertToImage();
    if (!image.IsOk())
        return 0;

    const int w = image.GetWidth();
    const int h = image.GetHeight();
    const unsigned char* rgb = image.GetData();
    const unsigned char* alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;
    if (!rgb || w <= 0 || h <= 0)
        return 0;

    std::vector<unsigned char> rgba((size_t)w * h * 4);
    for (int i = 0; i < w * h; ++i) {
        rgba[(size_t)i * 4 + 0] = rgb[i * 3 + 0];
        rgba[(size_t)i * 4 + 1] = rgb[i * 3 + 1];
        rgba[(size_t)i * 4 + 2] = rgb[i * 3 + 2];
        rgba[(size_t)i * 4 + 3] = alpha ? alpha[i] : 255;
    }

    GLuint tex_id = 0;
    glGenTextures(1, &tex_id);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex_id;
}

void TexturePreviewCanvas::upload_reset_icon_textures()
{
    if (m_reset_icon_tex && m_reset_icon_hover_tex && m_reset_icon_dark_tex && m_reset_icon_dark_hover_tex)
        return;

    if (!m_reset_icon_tex)
        m_reset_icon_tex = upload_reset_icon_texture("fit_camera");
    if (!m_reset_icon_hover_tex)
        m_reset_icon_hover_tex = upload_reset_icon_texture("fit_camera_hover");
    if (!m_reset_icon_dark_tex)
        m_reset_icon_dark_tex = upload_reset_icon_texture("fit_camera_dark");
    if (!m_reset_icon_dark_hover_tex)
        m_reset_icon_dark_hover_tex = upload_reset_icon_texture("fit_camera_dark_hover");
}

bool TexturePreviewCanvas::handle_reset_overlay_mouse(wxMouseEvent& evt)
{
    if (evt.Leaving()) {
        if (m_reset_overlay_pressed) {
            m_reset_overlay_hovered = false;
            m_reset_overlay_pressed = false;
            SetCursor(wxCursor(wxCURSOR_ARROW));
            if (HasCapture())
                ReleaseMouse();
            Refresh();
            return true;
        }
        if (m_reset_overlay_hovered) {
            m_reset_overlay_hovered = false;
            SetCursor(wxCursor(wxCURSOR_ARROW));
            Refresh();
        }
        return false;
    }

    const bool over = reset_overlay_rect().Contains(evt.GetPosition());
    if (over != m_reset_overlay_hovered) {
        m_reset_overlay_hovered = over;
        SetCursor(wxCursor(over ? wxCURSOR_HAND : wxCURSOR_ARROW));
        Refresh();
    }

    if (m_drag_mode != DragMode::None && !m_reset_overlay_pressed)
        return false;

    if (evt.LeftDown() && over) {
        m_reset_overlay_pressed = true;
        if (!HasCapture())
            CaptureMouse();
        Refresh();
        return true;
    }

    if (evt.LeftUp() && m_reset_overlay_pressed) {
        const bool activate = over;
        m_reset_overlay_pressed = false;
        if (HasCapture())
            ReleaseMouse();
        if (activate)
            reset_view();
        else
            Refresh();
        return true;
    }

    return over;
}

void TexturePreviewCanvas::update_bounding_box()
{
    if (m_vertices.empty()) return;
    std::array<float, 3> mn = m_vertices[0], mx = m_vertices[0];
    for (const auto& v : m_vertices) {
        for (int i = 0; i < 3; ++i) {
            mn[i] = std::min(mn[i], v[i]);
            mx[i] = std::max(mx[i], v[i]);
        }
    }
    m_center = { (mn[0]+mx[0])/2, (mn[1]+mx[1])/2, (mn[2]+mx[2])/2 };
    float dx = mx[0]-mn[0], dy = mx[1]-mn[1], dz = mx[2]-mn[2];
    m_radius = std::sqrt(dx*dx + dy*dy + dz*dz) / 2.0f;
    if (m_radius < 1e-6f) m_radius = 1.0f;
}

void TexturePreviewCanvas::ensure_gl_ready()
{
    if (m_gl_initialized) return;

    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        BOOST_LOG_TRIVIAL(error) << "TexturePreviewCanvas: glewInit failed: "
                                 << glewGetErrorString(err);
        return;
    }
    while (glGetError() != GL_NO_ERROR) {}

    m_gl_initialized = true;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    GLfloat light_pos[]     = { 0.5f, 1.0f, 1.0f, 0.0f };
    GLfloat light_ambient[] = { 0.3f, 0.3f, 0.3f, 1.0f };
    GLfloat light_diffuse[] = { 0.8f, 0.8f, 0.8f, 1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);
    glLightfv(GL_LIGHT0, GL_AMBIENT,  light_ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  light_diffuse);
}

void TexturePreviewCanvas::on_paint(wxPaintEvent&)
{
    wxPaintDC dc(this);
    if (!m_context) return;
    SetCurrent(*m_context);
    ensure_gl_ready();
    render();
    SwapBuffers();
}

void TexturePreviewCanvas::on_size(wxSizeEvent&)
{
    Refresh();
}

void TexturePreviewCanvas::on_mouse(wxMouseEvent& evt)
{
    if (handle_reset_overlay_mouse(evt))
        return;

    if (evt.LeftDown()) {
        m_drag_mode = DragMode::Rotate;
        m_last_mouse_pos = evt.GetPosition();
        if (!HasCapture()) CaptureMouse();
    }
    else if (evt.LeftUp()) {
        if (m_drag_mode == DragMode::Rotate) {
            m_drag_mode = DragMode::None;
            if (HasCapture()) ReleaseMouse();
        }
    }
    else if (evt.RightDown()) {
        m_drag_mode = DragMode::Pan;
        m_last_mouse_pos = evt.GetPosition();
        if (!HasCapture()) CaptureMouse();
    }
    else if (evt.MiddleDown()) {
        m_drag_mode = DragMode::Pan;
        m_last_mouse_pos = evt.GetPosition();
        if (!HasCapture()) CaptureMouse();
    }
    else if (evt.RightUp() || evt.MiddleUp()) {
        if (m_drag_mode == DragMode::Pan) {
            m_drag_mode = DragMode::None;
            if (HasCapture()) ReleaseMouse();
        }
    }
    else if (evt.Dragging() && m_drag_mode != DragMode::None) {
        wxPoint pos = evt.GetPosition();
        float dx = (float)(pos.x - m_last_mouse_pos.x);
        float dy = (float)(pos.y - m_last_mouse_pos.y);

        if (m_drag_mode == DragMode::Rotate) {
            m_rot_y += dx * 0.5f;
            m_rot_x += dy * 0.5f;
            m_rot_x = std::max(-89.0f, std::min(89.0f, m_rot_x));
        } else if (m_drag_mode == DragMode::Pan) {
            wxSize sz = GetClientSize();
            if (sz.x > 0)
                m_pan_x += dx / (float)sz.x * m_radius * 2.0f / m_zoom;
            if (sz.y > 0)
                m_pan_y -= dy / (float)sz.y * m_radius * 2.0f / m_zoom;
        }

        m_last_mouse_pos = pos;
        Refresh();
    }
    else if (evt.GetWheelRotation() != 0) {
        float delta = evt.GetWheelRotation() > 0 ? 1.1f : 0.9f;
        m_zoom *= delta;
        m_zoom = std::max(0.1f, std::min(20.0f, m_zoom));
        Refresh();
    }
}

void TexturePreviewCanvas::render()
{
    wxSize sz = GetClientSize();
    if (sz.x <= 0 || sz.y <= 0) return;

    wxSize viewport_sz = gl_viewport_size(this, sz);
    glViewport(0, 0, viewport_sz.x, viewport_sz.y);
    if (is_dark())
        glClearColor(0.24f, 0.24f, 0.27f, 1.0f);
    else
        glClearColor(0.933f, 0.933f, 0.933f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = (float)viewport_sz.x / (float)viewport_sz.y;
    float dist = m_radius * 3.0f / m_zoom;
    float near_plane = dist * 0.01f;
    float far_plane  = dist * 10.0f;
    float fov_rad    = 45.0f * static_cast<float>(M_PI) / 180.0f;
    float f          = 1.0f / std::tan(fov_rad / 2.0f);
    float proj[16]   = {};
    proj[0]  = f / aspect;
    proj[5]  = f;
    proj[10] = (far_plane + near_plane) / (near_plane - far_plane);
    proj[11] = -1.0f;
    proj[14] = (2.0f * far_plane * near_plane) / (near_plane - far_plane);
    glMultMatrixf(proj);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -dist);
    glTranslatef(m_pan_x, m_pan_y, 0.0f);
    glRotatef(m_rot_x, 1.0f, 0.0f, 0.0f);
    glRotatef(m_rot_y, 0.0f, 1.0f, 0.0f);
    glTranslatef(-m_center[0], -m_center[1], -m_center[2]);

    render_mesh();
    render_reset_overlay(sz, viewport_sz);
}

void TexturePreviewCanvas::render_reset_overlay(const wxSize& logical_size, const wxSize& viewport_size)
{
    if (logical_size.x <= 0 || logical_size.y <= 0 || viewport_size.x <= 0 || viewport_size.y <= 0)
        return;

    upload_reset_icon_textures();

    const unsigned int tex_id = is_dark()
        ? (m_reset_overlay_hovered ? m_reset_icon_dark_hover_tex : m_reset_icon_dark_tex)
        : (m_reset_overlay_hovered ? m_reset_icon_hover_tex : m_reset_icon_tex);
    if (!tex_id)
        return;

    wxRect rc = reset_overlay_rect();
    const float sx = (float)viewport_size.x / (float)logical_size.x;
    const float sy = (float)viewport_size.y / (float)logical_size.y;
    const float x0 = rc.GetLeft() * sx;
    const float y0 = rc.GetTop() * sy;
    const float x1 = (rc.GetLeft() + rc.GetWidth()) * sx;
    const float y1 = (rc.GetTop() + rc.GetHeight()) * sy;
    const float alpha = m_reset_overlay_hovered ? 1.0f : 0.78f;

    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, viewport_size.x, viewport_size.y, 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glColor4f(1.0f, 1.0f, 1.0f, alpha);
    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(x0, y0);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(x1, y0);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(x1, y1);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(x0, y1);
    glEnd();

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    glBindTexture(GL_TEXTURE_2D, 0);
    glPopAttrib();
}

void TexturePreviewCanvas::render_textured_original()
{
    if (m_vertices.empty() || m_indices.empty()) return;
    if (m_face_uvs.empty() || m_face_tex_ids.empty()) return;
    if (m_face_uvs.size() != m_indices.size()) return;

    upload_textures();

    const bool has_smooth = (m_vertex_normals.size() == m_vertices.size());

    // Group faces by texture id for batch rendering
    std::map<int, std::vector<size_t>> tex_groups;
    for (size_t fi = 0; fi < m_indices.size(); ++fi) {
        int tid = (fi < m_face_tex_ids.size()) ? m_face_tex_ids[fi] : -1;
        tex_groups[tid].push_back(fi);
    }

    glEnable(GL_LIGHTING);
    glColor3f(1.0f, 1.0f, 1.0f);

    for (const auto& [tid, face_list] : tex_groups) {
        bool tex_bound = false;
        if (tid >= 0 && tid < (int)m_gl_tex_ids.size() && m_gl_tex_ids[tid] != 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, m_gl_tex_ids[tid]);
            tex_bound = true;
        } else {
            glDisable(GL_TEXTURE_2D);
        }

        glBegin(GL_TRIANGLES);
        for (size_t fi : face_list) {
            const auto& face = m_indices[fi];
            const auto& uvs  = m_face_uvs[fi];

            if (!tex_bound) {
                if (fi < m_original_face_colors_rgb.size())
                    glColor3fv(m_original_face_colors_rgb[fi].data());
                else
                    glColor3f(0.7f, 0.7f, 0.7f);
            }

            for (int vi = 0; vi < 3; ++vi) {
                int idx = face[vi];
                if (idx < 0 || idx >= (int)m_vertices.size()) continue;

                if (has_smooth) {
                    glNormal3fv(m_vertex_normals[idx].data());
                } else if (vi == 0) {
                    const auto& v0 = m_vertices[face[0]];
                    const auto& v1 = m_vertices[face[1]];
                    const auto& v2 = m_vertices[face[2]];
                    float nx = (v1[1]-v0[1])*(v2[2]-v0[2]) - (v1[2]-v0[2])*(v2[1]-v0[1]);
                    float ny = (v1[2]-v0[2])*(v2[0]-v0[0]) - (v1[0]-v0[0])*(v2[2]-v0[2]);
                    float nz = (v1[0]-v0[0])*(v2[1]-v0[1]) - (v1[1]-v0[1])*(v2[0]-v0[0]);
                    float len = std::sqrt(nx*nx + ny*ny + nz*nz);
                    if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
                    glNormal3f(nx, ny, nz);
                }

                if (tex_bound)
                    glTexCoord2fv(uvs[vi].data());
                glVertex3fv(m_vertices[idx].data());
            }
        }
        glEnd();
    }

    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TexturePreviewCanvas::render_mesh()
{
    if (m_vertices.empty() || m_indices.empty()) return;

    // Original mode with texture data: use proper texture mapping
    if (m_mode == RenderMode::Original && !m_face_uvs.empty()) {
        render_textured_original();
        return;
    }

    // For Multi-Color / FilamentMap, use the painted (remeshed) geometry if available;
    // the face color arrays match the painted mesh, not the original mesh.
    const bool use_painted = (m_mode != RenderMode::Original)
                             && !m_painted_vertices.empty()
                             && !m_painted_indices.empty();

    const auto& verts   = use_painted ? m_painted_vertices : m_vertices;
    const auto& faces   = use_painted ? m_painted_indices  : m_indices;

    const std::vector<std::array<float, 3>>* colors_ptr = nullptr;
    if (m_mode == RenderMode::Original && !m_original_face_colors_rgb.empty()
        && m_original_face_colors_rgb.size() == m_indices.size()) {
        colors_ptr = &m_original_face_colors_rgb;
    } else if (m_mode == RenderMode::FilamentMap && !m_filament_colors_rgb.empty()
               && m_filament_colors_rgb.size() == faces.size()) {
        colors_ptr = &m_filament_colors_rgb;
    } else if (!m_face_colors_rgb.empty() && m_face_colors_rgb.size() == faces.size()) {
        colors_ptr = &m_face_colors_rgb;
    }

    // Use smooth normals for the original mesh when available
    const bool has_smooth = !use_painted
                            && (m_vertex_normals.size() == m_vertices.size());

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);

    glBegin(GL_TRIANGLES);
    for (size_t fi = 0; fi < faces.size(); ++fi) {
        if (colors_ptr)
            glColor3fv((*colors_ptr)[fi].data());
        else
            glColor3f(0.7f, 0.7f, 0.7f);

        const auto& face = faces[fi];
        for (int vi = 0; vi < 3; ++vi) {
            int idx = face[vi];
            if (idx < 0 || idx >= (int)verts.size()) continue;

            if (has_smooth && idx < (int)m_vertex_normals.size()) {
                glNormal3fv(m_vertex_normals[idx].data());
            } else if (vi == 0) {
                const auto& v0 = verts[face[0]];
                const auto& v1 = verts[face[1]];
                const auto& v2 = verts[face[2]];
                float nx = (v1[1]-v0[1])*(v2[2]-v0[2]) - (v1[2]-v0[2])*(v2[1]-v0[1]);
                float ny = (v1[2]-v0[2])*(v2[0]-v0[0]) - (v1[0]-v0[0])*(v2[2]-v0[2]);
                float nz = (v1[0]-v0[0])*(v2[1]-v0[1]) - (v1[1]-v0[1])*(v2[0]-v0[0]);
                float len = std::sqrt(nx*nx + ny*ny + nz*nz);
                if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
                glNormal3f(nx, ny, nz);
            }

            glVertex3fv(verts[idx].data());
        }
    }
    glEnd();
}


// ============================================================
// TextureImportDialog
// ============================================================

wxBEGIN_EVENT_TABLE(TextureImportDialog, DPIDialog)
    EVT_BUTTON(TextureImportDialog::ID_COLOR_4,    TextureImportDialog::on_color_preset_clicked)
    EVT_BUTTON(TextureImportDialog::ID_COLOR_8,    TextureImportDialog::on_color_preset_clicked)
    EVT_BUTTON(TextureImportDialog::ID_COLOR_16,   TextureImportDialog::on_color_preset_clicked)
    EVT_BUTTON(TextureImportDialog::ID_COLOR_AUTO,  TextureImportDialog::on_color_preset_clicked)
    EVT_BUTTON(TextureImportDialog::ID_BTN_APPLY,   TextureImportDialog::on_apply_clicked)
    EVT_BUTTON(TextureImportDialog::ID_BTN_SKIP,    TextureImportDialog::on_skip_clicked)
    EVT_BUTTON(wxID_OK,                              TextureImportDialog::on_ok_clicked)
wxEND_EVENT_TABLE()

TextureImportDialog::TextureImportDialog(
    wxWindow*                        parent,
    const Slic3r::TexturedMesh&      textured_mesh,
    const std::vector<std::string>&  filament_color_strs,
    const std::vector<std::string>&  filament_names,
    std::function<bool()>            initial_cancel_callback,
    std::function<bool(int)>         initial_progress_callback)
    : DPIDialog(parent, wxID_ANY, _L("Import Model"),
                wxDefaultPosition, wxDefaultSize,
                (wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) & ~(wxMINIMIZE_BOX | wxMAXIMIZE_BOX))
    , m_textured_mesh(textured_mesh)
    , m_filament_color_strs(filament_color_strs)
    , m_filament_names(filament_names)
    , m_initial_cancel_callback(std::move(initial_cancel_callback))
    , m_initial_progress_callback(std::move(initial_progress_callback))
{
    SetSize(wxSize(FromDIP(960), FromDIP(640)));

    m_filament_colors_rgba.reserve(filament_color_strs.size());
    for (const auto& s : filament_color_strs)
        m_filament_colors_rgba.push_back(parse_color_string(s));

    m_existing_filament_count = m_filament_colors_rgba.size();
    m_default_virtual_filament_preset_name = resolve_default_virtual_filament_preset_name();

    if (m_filament_names.empty()) {
        for (size_t i = 0; i < filament_color_strs.size(); ++i)
            m_filament_names.push_back("Filament " + std::to_string(i + 1));
    }

    Bind(EVT_TEXTURE_COMPUTE_DONE,     &TextureImportDialog::on_computation_complete, this);
    Bind(EVT_TEXTURE_COMPUTE_PROGRESS, &TextureImportDialog::on_computation_progress, this);
    Bind(EVT_TEXTURE_COMPUTE_ERROR,    &TextureImportDialog::on_computation_error,    this);
    Bind(EVT_TEXTURE_MESH_REPAIR_DECISION, &TextureImportDialog::on_mesh_repair_decision_required, this);

    build_ui();
    SetMinSize(wxSize(FromDIP(800), FromDIP(500)));
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);

    m_preview_canvas->set_mesh_data(m_textured_mesh.vertices, m_textured_mesh.indices);

    // Prepare texture rendering data for the Original tab
    if (!m_textured_mesh.textures.empty()) {
        std::vector<std::vector<unsigned char>> tex_pixels_rgb;
        std::vector<int> tex_widths, tex_heights;
        tex_pixels_rgb.reserve(m_textured_mesh.textures.size());
        tex_widths.reserve(m_textured_mesh.textures.size());
        tex_heights.reserve(m_textured_mesh.textures.size());

        for (const auto& ti : m_textured_mesh.textures) {
            std::vector<unsigned char> bgr_pixels;
            int w = 0, h = 0;
            if (Slic3r::decode_texture_to_pixels(ti, bgr_pixels, w, h) && !bgr_pixels.empty()) {
                // Convert BGR to RGB for OpenGL
                for (size_t p = 0; p < bgr_pixels.size(); p += 3)
                    std::swap(bgr_pixels[p], bgr_pixels[p + 2]);
                tex_pixels_rgb.push_back(std::move(bgr_pixels));
            } else {
                tex_pixels_rgb.push_back({});
            }
            tex_widths.push_back(w);
            tex_heights.push_back(h);
        }

        const size_t nf = m_textured_mesh.indices.size();
        const bool has_mapping = !m_textured_mesh.material_texture_map.empty();

        // Build per-face UV array
        std::vector<std::array<std::array<float,2>, 3>> face_uvs(nf);
        for (size_t fi = 0; fi < nf; ++fi) {
            if (m_textured_mesh.has_face_uvs()) {
                const auto& ui = m_textured_mesh.uv_indices[fi];
                for (int vi = 0; vi < 3; ++vi) {
                    int idx = ui[vi];
                    if (idx >= 0 && static_cast<size_t>(idx) < m_textured_mesh.uv_coords.size())
                        face_uvs[fi][vi] = m_textured_mesh.uv_coords[idx];
                    else
                        face_uvs[fi][vi] = {0.f, 0.f};
                }
            } else if (!m_textured_mesh.uvs.empty()) {
                const auto& face = m_textured_mesh.indices[fi];
                for (int vi = 0; vi < 3; ++vi) {
                    int idx = face[vi];
                    if (idx >= 0 && static_cast<size_t>(idx) < m_textured_mesh.uvs.size())
                        face_uvs[fi][vi] = m_textured_mesh.uvs[idx];
                    else
                        face_uvs[fi][vi] = {0.f, 0.f};
                }
            }
        }

        // Build per-face texture index
        std::vector<int> face_tex_ids(nf, 0);
        for (size_t fi = 0; fi < nf; ++fi) {
            int mat_idx = (fi < m_textured_mesh.material_ids.size())
                          ? m_textured_mesh.material_ids[fi] : -1;
            if (has_mapping && mat_idx >= 0
                && static_cast<size_t>(mat_idx) < m_textured_mesh.material_texture_map.size())
                face_tex_ids[fi] = m_textured_mesh.material_texture_map[mat_idx];
            else if (!tex_pixels_rgb.empty())
                face_tex_ids[fi] = 0;
            else
                face_tex_ids[fi] = -1;
        }

        m_preview_canvas->set_texture_render_data(
            tex_pixels_rgb, tex_widths, tex_heights, face_uvs, face_tex_ids);

        // Still sample per-face colors as fallback
        std::vector<std::array<std::size_t, 3>> orig_colors;
        if (Slic3r::sample_original_face_colors(m_textured_mesh, orig_colors))
            m_preview_canvas->set_original_face_colors(orig_colors);
    }

    set_state(TextureImportState::Idle);
}

TextureImportDialog::~TextureImportDialog()
{
    m_cancel_flag = true;
    if (m_worker && m_worker->joinable())
        m_worker->join();
}

int TextureImportDialog::ShowModal()
{
    if (m_state == TextureImportState::Idle && m_painted.face_colors.empty()) {
        start_computation(true, true);

        while (m_initial_computation_pending) {
            if (auto* event_loop = wxEventLoopBase::GetActive())
                event_loop->Yield();
            else
                wxYield();
            if (m_progress_dlg && m_progress_dlg->WasCancelled())
                m_cancel_flag = true;
            if (m_initial_cancel_callback && m_initial_cancel_callback())
                m_cancel_flag = true;
            wxMilliSleep(10);
        }

        if (m_worker && m_worker->joinable())
            m_worker->join();
        m_worker.reset();

        if (m_initial_computation_cancelled || m_initial_computation_failed)
            return wxID_CANCEL;
    }

    ScopedInteractiveBusyCursorSuspender busy_cursor_suspender;
    return DPIDialog::ShowModal();
}

void TextureImportDialog::build_ui()
{
    const wxColour dialog_bg = dark_or(*wxWHITE, wxColour(0x2D, 0x2D, 0x31));
    SetBackgroundColour(dialog_bg);
    SetForegroundColour(dark_or(wxColour(50, 58, 61), wxColour(0xEF, 0xEF, 0xF0)));

    wxBoxSizer* root_sizer = new wxBoxSizer(wxVERTICAL);

    auto line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    line_top->SetBackgroundColour(dark_or(wxColour(166, 169, 170), wxColour(80, 80, 86)));
    root_sizer->Add(line_top, 0, wxEXPAND);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer* left_sizer = new wxBoxSizer(wxVERTICAL);
    build_preview_panel(this, left_sizer);
    main_sizer->Add(left_sizer, 3, wxEXPAND | wxALL, FromDIP(8));

    wxBoxSizer* right_sizer = new wxBoxSizer(wxVERTICAL);
    build_params_panel(this, right_sizer);
    build_mapping_panel(this, right_sizer);
    build_bottom_buttons(right_sizer);
    main_sizer->Add(right_sizer, 2, wxEXPAND | wxALL, FromDIP(8));

    root_sizer->Add(main_sizer, 1, wxEXPAND);

    SetSizer(root_sizer);
    Layout();
    Bind(wxEVT_MOUSEWHEEL, &TextureImportDialog::dismiss_filament_popup_on_wheel, this);

#ifdef __WXMSW__
    wxPanel* size_grip_cover = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    size_grip_cover->SetBackgroundColour(dialog_bg);
    size_grip_cover->SetBackgroundStyle(wxBG_STYLE_COLOUR);

    auto update_size_grip_cover = [this, size_grip_cover]() {
        const int cover_size = FromDIP(20);
        wxSize client_size = GetClientSize();
        size_grip_cover->SetSize(client_size.x - cover_size, client_size.y - cover_size, cover_size, cover_size);
        size_grip_cover->Raise();
    };
    update_size_grip_cover();

    Bind(wxEVT_SIZE, [update_size_grip_cover](wxSizeEvent& e) {
        e.Skip();
        update_size_grip_cover();
    });
#endif
}

void TextureImportDialog::build_preview_panel(wxWindow* parent, wxSizer* sizer)
{
    wxColour preview_bg = dark_or(wxColour(238, 238, 238), wxColour(0x3E, 0x3E, 0x45));
    wxColour preview_bd = dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B));

    wxPanel* preview_container = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    preview_container->SetBackgroundColour(preview_bg);
    preview_container->SetBackgroundStyle(wxBG_STYLE_PAINT);
    preview_container->Bind(wxEVT_PAINT, [preview_bg, preview_bd](wxPaintEvent& e) {
        auto* p = static_cast<wxPanel*>(e.GetEventObject());
        wxAutoBufferedPaintDC dc(p);
        wxSize sz = p->GetClientSize();
        dc.SetBrush(wxBrush(p->GetParent()->GetBackgroundColour()));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(0, 0, sz.x, sz.y);
        dc.SetBrush(wxBrush(preview_bg));
        dc.SetPen(wxPen(preview_bd, 1));
        dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, 4);
    });

    wxBoxSizer* container_sizer = new wxBoxSizer(wxVERTICAL);

    wxGLAttributes canvas_attrs;
    canvas_attrs.PlatformDefaults().RGBA().DoubleBuffer().Depth(24).EndList();
    m_preview_canvas = new TexturePreviewCanvas(preview_container, canvas_attrs);
    container_sizer->Add(m_preview_canvas, 1, wxEXPAND | wxALL, FromDIP(1));

    preview_container->SetSizer(container_sizer);
    sizer->Add(preview_container, 1, wxEXPAND);

    m_tab_panel = new wxPanel(preview_container, wxID_ANY);
    m_tab_panel->SetBackgroundColour(preview_bg);

    m_btn_view_original   = new Button(m_tab_panel, _L("Original"));
    m_btn_view_original->SetId(ID_VIEW_ORIGINAL);
    m_btn_view_multicolor = new Button(m_tab_panel, _L("Multi-Color"));
    m_btn_view_multicolor->SetId(ID_VIEW_MULTICOLOR);
    m_btn_view_filament   = new Button(m_tab_panel, _L("Filament Mapping"));
    m_btn_view_filament->SetId(ID_VIEW_FILAMENT);

    m_btn_view_original->SetCornerRadius(FromDIP(12));
    m_btn_view_original->SetMinSize(wxSize(FromDIP(80), FromDIP(28)));
    m_btn_view_multicolor->SetCornerRadius(FromDIP(12));
    m_btn_view_multicolor->SetMinSize(wxSize(FromDIP(90), FromDIP(28)));
    m_btn_view_filament->SetCornerRadius(FromDIP(12));
    m_btn_view_filament->SetMinSize(wxSize(FromDIP(100), FromDIP(28)));

    wxBoxSizer* tab_sizer = new wxBoxSizer(wxHORIZONTAL);
    tab_sizer->Add(m_btn_view_original,   0, wxRIGHT, FromDIP(2));
    tab_sizer->Add(m_btn_view_multicolor, 0, wxRIGHT, FromDIP(2));
    tab_sizer->Add(m_btn_view_filament,   0);
    m_tab_panel->SetSizer(tab_sizer);
    m_tab_panel->Fit();

    m_btn_view_multicolor->Hide();
    m_btn_view_filament->Hide();

    m_btn_view_original->Bind(wxEVT_BUTTON,   &TextureImportDialog::on_view_button_clicked, this);
    m_btn_view_multicolor->Bind(wxEVT_BUTTON,  &TextureImportDialog::on_view_button_clicked, this);
    m_btn_view_filament->Bind(wxEVT_BUTTON,    &TextureImportDialog::on_view_button_clicked, this);

    auto update_preview_overlay_buttons = [this](const wxSize& cs) {
        wxPoint tab_pos(0, FromDIP(8));
        wxSize ts;
        if (m_tab_panel) {
            m_tab_panel->Fit();
            ts = m_tab_panel->GetSize();
            tab_pos.x = (cs.x - ts.x) / 2;
            m_tab_panel->SetPosition(tab_pos);
            m_tab_panel->Raise();
        }
    };

    preview_container->Bind(wxEVT_SIZE, [update_preview_overlay_buttons](wxSizeEvent& e) {
        e.Skip();
        update_preview_overlay_buttons(e.GetSize());
    });
    update_preview_overlay_buttons(preview_container->GetClientSize());

    highlight_view_button(0);
}

void TextureImportDialog::build_params_panel(wxWindow* parent, wxSizer* sizer)
{
    wxColour label_fg = dark_or(wxColour(50, 58, 61), wxColour(0xEF, 0xEF, 0xF0));

    wxBoxSizer* color_header_sizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* lbl_colors = new wxStaticText(parent, wxID_ANY, _L("Color Count"));
    lbl_colors->SetForegroundColour(label_fg);
    lbl_colors->SetFont(lbl_colors->GetFont().Bold());
    color_header_sizer->Add(lbl_colors, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    m_btn_color_4    = new Button(parent, "4");
    m_btn_color_4->SetId(ID_COLOR_4);
    m_btn_color_8    = new Button(parent, "8");
    m_btn_color_8->SetId(ID_COLOR_8);
    m_btn_color_16   = new Button(parent, "16");
    m_btn_color_16->SetId(ID_COLOR_16);
    m_btn_color_auto = new Button(parent, _L("Auto"));
    m_btn_color_auto->SetId(ID_COLOR_AUTO);

    {
        StateColor preset_bg(
            std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed | StateColor::Checked),
            std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered | StateColor::Checked),
            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Checked),
            std::pair<wxColour, int>(dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B)), StateColor::Pressed),
            std::pair<wxColour, int>(dark_or(wxColour(238, 238, 238), wxColour(0x4C, 0x4C, 0x55)), StateColor::Hovered),
            std::pair<wxColour, int>(dark_or(wxColour(255, 255, 255), wxColour(0x2D, 0x2D, 0x31)), StateColor::Normal));
        StateColor preset_bd(
            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Checked),
            std::pair<wxColour, int>(dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B)), StateColor::Normal));
        StateColor preset_text(
            std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Checked),
            std::pair<wxColour, int>(dark_or(wxColour(50, 58, 61), wxColour(0xEF, 0xEF, 0xF0)), StateColor::Normal));

        for (auto* btn : {m_btn_color_4, m_btn_color_8, m_btn_color_16}) {
            btn->SetCornerRadius(FromDIP(12));
            btn->SetMinSize(wxSize(FromDIP(28), FromDIP(28)));
            btn->SetBackgroundColor(preset_bg);
            btn->SetBorderColor(preset_bd);
            btn->SetTextColor(preset_text);
        }
    }

    update_color_count_preset_buttons();

    color_header_sizer->Add(m_btn_color_4,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(2));
    color_header_sizer->Add(m_btn_color_8,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(2));
    color_header_sizer->Add(m_btn_color_16, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(color_header_sizer, 0, wxBOTTOM, FromDIP(4));

    wxBoxSizer* color_slider_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_color_slider = new GreenSlider(parent, m_param_color_count, 1, (int)max_filament_count());
    m_color_spin = new SpinInput(parent, wxString::Format("%d", m_param_color_count),
                                 wxEmptyString, wxDefaultPosition,
                                 wxSize(FromDIP(60), FromDIP(28)),
                                 wxTE_PROCESS_ENTER, 1, (int)max_filament_count(), m_param_color_count);

    m_color_slider->Bind(wxEVT_SLIDER, &TextureImportDialog::on_color_slider_changed, this);
    m_color_spin->Bind(wxEVT_SPINCTRL, &TextureImportDialog::on_color_spin_changed, this);
    m_color_spin->Bind(EVT_SPINCTRL_TEXT, &TextureImportDialog::on_color_spin_text_changed, this);

    color_slider_sizer->Add(m_color_slider, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    color_slider_sizer->Add(m_color_spin, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(color_slider_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    wxStaticText* lbl_smooth = new wxStaticText(parent, wxID_ANY, _L("Smooth Level"));
    lbl_smooth->SetForegroundColour(label_fg);
    lbl_smooth->SetFont(lbl_smooth->GetFont().Bold());
    sizer->Add(lbl_smooth, 0, wxBOTTOM, FromDIP(4));

    wxBoxSizer* smooth_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_smooth_slider = new GreenSlider(parent, m_param_smooth, 0, 10);
    m_smooth_spin = new SpinInput(parent, wxString::Format("%d", m_param_smooth),
                                  wxEmptyString, wxDefaultPosition,
                                  wxSize(FromDIP(60), FromDIP(28)),
                                  wxTE_PROCESS_ENTER, 0, 10, m_param_smooth);

    m_smooth_slider->Bind(wxEVT_SLIDER, &TextureImportDialog::on_smooth_slider_changed, this);
    m_smooth_spin->Bind(wxEVT_SPINCTRL, &TextureImportDialog::on_smooth_spin_changed, this);
    m_smooth_spin->Bind(EVT_SPINCTRL_TEXT, &TextureImportDialog::on_smooth_spin_text_changed, this);

    smooth_sizer->Add(m_smooth_slider, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    smooth_sizer->Add(m_smooth_spin, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(smooth_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

    m_btn_apply = new Button(parent, _L("Apply"));
    m_btn_apply->SetId(ID_BTN_APPLY);

    {
        StateColor btn_bg_white(
            std::pair<wxColour, int>(dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B)), StateColor::Pressed),
            std::pair<wxColour, int>(dark_or(wxColour(238, 238, 238), wxColour(0x4C, 0x4C, 0x55)), StateColor::Hovered),
            std::pair<wxColour, int>(dark_or(wxColour(255, 255, 255), wxColour(0x2D, 0x2D, 0x31)), StateColor::Normal));
        StateColor btn_bd_green(
            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
        StateColor btn_text_green(
            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

        m_btn_color_auto->SetCornerRadius(FromDIP(12));
        m_btn_color_auto->SetMinSize(wxSize(FromDIP(60), FromDIP(28)));
        m_btn_color_auto->SetBackgroundColor(btn_bg_white);
        m_btn_color_auto->SetBorderColor(btn_bd_green);
        m_btn_color_auto->SetTextColor(btn_text_green);

        m_btn_apply->SetCornerRadius(FromDIP(12));
        m_btn_apply->SetMinSize(wxSize(FromDIP(60), FromDIP(28)));
        m_btn_apply->SetBackgroundColor(btn_bg_white);
        m_btn_apply->SetBorderColor(btn_bd_green);
        m_btn_apply->SetTextColor(btn_text_green);
    }

    m_btn_color_auto->SetToolTip(_L("Automatically determine the optimal color count only and recompute filament mapping"));
    m_btn_apply->SetToolTip(_L("Convert texture to painting using the specified color count and smooth level"));

    wxBoxSizer* apply_sizer = new wxBoxSizer(wxHORIZONTAL);
    apply_sizer->Add(m_btn_color_auto, 0, wxRIGHT, FromDIP(4));
    apply_sizer->Add(m_btn_apply, 0);
    sizer->Add(apply_sizer, 0, wxALIGN_RIGHT | wxBOTTOM, FromDIP(8));

    m_hint_label = new wxStaticText(parent, wxID_ANY,
        _L("Reminder: parameters changed, click Apply to take effect"));
    m_hint_label->SetForegroundColour(wxColour(0xFF, 0x6F, 0x00));
    m_hint_label->SetFont(texture_import_section_title_font(parent));
    m_hint_label->Hide();
    sizer->Add(m_hint_label, 0, wxBOTTOM, FromDIP(4));

    auto* mapping_separator = new StaticLine(parent);
    mapping_separator->SetLineColour(texture_import_separator_colour());
    sizer->Add(mapping_separator, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
}

void TextureImportDialog::build_mapping_panel(wxWindow* parent, wxSizer* sizer)
{
    wxColour secondary_fg = dark_or(wxColour(107, 107, 107), wxColour(0x81, 0x81, 0x83));

    wxBoxSizer* header_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxStaticText* lbl_mapping = new wxStaticText(parent, wxID_ANY, _L("Filament Mapping"));
    lbl_mapping->SetForegroundColour(secondary_fg);
    lbl_mapping->SetFont(texture_import_section_title_font(parent));
    header_sizer->Add(lbl_mapping, 0, wxALIGN_CENTER_VERTICAL);

    header_sizer->AddStretchSpacer();

    m_auto_merge_cb = new wxCheckBox(parent, wxID_ANY, _L("Auto-merge same filament"));
    m_auto_merge_cb->SetToolTip(_L("Automatically merge identical filaments into existing filaments in the project"));
    m_auto_merge_cb->SetForegroundColour(secondary_fg);
    m_auto_merge_cb->SetValue(true);
    m_auto_merge_cb->Bind(wxEVT_CHECKBOX, &TextureImportDialog::on_auto_merge_toggled, this);
    header_sizer->Add(m_auto_merge_cb, 0, wxALIGN_CENTER_VERTICAL);

    sizer->Add(header_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    m_mapping_scroll = new wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition,
                                             wxSize(-1, FromDIP(300)));
    m_mapping_scroll->SetScrollRate(0, FromDIP(10));
    m_mapping_scroll->SetBackgroundColour(dark_or(wxColour(255, 255, 255), wxColour(0x2D, 0x2D, 0x31)));
    m_mapping_scroll->Bind(wxEVT_MOUSEWHEEL, &TextureImportDialog::dismiss_filament_popup_on_wheel, this);

    m_mapping_sizer = new wxBoxSizer(wxVERTICAL);
    m_mapping_scroll->SetSizer(m_mapping_sizer);

    sizer->Add(m_mapping_scroll, 1, wxEXPAND | wxBOTTOM, FromDIP(8));
}

void TextureImportDialog::build_bottom_buttons(wxSizer* sizer)
{
    wxBoxSizer* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_btn_skip = new Button(this, _L("Skip Matching"));
    m_btn_skip->SetId(ID_BTN_SKIP);
    m_btn_skip->SetToolTip(_L("Skip filament mapping and import as a single-color model"));
    m_btn_skip->SetCornerRadius(FromDIP(20));
    m_btn_skip->SetMinSize(wxSize(FromDIP(136), FromDIP(40)));
    {
        StateColor skip_bg(
            std::pair<wxColour, int>(dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B)), StateColor::Pressed),
            std::pair<wxColour, int>(dark_or(wxColour(238, 238, 238), wxColour(0x4C, 0x4C, 0x55)), StateColor::Hovered),
            std::pair<wxColour, int>(dark_or(wxColour(255, 255, 255), wxColour(0x2D, 0x2D, 0x31)), StateColor::Normal));
        StateColor skip_bd(
            std::pair<wxColour, int>(dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B)), StateColor::Normal));
        StateColor skip_text(
            std::pair<wxColour, int>(dark_or(wxColour(107, 107, 107), wxColour(0xB3, 0xB3, 0xB5)), StateColor::Normal));
        m_btn_skip->SetBackgroundColor(skip_bg);
        m_btn_skip->SetBorderColor(skip_bd);
        m_btn_skip->SetTextColor(skip_text);
    }

    m_btn_ok = new Button(this, _L("Confirm"));
    m_btn_ok->SetId(wxID_OK);
    m_btn_ok->SetCornerRadius(FromDIP(20));
    m_btn_ok->SetMinSize(wxSize(FromDIP(156), FromDIP(40)));
    {
        StateColor ok_bg(
            std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
            std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
        StateColor ok_bd(
            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
        StateColor ok_text(
            std::pair<wxColour, int>(wxColour("#FFFFFE"), StateColor::Normal));
        m_btn_ok->SetBackgroundColor(ok_bg);
        m_btn_ok->SetBorderColor(ok_bd);
        m_btn_ok->SetTextColor(ok_text);
    }

    btn_sizer->AddStretchSpacer();
    btn_sizer->Add(m_btn_skip, 0, wxRIGHT, FromDIP(16));
    btn_sizer->Add(m_btn_ok, 0);

    sizer->Add(btn_sizer, 0, wxEXPAND | wxTOP, FromDIP(8));
}

// ---- State machine ----

void TextureImportDialog::set_state(TextureImportState new_state)
{
    m_state = new_state;
    update_ui_for_state();
}

void TextureImportDialog::update_ui_for_state()
{
    bool computing = (m_state == TextureImportState::Computing);
    bool ready     = (m_state == TextureImportState::Ready);
    bool idle      = (m_state == TextureImportState::Idle);
    bool valid     = has_valid_result();

    m_color_slider->Enable(!computing);
    m_color_spin->Enable(!computing);
    m_smooth_slider->Enable(!computing);
    m_smooth_spin->Enable(!computing);
    m_btn_apply->Enable(!computing);
    m_btn_color_4->Enable(!computing);
    m_btn_color_8->Enable(!computing);
    m_btn_color_16->Enable(!computing);
    m_btn_color_auto->Enable(!computing);

    m_btn_ok->Enable(ready && valid);
    m_btn_skip->Enable(ready || idle);

    m_auto_merge_cb->Enable(!computing);

    m_preview_canvas->set_computing_overlay(computing);

    if (ready && valid && is_params_dirty()) {
        m_btn_ok->Enable(true);
        StateColor gray_bg(
            std::pair<wxColour, int>(dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B)), StateColor::Normal));
        StateColor gray_bd(
            std::pair<wxColour, int>(dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B)), StateColor::Normal));
        StateColor gray_text(
            std::pair<wxColour, int>(dark_or(wxColour(107, 107, 107), wxColour(0xB3, 0xB3, 0xB5)), StateColor::Normal));
        m_btn_ok->SetBackgroundColor(gray_bg);
        m_btn_ok->SetBorderColor(gray_bd);
        m_btn_ok->SetTextColor(gray_text);
        m_btn_ok->SetToolTip(_L("Reminder: parameters changed, click Apply to take effect"));
        if (m_hint_label) m_hint_label->Show();
    } else if (ready && valid) {
        StateColor ok_bg(
            std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
            std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
        StateColor ok_bd(
            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
        StateColor ok_text(
            std::pair<wxColour, int>(wxColour("#FFFFFE"), StateColor::Normal));
        m_btn_ok->SetBackgroundColor(ok_bg);
        m_btn_ok->SetBorderColor(ok_bd);
        m_btn_ok->SetTextColor(ok_text);
        m_btn_ok->UnsetToolTip();
        if (m_hint_label) m_hint_label->Hide();
    } else {
        if (m_hint_label) m_hint_label->Hide();
    }

    m_btn_ok->Refresh();
    Layout();
}

// ---- Async computation ----

void TextureImportDialog::start_computation(bool auto_color, bool initial)
{
    cancel_computation();

    m_cancel_flag = false;
    m_current_computation_initial = initial;
    m_current_computation_auto_color = auto_color;
    if (initial) {
        m_initial_computation_pending = true;
        m_initial_computation_cancelled = false;
        m_initial_computation_failed = false;
    }
    set_state(TextureImportState::Computing);

    bool silent_initial = initial && static_cast<bool>(m_initial_cancel_callback);
    if (!silent_initial) {
        m_progress_dlg = new ProgressDialog(
            _L("Processing"), _L("Computing texture colors..."),
            100, initial ? GetParent() : this, wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_AUTO_HIDE);
    }

    Slic3r::TexturePaintingSettings settings;
    settings.target_colors_num = auto_color ? 0 : (size_t)m_param_color_count;
    settings.smooth_weight     = m_param_smooth / 10.0;
    settings.mesh_repair_decision = m_mesh_repair_decision;
#ifdef HAS_WIN10SDK
    settings.mesh_repair_callback = Slic3r::fix_mesh_by_win10_sdk;
#endif

    Slic3r::TexturedMesh mesh_copy = m_textured_mesh;
    wxEvtHandler* handler = this;

    m_worker = std::make_unique<std::thread>([this, settings, mesh_copy, handler]() {
        Slic3r::PaintedMesh result;

        auto progress_cb = [handler](int percent, const char*) {
            auto* evt = new wxCommandEvent(EVT_TEXTURE_COMPUTE_PROGRESS);
            evt->SetInt(percent);
            wxQueueEvent(handler, evt);
        };

        auto cancel_cb = [this]() -> bool {
            return m_cancel_flag.load();
        };

        auto worker_settings = settings;
        bool mesh_repair_decision_required = false;
        worker_settings.mesh_repair_decision_required = &mesh_repair_decision_required;
        bool ok = Slic3r::texture_to_painting(mesh_copy, result, worker_settings, progress_cb, cancel_cb);

        if (m_cancel_flag.load()) {
            wxQueueEvent(handler, new wxCommandEvent(EVT_TEXTURE_COMPUTE_ERROR));
            return;
        }

        if (!ok && mesh_repair_decision_required) {
            wxQueueEvent(handler, new wxCommandEvent(EVT_TEXTURE_MESH_REPAIR_DECISION));
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_result_mutex);
            m_pending_result = std::move(result);
        }

        if (ok) {
            wxQueueEvent(handler, new wxCommandEvent(EVT_TEXTURE_COMPUTE_DONE));
        } else {
            wxQueueEvent(handler, new wxCommandEvent(EVT_TEXTURE_COMPUTE_ERROR));
        }
    });
}

void TextureImportDialog::cancel_computation()
{
    m_cancel_flag = true;
    if (m_worker && m_worker->joinable())
        m_worker->join();
    m_worker.reset();

    if (m_progress_dlg) {
        m_progress_dlg->Destroy();
        m_progress_dlg = nullptr;
    }

    if (m_current_computation_initial) {
        m_initial_computation_cancelled = true;
        m_initial_computation_pending = false;
        m_current_computation_initial = false;
    }
}

void TextureImportDialog::on_computation_progress(wxCommandEvent& evt)
{
    if (m_progress_dlg) {
        if (!m_progress_dlg->Update(evt.GetInt()))
            m_cancel_flag = true;
    } else if (m_current_computation_initial && m_initial_progress_callback) {
        if (!m_initial_progress_callback(evt.GetInt()))
            m_cancel_flag = true;
    }
}

void TextureImportDialog::on_computation_complete(wxCommandEvent&)
{
    bool initial = m_current_computation_initial;

    if (m_progress_dlg) {
        m_progress_dlg->Destroy();
        m_progress_dlg = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(m_result_mutex);
        m_painted = std::move(m_pending_result);
    }

    int actual_colors = (int)m_painted.cluster_colors.size();
    if (actual_colors >= 2 && actual_colors <= (int)max_filament_count()) {
        set_color_count_value(actual_colors, true);
    }

    m_preview_canvas->set_painted_mesh_data(m_painted.vertices, m_painted.indices);
    m_preview_canvas->set_face_colors(m_painted.face_colors);

    do_auto_match();
    compact_used_virtual_filaments();
    update_filament_color_map();
    rebuild_mapping_rows();

    m_applied_color_count = m_param_color_count;
    m_applied_smooth      = m_param_smooth;

    set_state(TextureImportState::Ready);

    m_btn_view_multicolor->Show();
    m_btn_view_filament->Show();
    if (m_tab_panel) {
        m_tab_panel->GetSizer()->Layout();
        m_tab_panel->Fit();
        wxWindow* container = m_tab_panel->GetParent();
        if (container) {
            wxSize cs = container->GetClientSize();
            wxSize ts = m_tab_panel->GetSize();
            m_tab_panel->SetPosition(wxPoint((cs.x - ts.x) / 2, FromDIP(8)));
        }
    }
    GetSizer()->Layout();

    m_preview_canvas->set_render_mode(TexturePreviewCanvas::RenderMode::FilamentMap);
    highlight_view_button(2);

    if (initial) {
        m_initial_computation_pending = false;
        m_current_computation_initial = false;
    }
}

void TextureImportDialog::on_computation_error(wxCommandEvent&)
{
    bool initial = m_current_computation_initial;

    if (m_progress_dlg) {
        m_progress_dlg->Destroy();
        m_progress_dlg = nullptr;
    }

    if (m_cancel_flag.load()) {
        if (initial) {
            m_initial_computation_cancelled = true;
            m_initial_computation_pending = false;
            m_current_computation_initial = false;
            return;
        }
        if (has_valid_result()) {
            if (m_applied_color_count >= 0) {
                m_param_color_count = m_applied_color_count;
                m_color_slider->SetValue(m_param_color_count);
                m_color_spin->SetValue(m_param_color_count);
                update_color_count_preset_buttons();
            }
            if (m_applied_smooth >= 0) {
                m_param_smooth = m_applied_smooth;
                m_smooth_slider->SetValue(m_param_smooth);
                m_smooth_spin->SetValue(m_param_smooth);
            }
            set_state(TextureImportState::Ready);
            return;
        }
        set_state(TextureImportState::Idle);
        return;
    }

    if (initial) {
        m_initial_computation_failed = true;
        m_initial_computation_pending = false;
        m_current_computation_initial = false;
        m_fallback_to_geometry_only = true;
        return;
    }

    set_state(TextureImportState::Error);
    Slic3r::GUI::MessageDialog dlg(initial ? GetParent() : this,
        _L("Computation failed. Please adjust parameters and retry."),
        _L("Error"), wxOK | wxICON_ERROR);
    dlg.ShowModal();
}

void TextureImportDialog::on_mesh_repair_decision_required(wxCommandEvent&)
{
    bool initial = m_current_computation_initial;
    bool auto_color = m_current_computation_auto_color;

    if (m_progress_dlg) {
        m_progress_dlg->Destroy();
        m_progress_dlg = nullptr;
    }

#ifdef HAS_WIN10SDK
    Slic3r::GUI::MessageDialog dlg(initial ? GetParent() : this,
        _L("The mesh has non-manifold geometry or open boundaries. You can import it as-is or repair it with Windows 3D repair service before importing."),
        _L("Mesh repair"), wxYES_NO | wxICON_WARNING | wxYES_DEFAULT);
    dlg.SetButtonLabel(wxID_YES, _L("Import without repair"));
    dlg.SetButtonLabel(wxID_NO, _L("Repair and import"), true);
    StateColor primary_bg(
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor primary_bd(
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor primary_text(
        std::pair<wxColour, int>(wxColour("#FFFFFE"), StateColor::Normal));
    StateColor secondary_bg(
        std::pair<wxColour, int>(wxColour("#CECECE"), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour("#EEEEEE"), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));
    StateColor secondary_bd(
        std::pair<wxColour, int>(texture_import_gray9000(), StateColor::Normal));
    StateColor secondary_text(
        std::pair<wxColour, int>(texture_import_gray9000(), StateColor::Normal));
    if (auto* yes_btn = dynamic_cast<Button*>(dlg.FindWindow(wxID_YES))) {
        yes_btn->SetMinSize(wxSize(FromDIP(180), FromDIP(24)));
        yes_btn->SetBackgroundColor(secondary_bg);
        yes_btn->SetBorderColor(secondary_bd);
        yes_btn->SetTextColor(secondary_text);
    }
    if (auto* no_btn = dynamic_cast<Button*>(dlg.FindWindow(wxID_NO))) {
        no_btn->SetMinSize(wxSize(FromDIP(160), FromDIP(24)));
        no_btn->SetBackgroundColor(primary_bg);
        no_btn->SetBorderColor(primary_bd);
        no_btn->SetTextColor(primary_text);
    }
    dlg.Layout();
    dlg.Fit();
    dlg.CenterOnParent();
    int ret = dlg.ShowModal();
    m_mesh_repair_decision = (ret == wxID_NO)
        ? Slic3r::TexturePaintingSettings::MeshRepairDecision::RepairAndImport
        : Slic3r::TexturePaintingSettings::MeshRepairDecision::ImportWithoutRepair;
#else
    Slic3r::GUI::MessageDialog dlg(initial ? GetParent() : this,
        _L("Please note that the mesh has non-manifold geometry or open boundaries."),
        _L("Mesh issue"), wxOK | wxCANCEL | wxICON_WARNING | wxOK_DEFAULT);
    dlg.SetButtonLabel(wxID_OK, _L("Continue"), true);
    dlg.SetButtonLabel(wxID_CANCEL, _L("Cancel"));
    int ret = dlg.ShowModal();
    if (ret != wxID_OK) {
        m_cancel_flag = true;
        if (initial) {
            m_initial_computation_cancelled = true;
            m_initial_computation_pending = false;
            m_current_computation_initial = false;
        } else if (has_valid_result()) {
            set_state(TextureImportState::Ready);
        } else {
            set_state(TextureImportState::Idle);
        }
        return;
    }
    m_mesh_repair_decision = Slic3r::TexturePaintingSettings::MeshRepairDecision::ImportWithoutRepair;
#endif

    start_computation(auto_color, initial);
}

// ---- Mapping ----

void TextureImportDialog::update_filament_color_map()
{
    std::map<std::array<std::size_t, 3>, std::array<float, 3>> color_map;
    for (const auto& m : m_current_matches) {
        if (m.filament_index >= 0 && m.filament_index < (int)m_filament_colors_rgba.size()) {
            color_map[m.cluster_color] = {
                m_filament_colors_rgba[m.filament_index][0],
                m_filament_colors_rgba[m.filament_index][1],
                m_filament_colors_rgba[m.filament_index][2]
            };
        }
    }
    m_preview_canvas->set_filament_color_map(color_map);
}

size_t TextureImportDialog::max_filament_count() const
{
    return static_cast<size_t>(EnforcerBlockerType::ExtruderMax);
}

bool TextureImportDialog::can_add_virtual_filament() const
{
    return m_filament_colors_rgba.size() < max_filament_count();
}

void TextureImportDialog::show_filament_limit_warning_once()
{
    if (m_filament_limit_warning_shown)
        return;

    m_filament_limit_warning_shown = true;
    Slic3r::GUI::MessageDialog dlg(this,
        wxString::Format(
            _L("The project supports up to %d filaments. Extra filaments will be discarded."),
            (int)max_filament_count()),
        _L("Warning"), wxOK | wxICON_WARNING);
    dlg.ShowModal();
}

int TextureImportDialog::find_closest_filament_index(const std::array<std::size_t, 3>& color) const
{
    int best_idx = -1;
    double best_delta = std::numeric_limits<double>::max();
    const size_t filament_count = std::min(m_filament_colors_rgba.size(), max_filament_count());
    for (size_t i = 0; i < filament_count; ++i) {
        const double delta = Slic3r::compute_delta_e(color, m_filament_colors_rgba[i]);
        if (delta < best_delta) {
            best_delta = delta;
            best_idx = (int)i;
        }
    }
    return best_idx;
}

int TextureImportDialog::add_virtual_filament(const std::array<float, 4>& rgba, const std::string& hex,
                                              const std::string& preset_name)
{
    if (!can_add_virtual_filament()) {
        if (m_allow_filament_limit_warning_once)
            show_filament_limit_warning_once();
        return -1;
    }

    m_filament_colors_rgba.push_back(rgba);
    m_filament_color_strs.push_back(hex);
    m_filament_names.push_back(DEFAULT_VIRTUAL_FILAMENT_NAME);
    m_new_filament_colors.push_back(rgba);
    m_new_filament_preset_names.push_back(preset_name.empty() ? m_default_virtual_filament_preset_name : preset_name);
    return (int)m_filament_colors_rgba.size() - 1;
}

void TextureImportDialog::compact_used_virtual_filaments()
{
    if (m_current_matches.empty())
        return;

    const std::vector<std::array<float, 4>> old_colors = m_filament_colors_rgba;
    const std::vector<std::string> old_color_strs = m_filament_color_strs;
    const std::vector<std::string> old_names = m_filament_names;
    const std::vector<std::string> old_preset_names = m_new_filament_preset_names;

    std::set<int> used_virtual_indices;
    for (const auto& m : m_current_matches) {
        if (m.filament_index >= (int)m_existing_filament_count &&
            m.filament_index < (int)old_colors.size()) {
            used_virtual_indices.insert(m.filament_index);
        }
    }

    std::vector<std::array<float, 4>> compact_colors;
    std::vector<std::string> compact_color_strs;
    std::vector<std::string> compact_names;
    compact_colors.reserve(m_existing_filament_count + used_virtual_indices.size());
    compact_color_strs.reserve(m_existing_filament_count + used_virtual_indices.size());
    compact_names.reserve(m_existing_filament_count + used_virtual_indices.size());

    const size_t existing_count = std::min(m_existing_filament_count, old_colors.size());
    for (size_t i = 0; i < existing_count; ++i) {
        compact_colors.push_back(old_colors[i]);
        compact_color_strs.push_back(i < old_color_strs.size() ? old_color_strs[i] : "");
        compact_names.push_back(i < old_names.size() ? old_names[i] : "Filament " + std::to_string(i + 1));
    }

    std::map<int, int> old_to_new;
    std::vector<std::array<float, 4>> compact_new_colors;
    std::vector<std::string> compact_new_preset_names;
    compact_new_colors.reserve(used_virtual_indices.size());
    compact_new_preset_names.reserve(used_virtual_indices.size());

    for (int old_idx : used_virtual_indices) {
        old_to_new[old_idx] = (int)compact_colors.size();
        compact_colors.push_back(old_colors[old_idx]);
        compact_color_strs.push_back(old_idx < (int)old_color_strs.size() ? old_color_strs[old_idx] : "");
        compact_names.push_back(old_idx < (int)old_names.size() ? old_names[old_idx] : DEFAULT_VIRTUAL_FILAMENT_NAME);
        compact_new_colors.push_back(old_colors[old_idx]);

        size_t vi = (size_t)(old_idx - (int)m_existing_filament_count);
        compact_new_preset_names.push_back(vi < old_preset_names.size()
            ? old_preset_names[vi]
            : m_default_virtual_filament_preset_name);
    }

    m_filament_colors_rgba = std::move(compact_colors);
    m_filament_color_strs = std::move(compact_color_strs);
    m_filament_names = std::move(compact_names);
    m_new_filament_colors = std::move(compact_new_colors);
    m_new_filament_preset_names = std::move(compact_new_preset_names);

    for (auto& m : m_current_matches) {
        auto it = old_to_new.find(m.filament_index);
        if (it != old_to_new.end()) {
            m.filament_index = it->second;
        } else if (m.filament_index >= (int)m_existing_filament_count) {
            m.filament_index = find_closest_filament_index(m.cluster_color);
        }

        if (m.filament_index >= 0 && m.filament_index < (int)m_filament_colors_rgba.size()) {
            m.filament_color = m_filament_colors_rgba[m.filament_index];
            m.delta_e = Slic3r::compute_delta_e(m.cluster_color, m.filament_color);
            if (m.filament_index >= (int)m_existing_filament_count)
                m.delta_e = 0.0;
        }
    }
}

void TextureImportDialog::dismiss_filament_popup()
{
    if (!m_filament_popup) {
        m_filament_popup_row = -1;
        return;
    }

    FilamentSelectPopup* popup = m_filament_popup;
    m_filament_popup = nullptr;
    m_filament_popup_row = -1;
    if (popup->IsShown())
        popup->Dismiss();
    else
        popup->Destroy();
}

void TextureImportDialog::dismiss_filament_popup_on_wheel(wxMouseEvent& evt)
{
    dismiss_filament_popup();
    evt.Skip();
}

void TextureImportDialog::show_filament_popup(size_t row_index)
{
    if (row_index >= m_mapping_rows.size()) return;

    if (m_skip_next_filament_popup_row == (int)row_index) {
        m_skip_next_filament_popup_row = -1;
        return;
    }

    if (m_filament_popup && m_filament_popup->IsShown()) {
        if (m_filament_popup_row == (int)row_index) {
            dismiss_filament_popup();
            return;
        }
        dismiss_filament_popup();
    }

    auto on_select = [this, row_index](int idx) {
        if (row_index >= m_mapping_rows.size()) return;
        m_mapping_rows[row_index].target_filament_idx = idx;
        if (row_index < m_current_matches.size())
            m_current_matches[row_index].filament_index = idx;
        if (m_mapping_rows[row_index].target_panel) {
            wxString label = (idx >= 0 && idx < (int)m_filament_names.size())
                ? filament_name_to_wx_string(m_filament_names[idx])
                : wxString::Format("Filament %d", idx + 1);
            m_mapping_rows[row_index].target_panel->SetToolTip(label);
            m_mapping_rows[row_index].target_panel->Refresh();
        }
        update_filament_color_map();
    };

    auto on_add_filament = [this, row_index](wxColour clr) {
        std::array<float, 4> rgba = {clr.Red() / 255.f, clr.Green() / 255.f,
                                     clr.Blue() / 255.f, 1.0f};
        std::string hex = wxString::Format("#%02X%02X%02X",
            clr.Red(), clr.Green(), clr.Blue()).ToStdString();
        int new_idx = add_virtual_filament(rgba, hex);
        if (new_idx < 0)
            return;

        if (row_index < m_mapping_rows.size()) {
            m_mapping_rows[row_index].target_filament_idx = new_idx;
            if (row_index < m_current_matches.size())
                m_current_matches[row_index].filament_index = new_idx;
        }
        rebuild_mapping_rows();
        update_filament_color_map();
    };

    wxPanel* tp = m_mapping_rows[row_index].target_panel;
    if (!tp) return;

    auto on_close = [this, row_index](bool closed_by_action) {
        if (m_filament_popup_row == (int)row_index) {
            m_filament_popup = nullptr;
            m_filament_popup_row = -1;
        }
        if (!closed_by_action) {
            m_skip_next_filament_popup_row = (int)row_index;
            CallAfter([this, row_index]() {
                if (m_skip_next_filament_popup_row == (int)row_index)
                    m_skip_next_filament_popup_row = -1;
            });
        }
    };

    auto* popup = new FilamentSelectPopup(
        this, m_filament_colors_rgba, m_filament_names,
        m_existing_filament_count, tp->GetSize().x, tp, on_select, on_add_filament,
        [this]() { return can_add_virtual_filament(); },
        on_close);

    wxPoint pos = tp->ClientToScreen(wxPoint(0, tp->GetSize().y));
    wxRect display_rect;
    int display_idx = wxDisplay::GetFromPoint(pos);
    if (display_idx != wxNOT_FOUND)
        display_rect = wxDisplay(display_idx).GetClientArea();
    else
        display_rect = wxDisplay().GetClientArea();
    pos.x = std::clamp(pos.x, display_rect.GetLeft(),
                       std::max(display_rect.GetLeft(), display_rect.GetRight() - popup->GetSize().x));
    popup->Position(pos, wxSize(0, 0));
    popup->Bind(wxEVT_DESTROY, [this, popup](wxWindowDestroyEvent& e) {
        e.Skip();
        if (m_filament_popup == popup) {
            m_filament_popup = nullptr;
            m_filament_popup_row = -1;
        }
    });
    m_filament_popup = popup;
    m_filament_popup_row = (int)row_index;
    popup->Popup();
}

void TextureImportDialog::do_auto_match()
{
    if (m_painted.cluster_colors.empty()) return;

    const auto previous_matches = m_current_matches;

    std::map<std::array<std::size_t, 3>, int> previous_virtual_by_cluster;
    for (const auto& match : previous_matches) {
        if (match.filament_index >= (int)m_existing_filament_count &&
            match.filament_index < (int)m_filament_colors_rgba.size()) {
            previous_virtual_by_cluster[match.cluster_color] = match.filament_index;
        }
    }

    auto find_virtual_filament_by_color = [this](const std::array<std::size_t, 3>& color) -> int {
        std::string hex = rgb_to_hex(color).ToStdString();
        for (size_t i = m_existing_filament_count; i < m_filament_color_strs.size(); ++i) {
            if (m_filament_color_strs[i] == hex)
                return (int)i;
        }
        return -1;
    };

    auto get_or_add_virtual_filament = [this, &previous_virtual_by_cluster, &find_virtual_filament_by_color](
        const std::array<std::size_t, 3>& color) -> int {
        auto previous_it = previous_virtual_by_cluster.find(color);
        if (previous_it != previous_virtual_by_cluster.end() &&
            previous_it->second >= (int)m_existing_filament_count &&
            previous_it->second < (int)m_filament_colors_rgba.size()) {
            return previous_it->second;
        }

        int existing_idx = find_virtual_filament_by_color(color);
        if (existing_idx >= 0)
            return existing_idx;

        std::array<float, 4> rgba = {
            color[0] / 255.f,
            color[1] / 255.f,
            color[2] / 255.f,
            1.f
        };
        return add_virtual_filament(rgba, rgb_to_hex(color).ToStdString());
    };

    if (m_auto_merge_cb && m_auto_merge_cb->GetValue()) {
        // Match clusters to closest existing filaments
        std::vector<std::string> names;
        for (size_t i = 0; i < m_existing_filament_count; ++i)
            names.push_back(m_filament_names.size() > i ? m_filament_names[i] : "Filament " + std::to_string(i + 1));

        std::vector<std::array<float, 4>> existing_filament_colors(
            m_filament_colors_rgba.begin(),
            m_filament_colors_rgba.begin() + std::min(m_existing_filament_count, m_filament_colors_rgba.size()));

        m_current_matches = Slic3r::match_clusters_to_filaments(
            m_painted.cluster_colors, existing_filament_colors, names);

        // For clusters with poor match (CIEDE2000 ΔE > 5), create virtual filaments.
        constexpr double NEW_FILAMENT_THRESHOLD = 5.0;
        std::map<std::array<std::size_t, 3>, int> virtual_color_index;

        for (auto& m : m_current_matches) {
            if (m.delta_e <= NEW_FILAMENT_THRESHOLD)
                continue;

            auto it = virtual_color_index.find(m.cluster_color);
            if (it != virtual_color_index.end()) {
                m.filament_index = it->second;
            } else {
                int new_idx = get_or_add_virtual_filament(m.cluster_color);
                if (new_idx >= 0)
                    virtual_color_index[m.cluster_color] = new_idx;
                m.filament_index = new_idx >= 0 ? new_idx : find_closest_filament_index(m.cluster_color);
            }
            if (m.filament_index >= 0 && m.filament_index < (int)m_filament_colors_rgba.size()) {
                m.filament_color = m_filament_colors_rgba[m.filament_index];
                m.delta_e = Slic3r::compute_delta_e(m.cluster_color, m.filament_color);
                if (m.filament_index >= (int)m_existing_filament_count)
                    m.delta_e = 0.0;
            }
        }
    } else {
        // Keep all virtual filaments in this dialog; unused ones are pruned only on OK.
        m_current_matches.clear();
        std::map<std::array<std::size_t, 3>, int> virtual_map;

        for (size_t i = 0; i < m_painted.cluster_colors.size(); ++i) {
            const auto& cc = m_painted.cluster_colors[i];
            Slic3r::FilamentMatch fm;
            fm.cluster_index = (int)i;
            fm.cluster_color = cc;

            auto it = virtual_map.find(cc);
            if (it != virtual_map.end()) {
                fm.filament_index = it->second;
            } else {
                int idx = get_or_add_virtual_filament(cc);
                if (idx >= 0)
                    virtual_map[cc] = idx;
                fm.filament_index = idx >= 0 ? idx : find_closest_filament_index(cc);
            }
            if (fm.filament_index >= 0 && fm.filament_index < (int)m_filament_colors_rgba.size()) {
                fm.filament_color = m_filament_colors_rgba[fm.filament_index];
                fm.delta_e = Slic3r::compute_delta_e(fm.cluster_color, fm.filament_color);
                if (fm.filament_index >= (int)m_existing_filament_count)
                    fm.delta_e = 0.0;
            }
            m_current_matches.push_back(fm);
        }
    }

    update_filament_color_map();
}

void TextureImportDialog::rebuild_mapping_rows()
{
    m_mapping_scroll->Freeze();
    m_mapping_sizer->Clear(true);
    m_mapping_rows.clear();

    if (m_current_matches.empty()) {
        m_mapping_scroll->FitInside();
        m_mapping_scroll->Thaw();
        return;
    }

    auto get_target_wxcolor = [this](int idx) -> wxColour {
        if (idx >= 0 && idx < (int)m_filament_colors_rgba.size()) {
            const auto& c = m_filament_colors_rgba[idx];
            return wxColour((unsigned char)(c[0] * 255.f),
                            (unsigned char)(c[1] * 255.f),
                            (unsigned char)(c[2] * 255.f));
        }
        return wxColour(128, 128, 128);
    };

    auto get_filament_label = [this](int idx) -> wxString {
        if (idx >= 0 && idx < (int)m_filament_names.size())
            return filament_name_to_wx_string(m_filament_names[idx]);
        return wxString::Format("Filament %d", idx + 1);
    };

    const wxColour dash_clr   = dark_or(wxColour(179, 179, 179), wxColour(100, 100, 106));
    const wxColour hex_fg     = texture_import_text_colour();
    const wxColour card_bg    = dark_or(wxColour(235, 235, 235), wxColour(0x3C, 0x3C, 0x42));
    const wxColour card_bd    = dark_or(wxColour(224, 224, 224), wxColour(0x46, 0x46, 0x4C));
    const wxColour name_fg    = texture_import_text_colour();
    const wxColour chev_clr   = dark_or(wxColour(107, 107, 107), wxColour(0xB3, 0xB3, 0xB5));

    m_mapping_rows.resize(m_current_matches.size());
    for (size_t ci = 0; ci < m_current_matches.size(); ++ci) {
        auto& row = m_mapping_rows[ci];
        row.cluster_id = m_current_matches[ci].cluster_index;
        row.source_color = m_current_matches[ci].cluster_color;
        row.source_hex = rgb_to_hex(row.source_color).ToStdString();
        row.target_filament_idx = m_current_matches[ci].filament_index;

        wxColour src_wx_color(
            (unsigned char)row.source_color[0],
            (unsigned char)row.source_color[1],
            (unsigned char)row.source_color[2]);

        // --- Row container ---
        wxPanel* row_panel = new wxPanel(m_mapping_scroll, wxID_ANY);
        row_panel->SetBackgroundColour(m_mapping_scroll->GetBackgroundColour());
        row_panel->Bind(wxEVT_MOUSEWHEEL, &TextureImportDialog::dismiss_filament_popup_on_wheel, this);
        wxBoxSizer* row_sizer = new wxBoxSizer(wxHORIZONTAL);

        // --- Source card (dashed border, circle + hex) ---
        const int src_w = FromDIP(138);
        const int target_min_w = FromDIP(239);
        const int row_h = FromDIP(44);
        row.source_panel = new wxPanel(row_panel, wxID_ANY, wxDefaultPosition, wxSize(src_w, row_h),
                                       wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE);
        row.source_panel->SetMinSize(wxSize(src_w, row_h));
        row.source_panel->SetMaxSize(wxSize(src_w, row_h));
        row.source_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);

        row.source_panel->Bind(wxEVT_PAINT, [this, ci, src_wx_color, dash_clr, hex_fg](wxPaintEvent& e) {
            auto* p = static_cast<wxPanel*>(e.GetEventObject());
            wxAutoBufferedPaintDC dc(p);
            wxSize sz = p->GetClientSize();

            dc.SetBrush(wxBrush(p->GetParent()->GetBackgroundColour()));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, sz.x, sz.y);

            wxPen dash_pen(dash_clr, 1, wxPENSTYLE_SHORT_DASH);
            dc.SetPen(dash_pen);
            dc.SetBrush(wxBrush(p->GetParent()->GetBackgroundColour()));
            int r = p->FromDIP(8);
            dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, r);

            // Color circle 24px
            int cd = p->FromDIP(24);
            int cx = p->FromDIP(10);
            int cy = (sz.y - cd) / 2;
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(src_wx_color));
            dc.DrawEllipse(cx, cy, cd, cd);
            draw_filament_swatch_ellipse_border(dc, src_wx_color, cx, cy, cd, cd);

            if (ci < m_mapping_rows.size()) {
                wxFont hex_font = p->GetFont();
                hex_font.SetPointSize(9);
                dc.SetFont(hex_font);
                dc.SetTextForeground(hex_fg);
                wxString hex_str = wxString::Format("# %s", m_mapping_rows[ci].source_hex.substr(1));
                wxSize tsz = dc.GetTextExtent(hex_str);
                dc.DrawText(hex_str, cx + cd + p->FromDIP(6), (sz.y - tsz.y) / 2);
            }
        });
        row.source_panel->Bind(wxEVT_SIZE, [](wxSizeEvent& e) {
            e.Skip();
            static_cast<wxWindow*>(e.GetEventObject())->Refresh();
        });
        row.source_panel->Bind(wxEVT_MOUSEWHEEL, &TextureImportDialog::dismiss_filament_popup_on_wheel, this);

        row_sizer->Add(row.source_panel, 0, wxEXPAND);

        // --- Arrow panel (dashed arrow) ---
        const int arrow_w = FromDIP(24);
        wxPanel* arrow_panel = new wxPanel(row_panel, wxID_ANY, wxDefaultPosition, wxSize(arrow_w, row_h));
        arrow_panel->SetMinSize(wxSize(arrow_w, row_h));
        arrow_panel->SetMaxSize(wxSize(arrow_w, row_h));
        arrow_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
        arrow_panel->Bind(wxEVT_MOUSEWHEEL, &TextureImportDialog::dismiss_filament_popup_on_wheel, this);
        arrow_panel->Bind(wxEVT_PAINT, [dash_clr](wxPaintEvent& e) {
            auto* p = static_cast<wxPanel*>(e.GetEventObject());
            wxAutoBufferedPaintDC dc(p);
            wxSize sz = p->GetClientSize();

            dc.SetBrush(wxBrush(p->GetParent()->GetBackgroundColour()));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, sz.x, sz.y);

            int mid_y = sz.y / 2;
            int margin = p->FromDIP(2);
            int arrow_tip = sz.x - margin;
            int arrow_start = margin;

            wxPen dash_pen(dash_clr, p->FromDIP(1), wxPENSTYLE_SHORT_DASH);
            dc.SetPen(dash_pen);
            dc.DrawLine(arrow_start, mid_y, arrow_tip - p->FromDIP(4), mid_y);

            int ah = p->FromDIP(4);
            wxPoint tri[3] = {
                {arrow_tip, mid_y},
                {arrow_tip - ah, mid_y - ah / 2},
                {arrow_tip - ah, mid_y + ah / 2}
            };
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(dash_clr));
            dc.DrawPolygon(3, tri);
        });

        row_sizer->Add(arrow_panel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(4));

        // --- Target card (numbered square + material name + chevron) ---
        row.target_panel = new wxPanel(row_panel, wxID_ANY, wxDefaultPosition, wxSize(-1, row_h),
                                       wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE);
        row.target_panel->SetMinSize(wxSize(target_min_w, row_h));
        row.target_panel->SetToolTip(get_filament_label(row.target_filament_idx));
        row.target_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
        row.target_panel->SetCursor(wxCursor(wxCURSOR_HAND));

        row.target_panel->Bind(wxEVT_PAINT, [this, ci, get_target_wxcolor, get_filament_label,
                                              card_bg, card_bd, name_fg, chev_clr](wxPaintEvent& e) {
            auto* p = static_cast<wxPanel*>(e.GetEventObject());
            wxAutoBufferedPaintDC dc(p);
            wxSize sz = p->GetClientSize();

            dc.SetBrush(wxBrush(p->GetParent()->GetBackgroundColour()));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, sz.x, sz.y);

            if (ci >= m_mapping_rows.size()) return;
            int fil_idx = m_mapping_rows[ci].target_filament_idx;

            int r = p->FromDIP(8);
            dc.SetBrush(wxBrush(card_bg));
            dc.SetPen(wxPen(card_bd, 1));
            dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, r);

            // Numbered color square 32x32, rounded 6px
            int sq = p->FromDIP(32);
            int sq_x = p->FromDIP(6);
            int sq_y = (sz.y - sq) / 2;
            int sq_r = p->FromDIP(6);
            wxColour fil_clr = get_target_wxcolor(fil_idx);
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(fil_clr));
            dc.DrawRoundedRectangle(sq_x, sq_y, sq, sq, sq_r);
            draw_filament_swatch_border(dc, fil_clr, sq_x, sq_y, sq, sq, sq_r);

            {
                wxFont num_font = p->GetFont();
                num_font.SetPointSize(10);
                dc.SetFont(num_font);
                dc.SetTextForeground(fil_clr.GetLuminance() < 0.6 ? *wxWHITE : texture_import_gray9000());
                wxString num_str = wxString::Format("%d", fil_idx + 1);
                wxSize nsz = dc.GetTextExtent(num_str);
                dc.DrawText(num_str, sq_x + (sq - nsz.x) / 2, sq_y + (sq - nsz.y) / 2);
            }

            // Brand icon + material name
            {
                wxFont name_font = p->GetFont();
                name_font.SetPointSize(9);
                dc.SetFont(name_font);
                dc.SetTextForeground(name_fg);
                wxString name_str = get_filament_label(fil_idx);
                int text_x = draw_brand_icon_and_strip(dc, p, name_str, sq_x + sq + p->FromDIP(8), sz.y / 2);
                int max_text_w = sz.x - text_x - p->FromDIP(24);
                if (max_text_w > 0) {
                    name_str = ellipsize_text(dc, name_str, max_text_w);
                    wxSize tsz = dc.GetTextExtent(name_str);
                    dc.DrawText(name_str, text_x, (sz.y - tsz.y) / 2);
                }
            }

            // Dropdown chevron at right edge
            {
                int chev_cx = sz.x - p->FromDIP(14);
                int chev_cy = sz.y / 2;
                int hw = p->FromDIP(3);
                int hh = p->FromDIP(2);
                dc.SetPen(wxPen(chev_clr, p->FromDIP(1) > 0 ? p->FromDIP(1) : 1));
                dc.DrawLine(chev_cx - hw, chev_cy - hh, chev_cx, chev_cy + hh);
                dc.DrawLine(chev_cx, chev_cy + hh, chev_cx + hw, chev_cy - hh);
            }
        });

        row.target_panel->Bind(wxEVT_LEFT_DOWN, [this, ci](wxMouseEvent&) {
            show_filament_popup(ci);
        });
        row.target_panel->Bind(wxEVT_SIZE, [](wxSizeEvent& e) {
            e.Skip();
            static_cast<wxWindow*>(e.GetEventObject())->Refresh();
        });
        row.target_panel->Bind(wxEVT_MOUSEWHEEL, &TextureImportDialog::dismiss_filament_popup_on_wheel, this);

        row_sizer->Add(row.target_panel, 1, wxEXPAND);

        row_panel->SetSizer(row_sizer);
        m_mapping_sizer->Add(row_panel, 0, wxEXPAND | wxBOTTOM, FromDIP(12));
    }

    m_mapping_scroll->FitInside();
    m_mapping_scroll->Layout();
    m_mapping_scroll->Thaw();
}

std::vector<Slic3r::FilamentMatch> TextureImportDialog::build_matches_from_rows() const
{
    std::vector<Slic3r::FilamentMatch> matches(m_mapping_rows.size());
    for (size_t i = 0; i < m_mapping_rows.size(); ++i) {
        auto& m = matches[i];
        m.cluster_index = m_mapping_rows[i].cluster_id;
        m.cluster_color = m_mapping_rows[i].source_color;

        int sel = m_mapping_rows[i].target_filament_idx;
        if (sel >= 0 && sel < (int)m_filament_colors_rgba.size()) {
            m.filament_index = sel;
            m.filament_color = m_filament_colors_rgba[sel];
            m.delta_e = Slic3r::compute_delta_e(m.cluster_color, m.filament_color);
        }
    }
    return matches;
}

// ---- Event handlers ----

void TextureImportDialog::update_color_count_preset_buttons()
{
    if (m_btn_color_4)  m_btn_color_4->SetValue(m_param_color_count == 4);
    if (m_btn_color_8)  m_btn_color_8->SetValue(m_param_color_count == 8);
    if (m_btn_color_16) m_btn_color_16->SetValue(m_param_color_count == 16);
}

void TextureImportDialog::set_color_count_value(int value, bool update_spin)
{
    m_param_color_count = std::clamp(value, 1, (int)max_filament_count());
    m_color_slider->SetValue(m_param_color_count);
    if (update_spin)
        m_color_spin->SetValue(m_param_color_count);
    update_color_count_preset_buttons();
    update_confirm_button_state();
}

void TextureImportDialog::set_smooth_value(int value, bool update_spin)
{
    m_param_smooth = std::clamp(value, 0, 10);
    m_smooth_slider->SetValue(m_param_smooth);
    if (update_spin)
        m_smooth_spin->SetValue(m_param_smooth);
    update_confirm_button_state();
}

void TextureImportDialog::preview_spin_text_value(SpinInput* spin, GreenSlider* slider, int& param,
                                                  int min_value, int max_value, const wxString& text,
                                                  std::function<void()> on_value_changed)
{
    long value;
    if (!text.ToLong(&value))
        return;

    wxTextCtrl* tc = spin->GetTextCtrl();
    long parsed = value;
    value = std::clamp((int)parsed, min_value, max_value);

    wxString normalized = text;
    if (parsed > max_value || (text.length() > 1 && text[0] == '0'))
        normalized = wxString::Format("%ld", value);

    if (normalized != text) {
        long pos = tc->GetInsertionPoint();
        tc->ChangeValue(normalized);
        if (parsed > max_value)
            tc->SetInsertionPointEnd();
        else
            tc->SetInsertionPoint(std::min<long>(normalized.length(), std::max(0L, pos - 1)));
    }

    param = (int)value;
    slider->SetValue(param);
    if (on_value_changed)
        on_value_changed();
    update_confirm_button_state();
}

void TextureImportDialog::on_color_preset_clicked(wxCommandEvent& evt)
{
    int id = evt.GetId();
    int color_count = m_param_color_count;
    if (id == ID_COLOR_4)  { color_count = 4; }
    if (id == ID_COLOR_8)  { color_count = 8; }
    if (id == ID_COLOR_16) { color_count = 16; }

    if (id == ID_COLOR_AUTO) {
        start_computation(true);
        return;
    }

    set_color_count_value(color_count, true);
}

void TextureImportDialog::on_color_slider_changed(wxCommandEvent&)
{
    set_color_count_value(m_color_slider->GetValue(), true);
}

void TextureImportDialog::on_color_spin_changed(wxCommandEvent&)
{
    set_color_count_value(m_color_spin->GetValue(), true);
}

void TextureImportDialog::on_color_spin_text_changed(wxCommandEvent& evt)
{
    preview_spin_text_value(m_color_spin, m_color_slider, m_param_color_count,
                            1, (int)max_filament_count(), evt.GetString(),
                            [this]() { update_color_count_preset_buttons(); });
}

void TextureImportDialog::on_smooth_slider_changed(wxCommandEvent&)
{
    set_smooth_value(m_smooth_slider->GetValue(), true);
}

void TextureImportDialog::on_smooth_spin_changed(wxCommandEvent&)
{
    set_smooth_value(m_smooth_spin->GetValue(), true);
}

void TextureImportDialog::on_smooth_spin_text_changed(wxCommandEvent& evt)
{
    preview_spin_text_value(m_smooth_spin, m_smooth_slider, m_param_smooth,
                            0, 10, evt.GetString());
}

void TextureImportDialog::on_apply_clicked(wxCommandEvent&)
{
    start_computation();
}

void TextureImportDialog::on_auto_merge_toggled(wxCommandEvent&)
{
    bool auto_merge_enabled = !m_auto_merge_cb || m_auto_merge_cb->GetValue();
    bool allow_limit_warning = m_auto_merge_enabled && !auto_merge_enabled;
    m_auto_merge_enabled = auto_merge_enabled;

    if (m_state == TextureImportState::Ready) {
        m_allow_filament_limit_warning_once = allow_limit_warning;
        do_auto_match();
        m_allow_filament_limit_warning_once = false;
        compact_used_virtual_filaments();
        update_filament_color_map();
        rebuild_mapping_rows();
    }
}

void TextureImportDialog::on_view_button_clicked(wxCommandEvent& evt)
{
    if (!m_preview_canvas) return;

    int id = evt.GetId();
    if (id == ID_VIEW_ORIGINAL) {
        m_preview_canvas->set_render_mode(TexturePreviewCanvas::RenderMode::Original);
        highlight_view_button(0);
    } else if (id == ID_VIEW_MULTICOLOR) {
        m_preview_canvas->set_render_mode(TexturePreviewCanvas::RenderMode::MultiColor);
        highlight_view_button(1);
    } else if (id == ID_VIEW_FILAMENT) {
        m_preview_canvas->set_render_mode(TexturePreviewCanvas::RenderMode::FilamentMap);
        highlight_view_button(2);
    }
}

void TextureImportDialog::highlight_view_button(int view_index)
{
    Button* btns[] = { m_btn_view_original, m_btn_view_multicolor, m_btn_view_filament };

    StateColor active_bg(
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor active_bd(
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor active_text(
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));

    StateColor inactive_bg(
        std::pair<wxColour, int>(dark_or(wxColour(210, 210, 210), wxColour(0x54, 0x54, 0x5B)), StateColor::Pressed),
        std::pair<wxColour, int>(dark_or(wxColour(225, 225, 225), wxColour(0x4C, 0x4C, 0x55)), StateColor::Hovered),
        std::pair<wxColour, int>(dark_or(wxColour(238, 238, 238), wxColour(0x3E, 0x3E, 0x45)), StateColor::Normal));
    StateColor inactive_bd(
        std::pair<wxColour, int>(dark_or(wxColour(238, 238, 238), wxColour(0x3E, 0x3E, 0x45)), StateColor::Normal));
    StateColor inactive_text(
        std::pair<wxColour, int>(dark_or(wxColour(160, 160, 160), wxColour(0x81, 0x81, 0x83)), StateColor::Normal));

    for (int i = 0; i < 3; ++i) {
        if (!btns[i]) continue;
        if (i == view_index) {
            btns[i]->SetBackgroundColor(active_bg);
            btns[i]->SetBorderColor(active_bd);
            btns[i]->SetTextColor(active_text);
        } else {
            btns[i]->SetBackgroundColor(inactive_bg);
            btns[i]->SetBorderColor(inactive_bd);
            btns[i]->SetTextColor(inactive_text);
        }
        btns[i]->Refresh();
    }
}

void TextureImportDialog::on_skip_clicked(wxCommandEvent&)
{
    m_skipped = true;
    m_new_filament_colors.clear();
    m_new_filament_preset_names.clear();
    m_current_matches.clear();
    cancel_computation();
    EndModal(wxID_CANCEL);
}

bool TextureImportDialog::has_valid_result() const
{
    if (m_painted.face_colors.empty() || m_current_matches.empty() || m_mapping_rows.empty())
        return false;

    if (m_mapping_rows.size() != m_current_matches.size())
        return false;

    const int filament_count = (int)std::min(m_filament_colors_rgba.size(), max_filament_count());
    for (const auto& row : m_mapping_rows) {
        if (row.target_filament_idx < 0 || row.target_filament_idx >= filament_count)
            return false;
    }
    return true;
}

bool TextureImportDialog::is_params_dirty() const
{
    if (m_applied_color_count < 0)
        return false;
    return m_param_color_count != m_applied_color_count
        || m_param_smooth      != m_applied_smooth;
}

void TextureImportDialog::update_confirm_button_state()
{
    if (m_state != TextureImportState::Ready)
        return;

    if (!has_valid_result()) {
        m_btn_ok->Enable(false);
        if (m_hint_label) m_hint_label->Hide();
        m_btn_ok->Refresh();
        Layout();
        return;
    }

    bool dirty = is_params_dirty();

    m_btn_ok->Enable(true);

    if (dirty) {
        StateColor gray_bg(
            std::pair<wxColour, int>(dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B)), StateColor::Normal));
        StateColor gray_bd(
            std::pair<wxColour, int>(dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B)), StateColor::Normal));
        StateColor gray_text(
            std::pair<wxColour, int>(dark_or(wxColour(107, 107, 107), wxColour(0xB3, 0xB3, 0xB5)), StateColor::Normal));
        m_btn_ok->SetBackgroundColor(gray_bg);
        m_btn_ok->SetBorderColor(gray_bd);
        m_btn_ok->SetTextColor(gray_text);
        m_btn_ok->SetToolTip(_L("Reminder: parameters changed, click Apply to take effect"));
        if (m_hint_label) m_hint_label->Show();
    } else {
        StateColor ok_bg(
            std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
            std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
        StateColor ok_bd(
            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
        StateColor ok_text(
            std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
        m_btn_ok->SetBackgroundColor(ok_bg);
        m_btn_ok->SetBorderColor(ok_bd);
        m_btn_ok->SetTextColor(ok_text);
        m_btn_ok->UnsetToolTip();
        if (m_hint_label) m_hint_label->Hide();
    }

    m_btn_ok->Refresh();
    Layout();
}

void TextureImportDialog::on_ok_clicked(wxCommandEvent&)
{
    if (m_state != TextureImportState::Ready || !has_valid_result() || is_params_dirty())
        return;

    m_current_matches = build_matches_from_rows();
    if (m_current_matches.empty())
        return;

    compact_used_virtual_filaments();

    EndModal(wxID_OK);
}

// ---- Result accessors ----

Slic3r::PaintedMesh TextureImportDialog::get_painted_mesh() const
{
    return m_painted;
}

std::vector<Slic3r::FilamentMatch> TextureImportDialog::get_matches() const
{
    if (!m_current_matches.empty())
        return m_current_matches;
    return build_matches_from_rows();
}

void TextureImportDialog::on_dpi_changed(const wxRect&)
{
    Fit();
    Refresh();
    wxGetApp().UpdateDlgDarkUI(this);
}

}} // namespace Slic3r::GUI
