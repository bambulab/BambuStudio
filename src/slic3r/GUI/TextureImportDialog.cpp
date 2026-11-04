#include <GL/glew.h>

#include "TextureImportDialog.hpp"
#include "TextureImportPopupDismiss.hpp"
#include "TextureImportUi.hpp"
#include "MixingKitsHelpDialog.hpp"
#include "MixSupplementFilamentDialog.hpp"
#include "I18N.hpp"
#include "format.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "MsgDialog.hpp"
#include "ColorDecomposeSupport.hpp"
#include "EncodedFilament.hpp"
#include "FilamentBitmapUtils.hpp"
#include "WindowShadow.hpp"
#include "Widgets/StateColor.hpp"
#include "Widgets/StaticBox.hpp"
#include "Widgets/StaticLine.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/Label.hpp"
#include "libslic3r/ColorDecomposeRecipe.hpp"
#include "libslic3r/FilamentMixer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Win10ModelRepair.hpp"

#include <wx/button.h>
#include <wx/bmpbuttn.h>
#include <wx/colour.h>
#include <wx/colordlg.h>
#include <wx/dc.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/region.h>
#include <wx/display.h>
#include <wx/evtloop.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/scrolwin.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>
#include <wx/valtext.h>
#include <wx/timer.h>
#include <wx/app.h>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif
#ifdef __WXOSX__
#include <objc/message.h>
#include <objc/runtime.h>
#endif

#include <algorithm>
#include <cctype>
#include <utility>
#include <chrono>
#include <cstring>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <type_traits>
#include <unordered_map>

#include <boost/algorithm/string.hpp>
#include "libslic3r/Log.hpp"

static constexpr const char* DEFAULT_VIRTUAL_FILAMENT_BASIC_TYPE = "PLA Basic";
static constexpr const char* DEFAULT_VIRTUAL_FILAMENT_SHORT_TYPE = "PLA";
static constexpr const char* DEFAULT_VIRTUAL_FILAMENT_NAME       = "Bambu PLA Basic";
// CMYKW recipes are measured on the PLA Palette series, whose base colors differ
// from PLA Basic's, so its base filaments must not be created as PLA Basic.
static constexpr const char* PALETTE_VIRTUAL_FILAMENT_NAME       = "Bambu PLA Palette";
// CIEDE2000 cutoff for automatic filament matching. The matching step
// exposes this as a slider; 0 rejects every non-zero difference. The right
// end is unlimited, so every color binds to the closest candidate.
static constexpr int kMatchDeltaThresholdMin = 0;
static constexpr int kMatchDeltaThresholdMax = 50;
static constexpr int kMatchDeltaThresholdDefault = 20;
static_assert(kMatchDeltaThresholdDefault >= kMatchDeltaThresholdMin &&
              kMatchDeltaThresholdDefault <= kMatchDeltaThresholdMax, "");

// CIEDE2000 band treated as no color difference. Preferences key
// texture_import_color_tolerance. Integer, greater than 0.
static constexpr int kColorToleranceMin = 1;
static constexpr int kColorToleranceMax = 100;
static constexpr int kColorToleranceDefault = 5;
static constexpr const char* kColorToleranceConfigKey = "texture_import_color_tolerance";

static bool match_delta_accepted(double delta, int threshold)
{
    if (threshold >= kMatchDeltaThresholdMax)
        return true;
    return delta <= static_cast<double>(threshold);
}

using Slic3r::GUI::texture_import_is_dark;
using Slic3r::GUI::texture_import_dark_or;
using Slic3r::GUI::texture_import_paint;
using Slic3r::GUI::texture_import_needs_swatch_border;
using Slic3r::GUI::texture_import_swatch_border_colour;
using Slic3r::GUI::texture_import_draw_swatch_border;

static bool is_dark() { return texture_import_is_dark(); }

static wxColour dark_or(const wxColour& light, const wxColour& dark)
{
    return texture_import_dark_or(light, dark);
}

static wxColour texture_import_dialog_bg()
{
    return dark_or(*wxWHITE, wxColour(0x2D, 0x2D, 0x31));
}

static wxColour texture_import_dialog_fg()
{
    return dark_or(wxColour(50, 58, 61), wxColour(0xEF, 0xEF, 0xF0));
}

static wxColour texture_import_preview_bg()
{
    return dark_or(wxColour(238, 238, 238), wxColour(0x3E, 0x3E, 0x45));
}

static wxColour texture_import_preview_bd()
{
    return dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B));
}

static wxColour texture_import_tag_bg()
{
    return dark_or(wxColour(255, 255, 255), wxColour(0x54, 0x54, 0x5B));
}

static wxColour texture_import_tag_fg()
{
    return dark_or(wxColour(0x6B, 0x6B, 0x6B), wxColour(0xD0, 0xD0, 0xD2));
}

static wxColour texture_import_caption_fg()
{
    return dark_or(wxColour(107, 107, 107), wxColour(0x81, 0x81, 0x83));
}

static wxColour texture_import_title_line_colour()
{
    return dark_or(wxColour(166, 169, 170), wxColour(80, 80, 86));
}

static wxColour param_value_input_border()
{
    return dark_or(wxColour(0xCE, 0xCE, 0xCE), wxColour(0x5A, 0x5A, 0x60));
}

static wxColour texture_import_gray9000()
{
    return wxColour(38, 46, 48);
}

static wxColour texture_import_text_colour()
{
    // gray9000 is not in StateColor's dark map, so darkModeColorFor() would
    // keep the light-mode glyph color after an appearance switch.
    return dark_or(texture_import_gray9000(), wxColour(0xEF, 0xEF, 0xF0));
}

static wxColour blend_towards(const wxColour& fg, const wxColour& bg, double alpha)
{
    auto mix = [alpha](int a, int b) {
        return (unsigned char)std::lround(a * alpha + b * (1.0 - alpha));
    };
    return wxColour(mix(fg.Red(), bg.Red()), mix(fg.Green(), bg.Green()), mix(fg.Blue(), bg.Blue()));
}

// TextInput strips wxTE_CENTRE (it shares bits with wxALIGN_*), so center
// the inner wxTextCtrl from the dialog instead of changing TextInput.
static void center_param_text_ctrl(wxTextCtrl* tc)
{
    if (!tc)
        return;
#ifdef __WXMSW__
    HWND hwnd = (HWND)tc->GetHWND();
    if (!hwnd)
        return;
    wxClientDC dc(tc);
    dc.SetFont(tc->GetFont());
    wxString value = tc->GetValue();
    if (value.empty())
        value = "0";
    const int text_w = dc.GetTextExtent(value).x;
    const int client_w = tc->GetClientSize().x;
    const int left = std::max(0, (client_w - text_w) / 2);
    ::SendMessage(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM((WORD)left, 0));
#elif defined(__WXGTK__)
    GtkWidget* widget = static_cast<GtkWidget*>(tc->GetHandle());
    if (widget && GTK_IS_ENTRY(widget))
        gtk_entry_set_alignment(GTK_ENTRY(widget), 0.5f);
#elif defined(__WXOSX__)
    void* handle = tc->GetHandle();
    if (handle) {
        using MsgSendAlign = void (*)(void*, void*, long);
        void* sel = sel_registerName("setAlignment:");
        reinterpret_cast<MsgSendAlign>(objc_msgSend)(handle, sel, 1); // NSTextAlignmentCenter
    }
#endif
}

static void bind_param_text_ctrl_center(wxTextCtrl* tc)
{
    if (!tc)
        return;
    tc->Bind(wxEVT_TEXT, [tc](wxCommandEvent& e) {
        center_param_text_ctrl(tc);
        e.Skip();
    });
    tc->Bind(wxEVT_SIZE, [tc](wxSizeEvent& e) {
        center_param_text_ctrl(tc);
        e.Skip();
    });
    center_param_text_ctrl(tc);
}

static void style_param_value_input(TextInput* input)
{
    if (!input)
        return;
    const wxSize sz(input->FromDIP(64), input->FromDIP(28));
    input->SetCornerRadius(input->FromDIP(6));
    input->SetBorderWidth(1);
    const wxColour bd = param_value_input_border();
    input->SetBorderColor(StateColor(
        std::pair<wxColour, int>(bd, StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(0x00, 0xAE, 0x42), StateColor::Hovered),
        std::pair<wxColour, int>(bd, StateColor::Normal)));
    input->SetBackgroundColor(StateColor(
        std::pair<wxColour, int>(dark_or(wxColour(0xF0, 0xF0, 0xF1), wxColour(0x40, 0x40, 0x40)), StateColor::Disabled),
        std::pair<wxColour, int>(texture_import_dialog_bg(), StateColor::Normal)));
    input->SetTextColor(StateColor(
        std::pair<wxColour, int>(dark_or(wxColour(0x90, 0x90, 0x90), wxColour(0x5A, 0x5A, 0x5A)), StateColor::Disabled),
        std::pair<wxColour, int>(texture_import_text_colour(), StateColor::Normal)));
    if (wxTextCtrl* tc = input->GetTextCtrl()) {
        tc->SetFont(Label::Body_14);
        tc->SetBackgroundColour(texture_import_dialog_bg());
        tc->SetForegroundColour(texture_import_text_colour());
        wxSize text_size = tc->GetBestSize();
        text_size.x = std::max(1, sz.x - input->FromDIP(10));
        text_size.y = std::min(text_size.y, std::max(1, sz.y - input->FromDIP(8)));
        tc->SetMinSize(wxSize(-1, text_size.y));
        tc->SetSize(text_size);
        center_param_text_ctrl(tc);
    }
    input->SetMinSize(sz);
    input->SetMaxSize(sz);
    input->SetSize(sz);
    input->Refresh();
}

static void set_color_spin_error_border(TextInput* input, bool error)
{
    if (!input)
        return;
    if (error) {
        const wxColour error_bd(225, 71, 71);
        input->SetBorderColor(StateColor(
            std::pair<wxColour, int>(error_bd, StateColor::Disabled),
            std::pair<wxColour, int>(error_bd, StateColor::Hovered),
            std::pair<wxColour, int>(error_bd, StateColor::Normal)));
    } else {
        const wxColour bd = param_value_input_border();
        input->SetBorderColor(StateColor(
            std::pair<wxColour, int>(bd, StateColor::Disabled),
            std::pair<wxColour, int>(wxColour(0x00, 0xAE, 0x42), StateColor::Hovered),
            std::pair<wxColour, int>(bd, StateColor::Normal)));
    }
    input->Refresh();
}

static wxString gap_locked_tooltip_text()
{
    return _L("No valid computation result. This parameter cannot be adjusted.");
}

static void set_window_tooltip(wxWindow* win, const wxString& tip)
{
    if (!win)
        return;
    if (tip.empty())
        win->UnsetToolTip();
    else
        win->SetToolTip(tip);
}

// Keep the window enabled so Windows still delivers hover for the tooltip.
static void set_gap_spin_locked(TextInput* input, bool locked)
{
    if (!input)
        return;
    wxTextCtrl* tc = input->GetTextCtrl();
    if (tc && tc->IsEditable() != !locked)
        tc->SetEditable(!locked);
    if (locked) {
        if (tc) {
            tc->SetBackgroundColour(dark_or(wxColour(0xF0, 0xF0, 0xF1), wxColour(0x40, 0x40, 0x40)));
            tc->SetForegroundColour(dark_or(wxColour(0x90, 0x90, 0x90), wxColour(0x5A, 0x5A, 0x5A)));
        }
        const wxColour bd = param_value_input_border();
        input->SetBorderColor(StateColor(
            std::pair<wxColour, int>(bd, StateColor::Disabled),
            std::pair<wxColour, int>(bd, StateColor::Hovered),
            std::pair<wxColour, int>(bd, StateColor::Normal)));
    } else if (tc) {
        tc->SetBackgroundColour(texture_import_dialog_bg());
        tc->SetForegroundColour(texture_import_text_colour());
        const wxColour bd = param_value_input_border();
        input->SetBorderColor(StateColor(
            std::pair<wxColour, int>(bd, StateColor::Disabled),
            std::pair<wxColour, int>(wxColour(0x00, 0xAE, 0x42), StateColor::Hovered),
            std::pair<wxColour, int>(bd, StateColor::Normal)));
    }
    if (tc)
        tc->Refresh();
    input->Refresh();
}

static void set_text_input_int(TextInput* input, int value)
{
    if (!input || !input->GetTextCtrl())
        return;
    wxTextCtrl* tc = input->GetTextCtrl();
    const wxString text = wxString::Format("%d", value);
    if (tc->GetValue() != text)
        tc->ChangeValue(text);
    center_param_text_ctrl(tc);
}

static void set_text_input_double(TextInput* input, double value)
{
    if (!input || !input->GetTextCtrl())
        return;
    wxTextCtrl* tc = input->GetTextCtrl();
    const wxString text = wxString::Format("%.2f", value);
    if (tc->GetValue() != text)
        tc->ChangeValue(text);
    center_param_text_ctrl(tc);
}

static wxColour texture_import_separator_colour()
{
    return StateColor::darkModeColorFor(wxColour("#CECECE"));
}

// Outer frame of the import dialog (native window chrome): darker than inner
// separators so a side popup reads as the same kind of window, not a divider.
static wxColour texture_import_window_border_colour()
{
    return dark_or(wxColour(0x8A, 0x8A, 0x8A), wxColour(0x8B, 0x8B, 0x90));
}

static wxColour texture_import_brand_green()
{
    return dark_or(wxColour(0, 174, 66), wxColour(61, 203, 115));
}

static wxColour texture_import_muted_text_colour()
{
    return dark_or(wxColour(0x6B, 0x6B, 0x6B), wxColour(0xB3, 0xB3, 0xB5));
}

static wxColour texture_import_hint_text_colour()
{
    return dark_or(wxColour(0x90, 0x90, 0x90), wxColour(0xA0, 0xA0, 0xA2));
}

static wxFont texture_import_section_title_font(wxWindow* win)
{
    wxFont font = win ? win->GetFont() : wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    font.MakeBold();
    return font;
}

static wxFont texture_import_warning_body_font(wxWindow* win)
{
    return win ? win->GetFont() : wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
}

static wxFont texture_import_action_link_font(wxWindow* win)
{
    wxFont font = texture_import_section_title_font(win);
    font.SetUnderlined(true);
    return font;
}

static void apply_color_count_preset_text(Button* btn)
{
    if (!btn)
        return;
    StateColor muted(
        std::pair<wxColour, int>(texture_import_muted_text_colour(), StateColor::Normal));
    btn->SetTextColor(muted);
}

static void apply_color_count_preset_style(Button* btn)
{
    if (!btn)
        return;
    btn->SetCornerRadius(btn->FromDIP(4));
    btn->SetBorderWidth(0);
    const wxColour bg_normal = dark_or(wxColour(0xF8, 0xF8, 0xF8), wxColour(0x3A, 0x3A, 0x3E));
    const wxColour bg_hover  = dark_or(wxColour(0xEE, 0xEE, 0xEE), wxColour(0x48, 0x48, 0x4C));
    const wxColour bg_press  = dark_or(wxColour(0xE0, 0xE0, 0xE0), wxColour(0x54, 0x54, 0x5B));
    StateColor bg(
        std::pair<wxColour, int>(bg_press, StateColor::Pressed),
        std::pair<wxColour, int>(bg_hover, StateColor::Hovered),
        std::pair<wxColour, int>(bg_normal, StateColor::Normal));
    btn->SetBackgroundColor(bg);
    btn->SetBorderColor(bg);
    apply_color_count_preset_text(btn);
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

static void draw_filament_swatch_border(wxDC& dc, const wxColour& colour,
                                        int x, int y, int w, int h, int radius = 0)
{
    texture_import_draw_swatch_border(dc, colour, x, y, w, h, radius);
}

static void draw_filament_swatch_ellipse_border(wxDC& dc, const wxColour& colour,
                                                int x, int y, int w, int h)
{
    if (!texture_import_needs_swatch_border(colour))
        return;

    dc.SetPen(wxPen(texture_import_swatch_border_colour(), 1));
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

static wxString advanced_smooth_hint_text()
{
    return _L("Transition Smoothness (between color blocks): the higher, the smoother");
}

static wxString advanced_gap_hint_text()
{
    return _L("Auto Merge Small Fragments: the higher, the more merging");
}

// Height of two lines as the control itself lays them out. A wxClientDC extent leaves
// out the padding the native control adds around the text - the NSTextFieldCell insets
// on macOS - and the missing pixels clip the second line away.
static int two_line_hint_height(Label* hint)
{
    const wxString shown = hint->GetLabel();
    hint->wxStaticText::SetLabel("Ag\nAg");
    hint->InvalidateBestSize();
    const int height = hint->GetBestSize().y;
    hint->wxStaticText::SetLabel(shown);
    hint->InvalidateBestSize();
    return height;
}

// Break a hint into at most two lines; overflow on the last line ends with "...".
static wxString wrap_hint_two_lines(wxWindow* win, const wxString& text, int width, bool& truncated)
{
    truncated = false;
    if (width <= 0 || text.empty())
        return text;

    wxClientDC dc(win);
    dc.SetFont(win->GetFont());
    wxString wrapped;
    Label::split_lines(dc, width, text, wrapped, 2);

    const int nl = wrapped.Find('\n');
    if (nl == wxNOT_FOUND)
        return wrapped;

    wxString line1 = wrapped.Left(nl);
    wxString line2 = wrapped.Mid(nl + 1);
    line2.Replace("\n", " ");
    const wxString fitted = ellipsize_text(dc, line2, width);
    truncated = (fitted != line2);
    return line1 + "\n" + fitted;
}

// Push `full_text` into `hint` broken to fit `width`. The wrapper breaks on the raw text
// extent, but the width the control asks a sizer for is its best size: that extent plus
// whatever the native control pads around the text. A line measured just inside `width`
// is therefore laid out just outside it, and macOS clips the overflow instead of
// re-wrapping it (see Label::Wrap). Re-breaking by the measured overflow converges in one
// pass, because a narrower break can only reduce the best width.
static void fit_hint_two_lines(Label* hint, const wxString& full_text, int width)
{
    bool truncated = false;
    hint->SetLabel(wrap_hint_two_lines(hint, full_text, width, truncated));
    hint->InvalidateBestSize();

    const int overflow = hint->GetBestSize().x - width;
    if (overflow > 0) {
        hint->SetLabel(wrap_hint_two_lines(hint, full_text, width - overflow, truncated));
        hint->InvalidateBestSize();
    }

    // Keep the full sentence reachable when the second line had to be cut.
    if (truncated)
        hint->SetToolTip(full_text);
    else
        hint->UnsetToolTip();
}

static void init_advanced_hint(Label* hint)
{
    // Width 1, not -1: a default min component makes wxWindow fall back to the best size,
    // and the best size of the still unbroken sentence is its full single-line width. The
    // column would then never squeeze the label enough for the wrap to kick in, leaving
    // the hint on one clipped line.
    const int height = two_line_hint_height(hint);
    hint->SetMinSize(wxSize(1, height));
    hint->SetMaxSize(wxSize(-1, height));
}

static int draw_brand_icon_and_strip(wxDC& dc, wxWindow* win, wxString& name, int x, int cy,
                                     const ScalableBitmap* brand = nullptr)
{
    int icon_sz = win->FromDIP(16);
    if (name.StartsWith("Bambu ")) {
        name = name.Mid(6);
        wxBitmap bmp = (brand && brand->bmp().IsOk()) ? brand->bmp()
                                                      : create_scaled_bitmap("BambuStudioBlack", win, 16);
        if (bmp.IsOk()) {
            const wxSize bs = ScalableBitmap::GetBmpSize(bmp);
            dc.DrawBitmap(bmp, x, cy - bs.y / 2);
            icon_sz = std::max(icon_sz, bs.x);
        }
        x += icon_sz + win->FromDIP(4);
    }
    return x;
}

// ============================================================
// GreenSliderT — thin track + green triangle thumb (int or double)
// ============================================================

template<typename T>
class GreenSliderT : public wxPanel {
public:
    GreenSliderT(wxWindow* parent, T value, T minVal, T maxVal,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize);
    T    GetValue() const { return m_value; }
    T    GetMin() const { return m_min; }
    T    GetMax() const { return m_max; }
    void SetValue(T val);
    void SetRange(T minVal, T maxVal);
    // Stay enabled so a hover tooltip can show, but ignore drags and paint grey.
    void SetInteractive(bool interactive);
    bool Enable(bool enable = true) override;
private:
    void OnPaint(wxPaintEvent&);
    void OnMouse(wxMouseEvent&);
    void on_capture_lost(wxMouseCaptureLostEvent&);
    void release_drag();
    int  xFromValue() const;
    T    valueFromX(int x) const;
    T    quantize(T val) const;
    T    m_value, m_min, m_max;
    bool m_dragging = false;
    bool m_interactive = true;
};

template<typename T>
T GreenSliderT<T>::quantize(T val) const
{
    if constexpr (std::is_floating_point_v<T>)
        return std::round(val * T(100)) / T(100);
    else
        return val;
}

template<typename T>
GreenSliderT<T>::GreenSliderT(wxWindow* parent, T value, T minVal, T maxVal,
                              const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, wxID_ANY, pos, size.IsFullySpecified() ? size : wxSize(-1, parent->FromDIP(24)),
              wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE)
    , m_value(), m_min(minVal), m_max(maxVal)
{
    if (m_max < m_min)
        m_max = m_min;
    m_value = quantize(std::clamp(value, m_min, m_max));
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(-1, FromDIP(24)));

    Bind(wxEVT_PAINT, &GreenSliderT<T>::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) {
        evt.Skip();
        Refresh();
    });
    Bind(wxEVT_LEFT_DOWN, &GreenSliderT<T>::OnMouse, this);
    Bind(wxEVT_LEFT_UP,   &GreenSliderT<T>::OnMouse, this);
    Bind(wxEVT_MOTION,    &GreenSliderT<T>::OnMouse, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &GreenSliderT<T>::on_capture_lost, this);
}

template<typename T>
void GreenSliderT<T>::SetValue(T val)
{
    val = quantize(std::clamp(val, m_min, m_max));
    if (val != m_value) { m_value = val; Refresh(); }
}

template<typename T>
void GreenSliderT<T>::SetRange(T minVal, T maxVal)
{
    if (maxVal < minVal)
        maxVal = minVal;
    m_min = minVal;
    m_max = maxVal;
    const T clamped = quantize(std::clamp(m_value, m_min, m_max));
    if (clamped != m_value)
        m_value = clamped;
    Refresh();
}

template<typename T>
void GreenSliderT<T>::SetInteractive(bool interactive)
{
    if (m_interactive == interactive)
        return;
    m_interactive = interactive;
    if (!interactive)
        release_drag();
    Refresh();
}

template<typename T>
bool GreenSliderT<T>::Enable(bool enable)
{
    if (!enable)
        release_drag();
    bool ok = wxPanel::Enable(enable);
    Refresh();
    return ok;
}

template<typename T>
void GreenSliderT<T>::release_drag()
{
    m_dragging = false;
    if (HasCapture())
        ReleaseMouse();
}

template<typename T>
void GreenSliderT<T>::on_capture_lost(wxMouseCaptureLostEvent&)
{
    m_dragging = false;
}

template<typename T>
int GreenSliderT<T>::xFromValue() const
{
    wxSize sz = GetClientSize();
    int margin = FromDIP(6);
    int track_w = sz.x - 2 * margin;
    if (m_max <= m_min || track_w <= 0) return margin;
    if constexpr (std::is_integral_v<T>)
        return margin + (int)(m_value - m_min) * track_w / (int)(m_max - m_min);
    else
        return margin + (int)std::lround((m_value - m_min) * (double)track_w / (double)(m_max - m_min));
}

template<typename T>
T GreenSliderT<T>::valueFromX(int x) const
{
    wxSize sz = GetClientSize();
    int margin = FromDIP(6);
    int track_w = sz.x - 2 * margin;
    if (track_w <= 0 || m_max <= m_min) return m_min;
    if constexpr (std::is_integral_v<T>) {
        T val = m_min + (T)(x - margin) * (m_max - m_min) / (T)track_w;
        return std::clamp(val, m_min, m_max);
    } else {
        T val = m_min + (T)(x - margin) * (m_max - m_min) / (T)track_w;
        return quantize(std::clamp(val, m_min, m_max));
    }
}

template<typename T>
void GreenSliderT<T>::OnPaint(wxPaintEvent&)
{
    texture_import_paint(this, [this](wxDC& dc) {
        wxSize sz = GetClientSize();

        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();

        int margin = FromDIP(6);
        int track_y = sz.y / 2;
        int ts = FromDIP(8);
        int pen_w = FromDIP(2);

        const bool active = IsEnabled() && m_interactive;
        wxColour greenClr = active ? wxColour(0, 174, 66)
                                   : dark_or(wxColour(180, 180, 180), wxColour(90, 90, 96));
        wxColour grayClr  = active ? dark_or(wxColour(200, 200, 200), wxColour(90, 90, 96))
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
    });
}

template<typename T>
void GreenSliderT<T>::OnMouse(wxMouseEvent& evt)
{
    if (!IsEnabled() || !m_interactive) return;

    auto update = [&](int x) {
        T nv = valueFromX(x);
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
        release_drag();
    } else if (evt.Dragging() && m_dragging) {
        update(evt.GetX());
    }
}

class GreenSlider : public GreenSliderT<int> {
public:
    using GreenSliderT<int>::GreenSliderT;
};

class GreenDoubleSlider : public GreenSliderT<double> {
public:
    using GreenSliderT<double>::GreenSliderT;
};

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_TEXTURE_COMPUTE_DONE, wxCommandEvent);
wxDEFINE_EVENT(EVT_TEXTURE_COMPUTE_PROGRESS, wxCommandEvent);
wxDEFINE_EVENT(EVT_TEXTURE_COMPUTE_ERROR, wxCommandEvent);
wxDEFINE_EVENT(EVT_TEXTURE_PATCH_DONE, wxCommandEvent);

// Right end of the gap area slider, in percent of the total patch area.
static constexpr double kGapAreaMaxPercent = 5.0;
// CIEDE2000 difference at which a neighbor's boundary counts half when the gap
// merge picks the region a fragment is absorbed into.
static constexpr double kGapMergeColorSigma = 10.0;

struct ColorPatch {
    int  patch_id    = -1;
    double patch_area = 0.0;
    std::vector<int> patch_faces;
    std::array<std::size_t, 3> color_label{};
};

// One step of the gap area region merge: the patches in
// [member_begin, member_end) of GapPreviewState::merge_members formed a region
// that was absorbed by a neighbor and recolored from from_color to to_color.
struct GapMergeEvent {
    double key_area = 0.0; // area of the absorbed region at the time it was absorbed
    std::array<std::size_t, 3> from_color{};
    std::array<std::size_t, 3> to_color{};
    int member_begin = 0;
    int member_end   = 0;
};

struct GapPreviewState {
    std::vector<ColorPatch> color_patches;
    // key_area never decreases along merge_events, so a threshold selects a prefix.
    std::vector<GapMergeEvent> merge_events;
    std::vector<int>           merge_members;
    // Number of merge_events currently reflected in display_face_colors.
    int    applied_events  = 0;
    double gap_total_area  = 0.0;
    std::vector<std::array<std::size_t, 3>> simplified_face_colors;
    std::vector<std::array<std::size_t, 3>> simplified_cluster_colors;
    std::vector<std::array<std::size_t, 3>> display_face_colors;
};

namespace {

indexed_triangle_set painted_mesh_to_its(const PaintedMesh& painted)
{
    indexed_triangle_set its;
    its.vertices.reserve(painted.vertices.size());
    for (const auto& v : painted.vertices)
        its.vertices.emplace_back(v[0], v[1], v[2]);
    its.indices.reserve(painted.indices.size());
    for (const auto& f : painted.indices)
        its.indices.emplace_back(f[0], f[1], f[2]);
    return its;
}

// Same-color edge-connected components on the simplified mesh. shared_out[p][q]
// receives the boundary length shared by patches p and q.
std::vector<ColorPatch> build_color_patches(
    const PaintedMesh& painted,
    const std::vector<std::array<std::size_t, 3>>& face_colors,
    std::vector<std::unordered_map<int, double>>& shared_out)
{
    shared_out.clear();
    std::vector<ColorPatch> patches;
    const size_t nfaces = painted.indices.size();
    if (nfaces == 0 || face_colors.size() != nfaces)
        return patches;

    indexed_triangle_set its = painted_mesh_to_its(painted);
    if (its.indices.size() != nfaces)
        return patches;

    const std::vector<Vec3i> neighbors = its_face_neighbors_par(its);
    std::vector<int> face_to_patch(nfaces, -1);
    std::vector<char> visited(nfaces, 0);

    auto face_area = [&](int fi) -> double {
        const auto& tri = its.indices[fi];
        if (tri[0] < 0 || tri[1] < 0 || tri[2] < 0)
            return 0.0;
        if (tri[0] >= (int)its.vertices.size() ||
            tri[1] >= (int)its.vertices.size() ||
            tri[2] >= (int)its.vertices.size())
            return 0.0;
        const Vec3f& a = its.vertices[tri[0]];
        const Vec3f& b = its.vertices[tri[1]];
        const Vec3f& c = its.vertices[tri[2]];
        return 0.5 * (b - a).cross(c - a).cast<double>().norm();
    };

    for (size_t start = 0; start < nfaces; ++start) {
        if (visited[start])
            continue;

        ColorPatch patch;
        patch.patch_id = (int)patches.size();
        patch.color_label = face_colors[start];

        std::queue<int> q;
        q.push((int)start);
        visited[start] = 1;

        while (!q.empty()) {
            const int fi = q.front();
            q.pop();
            patch.patch_faces.push_back(fi);
            patch.patch_area += face_area(fi);
            face_to_patch[fi] = (int)patches.size();

            if (fi >= (int)neighbors.size())
                continue;
            for (int e = 0; e < 3; ++e) {
                const int nb = neighbors[fi][e];
                if (nb < 0 || nb >= (int)nfaces || visited[nb])
                    continue;
                if (face_colors[nb] != face_colors[start])
                    continue;
                visited[nb] = 1;
                q.push(nb);
            }
        }

        patches.push_back(std::move(patch));
    }

    const int npatches = (int)patches.size();
    std::vector<std::unordered_map<int, double>>& shared = shared_out;
    shared.assign(npatches, {});
    for (int fi = 0; fi < (int)nfaces; ++fi) {
        if (fi >= (int)neighbors.size())
            continue;
        const int pid = face_to_patch[fi];
        if (pid < 0)
            continue;
        for (int e = 0; e < 3; ++e) {
            const int nb = neighbors[fi][e];
            if (nb <= fi || nb >= (int)nfaces)
                continue;
            const int qid = face_to_patch[nb];
            if (qid < 0 || qid == pid)
                continue;
            const Vec2i edge = its_triangle_edge(its.indices[fi], e);
            if (edge[0] < 0 || edge[1] < 0 ||
                edge[0] >= (int)its.vertices.size() ||
                edge[1] >= (int)its.vertices.size())
                continue;
            const double len = (its.vertices[edge[0]] - its.vertices[edge[1]]).cast<double>().norm();
            shared[pid][qid] += len;
            shared[qid][pid] += len;
        }
    }
    return patches;
}

// Greedy region merge on the patch adjacency graph. The smallest live region is
// absorbed into the neighbor with the best shared boundary length weighted by
// color affinity 1 / (1 + (dE00 / sigma)^2), so a close color wins over a longer
// contact with a contrasting one. The absorbed region takes that neighbor's
// color, the merged region keeps the summed area, and every other former
// neighbor of the absorbed region with that same color joins it too, so
// adjacent live regions never share a color. All other regions only grow, so
// the recorded key areas never decrease. Merging stops at max_key_area.
void build_gap_merge_events(
    GapPreviewState& state,
    std::vector<std::unordered_map<int, double>> shared,
    double max_key_area)
{
    using FaceColor = std::array<std::size_t, 3>;
    state.merge_events.clear();
    state.merge_members.clear();
    state.applied_events = 0;

    const int n = (int)state.color_patches.size();
    if (n == 0 || (int)shared.size() != n)
        return;

    // Region colors always stay within the simplified palette, so regions carry a
    // palette index and the color affinity is looked up from a K x K table.
    std::vector<FaceColor>   palette;
    std::map<FaceColor, int> palette_index;
    std::vector<double>           area(n);
    std::vector<int>              color(n);
    std::vector<std::vector<int>> members(n);
    std::vector<int>              version(n, 0);
    std::vector<char>             alive(n, 1);
    for (int i = 0; i < n; ++i) {
        const FaceColor& label = state.color_patches[i].color_label;
        auto [it, inserted] = palette_index.emplace(label, (int)palette.size());
        if (inserted)
            palette.push_back(label);
        area[i]    = state.color_patches[i].patch_area;
        color[i]   = it->second;
        members[i] = {i};
    }

    const int k = (int)palette.size();
    std::vector<double> affinity((size_t)k * k, 1.0);
    for (int i = 0; i < k; ++i) {
        for (int j = i + 1; j < k; ++j) {
            const double de = compute_delta_e(palette[i], palette[j]) / kGapMergeColorSigma;
            const double w  = 1.0 / (1.0 + de * de);
            affinity[(size_t)i * k + j] = w;
            affinity[(size_t)j * k + i] = w;
        }
    }

    struct HeapItem {
        double area;
        int    region;
        int    version;
        bool operator>(const HeapItem& rhs) const {
            if (area != rhs.area)
                return area > rhs.area;
            return region > rhs.region;
        }
    };
    std::priority_queue<HeapItem, std::vector<HeapItem>, std::greater<HeapItem>> heap;
    for (int i = 0; i < n; ++i) {
        if (!shared[i].empty())
            heap.push({area[i], i, version[i]});
    }

    // Fold two regions into one carrying merged_color. The side with more
    // neighbors survives so adjacency updates stay small-to-large.
    auto fold = [&](int keep, int drop, int merged_color) -> int {
        if (shared[keep].size() < shared[drop].size())
            std::swap(keep, drop);
        area[keep] += area[drop];
        if (members[keep].size() < members[drop].size())
            members[keep].swap(members[drop]);
        members[keep].insert(members[keep].end(), members[drop].begin(), members[drop].end());
        std::vector<int>().swap(members[drop]);

        shared[keep].erase(drop);
        for (const auto& [nb, len] : shared[drop]) {
            if (nb == keep)
                continue;
            shared[keep][nb] += len;
            auto& nb_shared = shared[nb];
            nb_shared.erase(drop);
            nb_shared[keep] += len;
        }
        std::unordered_map<int, double>().swap(shared[drop]);

        color[keep] = merged_color;
        alive[drop] = 0;
        return keep;
    };

    std::vector<int> same_color;
    while (!heap.empty()) {
        const HeapItem top = heap.top();
        heap.pop();
        const int a = top.region;
        if (!alive[a] || top.version != version[a])
            continue;
        if (area[a] >= max_key_area)
            break;
        if (shared[a].empty())
            continue;

        int    b          = -1;
        double best_score = -1.0;
        double best_area  = -1.0;
        const double* affinity_row = affinity.data() + (size_t)color[a] * k;
        for (const auto& [q, len] : shared[a]) {
            const double score = len * affinity_row[color[q]];
            const double qarea = area[q];
            if (score > best_score ||
                (score == best_score && (qarea > best_area || (qarea == best_area && q < b)))) {
                b          = q;
                best_score = score;
                best_area  = qarea;
            }
        }
        if (b < 0)
            continue;

        const int target_color = color[b];
        GapMergeEvent ev;
        ev.key_area     = area[a];
        ev.from_color   = palette[color[a]];
        ev.to_color     = palette[target_color];
        ev.member_begin = (int)state.merge_members.size();
        state.merge_members.insert(state.merge_members.end(), members[a].begin(), members[a].end());
        ev.member_end   = (int)state.merge_members.size();
        state.merge_events.push_back(ev);

        same_color.clear();
        for (const auto& [q, len] : shared[a]) {
            if (q != b && color[q] == target_color)
                same_color.push_back(q);
        }

        int merged = fold(b, a, target_color);
        for (int c : same_color)
            merged = fold(merged, c, target_color);
        ++version[merged];
        if (!shared[merged].empty())
            heap.push({area[merged], merged, version[merged]});
    }
}

void build_gap_preview_regions(
    GapPreviewState& state,
    const PaintedMesh& painted,
    const std::vector<std::array<std::size_t, 3>>& face_colors)
{
    std::vector<std::unordered_map<int, double>> shared;
    state.color_patches = build_color_patches(painted, face_colors, shared);
    state.gap_total_area = 0.0;
    for (const auto& patch : state.color_patches)
        state.gap_total_area += patch.patch_area;
    build_gap_merge_events(state, std::move(shared),
                           (kGapAreaMaxPercent / 100.0) * state.gap_total_area);
}

std::unique_ptr<GapPreviewState> make_gap_preview_state(const PaintedMesh& painted)
{
    auto state = std::make_unique<GapPreviewState>();
    state->simplified_face_colors = painted.face_colors;
    state->simplified_cluster_colors = painted.cluster_colors;
    state->display_face_colors = painted.face_colors;
    build_gap_preview_regions(*state, painted, painted.face_colors);
    return state;
}

void write_patch_display_color(
    GapPreviewState& state,
    const ColorPatch& patch,
    const std::array<std::size_t, 3>& color)
{
    for (int fi : patch.patch_faces) {
        if (fi >= 0 && fi < (int)state.display_face_colors.size())
            state.display_face_colors[fi] = color;
    }
}

std::vector<std::array<std::size_t, 3>> remaining_cluster_colors(
    const std::vector<std::array<std::size_t, 3>>& simplified_clusters,
    const std::vector<std::array<std::size_t, 3>>& display_colors)
{
    std::set<std::array<std::size_t, 3>> remaining(display_colors.begin(), display_colors.end());
    std::vector<std::array<std::size_t, 3>> ordered;
    ordered.reserve(remaining.size());
    for (const auto& color : simplified_clusters) {
        auto it = remaining.find(color);
        if (it == remaining.end())
            continue;
        ordered.push_back(color);
        remaining.erase(it);
    }
    for (const auto& color : display_colors) {
        auto it = remaining.find(color);
        if (it == remaining.end())
            continue;
        ordered.push_back(color);
        remaining.erase(it);
    }
    return ordered;
}

} // namespace

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

static wxString default_filament_display_name(int display_number)
{
    return wxString::Format(_L("Filament %d"), display_number);
}

static std::string default_filament_stored_name(int display_number)
{
    const wxString label = default_filament_display_name(display_number);
    return std::string(label.ToUTF8().data());
}

static std::string texture_normalize_color_hex(std::string hex)
{
    if (hex.empty())
        return "#808080";
    if (hex.front() != '#')
        hex = "#" + hex;
    return decompose_normalize_color_hex(std::move(hex));
}

static std::string texture_rgba_to_hex(const std::array<float, 4>& rgba)
{
    return wxString::Format("#%02X%02X%02X",
        (unsigned char)std::clamp(rgba[0] * 255.f, 0.f, 255.f),
        (unsigned char)std::clamp(rgba[1] * 255.f, 0.f, 255.f),
        (unsigned char)std::clamp(rgba[2] * 255.f, 0.f, 255.f)).ToStdString();
}

static bool texture_entry_is_physical(TextureFilamentKind kind)
{
    return kind == TextureFilamentKind::ExistingPhysical || kind == TextureFilamentKind::NewPhysical;
}

static bool texture_entry_is_mixed(TextureFilamentKind kind)
{
    return kind == TextureFilamentKind::ExistingMixed || kind == TextureFilamentKind::NewMixed;
}

static std::string texture_entry_live_display_name(const TextureFilamentEntry& entry)
{
    if (entry.kind == TextureFilamentKind::ExistingPhysical) {
        auto* pb = wxGetApp().preset_bundle;
        if (pb && entry.project_config_index < pb->filament_presets.size()) {
            if (const Preset* preset = pb->filaments.find_preset(pb->filament_presets[entry.project_config_index], false)) {
                const std::string label = preset->label(false);
                if (!label.empty())
                    return label;
            }
        }
    }
    return entry.name;
}

static std::vector<int> texture_mixed_component_dialog_indices(const TextureFilamentEntry& entry)
{
    std::vector<int> comps;
    comps.reserve(entry.mixed_components.size());
    for (unsigned int c : entry.mixed_components) {
        if (c >= 1)
            comps.push_back((int)c - 1);
    }
    return comps;
}

static std::string texture_mixed_filament_display_name(
    const std::vector<TextureFilamentEntry>& entries,
    const std::vector<int>& component_dialog_indices)
{
    for (int idx : component_dialog_indices) {
        if (idx < 0 || idx >= (int)entries.size())
            continue;
        const std::string name = texture_entry_live_display_name(entries[idx]);
        if (!name.empty())
            return name;
    }
    return DEFAULT_VIRTUAL_FILAMENT_NAME;
}

static void texture_sync_mixed_filament_name(std::vector<TextureFilamentEntry>& entries,
                                            std::vector<std::string>& names,
                                            int mixed_idx,
                                            const std::vector<int>& component_dialog_indices)
{
    if (mixed_idx < 0 || mixed_idx >= (int)entries.size())
        return;
    const std::string name = texture_mixed_filament_display_name(entries, component_dialog_indices);
    entries[mixed_idx].name = name;
    if (mixed_idx < (int)names.size())
        names[mixed_idx] = name;
}

static wxString texture_filament_label_wx(const std::vector<TextureFilamentEntry>& entries,
                                         const std::vector<std::string>& names,
                                         int idx,
                                         int display_number)
{
    if (idx >= 0 && idx < (int)entries.size() && texture_entry_is_mixed(entries[idx].kind)) {
        const std::string name = texture_mixed_filament_display_name(
            entries, texture_mixed_component_dialog_indices(entries[idx]));
        if (!name.empty())
            return filament_name_to_wx_string(name);
    }
    if (idx >= 0 && idx < (int)names.size())
        return filament_name_to_wx_string(names[idx]);
    return default_filament_display_name(display_number);
}

// Reuse a slot as a base color only when it belongs to the requested series.
static std::string texture_entry_material_type(const TextureFilamentEntry& entry)
{
    if (entry.kind == TextureFilamentKind::ExistingPhysical) {
        auto* preset_bundle = wxGetApp().preset_bundle;
        if (preset_bundle && entry.project_config_index < preset_bundle->filament_presets.size()) {
            Preset* preset = preset_bundle->filaments.find_preset(
                preset_bundle->filament_presets[entry.project_config_index]);
            return filament_type_for_color_decompose(preset);
        }
    }
    return entry.type.empty() ? std::string(DEFAULT_VIRTUAL_FILAMENT_SHORT_TYPE) : entry.type;
}

static std::vector<TextureFilamentEntry> collect_project_filament_entries()
{
    std::vector<TextureFilamentEntry> filament_entries;
    auto* preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle)
        return filament_entries;

    auto& project_config = preset_bundle->project_config;
    auto* colours_opt = project_config.option<ConfigOptionStrings>("filament_colour");
    auto* is_mixed_opt = project_config.option<ConfigOptionBools>("filament_is_mixed");
    auto* type_opt = project_config.option<ConfigOptionStrings>("filament_type");
    auto* components_opt = project_config.option<ConfigOptionStrings>("filament_mixed_components");
    auto* ratios_opt = project_config.option<ConfigOptionStrings>("filament_mixed_sublayer_ratios");
    const size_t total = preset_bundle->filament_presets.size();
    filament_entries.reserve(total);
    for (size_t i = 0; i < total; ++i) {
        TextureFilamentEntry entry;
        entry.kind = (is_mixed_opt && i < is_mixed_opt->values.size() && is_mixed_opt->values[i]) ?
            TextureFilamentKind::ExistingMixed : TextureFilamentKind::ExistingPhysical;
        entry.dialog_index = (int)filament_entries.size();
        entry.project_config_index = i;
        entry.color_hex = (colours_opt && i < colours_opt->values.size()) ? colours_opt->values[i] : "#808080";
        entry.type = (type_opt && i < type_opt->values.size()) ? type_opt->values[i] : "";

        std::string name;
        if (i < preset_bundle->filament_presets.size()) {
            auto* preset = preset_bundle->filaments.find_preset(preset_bundle->filament_presets[i]);
            if (preset)
                name = preset->label(false);
        }
        if (name.empty())
            name = "Filament " + std::to_string(i + 1);
        entry.name = name;

        if (entry.kind == TextureFilamentKind::ExistingMixed) {
            if (components_opt && i < components_opt->values.size())
                entry.mixed_components = Slic3r::parse_mixed_components(components_opt->values[i]);
            std::vector<double> ratios = Slic3r::parse_mixed_ratios(
                ratios_opt && i < ratios_opt->values.size() ? ratios_opt->values[i] : "",
                entry.mixed_components.size());
            entry.mixed_ratios.reserve(ratios.size());
            for (double ratio : ratios)
                entry.mixed_ratios.push_back((int)std::lround(ratio * 100.0));
        }
        filament_entries.push_back(std::move(entry));
    }
    return filament_entries;
}

static bool starts_with_preset_name(const std::string& name, const char* prefix)
{
    const size_t prefix_len = std::strlen(prefix);
    return name.size() >= prefix_len && name.compare(0, prefix_len, prefix) == 0;
}

// Resolve a visible compatible preset whose name starts with series_display_name
// (e.g. "Bambu PLA Basic", "Bambu PETG", "Generic ABS").
// For the printer default series, prefer default_filament_profile; other series
// return empty if no matching preset is visible, instead of falling back to the
// currently selected filament.
static std::string resolve_series_virtual_filament_preset_name(const char* series_display_name)
{
    auto* preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle)
        return {};

    auto valid_preset_name = [preset_bundle](const std::string& name) -> bool {
        return !name.empty() && preset_bundle->filaments.find_preset(name, false) != nullptr;
    };

    const bool is_default_series = std::strcmp(series_display_name, DEFAULT_VIRTUAL_FILAMENT_NAME) == 0;
    if (is_default_series) {
        const auto* default_profiles = preset_bundle->printers.get_selected_preset()
            .config.option<ConfigOptionStrings>("default_filament_profile");
        if (default_profiles) {
            for (const std::string& name : default_profiles->values) {
                if (starts_with_preset_name(name, series_display_name) && valid_preset_name(name))
                    return name;
            }
        }
    }

    for (const Preset& preset : preset_bundle->filaments.get_presets()) {
        if (preset.is_system && preset.is_visible && preset.is_compatible &&
            starts_with_preset_name(preset.name, series_display_name)) {
            return preset.name;
        }
    }

    for (const Preset& preset : preset_bundle->filaments.get_presets()) {
        if (preset.is_visible && preset.is_compatible &&
            starts_with_preset_name(preset.name, series_display_name)) {
            return preset.name;
        }
    }

    if (!is_default_series)
        return {};

    std::string selected = preset_bundle->filaments.get_selected_preset_name();
    return valid_preset_name(selected) ? selected : std::string();
}

static std::string resolve_default_virtual_filament_preset_name()
{
    return resolve_series_virtual_filament_preset_name(DEFAULT_VIRTUAL_FILAMENT_NAME);
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

// Place the filament popup so its left edge sits against the import dialog's
// right edge. Vertically, the popup bottom matches align_bottom (the matching
// page hint bar) instead of the screen edge. If it does not fit on the current
// display, fall back to the left of the dialog.
static wxPoint texture_import_side_popup_position(wxWindow* dialog, int align_bottom, const wxSize& popup_size)
{
    if (!dialog)
        return wxDefaultPosition;

    const wxRect dlg = dialog->GetScreenRect();
    const int pop_w = std::max(popup_size.x, 1);
    const int pop_h = std::max(popup_size.y, 1);

    const int display_idx = wxDisplay::GetFromWindow(dialog);
    const wxRect display = (display_idx != wxNOT_FOUND)
        ? wxDisplay(display_idx).GetClientArea()
        : wxDisplay().GetClientArea();

    wxPoint pos;
    pos.x = dlg.GetX() + dlg.GetWidth();
    if (pos.x + pop_w > display.GetX() + display.GetWidth()) {
        const int left_x = dlg.GetX() - pop_w;
        pos.x = left_x >= display.GetX() ? left_x
                                         : std::max(display.GetX(), display.GetX() + display.GetWidth() - pop_w);
    }

    int bottom = align_bottom;
    if (bottom <= 0)
        bottom = dlg.GetY() + dlg.GetHeight();
    pos.y = bottom - pop_h;
    if (pos.y < display.GetTop())
        pos.y = display.GetTop();
    return pos;
}

static std::array<std::size_t, 3> texture_rgba_to_cluster_color(const std::array<float, 4>& rgba)
{
    auto to_u8 = [](float v) -> std::size_t {
        return (std::size_t)std::clamp(std::lround(v * 255.f), 0L, 255L);
    };
    return {to_u8(rgba[0]), to_u8(rgba[1]), to_u8(rgba[2])};
}

static std::string texture_blend_mixed_color_hex(const std::vector<unsigned int>& components,
                                                 const std::vector<int>& ratios,
                                                 const std::vector<std::array<float, 4>>& colors)
{
    float r = 0.f, g = 0.f, b = 0.f;
    int total = 0;
    for (size_t i = 0; i < components.size() && i < ratios.size(); ++i) {
        const int idx = components[i] >= 1 ? (int)components[i] - 1 : -1;
        if (idx < 0 || idx >= (int)colors.size())
            continue;
        const int ratio = std::max(0, ratios[i]);
        r += colors[idx][0] * (float)ratio;
        g += colors[idx][1] * (float)ratio;
        b += colors[idx][2] * (float)ratio;
        total += ratio;
    }
    if (total <= 0)
        return "#808080";
    const float inv = 1.f / (float)total;
    return texture_rgba_to_hex({r * inv, g * inv, b * inv, 1.f});
}

static std::string texture_filament_display_name_from_preset(const std::string& preset_name)
{
    auto* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        if (const Preset* preset = preset_bundle->filaments.find_preset(preset_name, false)) {
            const std::string label = preset->label(false);
            if (!label.empty())
                return label;
        }
    }
    return preset_name.empty() ? std::string(DEFAULT_VIRTUAL_FILAMENT_NAME) : preset_name;
}

static std::string texture_filament_type_from_preset(const std::string& preset_name)
{
    auto* preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle)
        return DEFAULT_VIRTUAL_FILAMENT_SHORT_TYPE;
    const Preset* preset = preset_bundle->filaments.find_preset(preset_name, false);
    if (!preset)
        return DEFAULT_VIRTUAL_FILAMENT_SHORT_TYPE;
    return filament_type_for_color_decompose(const_cast<Preset*>(preset));
}

static std::string texture_family_type_from_preset_name(const std::string& preset_name)
{
    auto* preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle || preset_name.empty())
        return {};
    Preset* preset = preset_bundle->filaments.find_preset(preset_name, false);
    if (!preset)
        return {};
    std::string display_type;
    return preset->config.get_filament_type(display_type);
}

static std::string texture_filament_preset_name_of(const TextureFilamentEntry& entry)
{
    if (!entry.preset_name.empty())
        return entry.preset_name;
    auto* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle &&
        (entry.kind == TextureFilamentKind::ExistingPhysical ||
         entry.kind == TextureFilamentKind::ExistingMixed) &&
        entry.project_config_index < preset_bundle->filament_presets.size()) {
        return preset_bundle->filament_presets[entry.project_config_index];
    }
    return {};
}

// Coarse family type (PLA / PETG / ABS / ...). Official series names such as
// "PLA Basic" are not used here. Empty type, preset parse failure, and broken
// mixed slots (mismatched components) return empty so callers skip them from
// the auto-match vote.
std::string texture_entry_family_type(const TextureFilamentEntry& entry,
                                            const std::vector<TextureFilamentEntry>& entries)
{
    auto type_of_slot = [](const TextureFilamentEntry& e) -> std::string {
        const std::string preset = texture_filament_preset_name_of(e);
        if (!preset.empty())
            return texture_family_type_from_preset_name(preset); // empty = parse fail, skip vote
        if (!e.type.empty() && e.type.find(' ') == std::string::npos)
            return e.type; // no preset: only a real short family name may vote
        return {};
    };

    if (texture_entry_is_mixed(entry.kind)) {
        std::string ref;
        bool found = false;
        bool mismatch = false;
        auto* preset_bundle = wxGetApp().preset_bundle;
        for (unsigned int comp : entry.mixed_components) {
            if (comp < 1)
                continue;
            const size_t idx = (size_t)comp - 1;
            std::string ft;
            if (entry.kind == TextureFilamentKind::ExistingMixed) {
                if (preset_bundle && idx < preset_bundle->filament_presets.size())
                    ft = texture_family_type_from_preset_name(preset_bundle->filament_presets[idx]);
            } else if (idx < entries.size() && texture_entry_is_physical(entries[idx].kind)) {
                ft = type_of_slot(entries[idx]);
            }
            if (ft.empty())
                continue;
            if (!found) {
                ref = ft;
                found = true;
            } else if (ft != ref) {
                mismatch = true;
                break;
            }
        }
        if (mismatch)
            return {};
        if (found)
            return ref;
        return type_of_slot(entry);
    }

    return type_of_slot(entry); // empty => skip vote (empty type, parse fail)
}

static int texture_family_type_priority(const std::string& type)
{
    if (type == "PLA")
        return 3;
    if (type == "PETG")
        return 2;
    if (type == "ABS")
        return 1;
    return 0;
}

static std::string pick_auto_match_filament_type(const std::vector<TextureFilamentEntry>& entries,
                                                size_t existing_count)
{
    std::map<std::string, int> counts;
    std::vector<std::string> first_seen;
    const size_t n = std::min(existing_count, entries.size());
    for (size_t i = 0; i < n; ++i) {
        const std::string ft = texture_entry_family_type(entries[i], entries);
        if (ft.empty())
            continue;
        if (counts.find(ft) == counts.end())
            first_seen.push_back(ft);
        counts[ft]++;
    }
    // No slot produced a usable family vote: auto-match against PLA Basic.
    // Keep returning the short name "PLA" so match_type compares equal to
    // texture_entry_family_type(); resolve_auto_match_preset_name maps it to
    // Bambu PLA Basic / m_default_virtual_filament_preset_name.
    if (counts.empty())
        return DEFAULT_VIRTUAL_FILAMENT_SHORT_TYPE;

    int best_count = 0;
    for (const auto& kv : counts)
        best_count = std::max(best_count, kv.second);

    std::string best;
    int best_priority = -1;
    size_t best_order = (size_t)-1;
    for (size_t i = 0; i < first_seen.size(); ++i) {
        const std::string& type = first_seen[i];
        if (counts[type] != best_count)
            continue;
        const int priority = texture_family_type_priority(type);
        if (priority > best_priority || (priority == best_priority && i < best_order)) {
            best = type;
            best_priority = priority;
            best_order = i;
        }
    }
    return best.empty() ? std::string(DEFAULT_VIRTUAL_FILAMENT_SHORT_TYPE) : best;
}

static std::string resolve_auto_match_preset_name(const std::vector<TextureFilamentEntry>& entries,
                                                 size_t existing_count,
                                                 const std::string& family_type,
                                                 const std::string& pla_basic_fallback)
{
    std::map<std::string, int> counts;
    std::vector<std::string> first_seen;
    const size_t n = std::min(existing_count, entries.size());
    for (size_t i = 0; i < n; ++i) {
        if (texture_entry_family_type(entries[i], entries) != family_type)
            continue;
        const std::string preset_name = texture_filament_preset_name_of(entries[i]);
        if (preset_name.empty())
            continue;
        if (counts.find(preset_name) == counts.end())
            first_seen.push_back(preset_name);
        counts[preset_name]++;
    }
    if (!first_seen.empty()) {
        int best_count = 0;
        for (const auto& kv : counts)
            best_count = std::max(best_count, kv.second);
        for (const auto& preset_name : first_seen) {
            if (counts[preset_name] == best_count)
                return preset_name;
        }
    }

    if (family_type == DEFAULT_VIRTUAL_FILAMENT_SHORT_TYPE) {
        const std::string pla = resolve_series_virtual_filament_preset_name(DEFAULT_VIRTUAL_FILAMENT_NAME);
        if (!pla.empty())
            return pla;
    }

    const std::string bambu = std::string("Bambu ") + family_type;
    std::string found = resolve_series_virtual_filament_preset_name(bambu.c_str());
    if (!found.empty())
        return found;

    const std::string generic = std::string("Generic ") + family_type;
    found = resolve_series_virtual_filament_preset_name(generic.c_str());
    if (!found.empty())
        return found;

    return pla_basic_fallback;
}

struct TextureAddFilamentTypeOption {
    std::string preset_name;
    std::string display_name;
    std::string filament_id;
};

static std::vector<TextureAddFilamentTypeOption> collect_texture_add_filament_types()
{
    std::vector<TextureAddFilamentTypeOption> out;
    auto* preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle)
        return out;

    auto collect = [&](bool official_only) {
        std::map<std::string, TextureAddFilamentTypeOption> by_id;
        for (const Preset& preset : preset_bundle->filaments.get_presets()) {
            if (preset.is_default || !preset.is_visible || !preset.is_compatible)
                continue;
            if (official_only && !boost::algorithm::istarts_with(preset.name, "Bambu"))
                continue;
            const std::string key = preset.filament_id.empty() ? preset.name : preset.filament_id;
            if (by_id.find(key) != by_id.end())
                continue;
            TextureAddFilamentTypeOption opt;
            opt.preset_name = preset.name;
            opt.display_name = preset.label(false);
            if (opt.display_name.empty())
                opt.display_name = preset.name;
            opt.filament_id = preset.filament_id;
            by_id.emplace(key, std::move(opt));
        }
        out.clear();
        out.reserve(by_id.size());
        for (auto& kv : by_id)
            out.push_back(std::move(kv.second));
    };

    collect(true);
    if (out.empty())
        collect(false);

    std::sort(out.begin(), out.end(), [](const TextureAddFilamentTypeOption& a,
                                         const TextureAddFilamentTypeOption& b) {
        return a.display_name < b.display_name;
    });
    return out;
}

static wxString texture_combo_type_label(const std::string& display_name, bool* has_bambu_prefix)
{
    wxString name = filament_name_to_wx_string(display_name);
    const bool bambu = name.StartsWith("Bambu ");
    if (has_bambu_prefix)
        *has_bambu_prefix = bambu;
    if (bambu)
        name = name.Mid(6);
    return name;
}

// Texture-import-only add-filament dialog: type ComboBox + official color grid.
class TextureImportAddFilamentDialog : public DPIDialog
{
public:
    TextureImportAddFilamentDialog(wxWindow* parent, const std::string& default_preset_name, int display_number)
        : DPIDialog(parent, wxID_ANY, _L("Add Filament"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
        , m_display_number(display_number > 0 ? display_number : 1)
        , m_query(new FilamentColorCodeQuery())
    {
        SetBackgroundColour(dark_or(*wxWHITE, wxColour(0x2D, 0x2D, 0x31)));
        m_types = collect_texture_add_filament_types();
        m_preset_name = default_preset_name;
        m_colour = wxColour(128, 128, 128);

        auto* container = new wxBoxSizer(wxHORIZONTAL);
        auto* main = new wxBoxSizer(wxVERTICAL);

        m_type_combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                    wxSize(-1, FromDIP(28)), 0, nullptr, wxCB_READONLY | CB_NO_DROP_ICON);
        m_type_combo->SetFont(Label::Body_13);
        m_type_combo->SetMinSize(wxSize(FromDIP(220), FromDIP(28)));
        const wxBitmap bambu_icon = create_scaled_bitmap("BambuStudioBlack", this, 16);
        int default_sel = 0;
        for (size_t i = 0; i < m_types.size(); ++i) {
            bool has_bambu = false;
            const wxString label = texture_combo_type_label(m_types[i].display_name, &has_bambu);
            if (has_bambu && bambu_icon.IsOk())
                m_type_combo->Append(label, bambu_icon);
            else
                m_type_combo->Append(label);
            if (!default_preset_name.empty() &&
                (m_types[i].preset_name == default_preset_name ||
                 boost::algorithm::starts_with(default_preset_name, m_types[i].preset_name) ||
                 boost::algorithm::starts_with(m_types[i].preset_name, default_preset_name))) {
                default_sel = (int)i;
            }
        }
        if (!m_types.empty()) {
            m_type_combo->SetSelection(default_sel);
            m_preset_name = m_types[default_sel].preset_name;
        }
        m_type_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& evt) {
            evt.StopPropagation();
            on_type_changed();
        });
        create_combo_arrow();

        main->AddSpacer(FromDIP(4));
        main->Add(create_preview_panel(), 0, wxEXPAND);
        main->AddSpacer(FromDIP(12));
        main->Add(create_separator_line(), 0, wxEXPAND);

        m_grid_host = new wxPanel(this);
        m_grid_host->SetBackgroundColour(GetBackgroundColour());
        m_grid_host->SetSizer(new wxBoxSizer(wxVERTICAL));
        main->Add(m_grid_host, 1, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(8));

        m_more_btn = new Button(this, "+ " + _L("More Colors"));
        m_more_btn->SetMinSize(wxSize(-1, FromDIP(36)));
        StateColor more_bg(
            std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
            std::pair<wxColour, int>(wxColour(248, 248, 248), StateColor::Normal));
        m_more_btn->SetBackgroundColor(more_bg);
        m_more_btn->SetBorderStyle(wxPENSTYLE_SHORT_DASH);
        m_more_btn->SetCornerRadius(FromDIP(0));
        m_more_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { pick_custom_color(); });
        main->Add(m_more_btn, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(8));
        main->AddSpacer(FromDIP(8));

        auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
        btn_row->AddStretchSpacer();
        auto* cancel_btn = new Button(this, _L("Cancel"), "", 0, 0, wxID_CANCEL);
        cancel_btn->SetMinSize(wxSize(FromDIP(55), FromDIP(24)));
        cancel_btn->SetCornerRadius(FromDIP(12));
        StateColor cancel_bg(
            std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
            std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
            std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
        StateColor cancel_bd(std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal));
        StateColor cancel_fg(std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal));
        cancel_btn->SetBackgroundColor(cancel_bg);
        cancel_btn->SetBorderColor(cancel_bd);
        cancel_btn->SetTextColor(cancel_fg);
        cancel_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
        btn_row->Add(cancel_btn, 0, wxEXPAND);
        btn_row->AddSpacer(FromDIP(10));

        auto* ok_btn = new Button(this, _L("OK"), "", 0, 0, wxID_OK);
        ok_btn->SetMinSize(wxSize(FromDIP(55), FromDIP(24)));
        ok_btn->SetCornerRadius(FromDIP(12));
        StateColor ok_bg(
            std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
            std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
        StateColor ok_bd(std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
        StateColor ok_fg(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));
        ok_btn->SetBackgroundColor(ok_bg);
        ok_btn->SetBorderColor(ok_bd);
        ok_btn->SetTextColor(ok_fg);
        ok_btn->SetFocus();
        ok_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_OK); });
        btn_row->Add(ok_btn, 0, wxEXPAND);
        main->Add(btn_row, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

        container->Add(main, 1, wxEXPAND | wxALL, FromDIP(16));
        SetSizer(container);
        update_preview_custom(m_colour);
        rebuild_color_grid(false);
        Layout();
        Fit();
        SetMinSize(wxSize(GetSize().x, FromDIP(180)));
        wxGetApp().UpdateDlgDarkUI(this);
        if (m_combo_arrow && m_type_combo) {
            m_combo_arrow->SetBitmap(create_scaled_bitmap("drop_down", m_type_combo, 16));
            layout_combo_arrow();
        }
    }

    ~TextureImportAddFilamentDialog() override
    {
        delete m_query;
        m_query = nullptr;
    }

    wxColour GetColour() const { return m_colour; }
    std::string GetPresetName() const { return m_preset_name; }

    void on_dpi_changed(const wxRect&) override
    {
        Layout();
        Fit();
    }

private:
    static constexpr int kGridCols = 9;
    static constexpr int kMaxVisibleRows = 7;

    wxSize color_demo_size() const { return wxSize(FromDIP(50), FromDIP(50)); }

    wxBoxSizer* create_preview_panel()
    {
        auto* preview_sizer = new wxBoxSizer(wxHORIZONTAL);

        m_color_demo = new wxPanel(this, wxID_ANY, wxDefaultPosition, color_demo_size(),
                                   wxFULL_REPAINT_ON_RESIZE);
        m_color_demo->SetMinSize(color_demo_size());
        m_color_demo->SetMaxSize(color_demo_size());
        m_color_demo->SetBackgroundStyle(wxBG_STYLE_PAINT);
        m_color_demo->Bind(wxEVT_PAINT, &TextureImportAddFilamentDialog::on_color_demo_paint, this);
        preview_sizer->Add(m_color_demo, 0, wxALIGN_CENTER_VERTICAL);
        preview_sizer->AddSpacer(FromDIP(12));
        if (m_type_combo)
            preview_sizer->Add(m_type_combo, 1, wxALIGN_CENTER_VERTICAL);
        return preview_sizer;
    }

    void create_combo_arrow()
    {
        if (!m_type_combo)
            return;
        const wxBitmap bmp = create_scaled_bitmap("drop_down", m_type_combo, 16);
        m_combo_arrow = new wxStaticBitmap(m_type_combo, wxID_ANY, bmp);
        m_combo_arrow->SetCursor(wxCursor(wxCURSOR_HAND));
        m_combo_arrow->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
            if (!m_type_combo)
                return;
            wxMouseEvent down(wxEVT_LEFT_DOWN);
            down.SetEventObject(m_type_combo);
            m_type_combo->GetEventHandler()->ProcessEvent(down);
        });
        m_type_combo->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
            layout_combo_arrow();
            e.Skip();
        });
        layout_combo_arrow();
    }

    void layout_combo_arrow()
    {
        if (!m_type_combo || !m_combo_arrow)
            return;
        const wxSize sz = m_type_combo->GetClientSize();
        wxSize as = m_combo_arrow->GetBestSize();
        if (as.x <= 0 || as.y <= 0)
            as = m_combo_arrow->GetSize();
        const int margin = m_type_combo->FromDIP(8);
        m_combo_arrow->SetSize(as);
        m_combo_arrow->Move(std::max(0, sz.x - as.x - margin), std::max(0, (sz.y - as.y) / 2));
    }

    void on_color_demo_paint(wxPaintEvent&)
    {
        if (!m_color_demo)
            return;
        // Use a plain PaintDC: wxGCDC/GDI+ interpolates the filament bitmap
        // against a white buffer and shows up as a white mosaic on the swatch.
        wxPaintDC dc(m_color_demo);
        const wxSize sz = m_color_demo->GetClientSize();
        dc.SetBrush(wxBrush(GetBackgroundColour()));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(0, 0, sz.x, sz.y);

        if (m_preview_bmp.IsOk())
            dc.DrawBitmap(m_preview_bmp, 0, 0, true);

        draw_filament_swatch_border(dc, m_colour, 0, 0, sz.x, sz.y);

        wxFont font = Label::Head_20;
        dc.SetFont(font);
        dc.SetTextForeground(m_colour.IsOk() && m_colour.GetLuminance() < 0.6
                                 ? *wxWHITE
                                 : texture_import_gray9000());
        const wxString num = wxString::Format("%d", m_display_number);
        const wxSize nsz = dc.GetTextExtent(num);
        dc.DrawText(num, (sz.x - nsz.x) / 2, (sz.y - nsz.y) / 2);
    }

    void set_preview_bitmap(const std::vector<wxColour>& wx_colors, bool gradient)
    {
        m_preview_bmp = create_filament_bitmap(wx_colors, color_demo_size(), gradient);
        if (m_color_demo)
            m_color_demo->Refresh();
    }

    wxBoxSizer* create_separator_line()
    {
        m_separator_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_separator_line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(1)));
        m_separator_line->SetBackgroundColour(wxColour(238, 238, 238));
        m_official_label = new wxStaticText(this, wxID_ANY, _L("Official Filament"),
                                            wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
        m_official_label->SetForegroundColour(wxColour(128, 128, 128));
        m_separator_sizer->Add(m_official_label, 0, wxEXPAND);
        m_separator_sizer->AddSpacer(FromDIP(8));
        m_separator_sizer->Add(m_separator_line, 1, wxALIGN_CENTER_VERTICAL);
        return m_separator_sizer;
    }

    void set_official_section_visible(bool visible)
    {
        if (m_official_label)
            m_official_label->Show(visible);
        if (m_separator_line)
            m_separator_line->Show(visible);
        if (m_grid_host) {
            m_grid_host->Show(visible);
            if (!visible)
                m_grid_host->SetMinSize(wxSize(0, 0));
        }
    }

    void update_preview_from_color_code(FilamentColorCode* color_code)
    {
        if (!color_code)
            return;
        const FilamentColor fila_color = color_code->GetFilaColor();
        const std::vector<wxColour> wx_colors = fila_color.GetColors();
        if (!wx_colors.empty())
            m_colour = wx_colors.front();
        set_preview_bitmap(wx_colors,
                           fila_color.m_color_type == FilamentColor::ColorType::GRADIENT_CLR);
    }

    void update_preview_custom(const wxColour& custom_color)
    {
        m_colour = custom_color;
        const std::vector<wxColour> wx_colors = {custom_color};
        set_preview_bitmap(wx_colors, false);
    }

    void on_type_changed()
    {
        const int sel = m_type_combo ? m_type_combo->GetSelection() : -1;
        if (sel < 0 || sel >= (int)m_types.size())
            return;
        m_preset_name = m_types[sel].preset_name;
        rebuild_color_grid(true);
        Layout();
        Fit();
    }

    bool load_color_codes(const std::string& filament_id)
    {
        m_color_codes = nullptr;
        if (!m_query || filament_id.empty())
            return false;
        m_color_codes = m_query->GetFilaInfoMap(wxString::FromUTF8(filament_id));
        if (!m_color_codes)
            return false;
        FilamentColor2CodeMap* color_map = m_color_codes->GetFilamentColor2CodeMap();
        return color_map && !color_map->empty();
    }

    void rebuild_color_grid(bool prompt_custom_if_missing)
    {
        if (!m_grid_host)
            return;
        if (auto* host_sizer = m_grid_host->GetSizer())
            host_sizer->Clear(true);
        else
            m_grid_host->DestroyChildren();
        m_selected_btn = nullptr;
        m_color_codes = nullptr;

        const int sel = m_type_combo ? m_type_combo->GetSelection() : -1;
        const std::string fila_id = (sel >= 0 && sel < (int)m_types.size()) ? m_types[sel].filament_id : std::string();
        const bool official = sel >= 0 && sel < (int)m_types.size() &&
                              boost::algorithm::istarts_with(m_types[sel].preset_name, "Bambu");

        if (!official || !load_color_codes(fila_id)) {
            set_official_section_visible(false);
            if (prompt_custom_if_missing)
                pick_custom_color();
            else
                update_preview_custom(m_colour);
            return;
        }

        FilamentColor2CodeMap* color_map = m_color_codes->GetFilamentColor2CodeMap();
        const int total = (int)color_map->size();
        const int needed_rows = std::max(1, (total + kGridCols - 1) / kGridCols);
        const bool need_scroll = needed_rows > kMaxVisibleRows;
        const wxSize btn_bmp_size(FromDIP(24), FromDIP(24));
        const wxSize btn_size(FromDIP(30), FromDIP(30));

        auto* scroll = new wxScrolledWindow(m_grid_host, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                            wxVSCROLL | wxNO_BORDER);
        scroll->SetBackgroundColour(m_grid_host->GetBackgroundColour());
        auto* grid = new wxGridSizer(needed_rows, kGridCols, FromDIP(2), FromDIP(2));

        wxBitmapButton* first_btn = nullptr;
        FilamentColorCode* first_code = nullptr;
        for (const auto& color_pair : *color_map) {
            const FilamentColor& fila_color = color_pair.first;
            FilamentColorCode* color_code = color_pair.second;
            if (!color_code)
                continue;
            std::vector<wxColour> wx_colors = fila_color.GetColors();
            wxBitmap btn_bmp = create_filament_bitmap(
                wx_colors, btn_bmp_size,
                fila_color.m_color_type == FilamentColor::ColorType::GRADIENT_CLR);
            if (!btn_bmp.IsOk())
                continue;

            auto* btn = new wxBitmapButton(scroll, wxID_ANY, btn_bmp, wxDefaultPosition, btn_size,
                                           wxBU_EXACTFIT | wxNO_BORDER);
            btn->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
            btn->SetToolTip(color_code->GetFilaColorName());
            if (!first_btn) {
                first_btn = btn;
                first_code = color_code;
            }
            btn->Bind(wxEVT_LEFT_DOWN, [this, btn, color_code, btn_size, btn_bmp_size](wxMouseEvent& evt) {
                select_color_code(btn, color_code, btn_size, btn_bmp_size);
                evt.Skip();
            });
            grid->Add(btn, 0, wxALL | wxALIGN_CENTER, FromDIP(1));
        }

        scroll->SetSizer(grid);
        if (need_scroll) {
            const int row_height = btn_size.GetHeight() + FromDIP(2);
            const int col_width = btn_size.GetWidth() + FromDIP(4);
            const int scrollbar_width = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);
            scroll->SetMinSize(wxSize(col_width * kGridCols + scrollbar_width, row_height * kMaxVisibleRows));
            scroll->FitInside();
            scroll->SetScrollRate(0, row_height);
        } else {
            scroll->FitInside();
            scroll->SetScrollRate(0, 0);
        }

        m_grid_host->GetSizer()->Add(scroll, 1, wxEXPAND);
        set_official_section_visible(true);
        m_grid_host->Layout();
        if (first_btn && first_code)
            select_color_code(first_btn, first_code, btn_size, btn_bmp_size);
        else
            update_preview_custom(m_colour);
    }

    void select_color_code(wxBitmapButton* btn, FilamentColorCode* color_code,
                           const wxSize& btn_size, const wxSize& btn_bmp_size)
    {
        if (!color_code)
            return;
        FilamentColor fila_color = color_code->GetFilaColor();
        const auto& colors = fila_color.GetColors();
        if (!colors.empty())
            m_colour = colors.front();
        if (m_selected_btn && m_selected_btn != btn) {
            m_selected_btn->Unbind(wxEVT_PAINT, &TextureImportAddFilamentDialog::on_selected_button_paint, this);
            m_selected_btn->Refresh();
        }
        m_selected_btn = btn;
        m_selected_btn_size = btn_size;
        m_selected_bmp_size = btn_bmp_size;
        btn->Unbind(wxEVT_PAINT, &TextureImportAddFilamentDialog::on_selected_button_paint, this);
        btn->Bind(wxEVT_PAINT, &TextureImportAddFilamentDialog::on_selected_button_paint, this);
        btn->Refresh();
        update_preview_from_color_code(color_code);
    }

    void on_selected_button_paint(wxPaintEvent& event)
    {
        wxWindow* button = dynamic_cast<wxWindow*>(event.GetEventObject());
        if (!button) {
            event.Skip();
            return;
        }
        // Same recipe as FilamentPickerDialog::OnButtonPaint. wxGCDC DrawBitmap
        // on MSW blends the swatch into a white buffer and looks like mosaic.
        wxPaintDC dc(button);
        dc.SetBrush(wxBrush(button->GetBackgroundColour()));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(0, 0, m_selected_btn_size.GetWidth(), m_selected_btn_size.GetHeight());
        auto* bmp_btn = dynamic_cast<wxBitmapButton*>(button);
        if (bmp_btn && bmp_btn->GetBitmap().IsOk()) {
            const wxBitmap& bmp = bmp_btn->GetBitmap();
            const int x = (m_selected_btn_size.GetWidth() - m_selected_bmp_size.GetWidth()) / 2;
            const int y = (m_selected_btn_size.GetHeight() - m_selected_bmp_size.GetHeight()) / 2;
            dc.DrawBitmap(bmp, x, y, true);
        }
        dc.SetPen(wxPen(wxColour("#00AE42"), 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(1, 1, m_selected_btn_size.GetWidth() - 1, m_selected_btn_size.GetHeight() - 1);
    }

    void pick_custom_color()
    {
        wxColourData data;
        data.SetChooseFull(true);
        if (m_colour.IsOk())
            data.SetColour(m_colour);
        wxColourData result = show_sys_picker_dialog(this, data);
        if (result.GetColour().IsOk()) {
            m_colour = result.GetColour();
            update_preview_custom(m_colour);
        }
        if (m_selected_btn) {
            m_selected_btn->Unbind(wxEVT_PAINT, &TextureImportAddFilamentDialog::on_selected_button_paint, this);
            m_selected_btn->Refresh();
            m_selected_btn = nullptr;
        }
    }

    ComboBox*                                 m_type_combo = nullptr;
    wxStaticBitmap*                           m_combo_arrow = nullptr;
    wxPanel*                                  m_color_demo = nullptr;
    wxBitmap                                  m_preview_bmp;
    int                                       m_display_number = 1;
    wxBoxSizer*                               m_separator_sizer = nullptr;
    wxStaticText*                             m_official_label = nullptr;
    wxPanel*                                  m_separator_line = nullptr;
    wxPanel*                                  m_grid_host = nullptr;
    Button*                                   m_more_btn = nullptr;
    wxBitmapButton*                           m_selected_btn = nullptr;
    wxSize                                    m_selected_btn_size;
    wxSize                                    m_selected_bmp_size;
    FilamentColorCodeQuery*                   m_query = nullptr;
    FilamentColorCodes*                       m_color_codes = nullptr;
    std::vector<TextureAddFilamentTypeOption> m_types;
    wxColour                                  m_colour;
    std::string                               m_preset_name;
};

// ============================================================
// Match-threshold gear + hover popup
// ============================================================

class MatchThresholdGear : public wxPanel
{
public:
    MatchThresholdGear(wxWindow* parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition,
                  wxSize(parent->FromDIP(16), parent->FromDIP(16)),
                  wxFULL_REPAINT_ON_RESIZE)
        , m_normal(this, "tree_set", 16)
        , m_hover(this, "tree_set_hover", 16)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(wxSize(FromDIP(16), FromDIP(16)));
        SetBackgroundColour(parent->GetBackgroundColour());
        Bind(wxEVT_PAINT, &MatchThresholdGear::on_paint, this);
        Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
    }

    void set_hovered(bool hovered)
    {
        if (m_hovered == hovered)
            return;
        m_hovered = hovered;
        Refresh();
    }

    void msw_rescale()
    {
        m_normal.msw_rescale();
        m_hover.msw_rescale();
        const int side = FromDIP(16);
        SetMinSize(wxSize(side, side));
        SetSize(wxSize(side, side));
        Refresh();
    }

private:
    void on_paint(wxPaintEvent&)
    {
        wxPaintDC dc(this);
        const wxColour bg = GetParent() ? GetParent()->GetBackgroundColour() : GetBackgroundColour();
        dc.SetBackground(wxBrush(bg));
        dc.Clear();
        const wxBitmap& bmp = (m_hovered ? m_hover : m_normal).bmp();
        if (!bmp.IsOk())
            return;
        const wxSize sz = GetClientSize();
        const wxSize bs = ScalableBitmap::GetBmpSize(bmp);
        dc.DrawBitmap(bmp, (sz.x - bs.x) / 2, (sz.y - bs.y) / 2, true);
    }

    ScalableBitmap m_normal;
    ScalableBitmap m_hover;
    bool           m_hovered = false;
};

class MatchThresholdPopup : public PopupWindow
{
public:
    MatchThresholdPopup(wxWindow* parent, wxWindow* anchor, int value,
                        std::function<void(int)> on_changed,
                        std::function<void(int)> on_committed,
                        std::function<void()> on_hidden)
        : PopupWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS)
        , m_anchor(anchor)
        , m_on_changed(std::move(on_changed))
        , m_on_committed(std::move(on_committed))
        , m_on_hidden(std::move(on_hidden))
        , m_hover_timer(this)
    {
        const wxColour pop_bg = texture_import_dialog_bg();
        SetBackgroundColour(pop_bg);
#ifdef __WXOSX__
        SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
#else
        SetBackgroundStyle(wxBG_STYLE_PAINT);
#endif
        Bind(wxEVT_PAINT, &MatchThresholdPopup::on_paint_border, this);
#ifdef __WXOSX__
        Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
#endif
        Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
            e.Skip();
            apply_rounded_shape();
            refresh_native_shadow();
        });
        Bind(wxEVT_TIMER, &MatchThresholdPopup::on_hover_timer, this);

        const int pad = FromDIP(12);
        m_pop_w = FromDIP(296);
        const int slider_row_min = FromDIP(80) + FromDIP(8) + FromDIP(64);
        const int content_w = std::max(slider_row_min, m_pop_w - 2 * pad);

        auto* title = new Label(this, Label::Head_14,
            _L("Set filament match color difference threshold"), wxST_NO_AUTORESIZE);
        title->SetForegroundColour(texture_import_dialog_fg());
        title->SetBackgroundColour(pop_bg);
        title->Wrap(content_w);
        title->SetMinSize(wxSize(content_w, title->GetBestSize().y));

        auto* hint = new Label(this, Label::Body_10,
            _L("Color tolerance for automatic filament matching. Higher value = easier to match."),
            wxST_NO_AUTORESIZE);
        hint->SetForegroundColour(texture_import_hint_text_colour());
        hint->SetBackgroundColour(pop_bg);
        hint->Wrap(content_w);
        hint->SetMinSize(wxSize(content_w, hint->GetBestSize().y));

        m_slider = new GreenSlider(this, std::clamp(value, kMatchDeltaThresholdMin, kMatchDeltaThresholdMax),
                                   kMatchDeltaThresholdMin, kMatchDeltaThresholdMax);
        m_slider->SetMinSize(wxSize(FromDIP(80), FromDIP(24)));
        m_slider->SetBackgroundColour(pop_bg);

        m_spin = new TextInput(this, wxString::Format("%d", m_slider->GetValue()),
                               "", "", wxDefaultPosition,
                               wxSize(FromDIP(64), FromDIP(28)), wxTE_PROCESS_ENTER);
        style_param_value_input(m_spin);
        if (wxTextCtrl* tc = m_spin->GetTextCtrl()) {
            tc->SetValidator(wxTextValidator(wxFILTER_DIGITS));
            bind_param_text_ctrl_center(tc);
            tc->Bind(wxEVT_TEXT, [this](wxCommandEvent& e) {
                on_spin_text();
                e.Skip();
            });
            tc->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { on_spin_commit(); });
            tc->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
                on_spin_commit();
                e.Skip();
            });
        }
        m_slider->Bind(wxEVT_SLIDER, &MatchThresholdPopup::on_slider, this);
        // GreenSlider reports each integer while dragging. Deletion waits for release.
        m_slider->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
            e.Skip();
            on_slider_commit();
        });
        m_slider->Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent& e) {
            e.Skip();
            on_slider_commit();
        });

        auto* slider_row = new wxBoxSizer(wxHORIZONTAL);
        slider_row->Add(m_slider, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        slider_row->Add(m_spin, 0, wxALIGN_CENTER_VERTICAL);

        auto* col = new wxBoxSizer(wxVERTICAL);
        col->Add(title, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
        col->Add(hint, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
        col->Add(slider_row, 0, wxEXPAND);
        auto* outer = new wxBoxSizer(wxVERTICAL);
        outer->Add(col, 0, wxEXPAND | wxALL, pad);
        SetSizer(outer);
        const wxSize min_sz = outer->GetMinSize();
        SetSize(m_pop_w, std::max(min_sz.y, FromDIP(109)));
        Layout();
        apply_rounded_shape();
    }

    ~MatchThresholdPopup() override
    {
        shutdown();
    }

    void shutdown()
    {
        m_hover_timer.Stop();
        unbind_activate();
        m_anchor = nullptr;
        m_on_changed = nullptr;
        m_on_committed = nullptr;
        m_on_hidden = nullptr;
        m_close_requested = true;
    }

    void set_value(int value)
    {
        value = std::clamp(value, kMatchDeltaThresholdMin, kMatchDeltaThresholdMax);
        m_updating = true;
        if (m_slider)
            m_slider->SetValue(value);
        set_text_input_int(m_spin, value);
        m_updating = false;
    }

    void place_and_popup()
    {
        if (IsShown())
            return;
        m_closed = false;
        m_close_requested = false;
        m_hidden_notified = false;
        if (wxSizer* sizer = GetSizer()) {
            const wxSize min_sz = sizer->GetMinSize();
            SetSize(m_pop_w, std::max(min_sz.y, FromDIP(109)));
            Layout();
        }
        if (m_anchor && !m_anchor->IsBeingDeleted()) {
            const int gap = FromDIP(4);
            wxPoint origin = m_anchor->ClientToScreen(wxPoint(0, m_anchor->GetSize().y + gap));
            const int display_idx = wxDisplay::GetFromWindow(m_anchor);
            const wxRect display = wxDisplay(display_idx == wxNOT_FOUND ? 0 : display_idx).GetClientArea();
            const wxSize sz = GetSize();
            if (origin.x + sz.x > display.GetRight())
                origin.x = std::max(display.GetLeft(), display.GetRight() - sz.x);
            if (origin.x < display.GetLeft())
                origin.x = display.GetLeft();
            if (origin.y + sz.y > display.GetBottom())
                origin.y = m_anchor->ClientToScreen(wxPoint(0, 0)).y - sz.y - gap;
            if (origin.y < display.GetTop())
                origin.y = display.GetTop();
            SetPosition(origin);
        }
        apply_rounded_shape();
        Popup(nullptr);
    }

    void request_close()
    {
        if (!IsShown())
            return;
        m_close_requested = true;
        Dismiss();
        m_close_requested = false;
    }

    void Popup(wxWindow* focus = nullptr) override
    {
        m_closed = false;
        m_close_requested = false;
        m_hidden_notified = false;
        PopupWindow::Popup(focus);
#ifdef __WXMSW__
        if (!m_unfocus_bound) {
            BindUnfocusEvent();
            m_unfocus_bound = true;
        }
#endif
        if (wxTheApp && !m_activate_bound) {
            wxTheApp->Bind(wxEVT_ACTIVATE_APP, &MatchThresholdPopup::on_activate_app, this);
            m_activate_bound = true;
        }
        m_hover_timer.Start(50);
        CallAfter([this]() {
            if (IsBeingDeleted() || !IsShown())
                return;
#ifdef __WXOSX__
            SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
#endif
            apply_rounded_shape();
            refresh_native_shadow();
        });
    }

    void Dismiss() override
    {
        if (!m_close_requested && should_stay_open()) {
            if (!IsShown())
                Show();
            return;
        }
        finish_close();
        if (IsShown())
            Hide();
        notify_hidden();
        PopupWindow::Dismiss();
    }

    void OnDismiss() override
    {
        if (!m_closed && !m_close_requested && should_stay_open()) {
            if (!IsShown())
                Show();
            return;
        }
        finish_close();
        if (IsShown())
            Hide();
        wxPopupTransientWindow::OnDismiss();
        notify_hidden();
    }

    bool ProcessLeftDown(wxMouseEvent& event) override
    {
        const wxPoint screen = ClientToScreen(event.GetPosition());
        if (GetScreenRect().Contains(screen))
            return PopupWindow::ProcessLeftDown(event);
        if (m_anchor && !m_anchor->IsBeingDeleted() && m_anchor->GetScreenRect().Contains(screen))
            return false;
        request_close();
        return false;
    }

private:
    void on_slider(wxCommandEvent&)
    {
        if (m_updating || !m_slider)
            return;
        const int value = m_slider->GetValue();
        m_updating = true;
        set_text_input_int(m_spin, value);
        m_updating = false;
        if (m_on_changed)
            m_on_changed(value);
    }

    void on_spin_text()
    {
        if (m_updating || !m_spin || !m_spin->GetTextCtrl() || !m_slider)
            return;
        long parsed = 0;
        if (!m_spin->GetTextCtrl()->GetValue().ToLong(&parsed))
            return;
        if (parsed < kMatchDeltaThresholdMin || parsed > kMatchDeltaThresholdMax)
            return;
        if (m_slider->GetValue() == (int)parsed)
            return;
        m_updating = true;
        m_slider->SetValue((int)parsed);
        m_updating = false;
        if (m_on_changed)
            m_on_changed((int)parsed);
    }

    void on_spin_commit()
    {
        if (m_updating || !m_spin || !m_spin->GetTextCtrl() || !m_slider)
            return;
        long parsed = 0;
        const wxString text = m_spin->GetTextCtrl()->GetValue();
        if (!text.ToLong(&parsed)) {
            set_value(m_slider->GetValue());
            if (m_on_committed)
                m_on_committed(m_slider->GetValue());
            return;
        }
        const int clamped = std::clamp((int)parsed, kMatchDeltaThresholdMin, kMatchDeltaThresholdMax);
        set_value(clamped);
        if (m_on_committed)
            m_on_committed(clamped);
    }

    void on_slider_commit()
    {
        if (m_updating || !m_slider)
            return;
        if (m_on_committed)
            m_on_committed(m_slider->GetValue());
    }

    void on_hover_timer(wxTimerEvent&)
    {
        if (IsBeingDeleted())
            return;
        if (!IsShown()) {
            m_hover_timer.Stop();
            return;
        }
        if (m_slider && m_slider->HasCapture())
            return;
        if (should_stay_open())
            return;
        request_close();
    }

    void on_activate_app(wxActivateEvent& e)
    {
        e.Skip();
        if (!e.GetActive() && IsShown())
            request_close();
    }

    bool should_stay_open() const
    {
        if (!wxGetApp().IsActive())
            return false;
        if (m_slider && m_slider->HasCapture())
            return true;
        return hot_zone_contains_pointer();
    }

    bool hot_zone_contains_pointer() const
    {
        const wxPoint pt = wxGetMousePosition();
        const int slack = FromDIP(6);
        auto contains = [&](const wxRect& rc) {
            wxRect inflated = rc;
            inflated.Inflate(slack);
            return inflated.Contains(pt);
        };
        if (IsShown() && contains(GetScreenRect()))
            return true;
        if (m_anchor && !m_anchor->IsBeingDeleted() && contains(m_anchor->GetScreenRect()))
            return true;
        if (!IsShown() || !m_anchor || m_anchor->IsBeingDeleted())
            return false;
        const wxRect gear = m_anchor->GetScreenRect();
        const wxRect pop = GetScreenRect();
        const int top = std::min(gear.GetBottom(), pop.GetTop());
        const int bottom = std::max(gear.GetBottom(), pop.GetTop());
        const int left = std::min(gear.GetLeft(), pop.GetLeft());
        const int right = std::max(gear.GetRight(), pop.GetRight());
        wxRect bridge(left, top, std::max(1, right - left), std::max(1, bottom - top));
        bridge.Inflate(slack);
        return bridge.Contains(pt);
    }

    void finish_close()
    {
        if (m_closed)
            return;
        m_closed = true;
        m_hover_timer.Stop();
        refresh_native_shadow();
    }

    void notify_hidden()
    {
        if (m_hidden_notified)
            return;
        m_hidden_notified = true;
        if (m_on_hidden)
            m_on_hidden();
    }

    void unbind_activate()
    {
        if (wxTheApp && m_activate_bound) {
            wxTheApp->Unbind(wxEVT_ACTIVATE_APP, &MatchThresholdPopup::on_activate_app, this);
            m_activate_bound = false;
        }
    }

    void apply_rounded_shape()
    {
#ifdef __WXOSX__
        return;
#else
        const wxSize sz = GetSize();
        if (sz.x <= 0 || sz.y <= 0)
            return;
        const int radius = FromDIP(8);
#ifdef __WXMSW__
        HWND hwnd = (HWND)GetHWND();
        if (!hwnd)
            return;
        const int d = radius * 2;
        HRGN body = CreateRoundRectRgn(0, 0, sz.x + 1, sz.y + 1, d, d);
        if (body)
            SetWindowRgn(hwnd, body, TRUE);
#else
        wxBitmap mask(sz.x, sz.y);
        {
            wxMemoryDC dc(mask);
            dc.SetBackground(*wxBLACK_BRUSH);
            dc.Clear();
            dc.SetBrush(*wxWHITE_BRUSH);
            dc.SetPen(*wxWHITE_PEN);
            dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, radius);
        }
        SetShape(wxRegion(mask, *wxBLACK));
#endif
#endif
    }

    void refresh_native_shadow()
    {
#ifdef __WXOSX__
        if (IsShown())
            m_shadow.Sync(this, true);
#endif
    }

    void on_paint_border(wxPaintEvent&)
    {
        const wxSize sz = GetClientSize();
        if (sz.x <= 0 || sz.y <= 0)
            return;
        const int radius = FromDIP(8);
        const int border_w = std::max(1, FromDIP(1));
        const wxColour border = dark_or(wxColour(0xEE, 0xEE, 0xEE), wxColour(0x46, 0x46, 0x4C));
        const wxColour bg = GetBackgroundColour();
#ifdef __WXMSW__
        wxPaintDC dc(this);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(border));
        dc.DrawRectangle(0, 0, sz.x, sz.y);
        dc.SetBrush(wxBrush(bg));
        dc.DrawRoundedRectangle(border_w, border_w,
                                std::max(0, sz.x - 2 * border_w),
                                std::max(0, sz.y - 2 * border_w),
                                std::max(0, radius - border_w));
#else
        texture_import_paint(this, [&](wxDC& dc) {
            dc.SetBrush(wxBrush(bg));
            dc.SetPen(wxPen(border, border_w));
            dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, radius);
        });
#endif
    }

    wxWindow*                m_anchor = nullptr;
    GreenSlider*             m_slider = nullptr;
    TextInput*               m_spin = nullptr;
    std::function<void(int)> m_on_changed;
    std::function<void(int)> m_on_committed;
    std::function<void()>    m_on_hidden;
    wxTimer                  m_hover_timer;
    int                      m_pop_w = 0;
    bool                     m_updating = false;
    bool                     m_close_requested = false;
    bool                     m_closed = true;
    bool                     m_hidden_notified = false;
    bool                     m_activate_bound = false;
#ifdef __WXMSW__
    bool                     m_unfocus_bound = false;
#endif
#ifdef __WXOSX__
    WindowShadow             m_shadow;
#endif
};

// ============================================================
// FilamentSelectPopup
// ============================================================

class FilamentSelectPopup : public PopupWindow
{
public:
    FilamentSelectPopup(wxWindow* parent,
                        const std::vector<TextureFilamentEntry>& entries,
                        const std::vector<std::array<float, 4>>& colors_rgba,
                        const std::vector<std::string>&          names,
                        size_t                                   existing_count,
                        int                                      popup_width,
                        wxWindow*                                dialog_anchor,
                        std::function<void(int)>                 on_select,
                        std::function<void()>                    on_add_filament,
                        std::function<void(int)>                 on_delete,
                        std::function<void(FilamentSelectPopup*)> on_close,
                        std::vector<int>                         display_numbers)
        : PopupWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS)
        , m_entries(entries)
        , m_colors_rgba(colors_rgba)
        , m_names(names)
        , m_existing_count(existing_count)
        , m_dialog_anchor(dialog_anchor)
        , m_on_select(std::move(on_select))
        , m_on_add_filament(std::move(on_add_filament))
        , m_on_delete(std::move(on_delete))
        , m_on_close(std::move(on_close))
        , m_display_numbers(std::move(display_numbers))
    {
        // Host bitmaps on the dialog, not this popup: an unshown popup
        // can report scale-factor 1 on Retina, which yields an empty/tiny trash icon.
        wxWindow* bmp_host = GetParent() ? GetParent() : static_cast<wxWindow*>(this);
        m_bmp_delete = ScalableBitmap(bmp_host, "tree_delete", 16);
        m_bmp_brand = ScalableBitmap(bmp_host, "BambuStudioBlack", 16);
        wxColour pop_bg = dark_or(*wxWHITE, wxColour(0x2D, 0x2D, 0x31));
        SetBackgroundColour(pop_bg);
#ifdef __WXOSX__
        // Preview-card recipe: unpainted corners must be clear, not NSWindow gray.
        // Windows keeps an opaque paint style and clips with SetWindowRgn instead.
        SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
#else
        SetBackgroundStyle(wxBG_STYLE_PAINT);
#endif

        m_pop_w = std::max(FromDIP(213), popup_width);
        m_row_h = FromDIP(32);
        m_pad = FromDIP(8);
        m_header_clr = dark_or(wxColour(0xAC, 0xAC, 0xAC), wxColour(0x81, 0x81, 0x83));

        Bind(wxEVT_PAINT, &FilamentSelectPopup::on_paint_border, this);
#ifdef __WXOSX__
        Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
#endif
        Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
            e.Skip();
            apply_rounded_shape();
            refresh_native_shadow();
        });
        Bind(wxEVT_MOVE, [this](wxMoveEvent& e) {
            e.Skip();
            refresh_native_shadow();
        });
        Bind(wxEVT_SHOW, [this](wxShowEvent& e) {
            e.Skip();
            if (!e.IsShown())
                return;
            // HWND exists only after Popup(); apply the round region then.
            CallAfter([this]() {
                if (IsBeingDeleted())
                    return;
#ifdef __WXOSX__
                // Native NSWindow is created at Popup(); re-assert clear chrome.
                SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
#endif
                apply_rounded_shape();
                refresh_native_shadow();
            });
        });
        Bind(wxEVT_MOTION, [this](wxMouseEvent& evt) {
            update_hover_from_pointer();
            evt.Skip();
        });
#ifdef __WXOSX__
        // Keep the transient popup's capture stable. This mirrors DropDown and
        // avoids wx releasing capture from idle before the first click completes.
        Bind(wxEVT_IDLE, [](wxIdleEvent&) {});
#endif
        Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) {
            cancel_pressed();
        });

        m_content = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
        m_content->SetBackgroundColour(pop_bg);
        m_content->SetScrollRate(0, FromDIP(5));
        m_content->Bind(wxEVT_MOTION, [this](wxMouseEvent& evt) {
            update_hover_from_pointer();
            evt.Skip();
        });
        m_content->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& evt) {
            update_hover_from_pointer();
            evt.Skip();
        });

        m_add_label = new wxStaticText(this, wxID_ANY, _L("+ Add Filament"));
        m_add_label->SetFont(Label::Body_12);
        m_add_label->SetBackgroundColour(pop_bg);
        bind_click(m_add_label, [this]() {
            if (add_filament_at_limit())
                return;
            auto on_add_filament = m_on_add_filament;
            wxWindow* owner = GetParent();
            DismissAndNotify();
            if (on_add_filament && owner && !owner->IsBeingDeleted())
                owner->CallAfter([on_add_filament]() { on_add_filament(); });
        });

        rebuild_content();

        auto* inner = new wxBoxSizer(wxVERTICAL);
        inner->Add(m_content, 0, wxEXPAND);
        inner->AddSpacer(FromDIP(4));
        auto* sep_line = new StaticLine(this);
        sep_line->SetLineColour(texture_import_separator_colour());
        inner->Add(sep_line, 0, wxEXPAND | wxLEFT | wxRIGHT, m_pad);
        inner->Add(m_add_label, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, m_pad);
        auto* top_sizer = new wxBoxSizer(wxVERTICAL);
        top_sizer->Add(inner, 0, wxEXPAND | wxALL, FromDIP(8));
        SetSizerAndFit(top_sizer);
        const wxSize min_sz = top_sizer->GetMinSize();
        SetSize(std::max(m_pop_w, min_sz.x), min_sz.y);
        apply_rounded_shape();

        if (m_dialog_anchor)
            m_dialog_anchor->Bind(wxEVT_DESTROY, &FilamentSelectPopup::on_dialog_anchor_destroyed, this);
    }

    ~FilamentSelectPopup()
    {
        uninstall_external_click_monitor();
        unbind_app_activate();
        cancel_pressed();
        detach_dialog_anchor();
    }

    void Popup(wxWindow* focus = nullptr) override
    {
        if (m_dismissed || IsShown())
            return;
        PopupWindow::Popup(focus);
        if (m_dismissed || !IsShown())
            return;
        if (wxTheApp && !m_activate_app_bound) {
            wxTheApp->Bind(wxEVT_ACTIVATE_APP, &FilamentSelectPopup::on_activate_app, this);
            m_activate_app_bound = true;
        }
        install_external_click_monitor();
    }

    void Dismiss() override
    {
        if (m_dismissed)
            return;

        // Invalidate the cached macOS shadow while still shown so the native
        // dismiss dirties the full overhang.
        refresh_native_shadow();
        PopupWindow::Dismiss();
        finish_close();
    }

    void refresh_filaments(const std::vector<TextureFilamentEntry>& entries,
                           const std::vector<std::array<float, 4>>& colors_rgba,
                           const std::vector<std::string>&          names,
                           size_t                                   existing_count,
                           std::vector<int>                         display_numbers)
    {
        m_entries = entries;
        m_colors_rgba = colors_rgba;
        m_names = names;
        m_existing_count = existing_count;
        m_display_numbers = std::move(display_numbers);
        Freeze();
        rebuild_content();
        if (wxSizer* sizer = GetSizer()) {
            Layout();
            const wxSize min_sz = sizer->GetMinSize();
            SetSize(std::max(m_pop_w, min_sz.x), min_sz.y);
            apply_rounded_shape();
        }
        Thaw();
        Refresh();
    }

    // The native popup ignores SetShape on MSW; round the native HWND
    // after it exists (post-Popup). Matches ParamTooltip / FilamentGroupPopup.
    // macOS: SetShape requires wxFRAME_SHAPED and is a no-op here; corners are
    // made clear via wxBG_STYLE_TRANSPARENT + rounded paint instead.
    void apply_rounded_shape()
    {
#ifdef __WXOSX__
        return;
#else
        const wxSize sz = GetSize();
        if (sz.x <= 0 || sz.y <= 0)
            return;
        const int radius = FromDIP(8);
#ifdef __WXMSW__
        HWND hwnd = (HWND)GetHWND();
        if (!hwnd)
            return;
        const int d = radius * 2;
        HRGN body = CreateRoundRectRgn(0, 0, sz.x + 1, sz.y + 1, d, d);
        if (body)
            SetWindowRgn(hwnd, body, TRUE);
#else
        wxBitmap mask(sz.x, sz.y);
        {
            wxMemoryDC dc(mask);
            dc.SetBackground(*wxBLACK_BRUSH);
            dc.Clear();
            dc.SetBrush(*wxWHITE_BRUSH);
            dc.SetPen(*wxWHITE_PEN);
            dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, radius);
        }
        SetShape(wxRegion(mask, *wxBLACK));
#endif
#endif
    }

    // macOS caches the drop shadow of a non-opaque NSWindow (wxBG_STYLE_TRANSPARENT
    // above) and only recomputes it on request. After a resize or move the cached
    // shadow can reach past the new frame, and ordering the window out then only
    // repaints what the window server believes the window covers, leaving the
    // overhanging shadow pixels on screen. Re-derive it from the current content
    // on every geometry change so dismissing takes the whole thing with it.
    // Windows clips with SetWindowRgn and draws its own border, so it is left
    // alone rather than given a second, layered shadow window.
    void refresh_native_shadow()
    {
#ifdef __WXOSX__
        if (IsShown())
            m_shadow.Sync(this, true);
#endif
    }

private:
    void cancel_pressed()
    {
        m_pressed_target = nullptr;
        // The transient popup may already own capture for event routing.
        // Release only capture explicitly acquired for the current click.
        if (m_click_capture_owned && HasCapture())
            ReleaseMouse();
        m_click_capture_owned = false;
    }

    void rebuild_content()
    {
        if (!m_content)
            return;

        cancel_pressed();
#ifdef __WXOSX__
        ClearHovered();
#endif
        m_hover_idx = -1;
        m_hover_row = nullptr;
        m_row_windows.clear();
        int scroll_x = 0;
        int scroll_y = 0;
        m_content->GetViewStart(&scroll_x, &scroll_y);

        if (wxSizer* old = m_content->GetSizer())
            old->Clear(true);

        auto* outer = new wxBoxSizer(wxVERTICAL);
        const int max_visible_rows = 10;
        int row_count = 0;

        auto add_section_header = [&](const wxString& label) {
            auto* hdr = new Label(m_content, Label::Body_10, label);
            hdr->SetForegroundColour(m_header_clr);
            hdr->Wrap(std::max(FromDIP(80), m_pop_w - 2 * m_pad));
            outer->Add(hdr, 0, wxLEFT | wxRIGHT | wxTOP, m_pad);
            auto* line = new StaticLine(m_content);
            line->SetLineColour(texture_import_separator_colour());
            outer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, m_pad);
        };

        auto add_section = [&](const wxString& label, TextureFilamentKind kind) {
            bool has_any = false;
            for (const auto& entry : m_entries) {
                if (entry.kind == kind) {
                    has_any = true;
                    break;
                }
            }
            if (!has_any)
                return;
            add_section_header(label);
            for (const auto& entry : m_entries) {
                if (entry.kind != kind)
                    continue;
                wxPanel* row = texture_entry_is_mixed(entry.kind) ? create_mixed_item_row(entry, m_row_h)
                                                                  : create_item_row((size_t)entry.dialog_index, m_row_h);
                outer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, m_pad);
                ++row_count;
            }
        };

        // Section order matches compute_display_numbers() so the visible IDs
        // ascend monotonically (ExistingPhysical -> NewPhysical -> ExistingMixed
        // -> NewMixed) instead of jumping (e.g. 1,2 -> 7 -> 3,4,5,6 -> 8,9,10).
        add_section(_L("Project Physical Filaments"), TextureFilamentKind::ExistingPhysical);
        add_section(_L("New Physical Filaments"), TextureFilamentKind::NewPhysical);
        add_section(_L("Project Mixed Filaments"), TextureFilamentKind::ExistingMixed);
        add_section(_L("New Mixed Filaments"), TextureFilamentKind::NewMixed);

        m_content->SetSizer(outer);
        m_content->FitInside();

        int list_h = outer->GetMinSize().y;
        if (row_count > max_visible_rows)
            list_h -= (row_count - max_visible_rows) * m_row_h;
        list_h = std::max(m_row_h, list_h);
        m_content->SetMinSize(wxSize(m_pop_w, list_h));
        m_content->SetMaxSize(wxSize(m_pop_w, list_h));
        m_content->Scroll(scroll_x, scroll_y);

        if (m_add_label) {
            const bool at_limit = add_filament_at_limit();
            m_add_label->SetForegroundColour(at_limit ? m_header_clr : wxColour(0x00, 0xAE, 0x42));
            m_add_label->SetCursor(wxCursor(at_limit ? wxCURSOR_ARROW : wxCURSOR_HAND));
            if (at_limit)
                m_add_label->SetToolTip(format_wxstr(
                    _L("The project supports up to %1% physical filaments."),
                    (int)MAX_PHYSICAL_FILAMENT_COUNT));
            else
                m_add_label->SetToolTip(wxString());
            m_add_label->Refresh();
        }
    }

    bool add_filament_at_limit() const
    {
        const size_t physical = (size_t)std::count_if(m_entries.begin(), m_entries.end(),
            [](const TextureFilamentEntry& e) { return texture_entry_is_physical(e.kind); });
        return physical >= MAX_PHYSICAL_FILAMENT_COUNT || m_entries.size() >= MAX_FILAMENT_COUNT;
    }

    void on_paint_border(wxPaintEvent&)
    {
        const wxSize sz = GetClientSize();
        if (sz.x <= 0 || sz.y <= 0)
            return;
        const int radius = FromDIP(8);
        const int border_w = std::max(1, FromDIP(1));
        const wxColour border = texture_import_window_border_colour();
        const wxColour bg = GetBackgroundColour();
#ifdef __WXMSW__
        // Paint with aliased GDI, not texture_import_paint/wxGCDC. GDI+ AA
        // blends the round edge into a white-cleared bitmap; those light
        // pixels stay inside CreateRoundRectRgn and show as a white jaggy
        // halo. Fill the whole client with the frame colour first so any
        // region-vs-round mismatch is border-coloured, not white.
        wxPaintDC dc(this);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(border));
        dc.DrawRectangle(0, 0, sz.x, sz.y);
        dc.SetBrush(wxBrush(bg));
        dc.DrawRoundedRectangle(border_w, border_w,
                                std::max(0, sz.x - 2 * border_w),
                                std::max(0, sz.y - 2 * border_w),
                                std::max(0, radius - border_w));
#else
        // Same recipe as the preview / mapping cards: rounded fill + 1px
        // stroke, no full-rect grey wash. Transparent chrome lets unpainted
        // corners show the desktop instead of NSWindow gray.
        texture_import_paint(this, [&](wxDC& dc) {
            dc.SetBrush(wxBrush(bg));
            dc.SetPen(wxPen(border, border_w));
            dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, radius);
        });
#endif
    }

    void on_dialog_anchor_destroyed(wxWindowDestroyEvent& e)
    {
        e.Skip();
        // Defensive: destroy events bubble, so ignore any future child of the anchor.
        if (e.GetEventObject() != m_dialog_anchor)
            return;
        m_dialog_anchor = nullptr;
    }

    void detach_dialog_anchor()
    {
        if (!m_dialog_anchor)
            return;
        m_dialog_anchor->Unbind(wxEVT_DESTROY, &FilamentSelectPopup::on_dialog_anchor_destroyed, this);
        m_dialog_anchor = nullptr;
    }

    void OnDismiss() override
    {
        finish_close();
    }

    void finish_close()
    {
        if (m_dismissed)
            return;
        m_dismissed = true;
        uninstall_external_click_monitor();
        unbind_app_activate();
        cancel_pressed();
        restore_cursor_state();
        detach_dialog_anchor();
        auto on_close = std::move(m_on_close);
        if (on_close)
            on_close(this);
        schedule_destroy();
    }

    void on_activate_app(wxActivateEvent& e)
    {
        e.Skip();
        if (e.GetActive() || !IsShown())
            return;
#ifdef __WXOSX__
        if (m_external_click_monitor) {
            request_texture_import_external_click_monitor_dismiss(m_external_click_monitor);
            return;
        }
#endif
        DismissAndNotify();
    }

    static void on_external_click(void* context)
    {
        auto* self = static_cast<FilamentSelectPopup*>(context);
        if (self && !self->IsBeingDeleted() && self->IsShown())
            self->DismissAndNotify();
    }

    void install_external_click_monitor()
    {
        if (!m_external_click_monitor && GetParent())
            m_external_click_monitor = install_texture_import_external_click_monitor(
                this, GetParent(), &FilamentSelectPopup::on_external_click, this);
    }

    void uninstall_external_click_monitor()
    {
        if (!m_external_click_monitor)
            return;
        uninstall_texture_import_external_click_monitor(m_external_click_monitor);
        m_external_click_monitor = nullptr;
    }

    void unbind_app_activate()
    {
        if (!wxTheApp || !m_activate_app_bound)
            return;
        wxTheApp->Unbind(wxEVT_ACTIVATE_APP, &FilamentSelectPopup::on_activate_app, this);
        m_activate_app_bound = false;
    }

    void restore_cursor_state()
    {
        SetCursor(wxNullCursor);
        if (m_content)
            m_content->SetCursor(wxNullCursor);
        if (m_dialog_anchor && !m_dialog_anchor->IsBeingDeleted())
            m_dialog_anchor->SetCursor(wxCursor(wxCURSOR_HAND));
        wxSetCursor(wxNullCursor);
    }

    void schedule_destroy()
    {
        if (m_destroy_scheduled)
            return;
        m_destroy_scheduled = true;
        CallAfter([this]() {
            if (!IsBeingDeleted())
                Destroy();
        });
    }

    bool row_can_delete(int idx) const
    {
        return idx >= 0 && idx < (int)m_entries.size() &&
               (m_entries[idx].kind == TextureFilamentKind::NewPhysical ||
                m_entries[idx].kind == TextureFilamentKind::NewMixed);
    }

    wxRect delete_icon_rect(wxWindow* row) const
    {
        const wxSize icon = m_bmp_delete.bmp().IsOk() ? m_bmp_delete.GetBmpSize()
                                                      : wxSize(row->FromDIP(16), row->FromDIP(16));
        const int pad = row->FromDIP(8);
        const wxSize sz = row->GetClientSize();
        return wxRect(sz.x - icon.x - pad, (sz.y - icon.y) / 2, icon.x, icon.y);
    }

    int name_right_margin(wxWindow* row, int idx) const
    {
        if (!row_can_delete(idx))
            return row->FromDIP(4);
        const int icon_w = m_bmp_delete.bmp().IsOk() ? m_bmp_delete.GetBmpSize().x : row->FromDIP(16);
        return row->FromDIP(8) + icon_w + row->FromDIP(4);
    }

    void update_row_tooltip(wxWindow* row, int idx, const wxPoint& pos, const wxString& name_tip)
    {
        const bool on_delete = row_can_delete(idx) && delete_icon_rect(row).Contains(pos);
        const wxString tip = on_delete ? _L("Delete this filament") : name_tip;
        if (row->GetToolTipText() != tip)
            row->SetToolTip(tip);
    }

    void update_hover_from_pointer()
    {
        const wxPoint screen = wxGetMousePosition();
        for (const auto& pr : m_row_windows) {
            wxWindow* row = pr.second;
            if (!row)
                continue;
            const wxPoint local = row->ScreenToClient(screen);
            if (wxRect(wxPoint(0, 0), row->GetClientSize()).Contains(local)) {
                set_hover_row(pr.first, row);
                return;
            }
        }
        set_hover_row(-1, nullptr);
    }

    // Show the trash icon only while the row is hovered. macOS PopupWindow
    // retargets mouse events and may synthesize LEAVE while the pointer is
    // still inside the row; hover is tracked from the real pointer position.
    void draw_delete_icon_if_needed(wxDC& dc, wxWindow* row, int idx)
    {
        if (!row_can_delete(idx) || m_hover_idx != idx || !m_bmp_delete.bmp().IsOk())
            return;
        const wxRect r = delete_icon_rect(row);
        const wxBitmap& bmp = m_bmp_delete.bmp();
        const wxSize bs = m_bmp_delete.GetBmpSize();
        wxGraphicsContext* gc = dc.GetGraphicsContext();
        std::unique_ptr<wxGraphicsContext> owned;
        if (!gc) {
            if (auto* pdc = dynamic_cast<wxPaintDC*>(&dc))
                owned.reset(wxGraphicsContext::Create(*pdc));
            else if (auto* mdc = dynamic_cast<wxMemoryDC*>(&dc))
                owned.reset(wxGraphicsContext::Create(*mdc));
            gc = owned.get();
        }
        if (gc) {
            gc->DrawBitmap(bmp, r.x, r.y, bs.x, bs.y);
            return;
        }
        dc.DrawBitmap(bmp, r.x, r.y);
    }

    void set_hover_row(int idx, wxWindow* row)
    {
        if (m_hover_idx == idx && m_hover_row == row)
            return;
        wxWindow* prev = m_hover_row;
        m_hover_idx = idx;
        m_hover_row = row;
        if (prev && prev != row)
            prev->Refresh();
        if (row && row != prev)
            row->Refresh();
    }

    void bind_row_hover(wxWindow* row, int idx, const wxString& name_str)
    {
        m_row_windows.push_back({idx, row});
        row->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& evt) {
            update_hover_from_pointer();
            evt.Skip();
        });
        row->Bind(wxEVT_MOTION, [this, row, idx, name_str](wxMouseEvent& evt) {
            update_hover_from_pointer();
            update_row_tooltip(row, idx, evt.GetPosition(), name_str);
            evt.Skip();
        });
        row->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& evt) {
            update_hover_from_pointer();
            evt.Skip();
        });
    }

    // Commit actions on mouse-up, matching DropDown. On macOS capture on the
    // popup so the release cannot be lost while Cocoa redirects events to the
    // transient window; PopupWindow forwards captured events to the child.
    // MSW and GTK have no such forwarding: a capture would route the mouse-up
    // to the popup itself and the row would never see it.
    void bind_click(wxWindow* target, std::function<void()> on_click)
    {
        target->Bind(wxEVT_LEFT_DOWN, [this, target](wxMouseEvent&) {
            if (!IsShown())
                return;
            m_pressed_target = target;
#ifdef __WXOSX__
            if (!HasCapture()) {
                CaptureMouse();
                m_click_capture_owned = true;
            }
#endif
        });
        target->Bind(wxEVT_LEFT_UP, [this, target, on_click = std::move(on_click)](wxMouseEvent&) {
            wxWindow* pressed = m_pressed_target ? m_pressed_target.get() : nullptr;
            m_pressed_target = nullptr;
            const bool release_capture = m_click_capture_owned;
            m_click_capture_owned = false;
            if (release_capture && HasCapture())
                ReleaseMouse();
            if (pressed != target)
                return;
            if (!target->GetScreenRect().Contains(wxGetMousePosition()))
                return;
            on_click();
        });
    }

    void handle_row_click(wxWindow* row, int idx)
    {
        const wxPoint pos = row->ScreenToClient(wxGetMousePosition());
        if (row_can_delete(idx) && delete_icon_rect(row).Contains(pos)) {
            // Keep the popup open and refresh the list in place. Dismiss+reopen
            // flashes the dropdown every time a new filament is removed.
            auto on_delete = m_on_delete;
            if (on_delete)
                on_delete(idx);
            return;
        }
        auto on_select = m_on_select;
        wxWindow* owner = GetParent();
        DismissAndNotify();
        if (on_select && owner && !owner->IsBeingDeleted())
            owner->CallAfter([on_select, idx]() { on_select(idx); });
    }

    wxPanel* create_item_row(size_t idx, int row_h)
    {
        wxColour row_bg    = dark_or(*wxWHITE, wxColour(0x2D, 0x2D, 0x31));
        wxColour hover_bg  = dark_or(wxColour(245, 245, 245), wxColour(0x3C, 0x3C, 0x42));
        wxColour name_fg   = texture_import_text_colour();

        wxPanel* row = new wxPanel(m_content, wxID_ANY, wxDefaultPosition, wxSize(-1, row_h),
                                  wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE);
        row->SetBackgroundColour(row_bg);
        row->SetBackgroundStyle(wxBG_STYLE_PAINT);
        row->SetMinSize(wxSize(-1, row_h));
        row->SetMaxSize(wxSize(-1, row_h));
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

        wxString name_str = texture_filament_label_wx(m_entries, m_names, (int)idx, display_number((int)idx));
        row->SetToolTip(name_str);

        row->Bind(wxEVT_PAINT, [this, idx, sq, sq_r, sq_x, gap1, fil_clr, name_str, row_bg, hover_bg, name_fg](wxPaintEvent& e) {
            auto* p = static_cast<wxPanel*>(e.GetEventObject());
            texture_import_paint(p, [this, p, idx, sq, sq_r, sq_x, gap1, fil_clr, name_str, row_bg, hover_bg, name_fg](wxDC& dc) {
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
                dc.SetFont(Label::Body_10);
                dc.SetTextForeground(paint_clr.GetLuminance() < 0.6 ? *wxWHITE : texture_import_gray9000());
                wxString ns = wxString::Format("%d", display_number((int)idx));
                wxSize tsz = dc.GetTextExtent(ns);
                dc.DrawText(ns, sq_x + (sq - tsz.x) / 2, sq_y + (sq - tsz.y) / 2);
            }

            // Brand icon + material name
            {
                dc.SetFont(Label::Body_12);
                dc.SetTextForeground(name_fg);
                wxString display = name_str;
                int tx = draw_brand_icon_and_strip(dc, p, display, sq_x + sq + gap1, sz.y / 2, &m_bmp_brand);
                display = ellipsize_text(dc, display, sz.x - tx - name_right_margin(p, (int)idx));
                wxSize tsz = dc.GetTextExtent(display);
                if (!display.empty())
                    dc.DrawText(display, tx, (sz.y - tsz.y) / 2);
            }
            draw_delete_icon_if_needed(dc, p, (int)idx);
            });
        });

        bind_row_hover(row, (int)idx, name_str);
        bind_click(row, [this, row, idx]() { handle_row_click(row, (int)idx); });

        return row;
    }

    wxPanel* create_mixed_item_row(const TextureFilamentEntry& entry, int row_h)
    {
        wxColour row_bg    = dark_or(*wxWHITE, wxColour(0x2D, 0x2D, 0x31));
        wxColour hover_bg  = dark_or(wxColour(245, 245, 245), wxColour(0x3C, 0x3C, 0x42));
        wxColour name_fg   = texture_import_text_colour();
        wxColour plus_fg   = dark_or(wxColour(38, 46, 48), wxColour(0xE6, 0xE6, 0xE8));
        const int idx = entry.dialog_index;

        wxPanel* row = new wxPanel(m_content, wxID_ANY, wxDefaultPosition, wxSize(-1, row_h),
                                  wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE);
        row->SetBackgroundColour(row_bg);
        row->SetBackgroundStyle(wxBG_STYLE_PAINT);
        row->SetMinSize(wxSize(-1, row_h));
        row->SetMaxSize(wxSize(-1, row_h));
        row->SetCursor(wxCursor(wxCURSOR_HAND));
        wxString name_str = texture_filament_label_wx(m_entries, m_names, idx, display_number(idx));
        row->SetToolTip(name_str);

        row->Bind(wxEVT_PAINT, [this, entry, idx, row_bg, hover_bg, name_fg, plus_fg](wxPaintEvent& e) {
            auto* p = static_cast<wxPanel*>(e.GetEventObject());
            texture_import_paint(p, [this, p, entry, idx, row_bg, hover_bg, name_fg, plus_fg](wxDC& dc) {
            wxSize sz = p->GetClientSize();
            const bool hovered = (m_hover_idx == idx);
            dc.SetBrush(wxBrush(hovered ? hover_bg : row_bg));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, sz.x, sz.y);

            dc.SetFont(Label::Body_10);
            int x = p->FromDIP(2);
            const int sw = p->FromDIP(22);
            const int sw_r = p->FromDIP(2);
            const int y = (sz.y - sw) / 2;
            const int right_limit = sz.x - name_right_margin(p, idx);

            {
                wxColour self_clr("#D9D9D9");
                if (idx >= 0 && idx < (int)m_colors_rgba.size()) {
                    const auto& c = m_colors_rgba[idx];
                    self_clr = wxColour((unsigned char)(c[0] * 255.f),
                                        (unsigned char)(c[1] * 255.f),
                                        (unsigned char)(c[2] * 255.f));
                }
                dc.SetBrush(wxBrush(self_clr));
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.DrawRoundedRectangle(x, y, sw, sw, sw_r);
                draw_filament_swatch_border(dc, self_clr, x, y, sw, sw, sw_r);

                wxString num = wxString::Format("%d", display_number(idx));
                wxSize nsz = dc.GetTextExtent(num);
                dc.SetTextForeground(self_clr.GetLuminance() < 0.6 ? *wxWHITE : texture_import_gray9000());
                dc.DrawText(num, x + (sw - nsz.x) / 2, y + (sw - nsz.y) / 2);
                x += sw + p->FromDIP(4);

                dc.SetTextForeground(plus_fg);
                wxString eq = "=";
                wxSize esz = dc.GetTextExtent(eq);
                dc.DrawText(eq, x, (sz.y - esz.y) / 2);
                x += esz.x + p->FromDIP(4);
            }

            for (size_t ci = 0; ci < entry.mixed_components.size() && ci < entry.mixed_ratios.size(); ++ci) {
                if (x >= right_limit)
                    break;
                if (ci > 0) {
                    dc.SetTextForeground(plus_fg);
                    wxString plus = "+";
                    wxSize psz = dc.GetTextExtent(plus);
                    dc.DrawText(plus, x, (sz.y - psz.y) / 2);
                    x += psz.x + p->FromDIP(4);
                }

                const unsigned int comp_id = entry.mixed_components[ci];
                const int comp_dialog_idx = comp_id >= 1 ? (int)comp_id - 1 : -1;
                wxColour comp_clr("#D9D9D9");
                if (comp_dialog_idx >= 0 && comp_dialog_idx < (int)m_colors_rgba.size()) {
                    const auto& c = m_colors_rgba[comp_dialog_idx];
                    comp_clr = wxColour((unsigned char)(c[0] * 255.f),
                                        (unsigned char)(c[1] * 255.f),
                                        (unsigned char)(c[2] * 255.f));
                }

                dc.SetBrush(wxBrush(comp_clr));
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.DrawRoundedRectangle(x, y, sw, sw, sw_r);
                draw_filament_swatch_border(dc, comp_clr, x, y, sw, sw, sw_r);

                wxString num = wxString::Format("%d", display_number(comp_dialog_idx));
                wxSize nsz = dc.GetTextExtent(num);
                dc.SetTextForeground(comp_clr.GetLuminance() < 0.6 ? *wxWHITE : texture_import_gray9000());
                dc.DrawText(num, x + (sw - nsz.x) / 2, y + (sw - nsz.y) / 2);
                x += sw + p->FromDIP(4);

                dc.SetTextForeground(name_fg);
                wxString pct = wxString::Format("%d%%", entry.mixed_ratios[ci]);
                wxSize psz = dc.GetTextExtent(pct);
                dc.DrawText(pct, x, (sz.y - psz.y) / 2);
                x += psz.x + p->FromDIP(4);
            }
            draw_delete_icon_if_needed(dc, p, idx);
            });
        });

        bind_row_hover(row, idx, name_str);
        bind_click(row, [this, row, idx]() { handle_row_click(row, idx); });

        return row;
    }

    wxScrolledWindow*                         m_content = nullptr;
    std::vector<TextureFilamentEntry>          m_entries;
    std::vector<std::array<float, 4>>          m_colors_rgba;
    std::vector<std::string>                   m_names;
    size_t                                     m_existing_count = 0;
    wxWindow*                                  m_dialog_anchor = nullptr;
    std::function<void(int)>                   m_on_select;
    std::function<void()>                      m_on_add_filament;
    std::function<void(int)>                   m_on_delete;
    std::function<void(FilamentSelectPopup*)>  m_on_close;
    // 1-based display number per dialog_index, mirroring the post-apply
    // sidebar ordering (ExistingPhysical, NewPhysical, ExistingMixed, NewMixed).
    std::vector<int>                           m_display_numbers;
    ScalableBitmap                             m_bmp_delete;
    ScalableBitmap                             m_bmp_brand;
    wxStaticText*                              m_add_label = nullptr;
    wxColour                                   m_header_clr;
    int                                        m_pop_w = 0;
    int                                        m_row_h = 0;
    int                                        m_pad = 0;
    int                                        m_hover_idx = -1;
    wxWindow*                                  m_hover_row = nullptr;
    std::vector<std::pair<int, wxWindow*>>     m_row_windows;
    wxWeakRef<wxWindow>                        m_pressed_target;
    bool                                       m_click_capture_owned = false;
    bool                                       m_dismissed = false;
    bool                                       m_destroy_scheduled = false;
    bool                                       m_activate_app_bound = false;
    void*                                      m_external_click_monitor = nullptr;
    WindowShadow                              m_shadow;

    // Returns the display number for a dialog_index, falling back to idx + 1
    // when no mapping is available (e.g. index out of range).
    int display_number(int idx) const {
        return (idx >= 0 && idx < (int)m_display_numbers.size() && m_display_numbers[idx] > 0)
            ? m_display_numbers[idx] : idx + 1;
    }
};

static int unique_face_color_count(const std::vector<std::array<std::size_t, 3>>& colors)
{
    std::set<std::array<std::size_t, 3>> unique(colors.begin(), colors.end());
    return (int)unique.size();
}


class AdvancedFoldHeader : public wxPanel
{
public:
    AdvancedFoldHeader(wxWindow* parent, const wxString& label)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE)
        , m_label(label)
    {
        SetFont(Label::Head_14);
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetCursor(wxCursor(wxCURSOR_HAND));
        Bind(wxEVT_PAINT, &AdvancedFoldHeader::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
            wxCommandEvent evt(wxEVT_BUTTON, GetId());
            evt.SetEventObject(this);
            ProcessWindowEvent(evt);
        });
    }

    void set_expanded(bool expanded)
    {
        if (m_expanded == expanded)
            return;
        m_expanded = expanded;
        Refresh();
    }

    bool is_expanded() const { return m_expanded; }

    wxSize DoGetBestSize() const override
    {
        wxClientDC dc(const_cast<AdvancedFoldHeader*>(this));
        dc.SetFont(GetFont());
        const wxSize text = dc.GetTextExtent(m_label.IsEmpty() ? " " : m_label);
        return wxSize(text.x + FromDIP(22), std::max(FromDIP(20), text.y + FromDIP(4)));
    }

private:
    void OnPaint(wxPaintEvent&)
    {
        texture_import_paint(this, [this](wxDC& dc) {
            dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
            dc.Clear();

            const wxColour fg = texture_import_brand_green();
            dc.SetFont(GetFont());
            dc.SetTextForeground(fg);
            const wxSize text = dc.GetTextExtent(m_label);
            const int y = std::max(0, (GetClientSize().y - text.y) / 2);
            dc.DrawText(m_label, 0, y);

            const int chev_w = FromDIP(8);
            const int chev_h = FromDIP(4);
            const int left = text.x + FromDIP(8);
            const int cy = GetClientSize().y / 2;
            const double pen_w = std::max(1.0, (double)FromDIP(1));
            if (wxGraphicsContext* gc = dc.GetGraphicsContext()) {
                wxGraphicsPenInfo info(fg, pen_w);
                info.Cap(wxCAP_ROUND);
                info.Join(wxJOIN_ROUND);
                gc->SetPen(gc->CreatePen(info));
                if (m_expanded) {
                    gc->StrokeLine(left, cy + chev_h / 2.0, left + chev_w / 2.0, cy - chev_h / 2.0);
                    gc->StrokeLine(left + chev_w / 2.0, cy - chev_h / 2.0, left + chev_w, cy + chev_h / 2.0);
                } else {
                    gc->StrokeLine(left, cy - chev_h / 2.0, left + chev_w / 2.0, cy + chev_h / 2.0);
                    gc->StrokeLine(left + chev_w / 2.0, cy + chev_h / 2.0, left + chev_w, cy - chev_h / 2.0);
                }
            } else {
                dc.SetPen(wxPen(fg, std::max(1, FromDIP(1))));
                if (m_expanded) {
                    dc.DrawLine(left, cy + chev_h / 2, left + chev_w / 2, cy - chev_h / 2);
                    dc.DrawLine(left + chev_w / 2, cy - chev_h / 2, left + chev_w, cy + chev_h / 2);
                } else {
                    dc.DrawLine(left, cy - chev_h / 2, left + chev_w / 2, cy + chev_h / 2);
                    dc.DrawLine(left + chev_w / 2, cy + chev_h / 2, left + chev_w, cy - chev_h / 2);
                }
            }
        });
    }

    wxString m_label;
    bool     m_expanded = false;
};

class UpdatingOverlayPanel : public wxPanel
{
public:
    static constexpr int kSpinnerPx = 32;

    UpdatingOverlayPanel(wxWindow* parent, const wxColour& bg, const wxColour& fg)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    {
        SetBackgroundColour(bg);
        m_spinner = ScalableBitmap(this, "sync_loading_arc", kSpinnerPx);

        m_icon = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE);
        m_icon->SetBackgroundColour(bg);
        m_icon->SetBackgroundStyle(wxBG_STYLE_PAINT);
        m_icon->SetMinSize(wxSize(FromDIP(kSpinnerPx), FromDIP(kSpinnerPx)));
        m_icon->Bind(wxEVT_PAINT, &UpdatingOverlayPanel::on_icon_paint, this);

        m_label = new Label(this, _L("Updating"));
        m_label->SetFont(Label::Head_14);
        m_label->SetForegroundColour(fg);
        m_label->SetBackgroundColour(bg);

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(m_icon, 0, wxALIGN_CENTER_HORIZONTAL);
        sizer->Add(m_label, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(8));
        SetSizer(sizer);

        m_timer.SetOwner(this);
        Bind(wxEVT_TIMER, &UpdatingOverlayPanel::on_timer, this);
        Hide();
    }

    ~UpdatingOverlayPanel() { m_timer.Stop(); }

    void Play()
    {
        Layout();
        Fit();
        if (!m_timer.IsRunning())
            m_timer.Start(33);
        Show();
        Raise();
    }

    void Stop()
    {
        m_timer.Stop();
        Hide();
    }

    void Rescale()
    {
        m_spinner.msw_rescale();
        if (m_icon)
            m_icon->SetMinSize(wxSize(FromDIP(kSpinnerPx), FromDIP(kSpinnerPx)));
        Layout();
        Fit();
        Refresh();
    }

    void set_theme(const wxColour& bg, const wxColour& fg)
    {
        SetBackgroundColour(bg);
        if (m_icon)
            m_icon->SetBackgroundColour(bg);
        if (m_label) {
            m_label->SetForegroundColour(fg);
            m_label->SetBackgroundColour(bg);
        }
        Refresh();
    }

private:
    void on_timer(wxTimerEvent&)
    {
        m_angle += 2.0 * M_PI * (33.0 / 800.0);
        if (m_angle > 2.0 * M_PI)
            m_angle -= 2.0 * M_PI;
        if (m_icon)
            m_icon->Refresh();
    }

    void on_icon_paint(wxPaintEvent&)
    {
        texture_import_paint(m_icon, [this](wxDC& dc) {
            dc.SetBackground(wxBrush(m_icon->GetBackgroundColour()));
            dc.Clear();
            wxGraphicsContext* gc = dc.GetGraphicsContext();
            std::unique_ptr<wxGraphicsContext> owned;
            if (!gc) {
                if (auto* pdc = dynamic_cast<wxPaintDC*>(&dc))
                    owned.reset(wxGraphicsContext::Create(*pdc));
                else if (auto* mdc = dynamic_cast<wxMemoryDC*>(&dc))
                    owned.reset(wxGraphicsContext::Create(*mdc));
                gc = owned.get();
            }
            if (!gc)
                return;
            const wxBitmap& bmp = m_spinner.bmp();
            if (!bmp.IsOk())
                return;
            const wxSize sz = m_icon->GetClientSize();
            const wxSize bs = m_spinner.GetBmpSize();
            gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
            gc->PushState();
            gc->Translate(sz.x / 2.0, sz.y / 2.0);
            gc->Rotate(m_angle);
            gc->DrawBitmap(bmp, -bs.x / 2.0, -bs.y / 2.0, bs.x, bs.y);
            gc->PopState();
        });
    }

    ScalableBitmap m_spinner;
    wxPanel*       m_icon  = nullptr;
    Label*         m_label = nullptr;
    wxTimer        m_timer;
    double         m_angle = 0.0;
};

class ColorCountWarningPanel : public wxPanel
{
public:
    ColorCountWarningPanel(wxWindow* parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    {
        m_icon = ScalableBitmap(this, "error", 16);
        m_icon_bmp = new wxStaticBitmap(this, wxID_ANY, m_icon.bmp());
        m_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
        m_label->SetFont(Label::Head_14);
        apply_colors();

        auto* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(m_icon_bmp, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
        sizer->Add(m_label, 0, wxALIGN_CENTER_VERTICAL);
        SetSizer(sizer);
        Hide();
    }

    void set_message(const wxString& msg)
    {
        if (m_label)
            m_label->SetLabel(msg);
        apply_colors();
        InvalidateBestSize();
        Layout();
        Refresh();
    }

    void apply_colors()
    {
        const wxColour bg = GetParent() ? GetParent()->GetBackgroundColour() : *wxWHITE;
        const wxColour fg = wxColour(225, 71, 71);
        SetBackgroundColour(bg);
        if (m_label) {
            m_label->SetForegroundColour(fg);
            m_label->SetBackgroundColour(bg);
        }
        if (m_icon_bmp)
            m_icon_bmp->SetBackgroundColour(bg);
    }

    void Rescale()
    {
        m_icon.msw_rescale();
        if (m_icon_bmp)
            m_icon_bmp->SetBitmap(m_icon.bmp());
        apply_colors();
        InvalidateBestSize();
        Layout();
        Refresh();
    }

private:
    ScalableBitmap  m_icon;
    wxStaticBitmap* m_icon_bmp = nullptr;
    wxStaticText*   m_label    = nullptr;
};

class TextureImportStepper : public wxPanel
{
public:
    TextureImportStepper(wxWindow* parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(wxSize(parent->FromDIP(420), parent->FromDIP(28)));
        Bind(wxEVT_PAINT, &TextureImportStepper::OnPaint, this);
    }

    void set_active_step(int step)
    {
        if (m_active == step)
            return;
        m_active = step;
        Refresh();
    }

private:
    int m_active = 0;

    // Ink bounds of `text` (drawn with dc's current font) relative to its
    // DrawText origin. The text extent is the full line box (ascent with
    // internal leading, descent, side bearings), so centering it leaves digits
    // visibly off center by a font- and platform-dependent amount.
    static wxRect text_ink_rect(wxDC& dc, const wxString& text)
    {
        const wxSize extent = dc.GetTextExtent(text);
        const int    pad    = std::max(2, extent.y / 2);
        wxBitmap     bmp(extent.x + pad * 2, extent.y + pad * 2);
        {
            wxMemoryDC mdc(bmp);
            mdc.SetBackground(*wxBLACK_BRUSH);
            mdc.Clear();
#ifdef __WXMSW__
            // Same renderer as texture_import_paint, so the glyph is rasterized the same way.
            wxGCDC gcdc(mdc);
            wxDC&  tdc = gcdc;
#else
            wxDC& tdc = mdc;
#endif
            tdc.SetFont(dc.GetFont());
            tdc.SetTextForeground(*wxWHITE);
            tdc.DrawText(text, pad, pad);
        }

        const wxImage img = bmp.ConvertToImage();
        if (!img.IsOk())
            return wxRect(wxPoint(0, 0), extent);
        const unsigned char* data = img.GetData();
        const int w = img.GetWidth();
        const int h = img.GetHeight();
        int x0 = w, y0 = h, x1 = -1, y1 = -1;
        for (int py = 0; py < h; ++py)
            for (int px = 0; px < w; ++px) {
                const unsigned char* p = data + (py * w + px) * 3;
                if (std::max({p[0], p[1], p[2]}) < 128)
                    continue;
                x0 = std::min(x0, px);
                y0 = std::min(y0, py);
                x1 = std::max(x1, px);
                y1 = std::max(y1, py);
            }
        if (x1 < 0)
            return wxRect(wxPoint(0, 0), extent);
        return wxRect(x0 - pad, y0 - pad, x1 - x0 + 1, y1 - y0 + 1);
    }

    void OnPaint(wxPaintEvent&)
    {
        texture_import_paint(this, [this](wxDC& dc) {
        const wxSize sz = GetClientSize();
        dc.SetBackground(wxBrush(GetParent() ? GetParent()->GetBackgroundColour() : *wxWHITE));
        dc.Clear();

        wxGraphicsContext* gc = dc.GetGraphicsContext();

        const wxColour active_fill(0, 174, 66);
        const wxColour inactive_fill = dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B));
        const wxColour active_text = texture_import_text_colour();
        const wxColour inactive_text = dark_or(wxColour(107, 107, 107), wxColour(0xB3, 0xB3, 0xB5));
        const wxColour line_clr = dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B));
        const wxColour inactive_mark = dark_or(texture_import_gray9000(), *wxWHITE);

        const int circle = FromDIP(20);
        const int y = (sz.y - circle) / 2;
        const wxString labels[2] = { _L("Simplify Colors"), _L("Filament Matching") };
        dc.SetFont(Label::Head_14);
        const int text_w0 = dc.GetTextExtent(labels[0]).x;
        const int text_w1 = dc.GetTextExtent(labels[1]).x;
        const int gap = FromDIP(8);
        const int block0 = circle + gap + text_w0;
        const int block1 = circle + gap + text_w1;
        const int connector = FromDIP(48);
        const int total = block0 + connector + block1;
        int x = std::max(0, (sz.x - total) / 2);

        auto draw_circle = [&](int left, const wxColour& fill) {
            if (gc) {
                gc->SetPen(wxPen(fill, 1));
                gc->SetBrush(wxBrush(fill));
                gc->DrawEllipse(left + 0.5, y + 0.5, circle - 1.0, circle - 1.0);
            } else {
                dc.SetPen(wxPen(fill, 1));
                dc.SetBrush(wxBrush(fill));
                dc.DrawEllipse(left, y, circle, circle);
            }
        };

        auto draw_check = [&](int left) {
            const double cx = left + circle / 2.0;
            const double cy = y + circle / 2.0;
            const double r = circle / 2.0;
            if (gc) {
                wxGraphicsPenInfo info(*wxWHITE, std::max(1.5, (double)FromDIP(2)));
                info.Cap(wxCAP_ROUND);
                info.Join(wxJOIN_ROUND);
                gc->SetPen(gc->CreatePen(info));
                gc->StrokeLine(cx - r * 0.38, cy + r * 0.02, cx - r * 0.12, cy + r * 0.32);
                gc->StrokeLine(cx - r * 0.12, cy + r * 0.32, cx + r * 0.40, cy - r * 0.28);
            } else {
                dc.SetPen(wxPen(*wxWHITE, std::max(1, FromDIP(2))));
                dc.DrawLine(int(cx - r * 0.38), int(cy), int(cx - r * 0.12), int(cy + r * 0.32));
                dc.DrawLine(int(cx - r * 0.12), int(cy + r * 0.32), int(cx + r * 0.40), int(cy - r * 0.28));
            }
        };

        auto draw_step = [&](int idx, int& cursor) {
            const bool done_or_active = m_active >= idx;
            draw_circle(cursor, done_or_active ? active_fill : inactive_fill);

            if (idx < m_active) {
                draw_check(cursor);
            } else {
                dc.SetFont(Label::Head_14);
                dc.SetTextForeground(done_or_active ? *wxWHITE : inactive_mark);
                const wxString mark = wxString::Format("%d", idx + 1);
                const wxRect   ink  = text_ink_rect(dc, mark);
                dc.DrawText(mark, cursor + (circle - ink.width) / 2 - ink.x, y + (circle - ink.height) / 2 - ink.y);
            }

            dc.SetFont(Label::Head_14);
            dc.SetTextForeground(done_or_active ? active_text : inactive_text);
            const wxSize label_sz = dc.GetTextExtent(labels[idx]);
            dc.DrawText(labels[idx], cursor + circle + gap, y + (circle - label_sz.y) / 2);
            cursor += (idx == 0 ? block0 : block1);
        };

        draw_step(0, x);
        const int line_y = y + circle / 2;
        if (gc) {
            gc->SetPen(wxPen(line_clr, 1));
            gc->StrokeLine(x + FromDIP(4), line_y + 0.5, x + connector - FromDIP(4), line_y + 0.5);
        } else {
            dc.SetPen(wxPen(line_clr, 1));
            dc.DrawLine(x + FromDIP(4), line_y, x + connector - FromDIP(4), line_y);
        }
        x += connector;
        draw_step(1, x);
        });
    }
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
        for (unsigned int id : {m_reset_icon_tex, m_reset_icon_hover_tex,
                                m_reset_icon_dark_tex, m_reset_icon_dark_hover_tex,
                                m_corner_tex})
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
    Refresh();
}

void TexturePreviewCanvas::set_painted_mesh_data(
    const std::vector<std::array<float, 3>>& vertices,
    const std::vector<std::array<int, 3>>&   indices)
{
    m_painted_vertices = vertices;
    m_painted_indices  = indices;
    if (m_vertices.empty())
        update_bounding_box();
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

void TexturePreviewCanvas::set_original_vertex_colors(const std::vector<std::array<float, 3>>& vertex_colors)
{
    m_original_vertex_colors_rgb = vertex_colors;
    Refresh();
}

void TexturePreviewCanvas::set_filament_color_map(
    const std::map<std::array<std::size_t, 3>, std::array<float, 3>>& color_map)
{
    m_color_map = color_map;
    m_filament_colors_rgb.resize(m_face_colors_rgb.size());
    const std::array<float, 3> unmatched_color = {0x75 / 255.f, 0x75 / 255.f, 0x7A / 255.f};
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
            m_filament_colors_rgb[i] = unmatched_color;
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

void TexturePreviewCanvas::set_computing_overlay(bool show)
{
    if (m_computing_overlay == show)
        return;
    m_computing_overlay = show;
    Refresh();
}

TexturePreviewCanvas::ViewState TexturePreviewCanvas::get_view_state() const
{
    ViewState state;
    state.zoom  = m_zoom;
    state.rot_x = m_rot_x;
    state.rot_y = m_rot_y;
    state.pan_x = m_pan_x;
    state.pan_y = m_pan_y;
    return state;
}

void TexturePreviewCanvas::set_view_state(const ViewState& state, bool notify)
{
    if (m_zoom == state.zoom && m_rot_x == state.rot_x && m_rot_y == state.rot_y &&
        m_pan_x == state.pan_x && m_pan_y == state.pan_y) {
        if (notify)
            notify_view_changed();
        return;
    }
    m_zoom  = state.zoom;
    m_rot_x = state.rot_x;
    m_rot_y = state.rot_y;
    m_pan_x = state.pan_x;
    m_pan_y = state.pan_y;
    present();
    if (notify)
        notify_view_changed();
}

void TexturePreviewCanvas::set_view_changed_callback(std::function<void(const ViewState&)> cb)
{
    m_view_changed_cb = std::move(cb);
}

void TexturePreviewCanvas::notify_view_changed()
{
    if (m_view_changed_cb)
        m_view_changed_cb(get_view_state());
}

void TexturePreviewCanvas::reset_view()
{
    m_zoom  = 1.0f;
    m_rot_x = 30.0f;
    m_rot_y = 30.0f;
    m_pan_x = 0.0f;
    m_pan_y = 0.0f;
    present();
    notify_view_changed();
}

void TexturePreviewCanvas::set_reset_overlay_align(ResetOverlayAlign align)
{
    m_reset_overlay_align = align;
    Refresh();
}

void TexturePreviewCanvas::set_reset_overlay_hovered(bool hovered)
{
    if (m_reset_overlay_hovered == hovered)
        return;
    m_reset_overlay_hovered = hovered;
    Refresh();
}

void TexturePreviewCanvas::set_reset_overlay_hover_callback(std::function<void(bool)> cb)
{
    m_reset_hover_cb = std::move(cb);
}

void TexturePreviewCanvas::set_rounded_corners(RoundedCornerSide side, int radius, int inset,
                                               const wxColour& outside, const wxColour& border)
{
    if (m_corner_side == side && m_corner_radius == radius && m_corner_inset == inset &&
        m_corner_outside == outside && m_corner_border == border)
        return;

    const bool colors_changed = (m_corner_outside != outside || m_corner_border != border);
    m_corner_side    = side;
    m_corner_radius  = radius;
    m_corner_inset   = inset;
    m_corner_outside = outside;
    m_corner_border  = border;
    if (colors_changed)
        m_corner_tex_px = 0; // force a rebuild with the new colors
    Refresh();
}

wxRect TexturePreviewCanvas::reset_overlay_rect() const
{
    if (m_reset_overlay_align != ResetOverlayAlign::BottomRight)
        return wxRect();
    wxSize sz = GetClientSize();
    const int button_size = FromDIP(24);
    const int margin = FromDIP(8);
    return wxRect(sz.x - button_size - margin,
                  std::max(0, sz.y - button_size - margin),
                  button_size, button_size);
}

unsigned int TexturePreviewCanvas::upload_reset_icon_texture(const std::string& icon_name)
{
    wxBitmap bmp = create_scaled_bitmap(icon_name, this, 24);
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
    if (m_reset_overlay_align == ResetOverlayAlign::Hidden)
        return false;

    if (evt.Leaving()) {
        if (m_reset_overlay_pressed) {
            m_reset_overlay_pressed = false;
            if (HasCapture())
                ReleaseMouse();
        }
        if (m_reset_overlay_hovered) {
            m_reset_overlay_hovered = false;
            SetCursor(wxCursor(wxCURSOR_ARROW));
            if (m_reset_hover_cb)
                m_reset_hover_cb(false);
            Refresh();
        }
        return false;
    }

    const bool over = reset_overlay_rect().Contains(evt.GetPosition());
    if (over != m_reset_overlay_hovered) {
        m_reset_overlay_hovered = over;
        SetCursor(wxCursor(over ? wxCURSOR_HAND : wxCURSOR_ARROW));
        if (m_reset_hover_cb)
            m_reset_hover_cb(over);
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
    const auto& verts = !m_vertices.empty() ? m_vertices : m_painted_vertices;
    if (verts.empty()) return;
    std::array<float, 3> mn = verts[0], mx = verts[0];
    for (const auto& v : verts) {
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
    present();
}

void TexturePreviewCanvas::present()
{
    if (!IsShownOnScreen() || !m_context)
        return;
    SetCurrent(*m_context);
    ensure_gl_ready();
    if (!m_gl_initialized)
        return;
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
        present();
        notify_view_changed();
    }
    else if (evt.GetWheelRotation() != 0) {
        float delta = evt.GetWheelRotation() > 0 ? 1.1f : 0.9f;
        m_zoom *= delta;
        m_zoom = std::max(0.1f, std::min(20.0f, m_zoom));
        present();
        notify_view_changed();
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
    // Mesh data is Z-up like the 3D scene; map world Z to screen up and yaw around world Z.
    glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(m_rot_y, 0.0f, 0.0f, 1.0f);
    glTranslatef(-m_center[0], -m_center[1], -m_center[2]);

    if (!m_computing_overlay)
        render_mesh();
    render_rounded_corners(sz, viewport_sz);
    render_reset_overlay(sz, viewport_sz);
}

void TexturePreviewCanvas::render_reset_overlay(const wxSize& logical_size, const wxSize& viewport_size)
{
    if (m_reset_overlay_align == ResetOverlayAlign::Hidden)
        return;
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

    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, viewport_size.x, viewport_size.y);
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

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
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
    glDisable(GL_SCISSOR_TEST);
    glPopAttrib();
}

// Builds one top-left corner tile: opaque outside the container outline, the
// outline itself in the border color, transparent inside so the 3D scene shows
// through. Coverage is supersampled here instead of relying on wxDC, whose
// antialiasing differs between GDI, Cairo and CoreGraphics.
void TexturePreviewCanvas::ensure_corner_texture(int texture_px)
{
    if (m_corner_tex && m_corner_tex_px == texture_px)
        return;
    if (m_corner_tex) {
        glDeleteTextures(1, &m_corner_tex);
        m_corner_tex = 0;
    }
    m_corner_tex_px = 0;
    if (texture_px <= 0 || m_corner_radius <= 0)
        return;

    const float n = (float)texture_px;
    const float border_w = std::max(1.0f, n / (float)m_corner_radius);
    const std::array<float, 3> outside = {
        m_corner_outside.Red() / 255.f, m_corner_outside.Green() / 255.f, m_corner_outside.Blue() / 255.f};
    const std::array<float, 3> border = {
        m_corner_border.Red() / 255.f, m_corner_border.Green() / 255.f, m_corner_border.Blue() / 255.f};

    constexpr int kSubSamples = 4;
    std::vector<unsigned char> rgba((size_t)texture_px * texture_px * 4);
    for (int y = 0; y < texture_px; ++y) {
        for (int x = 0; x < texture_px; ++x) {
            float sum[3] = {0.f, 0.f, 0.f};
            int covered = 0;
            for (int sy = 0; sy < kSubSamples; ++sy) {
                for (int sx = 0; sx < kSubSamples; ++sx) {
                    const float px = x + (sx + 0.5f) / kSubSamples;
                    const float py = y + (sy + 0.5f) / kSubSamples;
                    // Arc center sits at the inner end of the corner tile.
                    const float dx = px - n;
                    const float dy = py - n;
                    const float d = std::sqrt(dx * dx + dy * dy) - n;
                    if (d <= -border_w)
                        continue;
                    const std::array<float, 3>& c = (d <= 0.f) ? border : outside;
                    sum[0] += c[0];
                    sum[1] += c[1];
                    sum[2] += c[2];
                    ++covered;
                }
            }
            const size_t idx = ((size_t)y * texture_px + x) * 4;
            if (covered == 0) {
                rgba[idx + 0] = rgba[idx + 1] = rgba[idx + 2] = rgba[idx + 3] = 0;
                continue;
            }
            for (int c = 0; c < 3; ++c)
                rgba[idx + c] = (unsigned char)std::lround(std::min(1.f, sum[c] / covered) * 255.f);
            rgba[idx + 3] = (unsigned char)std::lround(
                (float)covered / (kSubSamples * kSubSamples) * 255.f);
        }
    }

    GLuint tex_id = 0;
    glGenTextures(1, &tex_id);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture_px, texture_px, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    m_corner_tex    = tex_id;
    m_corner_tex_px = texture_px;
}

void TexturePreviewCanvas::render_rounded_corners(const wxSize& logical_size, const wxSize& viewport_size)
{
    if (m_corner_side == RoundedCornerSide::None || m_corner_radius <= 0)
        return;
    if (logical_size.x <= 0 || logical_size.y <= 0 || viewport_size.x <= 0 || viewport_size.y <= 0)
        return;

    const float sx = (float)viewport_size.x / (float)logical_size.x;
    const float sy = (float)viewport_size.y / (float)logical_size.y;

    ensure_corner_texture(std::max(1, (int)std::lround(m_corner_radius * sx)));
    if (!m_corner_tex)
        return;

    // Corner tiles are anchored to the container edges, which sit "inset" pixels
    // outside the canvas on the rounded side.
    const float r  = (float)m_corner_radius;
    const float in = (float)m_corner_inset;

    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_corner_tex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, viewport_size.x, viewport_size.y, 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    auto draw_corner = [&](float left, float right, float top, float bottom, bool flip_x, bool flip_y) {
        const float u0 = flip_x ? 1.0f : 0.0f;
        const float u1 = flip_x ? 0.0f : 1.0f;
        const float v0 = flip_y ? 1.0f : 0.0f;
        const float v1 = flip_y ? 0.0f : 1.0f;
        glBegin(GL_QUADS);
            glTexCoord2f(u0, v0); glVertex2f(left * sx, top * sy);
            glTexCoord2f(u1, v0); glVertex2f(right * sx, top * sy);
            glTexCoord2f(u1, v1); glVertex2f(right * sx, bottom * sy);
            glTexCoord2f(u0, v1); glVertex2f(left * sx, bottom * sy);
        glEnd();
    };
    auto draw_side = [&](bool left_side) {
        const float outer_x = left_side ? -in : logical_size.x + in;
        const float left    = left_side ? outer_x : outer_x - r;
        const float right   = left + r;
        const bool  flip_x  = !left_side;
        draw_corner(left, right, -in, -in + r, flip_x, false);
        draw_corner(left, right, logical_size.y + in - r, logical_size.y + in, flip_x, true);
    };
    if (m_corner_side == RoundedCornerSide::All || m_corner_side == RoundedCornerSide::Left)
        draw_side(true);
    if (m_corner_side == RoundedCornerSide::All || m_corner_side == RoundedCornerSide::Right)
        draw_side(false);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    glBindTexture(GL_TEXTURE_2D, 0);
    glPopAttrib();
}

void TexturePreviewCanvas::render_mesh()
{
    // For Multi-Color / FilamentMap, use the painted (remeshed) geometry if available;
    // the face color arrays match the painted mesh, not the original mesh.
    const bool use_painted = (m_mode != RenderMode::Original)
                             && !m_painted_vertices.empty()
                             && !m_painted_indices.empty();
    if (!use_painted && (m_vertices.empty() || m_indices.empty()))
        return;

    const auto& verts   = use_painted ? m_painted_vertices : m_vertices;
    const auto& faces   = use_painted ? m_painted_indices  : m_indices;

    const bool use_vertex_colors = m_mode == RenderMode::Original
        && !use_painted
        && m_original_vertex_colors_rgb.size() == verts.size()
        && !m_original_vertex_colors_rgb.empty();

    const std::vector<std::array<float, 3>>* colors_ptr = nullptr;
    if (use_vertex_colors) {
        glShadeModel(GL_SMOOTH);
    } else if (m_mode == RenderMode::Original && !m_original_face_colors_rgb.empty()
        && m_original_face_colors_rgb.size() == m_indices.size()) {
        colors_ptr = &m_original_face_colors_rgb;
    } else if (m_mode == RenderMode::FilamentMap && !m_filament_colors_rgb.empty()
               && m_filament_colors_rgb.size() == faces.size()) {
        colors_ptr = &m_filament_colors_rgb;
    } else if (!m_face_colors_rgb.empty() && m_face_colors_rgb.size() == faces.size()) {
        colors_ptr = &m_face_colors_rgb;
    }

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);

    glBegin(GL_TRIANGLES);
    for (size_t fi = 0; fi < faces.size(); ++fi) {
        if (colors_ptr)
            glColor3fv((*colors_ptr)[fi].data());
        else
            glColor3f(0.7f, 0.7f, 0.7f);

        const auto& face = faces[fi];
        if (face[0] < 0 || face[0] >= (int)verts.size() ||
            face[1] < 0 || face[1] >= (int)verts.size() ||
            face[2] < 0 || face[2] >= (int)verts.size())
            continue;

        const auto& v0 = verts[face[0]];
        const auto& v1 = verts[face[1]];
        const auto& v2 = verts[face[2]];
        float nx = (v1[1]-v0[1])*(v2[2]-v0[2]) - (v1[2]-v0[2])*(v2[1]-v0[1]);
        float ny = (v1[2]-v0[2])*(v2[0]-v0[0]) - (v1[0]-v0[0])*(v2[2]-v0[2]);
        float nz = (v1[0]-v0[0])*(v2[1]-v0[1]) - (v1[1]-v0[1])*(v2[0]-v0[0]);
        float len = std::sqrt(nx*nx + ny*ny + nz*nz);
        if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
        glNormal3f(nx, ny, nz);

        const std::array<float, 3>* corner_colors[3] = {nullptr, nullptr, nullptr};
        if (use_vertex_colors) {
            corner_colors[0] = &m_original_vertex_colors_rgb[face[0]];
            corner_colors[1] = &m_original_vertex_colors_rgb[face[1]];
            corner_colors[2] = &m_original_vertex_colors_rgb[face[2]];
        }

        if (corner_colors[0]) glColor3fv(corner_colors[0]->data());
        glVertex3fv(v0.data());
        if (corner_colors[1]) glColor3fv(corner_colors[1]->data());
        glVertex3fv(v1.data());
        if (corner_colors[2]) glColor3fv(corner_colors[2]->data());
        glVertex3fv(v2.data());
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
    EVT_BUTTON(TextureImportDialog::ID_BTN_SKIP,    TextureImportDialog::on_skip_clicked)
    EVT_BUTTON(TextureImportDialog::ID_BTN_NEXT,    TextureImportDialog::on_next_clicked)
    EVT_BUTTON(TextureImportDialog::ID_BTN_PREV,    TextureImportDialog::on_prev_clicked)
    EVT_BUTTON(TextureImportDialog::ID_BTN_RESET,   TextureImportDialog::on_reset_clicked)
    EVT_BUTTON(wxID_OK,                              TextureImportDialog::on_ok_clicked)
wxEND_EVENT_TABLE()

TextureImportDialog::TextureImportDialog(
    wxWindow*                        parent,
    const Slic3r::TexturedMesh&      textured_mesh,
    const std::vector<TextureFilamentEntry>& filament_entries,
    std::function<bool()>            initial_cancel_callback,
    std::function<bool(int)>         initial_progress_callback,
    std::function<void(bool)>        initial_progress_visibility_callback,
    const wxString&                  hint)
    : DPIDialog(parent, wxID_ANY, _L("Import Model: Simplify Colors"),
                wxDefaultPosition, wxDefaultSize,
                (wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) & ~(wxMINIMIZE_BOX | wxMAXIMIZE_BOX))
    , m_textured_mesh(textured_mesh)
    , m_filament_entries(filament_entries)
    , m_initial_cancel_callback(std::move(initial_cancel_callback))
    , m_initial_progress_callback(std::move(initial_progress_callback))
    , m_initial_progress_visibility_callback(std::move(initial_progress_visibility_callback))
    , m_hint(hint)
    , m_gap_preview(std::make_unique<GapPreviewState>())
{
    m_bmp_unmatched = ScalableBitmap(this, "error", 16);
    m_bmp_brand = ScalableBitmap(this, "BambuStudioBlack", 16);
    m_mesh_repair_cache = Slic3r::make_mesh_repair_cache();

    m_filament_colors_rgba.reserve(m_filament_entries.size());
    m_filament_color_strs.reserve(m_filament_entries.size());
    m_filament_names.reserve(m_filament_entries.size());
    for (size_t i = 0; i < m_filament_entries.size(); ++i) {
        auto& entry = m_filament_entries[i];
        entry.dialog_index = (int)i;
        entry.color_hex = texture_normalize_color_hex(entry.color_hex);
        if (entry.name.empty())
            entry.name = default_filament_stored_name((int)i + 1);
        m_filament_color_strs.push_back(entry.color_hex);
        m_filament_names.push_back(entry.name);
        m_filament_colors_rgba.push_back(parse_color_string(entry.color_hex));
    }

    m_existing_filament_count = m_filament_colors_rgba.size();
    m_default_virtual_filament_preset_name = resolve_default_virtual_filament_preset_name();
    m_palette_virtual_filament_preset_name = resolve_series_virtual_filament_preset_name(PALETTE_VIRTUAL_FILAMENT_NAME);

    Bind(EVT_TEXTURE_COMPUTE_DONE,     &TextureImportDialog::on_computation_complete, this);
    Bind(EVT_TEXTURE_COMPUTE_PROGRESS, &TextureImportDialog::on_computation_progress, this);
    Bind(EVT_TEXTURE_COMPUTE_ERROR,    &TextureImportDialog::on_computation_error,    this);
    Bind(EVT_TEXTURE_PATCH_DONE,       &TextureImportDialog::on_patch_build_complete, this);

    m_recompute_timer = new wxTimer(this);
    Bind(wxEVT_TIMER, &TextureImportDialog::on_recompute_timer, this);

    build_ui();
    apply_dialog_geometry(false);
    // macOS only has a real NSWindow after show; Windows can apply immediately.
    on_window_geometry(this, [this]() { apply_dialog_geometry(true); });
    wxGetApp().UpdateDlgDarkUI(this);
    for (Button* btn : {m_btn_color_4, m_btn_color_8, m_btn_color_16, m_btn_color_auto})
        style_color_count_preset_button(btn);
    style_advanced_settings_card();

    for_each_preview([&](TexturePreviewCanvas* canvas) {
        canvas->set_mesh_data(m_textured_mesh.vertices, m_textured_mesh.indices);
    });

    if (m_preview_canvas && m_preview_canvas_right) {
        m_preview_canvas->set_reset_overlay_align(TexturePreviewCanvas::ResetOverlayAlign::BottomRight);
        m_preview_canvas_right->set_reset_overlay_align(TexturePreviewCanvas::ResetOverlayAlign::BottomRight);
        m_preview_canvas->set_view_changed_callback(
            [this](const TexturePreviewCanvas::ViewState& state) {
                if (m_preview_canvas_right)
                    m_preview_canvas_right->set_view_state(state, false);
            });
        m_preview_canvas_right->set_view_changed_callback(
            [this](const TexturePreviewCanvas::ViewState& state) {
                if (m_preview_canvas)
                    m_preview_canvas->set_view_state(state, false);
            });
    }

    // Pre-computed face colors (OBJ vertex colors / MTL face colors):
    // use them as a fallback Original preview until the prepared cache is ready.
    // Textured models wait for the oversampled unclustered mesh from the cache.
    if (!m_textured_mesh.precomputed_face_colors.empty()) {
        m_original_color_count = unique_face_color_count(m_textured_mesh.precomputed_face_colors);
        for_each_preview([&](TexturePreviewCanvas* canvas) {
            canvas->set_original_face_colors(m_textured_mesh.precomputed_face_colors);
        });
    }
    // Vertex colors are drawn on the source mesh so the left view stays a gradient.
    if (m_textured_mesh.precomputed_vertex_colors.size() == m_textured_mesh.vertices.size()
        && !m_textured_mesh.precomputed_vertex_colors.empty()) {
        std::vector<std::array<float, 3>> vertex_rgb(m_textured_mesh.precomputed_vertex_colors.size());
        std::set<std::array<int, 3>> unique_vertices;
        for (size_t i = 0; i < vertex_rgb.size(); ++i) {
            const auto& c = m_textured_mesh.precomputed_vertex_colors[i];
            vertex_rgb[i] = {c[0], c[1], c[2]};
            unique_vertices.insert({
                (int)std::lround(std::clamp(c[0], 0.f, 1.f) * 255.f),
                (int)std::lround(std::clamp(c[1], 0.f, 1.f) * 255.f),
                (int)std::lround(std::clamp(c[2], 0.f, 1.f) * 255.f)
            });
        }
        if (!unique_vertices.empty())
            m_original_color_count = (int)unique_vertices.size();
        m_lock_original_vertex_preview = true;
        for_each_preview([&](TexturePreviewCanvas* canvas) {
            canvas->set_original_vertex_colors(vertex_rgb);
        });
    }

    update_color_count_controls();
    update_preview_modes();
    set_state(TextureImportState::Idle);
}

TextureImportDialog::~TextureImportDialog()
{
    if (m_recompute_timer) {
        m_recompute_timer->Stop();
        delete m_recompute_timer;
        m_recompute_timer = nullptr;
    }
    // Tear down without Dismiss(): OnDismiss queues CallAfter on this dialog
    // and on the popup, which is unsafe while ~TextureImportDialog is running.
    if (m_filament_popup) {
        FilamentSelectPopup* popup = m_filament_popup;
        m_filament_popup = nullptr;
        m_filament_popup_row = -1;
        popup->Destroy();
    }
    if (m_match_threshold_popup) {
        MatchThresholdPopup* popup = m_match_threshold_popup;
        m_match_threshold_popup = nullptr;
        popup->shutdown();
        popup->Destroy();
    }
    m_cancel_flag = true;
    m_patch_generation.fetch_add(1);
    m_patch_building = false;
    if (m_worker.joinable())
        m_worker.join();
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

        if (m_worker.joinable())
            m_worker.join();
        m_worker = boost::thread();

        if (m_initial_computation_cancelled || m_initial_computation_failed)
            return wxID_CANCEL;
    }

    // Hide the outer load_files progress dialog while this UI is shown; restore
    // it when Confirm / Skip / close returns to the import pipeline.
    struct RestoreProgressVisibility {
        std::function<void(bool)> cb;
        explicit RestoreProgressVisibility(std::function<void(bool)> c) : cb(std::move(c)) {
            if (cb) cb(false);
        }
        ~RestoreProgressVisibility() {
            if (cb) cb(true);
        }
    } restore_progress(m_initial_progress_visibility_callback);

    ScopedInteractiveBusyCursorSuspender busy_cursor_suspender;
    return DPIDialog::ShowModal();
}

void TextureImportDialog::build_ui()
{
    const wxColour dialog_bg = texture_import_dialog_bg();
    SetBackgroundColour(dialog_bg);
    SetForegroundColour(texture_import_dialog_fg());

    wxBoxSizer* root_sizer = new wxBoxSizer(wxVERTICAL);

    m_title_line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_title_line->SetBackgroundColour(texture_import_title_line_colour());
    root_sizer->Add(m_title_line, 0, wxEXPAND);

    wxBoxSizer* content_sizer = new wxBoxSizer(wxVERTICAL);
    build_stepper(this, content_sizer);
    build_params_panel(this, content_sizer);
    build_preview_panel(this, content_sizer);
    build_mapping_panel(this, content_sizer);
    build_bottom_buttons(content_sizer);
    root_sizer->Add(content_sizer, 1, wxEXPAND | wxALL, FromDIP(12));

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

    update_wizard_ui();
}

void TextureImportDialog::build_stepper(wxWindow* parent, wxSizer* sizer)
{
    auto* stepper = new TextureImportStepper(parent);
    m_stepper_panel = stepper;
    sizer->Add(stepper, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(12));
}

void TextureImportDialog::build_preview_panel(wxWindow* parent, wxSizer* sizer)
{
    wxColour preview_bg = texture_import_preview_bg();
    wxColour preview_bd = texture_import_preview_bd();
    wxColour tag_bg = texture_import_tag_bg();
    wxColour tag_fg = texture_import_tag_fg();
    wxColour caption_fg = texture_import_caption_fg();
    const int preview_radius = FromDIP(8);

    wxPanel* preview_container = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    preview_container->SetBackgroundColour(parent->GetBackgroundColour());
    m_preview_container = preview_container;

    wxBoxSizer* halves = new wxBoxSizer(wxHORIZONTAL);
    wxGLAttributes canvas_attrs;
    canvas_attrs.PlatformDefaults().RGBA().DoubleBuffer().Depth(24).EndList();

    auto make_half = [&](TexturePreviewCanvas*& canvas, wxPanel*& tag_panel,
                         const wxString& tag_text) -> wxWindow* {
        wxPanel* half = new wxPanel(preview_container, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE);
        half->SetBackgroundColour(preview_bg);
        half->SetBackgroundStyle(wxBG_STYLE_PAINT);
        half->Bind(wxEVT_PAINT, [preview_radius](wxPaintEvent& e) {
            auto* p = static_cast<wxPanel*>(e.GetEventObject());
            const wxColour card_bg = texture_import_preview_bg();
            const wxColour card_bd = texture_import_preview_bd();
            texture_import_paint(p, [p, card_bg, card_bd, preview_radius](wxDC& dc) {
                wxSize sz = p->GetClientSize();
                dc.SetBrush(wxBrush(p->GetParent()->GetBackgroundColour()));
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.DrawRectangle(0, 0, sz.x, sz.y);
                dc.SetBrush(wxBrush(card_bg));
                dc.SetPen(wxPen(card_bd, 1));
                dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, preview_radius);
            });
        });

        wxBoxSizer* half_sizer = new wxBoxSizer(wxVERTICAL);

        wxPanel* canvas_host = new wxPanel(half, wxID_ANY);
        canvas_host->SetBackgroundColour(preview_bg);
        canvas = new TexturePreviewCanvas(canvas_host, canvas_attrs);
        wxBoxSizer* host_sizer = new wxBoxSizer(wxVERTICAL);
        host_sizer->Add(canvas, 1, wxEXPAND);
        canvas_host->SetSizer(host_sizer);

        auto* tag = new PreviewTagPanel(canvas_host, tag_text, tag_bg, tag_fg, preview_bg);
        tag_panel = tag;

        auto position_tag = [canvas_host, tag]() {
            tag->InvalidateBestSize();
            tag->Fit();
            const wxSize host_sz = canvas_host->GetClientSize();
            const wxSize tag_sz = tag->GetBestSize();
            tag->SetSize(tag_sz);
            tag->SetPosition(wxPoint(std::max(0, (host_sz.x - tag_sz.x) / 2), 0));
            tag->Raise();
        };
        canvas_host->Bind(wxEVT_SIZE, [this, position_tag](wxSizeEvent& e) {
            e.Skip();
            update_preview_rounded_corners();
            position_tag();
        });
        position_tag();

        half_sizer->Add(canvas_host, 1, wxEXPAND | wxALL, 1);
        half->SetSizer(half_sizer);
        return half;
    };

    wxWindow* left_half = make_half(m_preview_canvas, m_lbl_preview_left_panel, _L("Original"));
    wxWindow* right_half = make_half(m_preview_canvas_right, m_lbl_preview_right_panel, _L("Simplified"));

    if (m_preview_canvas_right) {
        wxWindow* right_host = m_preview_canvas_right->GetParent();
        auto* updating = new UpdatingOverlayPanel(right_host, preview_bg, tag_fg);
        m_updating_overlay = updating;
        auto position_updating = [right_host, updating]() {
            updating->InvalidateBestSize();
            updating->Fit();
            const wxSize host_sz = right_host->GetClientSize();
            const wxSize label_sz = updating->GetBestSize();
            updating->SetSize(label_sz);
            updating->SetPosition(wxPoint(
                std::max(0, (host_sz.x - label_sz.x) / 2),
                std::max(0, (host_sz.y - label_sz.y) / 2)));
            if (updating->IsShown())
                updating->Raise();
        };
        right_host->Bind(wxEVT_SIZE, [position_updating](wxSizeEvent& e) {
            e.Skip();
            position_updating();
        });
        position_updating();
    }

    halves->Add(left_half, 1, wxEXPAND);
    halves->AddSpacer(FromDIP(8));
    halves->Add(right_half, 1, wxEXPAND);

    wxBoxSizer* container_sizer = new wxBoxSizer(wxVERTICAL);
    container_sizer->Add(halves, 1, wxEXPAND);
    preview_container->SetSizer(container_sizer);

    preview_container->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        e.Skip();
        update_preview_rounded_corners();
    });

    preview_container->SetMinSize(wxSize(-1, FromDIP(351)));
    sizer->Add(preview_container, 1, wxEXPAND | wxBOTTOM, FromDIP(4));

    m_caption_row = new wxPanel(parent, wxID_ANY);
    m_caption_row->SetBackgroundColour(parent->GetBackgroundColour());
    wxBoxSizer* caption_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_lbl_caption_left = new wxStaticText(m_caption_row, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
    m_lbl_caption_left->SetForegroundColour(caption_fg);
    m_lbl_caption_left->SetFont(Label::Body_14);
    m_lbl_caption_right = new wxStaticText(m_caption_row, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
    m_lbl_caption_right->SetForegroundColour(caption_fg);
    m_lbl_caption_right->SetFont(Label::Body_14);
    caption_sizer->Add(m_lbl_caption_left, 1, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);
    caption_sizer->Add(m_lbl_caption_right, 1, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);
    if (m_hint.empty()) {
        m_caption_row->SetSizer(caption_sizer);
    } else {
        // Part of the caption row, so it is only shown in the first step as well.
        m_hint_icon = new wxStaticBitmap(m_caption_row, wxID_ANY, create_scaled_bitmap("warning", this, 16),
                                         wxDefaultPosition, FromDIP(wxSize(16, 16)), 0);
        m_hint_icon->SetBackgroundColour(m_caption_row->GetBackgroundColour());
        m_lbl_hint = new Label(m_caption_row, Label::Body_13, m_hint);
        m_lbl_hint->SetForegroundColour(wxColour("#FF6F00"));
        m_lbl_hint->SetBackgroundColour(m_caption_row->GetBackgroundColour());
        // The dialog client is 800 DIP wide (see the step size setup) with a 12 DIP content margin;
        // the indent, the icon and its gap take 3 + 16 + 3 DIP.
        m_lbl_hint->Wrap(FromDIP(800 - 2 * 12 - 3 - 16 - 3));
        wxBoxSizer* hint_sizer = new wxBoxSizer(wxHORIZONTAL);
        // Keep the icon centered on the first text line when the hint wraps.
        const int icon_offset = std::max(0, (m_lbl_hint->GetTextExtent("A").y - FromDIP(16)) / 2);
        wxBoxSizer* icon_sizer = new wxBoxSizer(wxVERTICAL);
        icon_sizer->AddSpacer(icon_offset);
        icon_sizer->Add(m_hint_icon, 0);
        hint_sizer->Add(icon_sizer, 0, wxALIGN_TOP | wxRIGHT, FromDIP(3));
        hint_sizer->Add(m_lbl_hint, 0, wxALIGN_TOP);
        wxBoxSizer* caption_column = new wxBoxSizer(wxVERTICAL);
        caption_column->Add(caption_sizer, 0, wxEXPAND);
        caption_column->AddSpacer(FromDIP(16));
        caption_column->Add(hint_sizer, 0, wxALIGN_LEFT | wxLEFT, FromDIP(3));
        m_caption_row->SetSizer(caption_column);
    }
    sizer->Add(m_caption_row, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
}

void TextureImportDialog::build_params_panel(wxWindow* parent, wxSizer* sizer)
{
    wxColour label_fg = texture_import_dialog_fg();

    m_params_panel = new wxPanel(parent, wxID_ANY);
    m_params_panel->SetBackgroundColour(parent->GetBackgroundColour());
    wxBoxSizer* panel_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* color_header_row = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* lbl_colors = new wxStaticText(m_params_panel, wxID_ANY, _L("Color Count"));
    lbl_colors->SetForegroundColour(label_fg);
    lbl_colors->SetFont(Label::Head_14);
    color_header_row->Add(lbl_colors, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    m_btn_color_4    = new Button(m_params_panel, "4");
    m_btn_color_4->SetId(ID_COLOR_4);
    m_btn_color_8    = new Button(m_params_panel, "8");
    m_btn_color_8->SetId(ID_COLOR_8);
    m_btn_color_16   = new Button(m_params_panel, "16");
    m_btn_color_16->SetId(ID_COLOR_16);
    m_btn_color_auto = new Button(m_params_panel, _L("Auto"));
    m_btn_color_auto->SetId(ID_COLOR_AUTO);

    auto size_color_preset = [this](Button* btn, bool is_auto) {
        btn->SetMinSize(wxSize(FromDIP(is_auto ? 52 : 46), FromDIP(21)));
        style_color_count_preset_button(btn);
    };
    size_color_preset(m_btn_color_auto, true);
    size_color_preset(m_btn_color_4, false);
    size_color_preset(m_btn_color_8, false);
    size_color_preset(m_btn_color_16, false);

    m_color_slider = new GreenSlider(m_params_panel, m_param_color_count, 1, (int)max_physical_filament_count());
    m_color_spin = new TextInput(m_params_panel, wxString::Format("%d", m_param_color_count),
                                 "", "", wxDefaultPosition,
                                 wxSize(FromDIP(64), FromDIP(28)), wxTE_PROCESS_ENTER);
    style_param_value_input(m_color_spin);
    if (wxTextCtrl* tc = m_color_spin->GetTextCtrl()) {
        tc->SetValidator(wxTextValidator(wxFILTER_DIGITS));
        bind_param_text_ctrl_center(tc);
        tc->Bind(wxEVT_TEXT, &TextureImportDialog::on_color_spin_text_changed, this);
        tc->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { on_color_spin_commit(true); });
        tc->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
            on_color_spin_commit(false);
            e.Skip();
        });
    }

    m_color_slider->Bind(wxEVT_SLIDER, &TextureImportDialog::on_color_slider_changed, this);

    color_header_row->Add(m_btn_color_4,    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    color_header_row->Add(m_btn_color_8,    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    color_header_row->Add(m_btn_color_16,   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    color_header_row->Add(m_btn_color_auto, 0, wxALIGN_CENTER_VERTICAL);
    panel_sizer->Add(color_header_row, 0, wxALIGN_LEFT | wxLEFT | wxRIGHT, FromDIP(16));

    wxBoxSizer* color_slider_row = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* color_slider_left = new wxBoxSizer(wxHORIZONTAL);
    color_slider_left->Add(m_color_slider, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    color_slider_left->Add(m_color_spin, 0, wxALIGN_CENTER_VERTICAL);
    color_slider_row->Add(color_slider_left, 1, wxEXPAND);
    color_slider_row->AddStretchSpacer(1);
    panel_sizer->AddSpacer(FromDIP(8));
    panel_sizer->Add(color_slider_row, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

    m_color_count_warning = new ColorCountWarningPanel(m_params_panel);
    wxBoxSizer* warning_row = new wxBoxSizer(wxHORIZONTAL);
    warning_row->Add(m_color_count_warning, 1, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxTOP, FromDIP(4));
    warning_row->AddStretchSpacer(1);
    panel_sizer->Add(warning_row, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

    auto* advanced_header = new AdvancedFoldHeader(m_params_panel, _L("Advanced settings"));
    m_advanced_header = advanced_header;
    advanced_header->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { toggle_advanced_design(); });
    panel_sizer->Add(advanced_header, 0, wxALIGN_LEFT | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    // Boundary smoothness and gap area both clean up texture sampling artifacts. An exact
    // source palette has none, and neither setting reaches its clustering path.
    if (m_textured_mesh.is_standard_3mf) {
        advanced_header->Show(false);
        panel_sizer->Show(advanced_header, false);
    }

    // wxTAB_TRAVERSAL is implicit for wxPanel but not for StaticBox, which derives
    // straight from wxWindow; without it MSW breaks the Tab chain between the two
    // spin inputs this card hosts.
    m_advanced_body = new StaticBox(m_params_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    style_advanced_settings_card();
    wxBoxSizer* advanced_sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* advanced_row = new wxBoxSizer(wxHORIZONTAL);

    auto make_advanced_col = [](wxWindow* parent) {
        auto* col_panel = new wxPanel(parent, wxID_ANY);
        col_panel->SetBackgroundColour(parent->GetBackgroundColour());
        col_panel->SetMinSize(wxSize(0, -1));
        return col_panel;
    };

    wxPanel* smooth_panel = make_advanced_col(m_advanced_body);
    auto* lbl_smooth = new Label(smooth_panel, Label::Head_14, _L("Boundary Smoothness"));
    lbl_smooth->SetForegroundColour(label_fg);
    // wxST_NO_AUTORESIZE: layout_advanced_hints() breaks the text against the column
    // width, which a self-resizing label would fight by snapping back to its best size.
    m_lbl_smooth_hint = new Label(smooth_panel, Label::Body_10, advanced_smooth_hint_text(),
                                  wxST_NO_AUTORESIZE);
    m_lbl_smooth_hint->SetForegroundColour(texture_import_hint_text_colour());
    m_lbl_smooth_hint->SetBackgroundColour(smooth_panel->GetBackgroundColour());
    init_advanced_hint(m_lbl_smooth_hint);

    wxBoxSizer* smooth_slider_row = new wxBoxSizer(wxHORIZONTAL);
    m_smooth_slider = new GreenSlider(smooth_panel, m_param_smooth, 0, 10);
    m_smooth_slider->SetMinSize(wxSize(FromDIP(80), FromDIP(24)));
    m_smooth_spin = new TextInput(smooth_panel, wxString::Format("%d", m_param_smooth),
                                  "", "", wxDefaultPosition,
                                  wxSize(FromDIP(64), FromDIP(28)), wxTE_PROCESS_ENTER);
    style_param_value_input(m_smooth_spin);
    if (wxTextCtrl* tc = m_smooth_spin->GetTextCtrl()) {
        tc->SetValidator(wxTextValidator(wxFILTER_DIGITS));
        bind_param_text_ctrl_center(tc);
        tc->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { on_smooth_spin_commit(); });
        tc->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
            on_smooth_spin_commit();
            e.Skip();
        });
    }
    m_smooth_slider->Bind(wxEVT_SLIDER, &TextureImportDialog::on_smooth_slider_changed, this);
    smooth_slider_row->Add(m_smooth_slider, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    smooth_slider_row->Add(m_smooth_spin, 0, wxALIGN_CENTER_VERTICAL);

    wxBoxSizer* smooth_col = new wxBoxSizer(wxVERTICAL);
    smooth_col->Add(lbl_smooth, 0, wxBOTTOM, FromDIP(4));
    smooth_col->Add(m_lbl_smooth_hint, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
    smooth_col->Add(smooth_slider_row, 0, wxEXPAND);
    smooth_panel->SetSizer(smooth_col);

    wxPanel* gap_panel = make_advanced_col(m_advanced_body);
    auto* lbl_gap = new Label(gap_panel, Label::Head_14, _L("Gap Area") + " (%)");
    lbl_gap->SetForegroundColour(label_fg);
    m_lbl_gap_hint = new Label(gap_panel, Label::Body_10, advanced_gap_hint_text(),
                               wxST_NO_AUTORESIZE);
    m_lbl_gap_hint->SetForegroundColour(texture_import_hint_text_colour());
    m_lbl_gap_hint->SetBackgroundColour(gap_panel->GetBackgroundColour());
    init_advanced_hint(m_lbl_gap_hint);

    wxBoxSizer* gap_slider_row = new wxBoxSizer(wxHORIZONTAL);
    m_gap_slider = new GreenDoubleSlider(gap_panel, m_param_gap_area, 0.0, kGapAreaMaxPercent);
    m_gap_slider->SetMinSize(wxSize(FromDIP(80), FromDIP(24)));
    m_gap_spin = new TextInput(gap_panel, wxString::Format("%.2f", m_param_gap_area),
                               "", "", wxDefaultPosition,
                               wxSize(FromDIP(64), FromDIP(28)), wxTE_PROCESS_ENTER);
    style_param_value_input(m_gap_spin);
    if (wxTextCtrl* tc = m_gap_spin->GetTextCtrl()) {
        tc->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
        bind_param_text_ctrl_center(tc);
        tc->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { on_gap_spin_commit(); });
        tc->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
            on_gap_spin_commit();
            e.Skip();
        });
    }
    m_gap_slider->Bind(wxEVT_SLIDER, &TextureImportDialog::on_gap_slider_changed, this);
    gap_slider_row->Add(m_gap_slider, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    gap_slider_row->Add(m_gap_spin, 0, wxALIGN_CENTER_VERTICAL);

    wxBoxSizer* gap_col = new wxBoxSizer(wxVERTICAL);
    gap_col->Add(lbl_gap, 0, wxBOTTOM, FromDIP(4));
    gap_col->Add(m_lbl_gap_hint, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
    gap_col->Add(gap_slider_row, 0, wxEXPAND);
    gap_panel->SetSizer(gap_col);

    advanced_row->Add(smooth_panel, 1, wxEXPAND | wxRIGHT, FromDIP(24));
    advanced_row->Add(gap_panel, 1, wxEXPAND);
    advanced_row->SetItemMinSize(smooth_panel, wxSize(0, -1));
    advanced_row->SetItemMinSize(gap_panel, wxSize(0, -1));
    advanced_sizer->AddSpacer(FromDIP(12));
    advanced_sizer->Add(advanced_row, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));
    advanced_sizer->AddSpacer(FromDIP(12));

    m_advanced_body->SetSizer(advanced_sizer);
    // Drive the hint wrapping off the card, whose width does not depend on the hints
    // (their min width is 1), so there is no size feedback loop to break out of.
    m_advanced_body->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        e.Skip();
        layout_advanced_hints();
    });
    panel_sizer->Add(m_advanced_body, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    panel_sizer->Show(m_advanced_body, false);

    m_params_panel->SetSizer(panel_sizer);

    Bind(wxEVT_SHOW, [this](wxShowEvent& e) {
        e.Skip();
        if (!e.IsShown() || m_initial_tooltips_set)
            return;
        m_initial_tooltips_set = true;
        CallAfter([this]() {
            if (m_btn_color_auto)
                m_btn_color_auto->SetToolTip(_L("Automatically determine the optimal color count"));
        });
    });

    sizer->Add(m_params_panel, 0, wxEXPAND | wxBOTTOM, FromDIP(12));
}

void TextureImportDialog::build_mapping_panel(wxWindow* parent, wxSizer* sizer)
{
    m_mapping_panel = new wxPanel(parent, wxID_ANY);
    m_mapping_panel->SetBackgroundColour(parent->GetBackgroundColour());
    wxBoxSizer* panel_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* header_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_match_threshold_gear = new MatchThresholdGear(m_mapping_panel);
    m_match_threshold_gear->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& e) {
        show_match_threshold_popup();
        e.Skip();
    });
    header_sizer->Add(m_match_threshold_gear, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    m_lbl_mapping = new wxStaticText(m_mapping_panel, wxID_ANY, _L("Filament Matching"));
    m_lbl_mapping->SetForegroundColour(dark_or(wxColour(50, 58, 61), wxColour(0xEF, 0xEF, 0xF0)));
    m_lbl_mapping->SetFont(Label::Head_14);
    header_sizer->Add(m_lbl_mapping, 0, wxALIGN_CENTER_VERTICAL);

    m_mix_cb = new CheckBox(m_mapping_panel);
    m_mix_cb->SetValue(m_mix_enabled);
    m_mix_cb->Bind(wxEVT_TOGGLEBUTTON, &TextureImportDialog::on_mix_toggled, this);
    header_sizer->Add(m_mix_cb, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    auto* mix_label = new Label(m_mapping_panel, _L("Color mixing"));
    mix_label->SetForegroundColour(dark_or(wxColour(50, 58, 61), wxColour(0xEF, 0xEF, 0xF0)));
    mix_label->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        if (!m_mix_cb || !m_mix_cb->IsEnabled())
            return;
        m_mix_cb->SetValue(!m_mix_cb->GetValue());
        wxCommandEvent evt(wxEVT_TOGGLEBUTTON, m_mix_cb->GetId());
        on_mix_toggled(evt);
    });
    header_sizer->Add(mix_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));

    header_sizer->AddStretchSpacer();

    m_lbl_mix_help = new wxStaticText(m_mapping_panel, wxID_ANY, _L("Use official mixing kits →"));
    m_lbl_mix_help->SetForegroundColour(wxColour(0, 174, 66));
    m_lbl_mix_help->SetFont(Label::Body_12);
    m_lbl_mix_help->SetCursor(wxCursor(wxCURSOR_HAND));
    m_lbl_mix_help->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        CallAfter([this]() { show_mixing_kits_help(); });
    });
    header_sizer->Add(m_lbl_mix_help, 0, wxALIGN_CENTER_VERTICAL);

    panel_sizer->Add(header_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    m_mapping_scroll = new wxScrolledWindow(m_mapping_panel, wxID_ANY, wxDefaultPosition,
                                             wxSize(-1, FromDIP(252)));
    m_mapping_scroll->SetScrollRate(0, FromDIP(10));
    m_mapping_scroll->SetBackgroundColour(dark_or(wxColour(255, 255, 255), wxColour(0x2D, 0x2D, 0x31)));
    m_mapping_scroll->Bind(wxEVT_MOUSEWHEEL, &TextureImportDialog::dismiss_filament_popup_on_wheel, this);

    m_mapping_sizer = new wxBoxSizer(wxVERTICAL);
    m_mapping_scroll->SetSizer(m_mapping_sizer);
    m_mapping_scroll->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        e.Skip();
        layout_mapping_rows();
    });

    panel_sizer->Add(m_mapping_scroll, 1, wxEXPAND);
    m_mapping_panel->SetSizer(panel_sizer);
    m_mapping_panel->Hide();
    sizer->Add(m_mapping_panel, 1, wxEXPAND | wxBOTTOM, FromDIP(8));
}

void TextureImportDialog::build_bottom_buttons(wxSizer* sizer)
{
    m_unmatched_warning = new wxPanel(this, wxID_ANY);
    m_unmatched_warning->SetBackgroundColour(GetBackgroundColour());
    auto* unmatched_sizer = new wxBoxSizer(wxVERTICAL);
    auto* unmatched_row = new wxBoxSizer(wxHORIZONTAL);
    m_unmatched_warning_icon = new wxStaticBitmap(m_unmatched_warning, wxID_ANY,
        m_bmp_unmatched.bmp());
    m_unmatched_warning_icon->SetBackgroundColour(GetBackgroundColour());
    m_unmatched_warning_label = new Label(m_unmatched_warning,
        texture_import_warning_body_font(this),
        _L("Some colors have no matching filament. Please match them before importing."));
    m_unmatched_warning_label->SetForegroundColour(wxColour(225, 71, 71));
    m_unmatched_add_link = new Label(m_unmatched_warning,
        texture_import_action_link_font(this), _L("Complete All Filaments"));
    m_unmatched_add_link->SetWindowStyleFlag(
        m_unmatched_add_link->GetWindowStyleFlag() | LB_HYPERLINK);
    m_unmatched_add_link->SetForegroundColour(wxColour(0, 174, 66));
    m_unmatched_add_link->SetFont(texture_import_action_link_font(this));
    m_unmatched_add_link->SetCursor(wxCursor(wxCURSOR_HAND));
    m_unmatched_add_link->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        if (!m_unmatched_add_link || !m_unmatched_add_link->IsEnabled())
            return;
        complete_unmatched_filaments();
    });
    unmatched_row->Add(m_unmatched_warning_icon, 0, wxALIGN_TOP | wxRIGHT, FromDIP(4));
    unmatched_row->Add(m_unmatched_warning_label, 0, wxALIGN_TOP);
    unmatched_row->Add(m_unmatched_add_link, 0, wxALIGN_TOP | wxLEFT, FromDIP(4));
    unmatched_sizer->Add(unmatched_row, 0, wxEXPAND);
    m_unmatched_warning->SetSizer(unmatched_sizer);
    m_unmatched_warning->Hide();
    sizer->Add(m_unmatched_warning, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

    wxBoxSizer* footer = new wxBoxSizer(wxHORIZONTAL);
    footer->AddStretchSpacer();

    m_footer_btn_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_btn_skip = new Button(this, _L("Skip"));
    m_btn_skip->SetId(ID_BTN_SKIP);
    m_btn_skip->SetToolTip(_L("Skip filament mapping and import as a single-color model"));
    m_btn_skip->SetCornerRadius(FromDIP(12));
    m_btn_skip->SetMinSize(wxSize(FromDIP(60), FromDIP(24)));
    style_secondary_button(m_btn_skip);

    m_btn_next = new Button(this, _L("Next"));
    m_btn_next->SetId(ID_BTN_NEXT);
    m_btn_next->SetCornerRadius(FromDIP(12));
    m_btn_next->SetMinSize(wxSize(FromDIP(60), FromDIP(24)));
    style_primary_button(m_btn_next);

    m_btn_prev = new Button(this, wxString::FromUTF8(_CTX_utf8(L_CONTEXT("Previous", "TextureImport"), "TextureImport")));
    m_btn_prev->SetId(ID_BTN_PREV);
    m_btn_prev->SetCornerRadius(FromDIP(12));
    m_btn_prev->SetMinSize(wxSize(FromDIP(72), FromDIP(24)));
    style_secondary_button(m_btn_prev);

    m_btn_reset = new Button(this, _L("Reset"));
    m_btn_reset->SetId(ID_BTN_RESET);
    m_btn_reset->SetToolTip(_L("Discard newly added filaments and re-run automatic matching"));
    m_btn_reset->SetCornerRadius(FromDIP(12));
    m_btn_reset->SetMinSize(wxSize(FromDIP(72), FromDIP(24)));
    style_secondary_button(m_btn_reset);

    m_btn_ok = new Button(this, _L("Confirm"));
    m_btn_ok->SetId(wxID_OK);
    m_btn_ok->SetCornerRadius(FromDIP(12));
    m_btn_ok->SetMinSize(wxSize(FromDIP(60), FromDIP(24)));
    style_primary_button(m_btn_ok);

    m_footer_btn_sizer->Add(m_btn_skip, 0, wxRIGHT, FromDIP(16));
    m_footer_btn_sizer->Add(m_btn_next, 0);
    m_footer_btn_sizer->Add(m_btn_prev, 0, wxRIGHT, FromDIP(16));
    m_footer_btn_sizer->Add(m_btn_ok, 0, wxRIGHT, FromDIP(16));
    m_footer_btn_sizer->Add(m_btn_reset, 0);
    m_btn_prev->Hide();
    m_btn_reset->Hide();
    m_btn_ok->Hide();
    footer->Add(m_footer_btn_sizer, 0, wxALIGN_CENTER_VERTICAL);

    sizer->Add(footer, 0, wxEXPAND | wxTOP, FromDIP(8));
}

void TextureImportDialog::style_primary_button(Button* btn)
{
    texture_import_style_primary_button(btn);
}

void TextureImportDialog::style_secondary_button(Button* btn)
{
    texture_import_style_secondary_button(btn);
}

void TextureImportDialog::style_color_count_preset_button(Button* btn)
{
    apply_color_count_preset_style(btn);
}

void TextureImportDialog::style_advanced_settings_card()
{
    if (!m_advanced_body)
        return;
    m_advanced_body->SetCornerRadius(FromDIP(4));
    m_advanced_body->SetBorderWidth(1);
    const wxColour bd = texture_import_separator_colour();
    m_advanced_body->SetBorderColor(StateColor(
        std::pair<wxColour, int>(bd, StateColor::Normal)));
    const wxColour bg = texture_import_dialog_bg();
    m_advanced_body->SetBackgroundColor(StateColor(
        std::pair<wxColour, int>(bg, StateColor::Normal)));
    m_advanced_body->SetBackgroundColour(bg);
}

void TextureImportDialog::layout_advanced_hints()
{
    if (!m_advanced_body || m_laying_out_hints)
        return;
    m_laying_out_hints = true;
    Slic3r::ScopeGuard hints_guard([this]() { m_laying_out_hints = false; });

    // Lay the card out first: this runs ahead of the default size handler, and the
    // columns must already carry their new width before we break the text against it.
    m_advanced_body->Layout();

    const std::pair<Label*, wxString> hints[] = {
        {m_lbl_smooth_hint, advanced_smooth_hint_text()},
        {m_lbl_gap_hint,    advanced_gap_hint_text()},
    };
    bool changed = false;
    for (const auto& [hint, text] : hints) {
        if (!hint || !hint->GetHandle())
            continue;
        wxWindow* column = hint->GetParent();
        const int width = column ? column->GetClientSize().x : hint->GetSize().x;
        if (width <= 1)
            continue;
        fit_hint_two_lines(hint, text, width);
        const int height = two_line_hint_height(hint);
        if (hint->GetMinSize().y != height) {
            hint->SetMinSize(wxSize(1, height));
            hint->SetMaxSize(wxSize(-1, height));
            changed = true;
        }
    }
    if (changed)
        m_advanced_body->Layout();
}

void TextureImportDialog::apply_theme()
{
    wxGetApp().init_label_colours();
    dismiss_filament_popup();
    // Map generic wx colours first; custom preview / warning / button colours
    // below overwrite entries that are missing from StateColor's tables.
    wxGetApp().UpdateDlgDarkUI(this);

    const wxColour dialog_bg = texture_import_dialog_bg();
    const wxColour dialog_fg = texture_import_dialog_fg();
    const wxColour preview_bg = texture_import_preview_bg();
    const wxColour tag_bg = texture_import_tag_bg();
    const wxColour tag_fg = texture_import_tag_fg();
    const wxColour caption_fg = texture_import_caption_fg();

    SetBackgroundColour(dialog_bg);
    SetForegroundColour(dialog_fg);
    if (m_title_line)
        m_title_line->SetBackgroundColour(texture_import_title_line_colour());

    if (m_preview_container)
        m_preview_container->SetBackgroundColour(dialog_bg);
    for_each_preview([&](TexturePreviewCanvas* canvas) {
        if (!canvas)
            return;
        if (wxWindow* host = canvas->GetParent()) {
            host->SetBackgroundColour(preview_bg);
            if (wxWindow* half = host->GetParent())
                half->SetBackgroundColour(preview_bg);
        }
        canvas->Refresh();
    });
    if (auto* tag = dynamic_cast<PreviewTagPanel*>(m_lbl_preview_left_panel))
        tag->set_theme(tag_bg, tag_fg, preview_bg);
    if (auto* tag = dynamic_cast<PreviewTagPanel*>(m_lbl_preview_right_panel))
        tag->set_theme(tag_bg, tag_fg, preview_bg);
    if (auto* overlay = dynamic_cast<UpdatingOverlayPanel*>(m_updating_overlay))
        overlay->set_theme(preview_bg, tag_fg);
    update_preview_rounded_corners();

    if (m_caption_row)
        m_caption_row->SetBackgroundColour(dialog_bg);
    if (m_lbl_caption_left) {
        m_lbl_caption_left->SetForegroundColour(caption_fg);
        m_lbl_caption_left->SetBackgroundColour(dialog_bg);
    }
    if (m_lbl_caption_right) {
        m_lbl_caption_right->SetForegroundColour(caption_fg);
        m_lbl_caption_right->SetBackgroundColour(dialog_bg);
    }
    if (m_hint_icon)
        m_hint_icon->SetBackgroundColour(dialog_bg);
    if (m_lbl_hint)
        m_lbl_hint->SetBackgroundColour(dialog_bg);

    if (m_params_panel)
        m_params_panel->SetBackgroundColour(dialog_bg);
    if (m_advanced_header)
        m_advanced_header->Refresh();
    style_advanced_settings_card();
    if (m_stepper_panel)
        m_stepper_panel->Refresh();

    std::function<void(wxWindow*)> recolor_params;
    recolor_params = [&](wxWindow* w) {
        if (!w)
            return;
        if (auto* warning = dynamic_cast<ColorCountWarningPanel*>(w)) {
            warning->apply_colors();
            return;
        }
        if (dynamic_cast<Button*>(w) || dynamic_cast<TextInput*>(w) ||
            dynamic_cast<GreenSlider*>(w) || dynamic_cast<GreenDoubleSlider*>(w) ||
            dynamic_cast<AdvancedFoldHeader*>(w))
            return;
        if (w == m_advanced_body) {
            style_advanced_settings_card();
            for (wxWindow* child : w->GetChildren())
                recolor_params(child);
            return;
        }
        if (w == m_lbl_smooth_hint || w == m_lbl_gap_hint) {
            w->SetBackgroundColour(dialog_bg);
            return;
        }
        if (auto* st = dynamic_cast<wxStaticText*>(w)) {
            st->SetForegroundColour(dialog_fg);
            st->SetBackgroundColour(dialog_bg);
        } else {
            w->SetBackgroundColour(dialog_bg);
        }
        for (wxWindow* child : w->GetChildren())
            recolor_params(child);
    };
    if (m_params_panel)
        recolor_params(m_params_panel);
    for (Label* hint : {m_lbl_smooth_hint, m_lbl_gap_hint}) {
        if (!hint)
            continue;
        hint->SetForegroundColour(texture_import_hint_text_colour());
        hint->SetBackgroundColour(dialog_bg);
    }

    if (m_mapping_panel)
        m_mapping_panel->SetBackgroundColour(dialog_bg);
    if (m_lbl_mapping) {
        m_lbl_mapping->SetForegroundColour(dialog_fg);
        m_lbl_mapping->SetBackgroundColour(dialog_bg);
    }
    if (m_mapping_scroll)
        m_mapping_scroll->SetBackgroundColour(dialog_bg);
    if (m_mix_cb) {
        m_mix_cb->SetBackgroundColour(dialog_bg);
        m_mix_cb->Rescale();
    }

    style_param_value_input(m_color_spin);
    style_param_value_input(m_smooth_spin);
    style_param_value_input(m_gap_spin);
    update_gap_param_lock();
    set_color_spin_error_border(m_color_spin, m_color_count_exceeded
        && m_wizard_step == TextureImportWizardStep::SimplifyColors);
    style_primary_button(m_btn_next);
    style_primary_button(m_btn_ok);
    style_secondary_button(m_btn_skip);
    style_secondary_button(m_btn_prev);
    style_secondary_button(m_btn_reset);
    for (Button* btn : {m_btn_color_4, m_btn_color_8, m_btn_color_16, m_btn_color_auto})
        style_color_count_preset_button(btn);
    if (m_color_slider)
        m_color_slider->Refresh();
    if (m_smooth_slider)
        m_smooth_slider->Refresh();
    if (m_gap_slider)
        m_gap_slider->Refresh();

    if (m_unmatched_warning)
        m_unmatched_warning->SetBackgroundColour(dialog_bg);
    if (m_unmatched_warning_icon)
        m_unmatched_warning_icon->SetBackgroundColour(dialog_bg);
    if (m_unmatched_warning_label) {
        m_unmatched_warning_label->SetForegroundColour(wxColour(225, 71, 71));
        m_unmatched_warning_label->SetBackgroundColour(dialog_bg);
    }
    if (m_unmatched_add_link) {
        m_unmatched_add_link->SetForegroundColour(wxColour(0, 174, 66));
        m_unmatched_add_link->SetBackgroundColour(dialog_bg);
    }

    m_bmp_unmatched.msw_rescale();
    m_bmp_brand.msw_rescale();
    if (m_match_threshold_gear)
        m_match_threshold_gear->msw_rescale();
    hide_match_threshold_popup();
    if (m_unmatched_warning_icon)
        m_unmatched_warning_icon->SetBitmap(m_bmp_unmatched.bmp());
    if (auto* warning = dynamic_cast<ColorCountWarningPanel*>(m_color_count_warning))
        warning->Rescale();

    int mapping_scroll_x = 0;
    int mapping_scroll_y = 0;
    if (m_mapping_scroll)
        m_mapping_scroll->GetViewStart(&mapping_scroll_x, &mapping_scroll_y);
    rebuild_mapping_rows();
    if (m_mapping_scroll)
        m_mapping_scroll->Scroll(mapping_scroll_x, mapping_scroll_y);
    if (wxSizer* sizer = GetSizer())
        sizer->Layout();
    Layout();
    recenter_preview_tags();
    Refresh();
    Update();
}

void TextureImportDialog::on_sys_color_changed()
{
    apply_theme();
}

// ---- State machine ----

void TextureImportDialog::set_state(TextureImportState new_state)
{
    m_state = new_state;
    update_ui_for_state();
}

void TextureImportDialog::for_each_preview(const std::function<void(TexturePreviewCanvas*)>& fn)
{
    if (m_preview_canvas)
        fn(m_preview_canvas);
    if (m_preview_canvas_right)
        fn(m_preview_canvas_right);
}

void TextureImportDialog::set_wizard_step(TextureImportWizardStep step)
{
    if (m_wizard_step == step)
        return;
    hide_match_threshold_popup();
    const bool returning_to_step1 = (step == TextureImportWizardStep::SimplifyColors);
    m_wizard_step = step;
    auto set_spin_can_focus = [](TextInput* input, bool can) {
        if (!input)
            return;
        input->SetCanFocus(can);
        if (wxTextCtrl* tc = input->GetTextCtrl())
            tc->SetCanFocus(can);
    };
    if (returning_to_step1) {
        set_spin_can_focus(m_color_spin, false);
        set_spin_can_focus(m_smooth_spin, false);
        set_spin_can_focus(m_gap_spin, false);
    }
    update_wizard_ui();
    if (returning_to_step1) {
        clear_param_spin_selection();
        CallAfter([this, set_spin_can_focus] {
            if (IsBeingDeleted())
                return;
            clear_param_spin_selection();
            set_spin_can_focus(m_color_spin, true);
            set_spin_can_focus(m_smooth_spin, true);
            set_spin_can_focus(m_gap_spin, true);
        });
    }
}

void TextureImportDialog::update_stepper()
{
    if (auto* stepper = dynamic_cast<TextureImportStepper*>(m_stepper_panel))
        stepper->set_active_step(m_wizard_step == TextureImportWizardStep::SimplifyColors ? 0 : 1);
}

void TextureImportDialog::update_preview_modes()
{
    const bool step1 = (m_wizard_step == TextureImportWizardStep::SimplifyColors);
    if (m_preview_canvas) {
        m_preview_canvas->set_render_mode(step1 ? TexturePreviewCanvas::RenderMode::Original :
                                                  TexturePreviewCanvas::RenderMode::MultiColor);
    }
    if (m_preview_canvas_right) {
        m_preview_canvas_right->set_render_mode(step1 ? TexturePreviewCanvas::RenderMode::MultiColor :
                                                       TexturePreviewCanvas::RenderMode::FilamentMap);
    }
    update_color_captions();
}

void TextureImportDialog::update_color_captions()
{
    const bool step1 = (m_wizard_step == TextureImportWizardStep::SimplifyColors);
    if (auto* left = dynamic_cast<PreviewTagPanel*>(m_lbl_preview_left_panel))
        left->set_label(step1 ? _L("Original") : _L("Before matching"));
    if (auto* right = dynamic_cast<PreviewTagPanel*>(m_lbl_preview_right_panel))
        right->set_label(step1 ? _L("Simplified") : _L("After matching"));

    if (m_lbl_caption_left) {
        if (step1 && m_original_color_count > 0)
            m_lbl_caption_left->SetLabel(wxString::Format(_L("Before simplification: %d colors"), m_original_color_count));
        else
            m_lbl_caption_left->SetLabel(wxEmptyString);
        m_lbl_caption_left->Show(step1 && m_original_color_count > 0);
    }
    if (m_lbl_caption_right) {
        const int simplified = visible_simplified_color_count();
        if (step1 && simplified > 0)
            m_lbl_caption_right->SetLabel(wxString::Format(_L("After simplification: %d colors"), simplified));
        else
            m_lbl_caption_right->SetLabel(wxEmptyString);
        m_lbl_caption_right->Show(step1 && simplified > 0);
    }
    if (m_caption_row) {
        m_caption_row->Show(step1);
        m_caption_row->Layout();
    }
    recenter_preview_tags();
}

void TextureImportDialog::recenter_preview_tags()
{
    auto recenter = [](wxPanel* tag) {
        if (!tag || !tag->GetParent())
            return;
        wxWindow* host = tag->GetParent();
        tag->InvalidateBestSize();
        tag->Fit();
        const wxSize host_sz = host->GetClientSize();
        const wxSize tag_sz = tag->GetBestSize();
        tag->SetSize(tag_sz);
        tag->SetPosition(wxPoint(std::max(0, (host_sz.x - tag_sz.x) / 2), 0));
        tag->Raise();
    };
    recenter(m_lbl_preview_left_panel);
    recenter(m_lbl_preview_right_panel);
    if (m_updating_overlay && m_updating_overlay->GetParent()) {
        wxWindow* host = m_updating_overlay->GetParent();
        m_updating_overlay->InvalidateBestSize();
        m_updating_overlay->Fit();
        const wxSize host_sz = host->GetClientSize();
        const wxSize label_sz = m_updating_overlay->GetBestSize();
        m_updating_overlay->SetSize(label_sz);
        m_updating_overlay->SetPosition(wxPoint(
            std::max(0, (host_sz.x - label_sz.x) / 2),
            std::max(0, (host_sz.y - label_sz.y) / 2)));
        if (m_updating_overlay->IsShown())
            m_updating_overlay->Raise();
    }
    update_preview_rounded_corners();
}

void TextureImportDialog::clear_param_spin_selection()
{
    if (m_preview_canvas && m_preview_canvas->IsShown())
        m_preview_canvas->SetFocus();
    else if (m_stepper_panel)
        m_stepper_panel->SetFocus();
    else
        SetFocus();

    auto clear_sel = [](TextInput* input) {
        if (!input || !input->GetTextCtrl())
            return;
        wxTextCtrl* tc = input->GetTextCtrl();
        const long pos = tc->GetLastPosition();
        tc->SetSelection(pos, pos);
    };
    clear_sel(m_color_spin);
    clear_sel(m_smooth_spin);
    clear_sel(m_gap_spin);
}

void TextureImportDialog::update_preview_rounded_corners()
{
    if (!m_preview_container)
        return;
    const int radius = m_preview_container->FromDIP(8);
    const int inset  = 1; // each preview card keeps a 1px margin around the canvas
    const wxColour outside = m_preview_container->GetParent()
        ? m_preview_container->GetParent()->GetBackgroundColour()
        : GetBackgroundColour();
    const wxColour border  = dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B));
    if (m_preview_canvas)
        m_preview_canvas->set_rounded_corners(
            TexturePreviewCanvas::RoundedCornerSide::All, radius, inset, outside, border);
    if (m_preview_canvas_right)
        m_preview_canvas_right->set_rounded_corners(
            TexturePreviewCanvas::RoundedCornerSide::All, radius, inset, outside, border);
}

void TextureImportDialog::update_dialog_min_size()
{
    apply_dialog_geometry(false);
}

void TextureImportDialog::apply_dialog_geometry(bool allow_center)
{
    const bool step1 = (m_wizard_step == TextureImportWizardStep::SimplifyColors);
    const int step1_min_h = m_advanced_expanded ? 620 : 560;
    const int step1_target_h = m_advanced_expanded ? 740 : 670;
    const wxSize min_client(FromDIP(800), FromDIP(step1 ? step1_min_h : 760));
    const wxSize target_client(FromDIP(800), FromDIP(step1 ? step1_target_h : 760));
    SetMinClientSize(min_client);

    // Before the native window exists (macOS pre-SHOW), SetClientSize is a no-op.
    if (!allow_center && !IsShown())
        return;

    const wxSize cur = GetClientSize();
    const wxSize next(std::max(cur.x, target_client.x),
                      std::max(cur.y, target_client.y));
    const bool grew = (next != cur);
    if (grew)
        SetClientSize(next);
    Layout();

    // Center exactly once. On macOS on_window_geometry keeps a permanent
    // wxEVT_SHOW binding, and update_wizard_ui / update_dialog_min_size run on
    // every step switch, so centering here unconditionally would drag the
    // window back to the middle of the screen each time.
    bool did_center = false;
    if (allow_center && !m_geometry_centered) {
        m_geometry_centered = true;
        CenterOnParent();
        did_center = true;
    }
    if (grew || did_center)
        keep_dialog_within_display();
}

void TextureImportDialog::keep_dialog_within_display()
{
    const wxRect frame = GetRect();
    // GetFromWindow() returns wxNOT_FOUND for windows that are not shown yet.
    // Windows calls this during construction (on_window_geometry), after
    // CenterOnParent() has already placed us on the parent's display. Falling
    // back to wxDisplay() (primary) would drag a secondary-monitor dialog back
    // to the main screen. Use the just-written origin, then the parent.
    int display_idx = wxDisplay::GetFromPoint(frame.GetPosition());
    if (display_idx == wxNOT_FOUND)
        display_idx = wxDisplay::GetFromWindow(this);
    if (display_idx == wxNOT_FOUND && GetParent())
        display_idx = wxDisplay::GetFromWindow(GetParent());
    if (display_idx == wxNOT_FOUND)
        return;

    const wxRect area = wxDisplay(display_idx).GetClientArea();
    wxPoint pos = frame.GetPosition();
    pos.x = std::clamp(pos.x, area.GetLeft(),
                       std::max(area.GetLeft(), area.GetRight() - frame.width + 1));
    pos.y = std::clamp(pos.y, area.GetTop(),
                       std::max(area.GetTop(), area.GetBottom() - frame.height + 1));
    if (pos != frame.GetPosition())
        Move(pos);
}

void TextureImportDialog::update_wizard_ui()
{
    const bool step1 = (m_wizard_step == TextureImportWizardStep::SimplifyColors);
    SetTitle(step1 ? _L("Import Model: Simplify Colors") : _L("Import Model: Filament Matching"));
    update_stepper();
    update_preview_modes();

    if (m_params_panel)
        m_params_panel->Show(step1);
    if (m_mapping_panel)
        m_mapping_panel->Show(!step1);
    if (m_preview_container)
        m_preview_container->SetMinSize(wxSize(-1, FromDIP(351)));
    if (m_mapping_scroll)
        m_mapping_scroll->SetMinSize(wxSize(-1, step1 ? -1 : FromDIP(252)));
    if (m_caption_row)
        m_caption_row->Show(step1);
    update_unmatched_warning_visibility();
    update_color_count_warning();

    if (m_btn_skip) m_btn_skip->Show(step1);
    if (m_btn_next) m_btn_next->Show(step1);
    if (m_btn_prev) m_btn_prev->Show(!step1);
    if (m_btn_reset) m_btn_reset->Show(!step1);
    if (m_btn_ok) m_btn_ok->Show(!step1);

    update_ui_for_state();
    apply_dialog_geometry(false);
    Layout();
    layout_mapping_rows();
    recenter_preview_tags();
    Refresh();
}

void TextureImportDialog::invalidate_inflight_computation()
{
    m_cancel_flag = true;
    m_compute_generation.fetch_add(1);
    m_compute_superseded = true;
}

void TextureImportDialog::show_last_result_after_cancel()
{
    const bool in_dialog_compute = m_state == TextureImportState::Computing && !m_current_computation_initial;
    if (!in_dialog_compute)
        return;
    invalidate_inflight_computation();
    m_compute_superseded = false;
    set_state(m_painted.face_colors.empty() ? TextureImportState::Idle : TextureImportState::Ready);
}

bool TextureImportDialog::params_match_last_result() const
{
    if (m_applied_color_count < 1 || m_applied_smooth < 0)
        return false;
    if (m_painted.face_colors.empty())
        return false;
    return m_param_color_count == m_applied_color_count && m_param_smooth == m_applied_smooth;
}

void TextureImportDialog::update_gap_param_lock()
{
    const bool computing = (m_state == TextureImportState::Computing);
    const bool gap_enabled = !computing && !m_patch_building;
    if (m_gap_slider) {
        if (!m_gap_slider->IsEnabled())
            m_gap_slider->Enable(true);
        m_gap_slider->SetInteractive(gap_enabled);
    }
    if (m_gap_spin) {
        if (!m_gap_spin->IsEnabled())
            m_gap_spin->Enable(true);
        set_gap_spin_locked(m_gap_spin, !gap_enabled);
    }
    const wxString tip = computing ? gap_locked_tooltip_text() : wxString();
    set_window_tooltip(m_gap_slider, tip);
    if (m_gap_spin) {
        set_window_tooltip(m_gap_spin, tip);
        set_window_tooltip(m_gap_spin->GetTextCtrl(), tip);
    }
}

void TextureImportDialog::schedule_recompute(bool auto_color, int delay_ms)
{
    if (m_updating_params)
        return;
    if (m_color_count_exceeded)
        return;
    m_advance_to_matching_when_ready = false;
    m_pending_auto_color = auto_color;

    const bool in_dialog_compute = m_state == TextureImportState::Computing && !m_current_computation_initial;
    if (!auto_color && params_match_last_result()) {
        if (m_recompute_timer && m_recompute_timer->IsRunning())
            m_recompute_timer->Stop();
        if (in_dialog_compute)
            show_last_result_after_cancel();
        return;
    }

    if (in_dialog_compute)
        invalidate_inflight_computation();

    if (!m_recompute_timer) {
        start_computation(auto_color);
        return;
    }
    if (delay_ms <= 0) {
        m_recompute_timer->Stop();
        start_computation(auto_color);
        return;
    }
    m_recompute_timer->Start(delay_ms, wxTIMER_ONE_SHOT);
}

void TextureImportDialog::on_recompute_timer(wxTimerEvent&)
{
    if (m_color_count_exceeded)
        return;
    if (!m_pending_auto_color && params_match_last_result()) {
        show_last_result_after_cancel();
        return;
    }
    start_computation(m_pending_auto_color);
}

void TextureImportDialog::update_ui_for_state()
{
    bool computing = (m_state == TextureImportState::Computing);
    bool ready     = (m_state == TextureImportState::Ready);
    bool idle      = (m_state == TextureImportState::Idle);
    const bool step1 = (m_wizard_step == TextureImportWizardStep::SimplifyColors);

    if (m_color_slider) m_color_slider->Enable(true);
    if (m_color_spin) m_color_spin->Enable(true);
    if (m_smooth_slider) m_smooth_slider->Enable(true);
    if (m_smooth_spin) m_smooth_spin->Enable(true);
    if (m_btn_color_4) m_btn_color_4->Enable(true);
    if (m_btn_color_8) m_btn_color_8->Enable(true);
    if (m_btn_color_16) m_btn_color_16->Enable(true);
    if (m_btn_color_auto) m_btn_color_auto->Enable(true);
    update_gap_param_lock();
    if (m_mix_cb)
        m_mix_cb->Enable(!computing);
    if (m_lbl_mix_help)
        m_lbl_mix_help->Enable(!computing);

    if (m_btn_ok)
        m_btn_ok->Enable(can_confirm());
    if (m_unmatched_add_link)
        m_unmatched_add_link->Enable(!computing);
    update_unmatched_warning_visibility();
    if (m_btn_next)
        m_btn_next->Enable(step1 && ready && !m_painted.face_colors.empty() && !m_color_count_exceeded && !m_patch_building);
    if (m_btn_skip)
        m_btn_skip->Enable(step1 && (ready || idle || computing));
    if (m_btn_prev)
        m_btn_prev->Enable(!step1);
    if (m_btn_reset)
        m_btn_reset->Enable(!step1 && !computing);

    const bool in_dialog_recompute = computing && !m_current_computation_initial;
    if (m_preview_canvas)
        m_preview_canvas->set_computing_overlay(false);
    if (m_preview_canvas_right)
        m_preview_canvas_right->set_computing_overlay(in_dialog_recompute);
    if (auto* overlay = dynamic_cast<UpdatingOverlayPanel*>(m_updating_overlay)) {
        if (in_dialog_recompute)
            overlay->Play();
        else
            overlay->Stop();
    }
    if (in_dialog_recompute)
        recenter_preview_tags();

    if (m_btn_ok && can_confirm())
        style_primary_button(m_btn_ok);
    Layout();
    recenter_preview_tags();
}

// ---- Async computation ----

void TextureImportDialog::start_computation(bool auto_color, bool initial)
{
    cancel_computation();
    m_compute_superseded = false;

    if (!initial)
        capture_compute_snapshot();

    const int gen = m_compute_generation.fetch_add(1) + 1;
    m_patch_generation.fetch_add(1);
    m_patch_building = false;
    m_cancel_flag = false;
    m_current_computation_initial = initial;
    m_current_computation_auto_color = auto_color;
    if (initial) {
        m_initial_computation_pending = true;
        m_initial_computation_cancelled = false;
        m_initial_computation_failed = false;
    }
    set_state(TextureImportState::Computing);

    const bool silent_initial = initial && static_cast<bool>(m_initial_cancel_callback);
    if (initial && !silent_initial) {
        wxWindow* progress_parent = GetParent();
        m_progress_dlg = new ProgressDialog(
            _L("Processing"), _L("Computing texture colors..."),
            100, progress_parent, wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_AUTO_HIDE);
    }

    Slic3r::TexturePaintingSettings settings;
    settings.target_colors_num = auto_color ? 0 : (size_t)m_param_color_count;
    settings.smooth_weight     = m_param_smooth / 10.0;
    // Repair automatically where the Windows 3D repair service exists; elsewhere import the mesh as-is.
    if (Slic3r::is_win10_model_repair_available()) {
        settings.mesh_repair_decision = Slic3r::TexturePaintingSettings::MeshRepairDecision::RepairAndImport;
        settings.mesh_repair_callback = Slic3r::fix_mesh_by_win10_sdk;
    } else {
        settings.mesh_repair_decision = Slic3r::TexturePaintingSettings::MeshRepairDecision::ImportWithoutRepair;
    }
    settings.mesh_repair_timeout = std::chrono::seconds{90};
    settings.mesh_repair_cache = m_mesh_repair_cache.get();

    const bool cache_hit = Slic3r::mesh_repair_cache_can_skip_input(
        m_mesh_repair_cache, m_textured_mesh.indices.size(), settings);

    Slic3r::TexturedMesh mesh_copy;
    if (!cache_hit)
        mesh_copy = m_textured_mesh;
    wxEvtHandler* handler = this;

    m_worker = create_thread([this, settings, mesh_copy = std::move(mesh_copy), handler, gen, cache_hit]() {
        Slic3r::PaintedMesh result;

        auto progress_cb = [handler, gen](int percent, const char* message) {
            auto* evt = new wxCommandEvent(EVT_TEXTURE_COMPUTE_PROGRESS);
            evt->SetInt(gen);
            evt->SetExtraLong(percent);
            if (message && message[0] != '\0')
                evt->SetString(wxString::FromUTF8(message));
            wxQueueEvent(handler, evt);
        };

        auto cancel_cb = [this]() -> bool {
            return m_cancel_flag.load();
        };

        auto worker_settings = settings;
        bool ok;
        if (cache_hit) {
            ok = Slic3r::cluster_from_repair_cache(result, worker_settings, progress_cb, cancel_cb);
        } else if (mesh_copy.is_standard_3mf) {
            ok = Slic3r::standard_3mf_colors_to_painting(
                mesh_copy, result, worker_settings, progress_cb, cancel_cb);
        } else if (!mesh_copy.precomputed_face_colors.empty()) {
            ok = Slic3r::face_colors_to_painting(
                mesh_copy, result, worker_settings, progress_cb, cancel_cb);
        } else {
            ok = Slic3r::texture_to_painting(mesh_copy, result, worker_settings, progress_cb, cancel_cb);
        }

        if (m_cancel_flag.load()) {
            auto* evt = new wxCommandEvent(EVT_TEXTURE_COMPUTE_ERROR);
            evt->SetInt(gen);
            wxQueueEvent(handler, evt);
            return;
        }

        std::unique_ptr<GapPreviewState> pending_preview;
        if (ok && !m_cancel_flag.load())
            pending_preview = make_gap_preview_state(result);

        if (m_cancel_flag.load()) {
            auto* evt = new wxCommandEvent(EVT_TEXTURE_COMPUTE_ERROR);
            evt->SetInt(gen);
            wxQueueEvent(handler, evt);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_result_mutex);
            m_pending_result = std::move(result);
            m_pending_gap_preview = std::move(pending_preview);
        }

        if (ok) {
            auto* evt = new wxCommandEvent(EVT_TEXTURE_COMPUTE_DONE);
            evt->SetInt(gen);
            wxQueueEvent(handler, evt);
        } else {
            auto* evt = new wxCommandEvent(EVT_TEXTURE_COMPUTE_ERROR);
            evt->SetInt(gen);
            wxQueueEvent(handler, evt);
        }
    });
}

void TextureImportDialog::cancel_computation()
{
    m_cancel_flag = true;
    m_patch_generation.fetch_add(1);
    m_patch_building = false;
    if (m_worker.joinable())
        m_worker.join();
    m_worker = boost::thread();

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

void TextureImportDialog::capture_compute_snapshot()
{
    m_compute_snapshot.valid = true;
    m_compute_snapshot.painted = m_painted;
    m_compute_snapshot.matches = m_current_matches;
    m_compute_snapshot.filament_color_strs = m_filament_color_strs;
    m_compute_snapshot.filament_names = m_filament_names;
    m_compute_snapshot.filament_colors_rgba = m_filament_colors_rgba;
    m_compute_snapshot.filament_entries = m_filament_entries;
    m_compute_snapshot.new_filament_colors = m_new_filament_colors;
    m_compute_snapshot.new_filament_preset_names = m_new_filament_preset_names;
    m_compute_snapshot.new_mixed_filaments = m_new_mixed_filaments;
    m_compute_snapshot.param_color_count = m_applied_color_count >= 0 ? m_applied_color_count : m_param_color_count;
    m_compute_snapshot.param_smooth = m_applied_smooth >= 0 ? m_applied_smooth : m_param_smooth;
    m_compute_snapshot.applied_color_count = m_applied_color_count;
    m_compute_snapshot.applied_smooth = m_applied_smooth;
    m_compute_snapshot.auto_preset_selected = m_applied_auto_preset;
    m_compute_snapshot.state = m_painted.face_colors.empty() ? TextureImportState::Idle : TextureImportState::Ready;
    m_compute_snapshot.param_gap_area = m_param_gap_area;
    if (m_gap_preview)
        m_compute_snapshot.gap_preview = std::make_unique<GapPreviewState>(*m_gap_preview);
    else
        m_compute_snapshot.gap_preview.reset();
}

void TextureImportDialog::restore_compute_snapshot()
{
    if (!m_compute_snapshot.valid) {
        m_advance_to_matching_when_ready = false;
        set_state(m_painted.face_colors.empty() ? TextureImportState::Idle : TextureImportState::Ready);
        return;
    }

    m_advance_to_matching_when_ready = false;
    m_painted = m_compute_snapshot.painted;
    m_current_matches = m_compute_snapshot.matches;
    m_filament_color_strs = m_compute_snapshot.filament_color_strs;
    m_filament_names = m_compute_snapshot.filament_names;
    m_filament_colors_rgba = m_compute_snapshot.filament_colors_rgba;
    m_filament_entries = m_compute_snapshot.filament_entries;
    m_new_filament_colors = m_compute_snapshot.new_filament_colors;
    m_new_filament_preset_names = m_compute_snapshot.new_filament_preset_names;
    m_new_mixed_filaments = m_compute_snapshot.new_mixed_filaments;
    m_applied_color_count = m_compute_snapshot.applied_color_count;
    m_applied_smooth = m_compute_snapshot.applied_smooth;
    m_applied_auto_preset = m_compute_snapshot.auto_preset_selected;
    m_auto_preset_selected = m_compute_snapshot.auto_preset_selected;

    m_updating_params = true;
    set_color_count_value(m_compute_snapshot.param_color_count, true);
    set_smooth_value(m_compute_snapshot.param_smooth, true);
    set_gap_value(m_compute_snapshot.param_gap_area, true);
    m_updating_params = false;

    for_each_preview([&](TexturePreviewCanvas* canvas) {
        canvas->set_painted_mesh_data(m_painted.vertices, m_painted.indices);
        canvas->set_face_colors(m_painted.face_colors);
    });
    update_filament_color_map();
    rebuild_mapping_rows();
    update_preview_modes();
    set_state(m_compute_snapshot.state);
    if (m_compute_snapshot.gap_preview)
        m_gap_preview = std::make_unique<GapPreviewState>(*m_compute_snapshot.gap_preview);
    else
        m_gap_preview = std::make_unique<GapPreviewState>();
    apply_gap_area();
    if (GetSizer())
        GetSizer()->Layout();
    Layout();
    recenter_preview_tags();
}

void TextureImportDialog::on_computation_progress(wxCommandEvent& evt)
{
    if (evt.GetInt() != m_compute_generation.load())
        return;

    const int percent = static_cast<int>(evt.GetExtraLong());
    const wxString msg = evt.GetString();
    if (m_progress_dlg) {
        if (!m_progress_dlg->Update(percent, msg))
            m_cancel_flag = true;
    } else if (m_current_computation_initial && m_initial_progress_callback) {
        if (!m_initial_progress_callback(percent))
            m_cancel_flag = true;
    }
}

void TextureImportDialog::on_computation_complete(wxCommandEvent& evt)
{
    if (evt.GetInt() != m_compute_generation.load())
        return;

    bool initial = m_current_computation_initial;

    if (m_progress_dlg) {
        m_progress_dlg->Destroy();
        m_progress_dlg = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(m_result_mutex);
        m_painted = std::move(m_pending_result);
        if (m_pending_gap_preview)
            m_gap_preview = std::move(m_pending_gap_preview);
        else
            m_gap_preview = std::make_unique<GapPreviewState>();
        m_pending_gap_preview.reset();
    }

    int actual_colors = (int)m_painted.cluster_colors.size();
    bool color_spin_dirty = false;
    if (m_color_spin && m_color_spin->GetTextCtrl() && m_color_spin->GetTextCtrl()->HasFocus()) {
        long parsed = 0;
        const wxString text = m_color_spin->GetTextCtrl()->GetValue();
        color_spin_dirty = !text.ToLong(&parsed) || (int)parsed != m_param_color_count;
    }
    if (!color_spin_dirty && actual_colors >= 1 && actual_colors <= (int)max_physical_filament_count()) {
        m_updating_params = true;
        set_color_count_value(actual_colors, true);
        m_updating_params = false;
    }

    for_each_preview([&](TexturePreviewCanvas* canvas) {
        canvas->set_painted_mesh_data(m_painted.vertices, m_painted.indices);
        canvas->set_face_colors(m_painted.face_colors);
    });

    if (!m_lock_original_vertex_preview && !m_original_preview_ready &&
        Slic3r::copy_prepared_mesh_from_cache(m_mesh_repair_cache, m_original_preview)) {
        m_original_preview_ready = true;
        m_original_color_count = (int)m_original_preview.cluster_colors.size();
        for_each_preview([&](TexturePreviewCanvas* canvas) {
            canvas->set_mesh_data(m_original_preview.vertices, m_original_preview.indices);
        });
        if (m_preview_canvas)
            m_preview_canvas->set_original_face_colors(m_original_preview.face_colors);
        update_color_count_controls();
    }

    // A fresh texture computation replaces m_painted, so virtual filaments from
    // the previous computation must not consume capacity when deciding whether
    // this run drops extra colors. Rebuild virtual filaments from this result.
    if (m_mix_enabled) {
        drop_virtual_filaments_keep_project();
        apply_mix_from_existing_filaments();
    } else {
        reset_to_project_filaments_and_auto_match();
    }

    m_applied_color_count = actual_colors >= 1 ? actual_colors : m_param_color_count;
    m_applied_smooth      = m_param_smooth;
    m_applied_auto_preset = m_auto_preset_selected;

    m_updating_params = true;
    set_gap_value(0.0, true);
    m_updating_params = false;
    apply_gap_area();

    set_state(TextureImportState::Ready);
    update_preview_modes();
    GetSizer()->Layout();
    recenter_preview_tags();

    if (initial) {
        m_initial_computation_pending = false;
        m_current_computation_initial = false;
    }

    if (m_advance_to_matching_when_ready && !m_painted.face_colors.empty()) {
        m_advance_to_matching_when_ready = false;
        commit_gap_area_to_painted();
        set_wizard_step(TextureImportWizardStep::FilamentMatching);
    }
}

void TextureImportDialog::on_computation_error(wxCommandEvent& evt)
{
    if (evt.GetInt() != m_compute_generation.load())
        return;

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
        restore_compute_snapshot();
        return;
    }

    m_advance_to_matching_when_ready = false;

    if (initial) {
        m_initial_computation_failed = true;
        m_initial_computation_pending = false;
        m_current_computation_initial = false;
        m_fallback_to_geometry_only = true;
        return;
    }

    restore_compute_snapshot();
    Slic3r::GUI::MessageDialog dlg(this,
        _L("Computation failed. Please adjust parameters and retry."),
        _L("Error"), wxOK | wxICON_ERROR);
    dlg.ShowModal();
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
    for_each_preview([&](TexturePreviewCanvas* canvas) {
        canvas->set_filament_color_map(color_map);
    });
}

// Canonical ordering used on the very first display after a computation:
// sort ascending by filament_index, and push unmapped (filament_index < 0)
// entries to the end. This gives the user a stable, predictable mapping
// layout regardless of the cluster discovery order.
void TextureImportDialog::sort_current_matches_by_filament_index()
{
    std::stable_sort(m_current_matches.begin(), m_current_matches.end(),
        [](const auto& lhs, const auto& rhs) {
            const bool lhs_valid = lhs.filament_index >= 0;
            const bool rhs_valid = rhs.filament_index >= 0;

            if (lhs_valid != rhs_valid)
                return lhs_valid;
            if (!lhs_valid)
                return false;

            return lhs.filament_index < rhs.filament_index;
        });
}

size_t TextureImportDialog::max_physical_filament_count() const
{
    // Physical filaments must match the sidebar's limit, otherwise the predicted
    // filament indices drift from the ones actually created.
    return MAX_PHYSICAL_FILAMENT_COUNT;
}

size_t TextureImportDialog::physical_filament_entry_count() const
{
    return (size_t)std::count_if(m_filament_entries.begin(), m_filament_entries.end(),
        [](const TextureFilamentEntry& entry) { return texture_entry_is_physical(entry.kind); });
}

size_t TextureImportDialog::mixed_filament_entry_count() const
{
    return (size_t)std::count_if(m_filament_entries.begin(), m_filament_entries.end(),
        [](const TextureFilamentEntry& entry) { return texture_entry_is_mixed(entry.kind); });
}

int TextureImportDialog::max_color_count() const
{
    const int filament_max = (int)max_physical_filament_count();
    if (m_original_color_count <= 0)
        return filament_max;
    return std::min(filament_max, m_original_color_count);
}

void TextureImportDialog::update_color_count_controls()
{
    const int max_c = max_color_count();
    const bool known_original = m_original_color_count > 0;
    if (m_btn_color_4)
        m_btn_color_4->Show(!known_original || 4 <= m_original_color_count);
    if (m_btn_color_8)
        m_btn_color_8->Show(!known_original || 8 <= m_original_color_count);
    if (m_btn_color_16)
        m_btn_color_16->Show(!known_original || 16 <= m_original_color_count);

    if (m_color_slider)
        m_color_slider->SetRange(1, max_c);

    if (!m_color_count_exceeded && m_param_color_count > max_c)
        set_color_count_value(max_c, true);
    else if (m_color_slider)
        m_color_slider->SetValue(m_param_color_count);

    if (m_params_panel)
        m_params_panel->Layout();
    if (GetSizer())
        GetSizer()->Layout();
    Layout();
}

void TextureImportDialog::set_color_count_exceeded(bool exceeded)
{
    const bool changed = (m_color_count_exceeded != exceeded);
    m_color_count_exceeded = exceeded;
    if (exceeded) {
        if (m_recompute_timer && m_recompute_timer->IsRunning())
            m_recompute_timer->Stop();
        m_advance_to_matching_when_ready = false;
        if (m_compute_superseded && m_state == TextureImportState::Computing && !m_current_computation_initial) {
            m_compute_superseded = false;
            set_state(m_painted.face_colors.empty() ? TextureImportState::Idle : TextureImportState::Ready);
        }
    }
    if (changed || exceeded)
        update_color_count_warning();
    if (changed)
        update_ui_for_state();
}

void TextureImportDialog::update_color_count_warning()
{
    auto* warning = dynamic_cast<ColorCountWarningPanel*>(m_color_count_warning);
    if (!warning)
        return;

    const bool show = m_color_count_exceeded
        && m_wizard_step == TextureImportWizardStep::SimplifyColors;
    if (show) {
        const int max_c = max_color_count();
        if (m_original_color_count > 0 && max_c == m_original_color_count)
            warning->set_message(wxString::Format(
                _L("The color count cannot exceed the original color count (%d)."),
                m_original_color_count));
        else
            warning->set_message(wxString::Format(
                _L("Up to %d colors supported."),
                (int)max_physical_filament_count()));
        warning->InvalidateBestSize();
        warning->SetMinSize(warning->GetBestSize());
    } else {
        warning->SetMinSize(wxDefaultSize);
    }
    if (warning->IsShown() != show)
        warning->Show(show);
    set_color_spin_error_border(m_color_spin, show);
    if (m_params_panel)
        m_params_panel->Layout();
    if (GetSizer())
        GetSizer()->Layout();
    Layout();
}

bool TextureImportDialog::filament_count_exceeded() const
{
    return physical_filament_entry_count() > MAX_PHYSICAL_FILAMENT_COUNT ||
           mixed_filament_entry_count() > MAX_MIXED_FILAMENT_COUNT ||
           m_filament_entries.size() > MAX_FILAMENT_COUNT;
}

int TextureImportDialog::find_closest_filament_index(const std::array<std::size_t, 3>& color) const
{
    return find_closest_filament_index(color, -1, false, std::string());
}

int TextureImportDialog::find_closest_filament_index(const std::array<std::size_t, 3>& color,
                                                    int skip_index, bool physical_only,
                                                    const std::string& family_type) const
{
    auto search = [&](bool filter_type) -> int {
        int best_idx = -1;
        double best_delta = std::numeric_limits<double>::max();
        const size_t filament_count = m_filament_colors_rgba.size();
        for (size_t i = 0; i < filament_count; ++i) {
            if ((int)i == skip_index)
                continue;
            if (physical_only && (i >= m_filament_entries.size() ||
                                  !texture_entry_is_physical(m_filament_entries[i].kind)))
                continue;
            if (filter_type) {
                if (i >= m_filament_entries.size())
                    continue;
                const std::string ft = texture_entry_family_type(m_filament_entries[i], m_filament_entries);
                if (ft != family_type)
                    continue;
            }
            const double delta = Slic3r::compute_delta_e(color, m_filament_colors_rgba[i]);
            if (delta < best_delta) {
                best_delta = delta;
                best_idx = (int)i;
            }
        }
        return best_idx;
    };

    if (!family_type.empty()) {
        const int typed_idx = search(true);
        if (typed_idx >= 0)
            return typed_idx;
    }
    return search(false);
}

int TextureImportDialog::add_virtual_filament(const std::array<float, 4>& rgba, const std::string& hex,
                                              const std::string& preset_name,
                                              const std::string& display_name)
{
    if (m_filament_color_strs.size() != m_filament_colors_rgba.size() ||
        m_filament_names.size() != m_filament_colors_rgba.size() ||
        m_filament_entries.size() != m_filament_colors_rgba.size()) {
        return -1;
    }

    // Prefer the caller-supplied display name (series decompose path); otherwise
    // resolve it from the preset so compact_used_virtual_filaments() can restore
    // entry.name from m_filament_names without falling back to a generic series name.
    const std::string resolved_preset = preset_name.empty() ? m_default_virtual_filament_preset_name : preset_name;
    const std::string resolved_name = !display_name.empty()
        ? display_name
        : texture_filament_display_name_from_preset(resolved_preset);

    const int new_idx = (int)m_filament_colors_rgba.size();
    m_filament_colors_rgba.push_back(rgba);
    m_filament_color_strs.push_back(hex);
    m_filament_names.push_back(resolved_name);
    TextureFilamentEntry entry;
    entry.kind = TextureFilamentKind::NewPhysical;
    entry.dialog_index = new_idx;
    entry.project_config_index = size_t(-1);
    entry.color_hex = texture_normalize_color_hex(hex);
    entry.name = resolved_name;
    entry.type = texture_filament_type_from_preset(resolved_preset);
    entry.preset_name = resolved_preset;
    m_filament_entries.push_back(entry);
    m_new_filament_colors.push_back(rgba);
    m_new_filament_preset_names.push_back(resolved_preset);
    return new_idx;
}

int TextureImportDialog::add_virtual_mixed_filament(const std::string& color_hex,
                                                    const std::vector<int>& component_dialog_indices,
                                                    const std::vector<int>& ratios)
{
    if (m_filament_color_strs.size() != m_filament_colors_rgba.size() ||
        m_filament_names.size() != m_filament_colors_rgba.size() ||
        m_filament_entries.size() != m_filament_colors_rgba.size()) {
        return -1;
    }
    if (component_dialog_indices.size() < 2 || component_dialog_indices.size() != ratios.size())
        return -1;
    for (int idx : component_dialog_indices) {
        if (idx < 0 || idx >= (int)m_filament_entries.size() ||
            !texture_entry_is_physical(m_filament_entries[idx].kind)) {
            return -1;
        }
    }

    const int new_idx = (int)m_filament_colors_rgba.size();
    TextureFilamentEntry entry;
    entry.kind = TextureFilamentKind::NewMixed;
    entry.dialog_index = new_idx;
    entry.project_config_index = size_t(-1);
    entry.color_hex = texture_normalize_color_hex(color_hex);
    entry.name = texture_mixed_filament_display_name(m_filament_entries, component_dialog_indices);
    entry.mixed_ratios = ratios;
    for (int idx : component_dialog_indices)
        entry.mixed_components.push_back((unsigned int)(idx + 1));

    TextureNewMixedFilament mixed;
    mixed.dialog_index = entry.dialog_index;
    mixed.color_hex = entry.color_hex;
    mixed.component_dialog_indices = component_dialog_indices;
    mixed.ratios = ratios;

    m_filament_entries.push_back(entry);
    m_filament_color_strs.push_back(entry.color_hex);
    m_filament_names.push_back(entry.name);
    m_filament_colors_rgba.push_back(parse_color_string(entry.color_hex));
    m_new_mixed_filaments.push_back(mixed);
    return entry.dialog_index;
}

bool TextureImportDialog::remove_virtual_filament(int dialog_index)
{
    if (dialog_index < 0 || dialog_index >= (int)m_filament_entries.size() ||
        m_filament_entries.size() != m_filament_colors_rgba.size() ||
        m_filament_color_strs.size() != m_filament_colors_rgba.size() ||
        m_filament_names.size() != m_filament_colors_rgba.size()) {
        return false;
    }

    const TextureFilamentKind kind = m_filament_entries[dialog_index].kind;
    if (kind != TextureFilamentKind::NewPhysical && kind != TextureFilamentKind::NewMixed)
        return false;

    const auto deleted_color = texture_rgba_to_cluster_color(m_filament_colors_rgba[dialog_index]);
    const bool physical_only = kind == TextureFilamentKind::NewPhysical;
    const int replacement = find_closest_filament_index(deleted_color, dialog_index, physical_only);
    if (replacement < 0)
        return false;

    auto apply_mixed_color = [this](TextureFilamentEntry& entry) {
        const std::string hex = texture_blend_mixed_color_hex(
            entry.mixed_components, entry.mixed_ratios, m_filament_colors_rgba);
        entry.color_hex = texture_normalize_color_hex(hex);
        if (entry.dialog_index >= 0 && entry.dialog_index < (int)m_filament_color_strs.size())
            m_filament_color_strs[entry.dialog_index] = entry.color_hex;
        if (entry.dialog_index >= 0 && entry.dialog_index < (int)m_filament_colors_rgba.size())
            m_filament_colors_rgba[entry.dialog_index] = parse_color_string(entry.color_hex);
        for (auto& mixed : m_new_mixed_filaments) {
            if (mixed.dialog_index != entry.dialog_index)
                continue;
            mixed.color_hex = entry.color_hex;
            mixed.ratios = entry.mixed_ratios;
            mixed.component_dialog_indices.clear();
            mixed.component_dialog_indices.reserve(entry.mixed_components.size());
            for (unsigned int comp : entry.mixed_components)
                mixed.component_dialog_indices.push_back(comp >= 1 ? (int)comp - 1 : -1);
        }
    };

    if (kind == TextureFilamentKind::NewPhysical) {
        for (auto& entry : m_filament_entries) {
            if (entry.kind != TextureFilamentKind::NewMixed)
                continue;
            bool changed = false;
            for (unsigned int& comp : entry.mixed_components) {
                const int comp_idx = comp >= 1 ? (int)comp - 1 : -1;
                if (comp_idx == dialog_index) {
                    comp = (unsigned int)(replacement + 1);
                    changed = true;
                }
            }
            if (!changed)
                continue;
            const int sole = fold_new_mixed_components(entry.dialog_index);
            if (sole >= 0)
                retarget_filament_slot(entry.dialog_index, sole);
            else
                apply_mixed_color(entry);
        }
        for (auto& mixed : m_new_mixed_filaments) {
            for (int& comp_idx : mixed.component_dialog_indices) {
                if (comp_idx == dialog_index)
                    comp_idx = replacement;
            }
        }
    }

    for (auto& m : m_current_matches) {
        if (m.filament_index == dialog_index)
            m.filament_index = replacement;
    }
    for (auto& row : m_mapping_rows) {
        if (row.target_filament_idx == dialog_index)
            row.target_filament_idx = replacement;
    }

    auto remap_index = [dialog_index](int idx) -> int {
        if (idx == dialog_index)
            return -1;
        if (idx > dialog_index)
            return idx - 1;
        return idx;
    };

    m_filament_entries.erase(m_filament_entries.begin() + dialog_index);
    m_filament_colors_rgba.erase(m_filament_colors_rgba.begin() + dialog_index);
    m_filament_color_strs.erase(m_filament_color_strs.begin() + dialog_index);
    m_filament_names.erase(m_filament_names.begin() + dialog_index);

    for (size_t i = 0; i < m_filament_entries.size(); ++i)
        m_filament_entries[i].dialog_index = (int)i;

    for (auto& entry : m_filament_entries) {
        if (entry.kind != TextureFilamentKind::NewMixed)
            continue;
        for (unsigned int& comp : entry.mixed_components) {
            const int old_idx = comp >= 1 ? (int)comp - 1 : -1;
            const int new_idx = remap_index(old_idx);
            if (new_idx >= 0)
                comp = (unsigned int)(new_idx + 1);
        }
    }

    for (auto it = m_new_mixed_filaments.begin(); it != m_new_mixed_filaments.end(); ) {
        if (it->dialog_index == dialog_index) {
            it = m_new_mixed_filaments.erase(it);
            continue;
        }
        it->dialog_index = remap_index(it->dialog_index);
        for (int& comp_idx : it->component_dialog_indices)
            comp_idx = remap_index(comp_idx);
        ++it;
    }

    m_new_filament_colors.clear();
    m_new_filament_preset_names.clear();
    for (const auto& entry : m_filament_entries) {
        if (entry.kind != TextureFilamentKind::NewPhysical)
            continue;
        if (entry.dialog_index < 0 || entry.dialog_index >= (int)m_filament_colors_rgba.size())
            continue;
        m_new_filament_colors.push_back(m_filament_colors_rgba[entry.dialog_index]);
        m_new_filament_preset_names.push_back(
            entry.preset_name.empty() ? m_default_virtual_filament_preset_name : entry.preset_name);
    }

    for (auto& m : m_current_matches) {
        m.filament_index = remap_index(m.filament_index);
        if (m.filament_index >= 0 && m.filament_index < (int)m_filament_colors_rgba.size()) {
            m.filament_color = m_filament_colors_rgba[m.filament_index];
            m.delta_e = Slic3r::compute_delta_e(m.cluster_color, m.filament_color);
            if (m.filament_index >= (int)m_existing_filament_count)
                m.delta_e = 0.0;
        }
    }
    for (auto& row : m_mapping_rows)
        row.target_filament_idx = remap_index(row.target_filament_idx);

    return true;
}

void TextureImportDialog::compact_used_virtual_filaments()
{
    if (m_current_matches.empty())
        return;

    const std::vector<std::array<float, 4>> old_colors = m_filament_colors_rgba;
    const std::vector<std::string> old_color_strs = m_filament_color_strs;
    const std::vector<std::string> old_names = m_filament_names;
    const std::vector<TextureFilamentEntry> old_entries = m_filament_entries;

    auto old_new_mixed_has_valid_components = [&old_entries, &old_colors](const TextureFilamentEntry& entry) {
        if (entry.kind != TextureFilamentKind::NewMixed)
            return true;
        if (entry.mixed_components.size() < 2 || entry.mixed_components.size() != entry.mixed_ratios.size())
            return false;
        for (unsigned int comp : entry.mixed_components) {
            int comp_idx = comp >= 1 ? (int)comp - 1 : -1;
            if (comp_idx < 0 || comp_idx >= (int)old_entries.size() || comp_idx >= (int)old_colors.size() ||
                !texture_entry_is_physical(old_entries[comp_idx].kind)) {
                return false;
            }
        }
        return true;
    };

    std::set<int> used_virtual_indices;
    for (const auto& m : m_current_matches) {
        if (m.filament_index >= (int)m_existing_filament_count &&
            m.filament_index < (int)old_colors.size()) {
            if (m.filament_index < (int)old_entries.size() &&
                old_entries[m.filament_index].kind == TextureFilamentKind::NewMixed &&
                !old_new_mixed_has_valid_components(old_entries[m.filament_index])) {
                continue;
            }
            used_virtual_indices.insert(m.filament_index);
        }
    }
    bool added_dependency = true;
    while (added_dependency) {
        added_dependency = false;
        std::vector<int> current_used(used_virtual_indices.begin(), used_virtual_indices.end());
        for (int used_idx : current_used) {
            if (used_idx < 0 || used_idx >= (int)old_entries.size() ||
                old_entries[used_idx].kind != TextureFilamentKind::NewMixed ||
                !old_new_mixed_has_valid_components(old_entries[used_idx]))
                continue;
            for (unsigned int comp : old_entries[used_idx].mixed_components) {
                int comp_idx = comp >= 1 ? (int)comp - 1 : -1;
                if (comp_idx >= (int)m_existing_filament_count && comp_idx < (int)old_entries.size() &&
                    used_virtual_indices.insert(comp_idx).second) {
                    added_dependency = true;
                }
            }
        }
    }

    std::vector<std::array<float, 4>> compact_colors;
    std::vector<std::string> compact_color_strs;
    std::vector<std::string> compact_names;
    std::vector<TextureFilamentEntry> compact_entries;
    compact_colors.reserve(m_existing_filament_count + used_virtual_indices.size());
    compact_color_strs.reserve(m_existing_filament_count + used_virtual_indices.size());
    compact_names.reserve(m_existing_filament_count + used_virtual_indices.size());
    compact_entries.reserve(m_existing_filament_count + used_virtual_indices.size());

    const size_t existing_count = std::min(m_existing_filament_count, old_colors.size());
    for (size_t i = 0; i < existing_count; ++i) {
        compact_colors.push_back(old_colors[i]);
        compact_color_strs.push_back(i < old_color_strs.size() ? old_color_strs[i] : "");
        compact_names.push_back(i < old_names.size() ? old_names[i] : default_filament_stored_name((int)i + 1));
        TextureFilamentEntry entry = i < old_entries.size() ? old_entries[i] : TextureFilamentEntry{};
        entry.dialog_index = (int)i;
        entry.color_hex = texture_normalize_color_hex(compact_color_strs.back());
        entry.name = compact_names.back();
        compact_entries.push_back(entry);
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
        TextureFilamentEntry entry = old_idx < (int)old_entries.size() ? old_entries[old_idx] : TextureFilamentEntry{};
        entry.dialog_index = (int)compact_entries.size();
        entry.color_hex = texture_normalize_color_hex(compact_color_strs.back());
        entry.name = compact_names.back();
        if (entry.kind == TextureFilamentKind::NewPhysical) {
            compact_new_colors.push_back(old_colors[old_idx]);
            compact_new_preset_names.push_back(entry.preset_name.empty() ? m_default_virtual_filament_preset_name : entry.preset_name);
        }
        compact_entries.push_back(entry);
    }

    m_filament_colors_rgba = std::move(compact_colors);
    m_filament_color_strs = std::move(compact_color_strs);
    m_filament_names = std::move(compact_names);
    m_filament_entries = std::move(compact_entries);
    m_new_filament_colors = std::move(compact_new_colors);
    m_new_filament_preset_names = std::move(compact_new_preset_names);
    m_new_mixed_filaments.clear();
    std::set<int> invalid_compacted_mixed_indices;
    for (auto& entry : m_filament_entries) {
        if (entry.kind != TextureFilamentKind::NewMixed)
            continue;
        TextureNewMixedFilament mixed;
        mixed.dialog_index = entry.dialog_index;
        mixed.color_hex = entry.color_hex;
        mixed.ratios = entry.mixed_ratios;
        mixed.component_dialog_indices.reserve(entry.mixed_components.size());
        bool valid_components = entry.mixed_components.size() >= 2 &&
            entry.mixed_components.size() == entry.mixed_ratios.size();
        for (unsigned int comp : entry.mixed_components) {
            int old_comp_idx = comp >= 1 ? (int)comp - 1 : -1;
            if (old_comp_idx < 0) {
                valid_components = false;
                break;
            }
            auto remap_it = old_to_new.find(old_comp_idx);
            int new_comp_idx = remap_it != old_to_new.end() ? remap_it->second : old_comp_idx;
            if (new_comp_idx < 0 || new_comp_idx >= (int)m_filament_entries.size() ||
                !texture_entry_is_physical(m_filament_entries[new_comp_idx].kind)) {
                valid_components = false;
                break;
            }
            mixed.component_dialog_indices.push_back(new_comp_idx);
        }
        if (!valid_components) {
            invalid_compacted_mixed_indices.insert(entry.dialog_index);
            continue;
        }
        entry.mixed_components.clear();
        for (int comp_idx : mixed.component_dialog_indices)
            entry.mixed_components.push_back((unsigned int)(comp_idx + 1));
        m_new_mixed_filaments.push_back(mixed);
    }

    auto find_closest_physical_filament_index = [this](const std::array<std::size_t, 3>& color) {
        int best_idx = -1;
        double best_delta = std::numeric_limits<double>::max();
        const size_t filament_count = m_filament_colors_rgba.size();
        for (size_t i = 0; i < filament_count && i < m_filament_entries.size(); ++i) {
            if (!texture_entry_is_physical(m_filament_entries[i].kind))
                continue;
            const double delta = Slic3r::compute_delta_e(color, m_filament_colors_rgba[i]);
            if (delta < best_delta) {
                best_delta = delta;
                best_idx = (int)i;
            }
        }
        return best_idx;
    };

    for (auto& m : m_current_matches) {
        auto it = old_to_new.find(m.filament_index);
        if (it != old_to_new.end()) {
            m.filament_index = it->second;
        } else if (m.filament_index >= (int)m_existing_filament_count) {
            m.filament_index = find_closest_filament_index(m.cluster_color, -1, !m_mix_enabled);
        }
        if (invalid_compacted_mixed_indices.count(m.filament_index) > 0) {
            int fallback_idx = find_closest_physical_filament_index(m.cluster_color);
            m.filament_index = fallback_idx >= 0 ? fallback_idx : find_closest_filament_index(m.cluster_color, -1, !m_mix_enabled);
        }

        if (m.filament_index >= 0 && m.filament_index < (int)m_filament_colors_rgba.size()) {
            m.filament_color = m_filament_colors_rgba[m.filament_index];
            m.delta_e = Slic3r::compute_delta_e(m.cluster_color, m.filament_color);
            if (m.filament_index >= (int)m_existing_filament_count)
                m.delta_e = 0.0;
        }
    }

    for (auto& row : m_mapping_rows) {
        for (const auto& m : m_current_matches) {
            if (m.cluster_index != row.cluster_id)
                continue;
            row.target_filament_idx = m.filament_index;
            break;
        }
    }
}

void TextureImportDialog::refresh_mapping_target_panels()
{
    const auto display_numbers = compute_display_numbers();
    for (auto& row : m_mapping_rows) {
        if (!row.target_panel)
            continue;
        const int idx = row.target_filament_idx;
        if (idx < 0)
            row.target_panel->SetToolTip(wxString());
        else {
            const int display = (idx < (int)display_numbers.size() && display_numbers[idx] > 0)
                ? display_numbers[idx] : idx + 1;
            row.target_panel->SetToolTip(
                texture_filament_label_wx(m_filament_entries, m_filament_names, idx, display));
        }
        row.target_panel->Refresh();
    }
}

void TextureImportDialog::bind_match_inplace(Slic3r::FilamentMatch& match, int filament_index, bool repaint)
{
    match.filament_index = filament_index;
    if (filament_index >= 0 && filament_index < (int)m_filament_colors_rgba.size()) {
        match.filament_color = m_filament_colors_rgba[filament_index];
        match.delta_e = Slic3r::compute_delta_e(match.cluster_color, match.filament_color);
        if (filament_index >= (int)m_existing_filament_count)
            match.delta_e = 0.0;
    }
    for (auto& row : m_mapping_rows) {
        if (row.cluster_id != match.cluster_index)
            continue;
        row.target_filament_idx = filament_index;
        if (row.target_panel && repaint) {
            row.target_panel->Refresh();
            // Flush the repaint now so a batch bind shows progress. Unlike
            // wxYield this dispatches no input, so callers iterating over
            // m_current_matches cannot be re-entered mid-loop.
            row.target_panel->Update();
        }
        break;
    }
}

static int texture_import_color_tolerance()
{
    auto* config = wxGetApp().app_config;
    if (!config)
        return kColorToleranceDefault;
    const std::string stored = config->get(kColorToleranceConfigKey);
    if (stored.empty())
        return kColorToleranceDefault;
    long parsed = 0;
    if (!wxString::FromUTF8(stored).ToLong(&parsed))
        return kColorToleranceDefault;
    return std::clamp(static_cast<int>(parsed), kColorToleranceMin, kColorToleranceMax);
}

// Lower rank wins inside the equal-color band.
static int filament_equal_color_rank(TextureFilamentKind kind)
{
    switch (kind) {
    case TextureFilamentKind::ExistingPhysical: return 0;
    case TextureFilamentKind::NewPhysical:      return 1;
    case TextureFilamentKind::ExistingMixed:    return 2;
    case TextureFilamentKind::NewMixed:         return 3;
    }
    return 4;
}

int TextureImportDialog::select_filament_for_color(const std::array<std::size_t, 3>& color,
                                                   const std::vector<int>& candidates) const
{
    struct Candidate {
        int    idx;
        double delta;
        int    rank;
    };

    const int tolerance = texture_import_color_tolerance();
    std::vector<Candidate> accepted;
    std::vector<Candidate> equal;
    accepted.reserve(candidates.size());
    for (int idx : candidates) {
        if (idx < 0 || idx >= (int)m_filament_colors_rgba.size())
            continue;
        const double delta = Slic3r::compute_delta_e(color, m_filament_colors_rgba[idx]);
        if (!match_delta_accepted(delta, m_match_delta_threshold))
            continue;
        const int rank = (idx < (int)m_filament_entries.size())
            ? filament_equal_color_rank(m_filament_entries[idx].kind)
            : 4;
        const Candidate cand{idx, delta, rank};
        accepted.push_back(cand);
        if (delta <= static_cast<double>(tolerance))
            equal.push_back(cand);
    }
    if (accepted.empty())
        return -1;

    const bool use_priority = !equal.empty();
    const std::vector<Candidate>& pool = use_priority ? equal : accepted;
    Candidate best = pool.front();
    for (size_t i = 1; i < pool.size(); ++i) {
        const Candidate& cand = pool[i];
        const bool better_rank = use_priority && cand.rank < best.rank;
        const bool same_rank = !use_priority || cand.rank == best.rank;
        const bool better_delta = cand.delta < best.delta ||
            (cand.delta == best.delta && cand.idx < best.idx);
        if (better_rank || (same_rank && better_delta))
            best = cand;
    }
    return best.idx;
}

void TextureImportDialog::reapply_match_threshold(bool drop_unused)
{
    if (m_state != TextureImportState::Ready || m_mix_applying)
        return;
    if (m_painted.cluster_colors.empty() || m_current_matches.empty())
        return;

    // Same family vote as the initial auto-match. Empty family is kept in the
    // typed pass; a filament is skipped only when its family is known and differs.
    // Mixed slots use their display color, and only while Color Mixing is on.
    const std::string match_type = pick_auto_match_filament_type(m_filament_entries, m_existing_filament_count);
    const size_t filament_count = std::min(m_filament_entries.size(), m_filament_colors_rgba.size());

    auto collect = [&](bool same_type_only) {
        std::vector<int> indices;
        indices.reserve(filament_count);
        for (size_t i = 0; i < filament_count; ++i) {
            const TextureFilamentKind kind = m_filament_entries[i].kind;
            if (!m_mix_enabled && texture_entry_is_mixed(kind))
                continue;
            if (!texture_entry_is_physical(kind) && !texture_entry_is_mixed(kind))
                continue;
            if (same_type_only) {
                const std::string ft = texture_entry_family_type(m_filament_entries[i], m_filament_entries);
                if (!ft.empty() && ft != match_type)
                    continue;
            }
            indices.push_back((int)i);
        }
        return indices;
    };

    std::vector<int> candidates = collect(true);
    if (candidates.empty())
        candidates = collect(false);

    for (auto& match : m_current_matches) {
        const int bind = select_filament_for_color(match.cluster_color, candidates);
        if (match.filament_index != bind)
            bind_match_inplace(match, bind, false);
    }

    if (drop_unused) {
        m_match_threshold_drop_pending = false;
        drop_unused_new_filaments_and_refresh();
    } else {
        m_match_threshold_drop_pending = true;
        refresh_mapping_target_panels();
        update_filament_color_map();
        update_unmatched_warning_visibility();
        update_confirm_button_state();
    }
}

void TextureImportDialog::show_match_threshold_popup()
{
    if (!m_match_threshold_gear || m_match_threshold_gear->IsBeingDeleted())
        return;
    if (m_wizard_step != TextureImportWizardStep::FilamentMatching)
        return;

    dismiss_filament_popup();

    if (!m_match_threshold_popup) {
        m_match_threshold_popup = new MatchThresholdPopup(
            this, m_match_threshold_gear, m_match_delta_threshold,
            [this](int value) { on_match_threshold_changed(value); },
            [this](int value) { on_match_threshold_committed(value); },
            [this]() {
                if (m_match_threshold_drop_pending) {
                    m_match_threshold_drop_pending = false;
                    drop_unused_new_filaments_and_refresh();
                }
                update_match_threshold_gear_hover();
            });
    } else {
        m_match_threshold_popup->set_value(m_match_delta_threshold);
    }
    m_match_threshold_popup->place_and_popup();
    if (m_match_threshold_gear)
        m_match_threshold_gear->set_hovered(true);
}

void TextureImportDialog::hide_match_threshold_popup()
{
    if (m_match_threshold_popup)
        m_match_threshold_popup->request_close();
    update_match_threshold_gear_hover();
}

void TextureImportDialog::update_match_threshold_gear_hover()
{
    if (!m_match_threshold_gear || m_match_threshold_gear->IsBeingDeleted())
        return;
    const bool popup_open = m_match_threshold_popup && m_match_threshold_popup->IsShown();
    const bool over_gear = m_match_threshold_gear->GetScreenRect().Contains(wxGetMousePosition());
    m_match_threshold_gear->set_hovered(popup_open || over_gear);
}

void TextureImportDialog::disable_mix_for_threshold_change()
{
    // The threshold only drives physical matching, so leave Color mixing once the user tunes it.
    if (!m_mix_enabled)
        return;
    m_mix_enabled = false;
    if (m_mix_cb)
        m_mix_cb->SetValue(false);
    BOOST_LOG_TRIVIAL(info) << "color mixing turned off by match threshold change";
    reset_auto_mix();
}

void TextureImportDialog::on_match_threshold_changed(int value)
{
    value = std::clamp(value, kMatchDeltaThresholdMin, kMatchDeltaThresholdMax);
    if (m_match_delta_threshold == value)
        return;
    m_match_delta_threshold = value;
    disable_mix_for_threshold_change();
    reapply_match_threshold(false);
}

void TextureImportDialog::on_match_threshold_committed(int value)
{
    value = std::clamp(value, kMatchDeltaThresholdMin, kMatchDeltaThresholdMax);
    const bool changed = m_match_delta_threshold != value;
    m_match_delta_threshold = value;
    if (changed) {
        disable_mix_for_threshold_change();
        reapply_match_threshold(true);
    } else if (m_match_threshold_drop_pending) {
        m_match_threshold_drop_pending = false;
        drop_unused_new_filaments_and_refresh();
    }
}

void TextureImportDialog::drop_unused_new_filaments_and_refresh()
{
    compact_used_virtual_filaments();
    refresh_mapping_target_panels();
    update_filament_color_map();
    update_unmatched_warning_visibility();
    update_confirm_button_state();
    if (m_filament_popup && m_filament_popup->IsShown()) {
        m_filament_popup->refresh_filaments(
            m_filament_entries, m_filament_colors_rgba, m_filament_names,
            m_existing_filament_count, compute_display_numbers());
    }
}

std::vector<int> TextureImportDialog::compute_display_numbers() const
{
    // Assigns each entry a 1-based display number in the order the sidebar will
    // show after apply: ExistingPhysical, NewPhysical, ExistingMixed, NewMixed.
    // This keeps the dialog's visible IDs in sync with the post-apply sidebar,
    // instead of the raw dialog_index (which interleaves physicals and mixeds
    // by processing order and causes e.g. CMYW to show 4,5,6,8 instead of 3,4,5,6).
    // MUST mirror ordering in apply_textured_mesh_import_result (Plater.cpp:9896):
    //   - ExistingPhysical keeps its project_config_index
    //   - NewPhysical is inserted at existing_physical_count + new_order
    //   - ExistingMixed shifts to project_config_index + new_physical_count
    //   - NewMixed is appended after all existing mixeds
    std::vector<int> result(m_filament_entries.size(), 0);
    int next = 1;

    auto assign_group = [&](TextureFilamentKind kind, bool by_project_config_index) {
        if (by_project_config_index) {
            std::vector<const TextureFilamentEntry*> group;
            for (const auto& e : m_filament_entries)
                if (e.kind == kind)
                    group.push_back(&e);
            std::sort(group.begin(), group.end(),
                      [](const TextureFilamentEntry* a, const TextureFilamentEntry* b) {
                          return a->project_config_index < b->project_config_index;
                      });
            for (const auto* e : group) {
                if (e->dialog_index >= 0 && e->dialog_index < (int)result.size())
                    result[e->dialog_index] = next;
                ++next;
            }
        } else {
            for (const auto& e : m_filament_entries) {
                if (e.kind != kind)
                    continue;
                if (e.dialog_index >= 0 && e.dialog_index < (int)result.size())
                    result[e.dialog_index] = next;
                ++next;
            }
        }
    };

    assign_group(TextureFilamentKind::ExistingPhysical, true);
    assign_group(TextureFilamentKind::NewPhysical,      false);
    assign_group(TextureFilamentKind::ExistingMixed,    true);
    assign_group(TextureFilamentKind::NewMixed,         false);
    return result;
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
    // Dismiss() owns the complete teardown and is idempotent.
    popup->Dismiss();
}

void TextureImportDialog::show_mixing_kits_help()
{
    // Do not auto-delete project filaments that the current texture mapping still uses.
    std::set<size_t> protected_indices;
    for (const auto& match : m_current_matches) {
        const int idx = match.filament_index;
        if (idx < 0 || idx >= (int)m_filament_entries.size())
            continue;
        const TextureFilamentEntry& entry = m_filament_entries[idx];
        if (entry.kind == TextureFilamentKind::ExistingPhysical)
            protected_indices.insert(entry.project_config_index);
        for (unsigned int comp : entry.mixed_components) {
            if (comp >= 1)
                protected_indices.insert(comp - 1);
        }
    }
    const OfficialMixingKitAddPlan plan = plan_official_mixing_kit_addition(0, protected_indices);
    MixingKitsHelpDialog dlg(this, plan);
    if (dlg.ShowModal() == wxID_APPLY)
        add_pla_palette_cmykw_to_project(plan);
}

void TextureImportDialog::on_mix_toggled(wxCommandEvent& evt)
{
    evt.Skip();
    if (m_mix_cb)
        m_mix_enabled = m_mix_cb->GetValue();
    // With a single color every cluster would bind to that one filament, so ask for
    // more filaments first. Only on this user toggle: rematches must not prompt again.
    if (m_mix_enabled && mix_needs_supplement_filaments()) {
        if (!prompt_supplement_filaments_for_mix()) {
            m_mix_enabled = false;
            if (m_mix_cb)
                m_mix_cb->SetValue(false);
            return;
        }
        rematch_after_project_filament_change();
        return;
    }
    if (m_mix_enabled)
        apply_mix_from_existing_filaments();
    else
        reset_auto_mix();
}

void TextureImportDialog::reload_existing_filaments_from_project(const std::set<size_t>& deleted_project_indices)
{
    const std::vector<TextureFilamentEntry> old_entries = m_filament_entries;
    const std::vector<std::array<float, 4>> old_colors  = m_filament_colors_rgba;

    m_filament_entries = collect_project_filament_entries();
    m_filament_color_strs.clear();
    m_filament_names.clear();
    m_filament_colors_rgba.clear();
    m_filament_color_strs.reserve(m_filament_entries.size());
    m_filament_names.reserve(m_filament_entries.size());
    m_filament_colors_rgba.reserve(m_filament_entries.size());
    for (auto& entry : m_filament_entries) {
        entry.color_hex = texture_normalize_color_hex(entry.color_hex);
        if (entry.name.empty())
            entry.name = default_filament_stored_name(entry.dialog_index + 1);
        m_filament_color_strs.push_back(entry.color_hex);
        m_filament_names.push_back(entry.name);
        m_filament_colors_rgba.push_back(parse_color_string(entry.color_hex));
    }
    m_existing_filament_count = m_filament_entries.size();
    m_new_filament_colors.clear();
    m_new_filament_preset_names.clear();
    m_new_mixed_filaments.clear();

    // Surviving existing entries keep their relative order. Filaments deleted to make room for the
    // official mixing kit are skipped so they cannot claim a later entry with the same color.
    m_reload_index_remap.assign(old_entries.size(), -1);
    size_t new_pos = 0;
    for (const auto& old_entry : old_entries) {
        if (old_entry.kind != TextureFilamentKind::ExistingPhysical && old_entry.kind != TextureFilamentKind::ExistingMixed)
            continue;
        if (deleted_project_indices.count(old_entry.project_config_index) > 0)
            continue;
        const std::string old_color = texture_normalize_color_hex(old_entry.color_hex);
        for (size_t i = new_pos; i < m_existing_filament_count; ++i) {
            const auto& entry = m_filament_entries[i];
            if (entry.kind == old_entry.kind && entry.color_hex == old_color) {
                if (old_entry.dialog_index >= 0 && old_entry.dialog_index < (int)old_entries.size())
                    m_reload_index_remap[old_entry.dialog_index] = (int)i;
                new_pos = i + 1;
                break;
            }
        }
    }

    // Keep the pending physical filaments still bound to a row while physical slots
    // remain. Filaments the user just added to the project take priority; rows whose
    // pending filament no longer fits are left unmatched for the following rematch.
    // Pending mixed ones are regenerated by the following auto-mix from the new filament pool.
    std::set<int> referenced;
    for (const auto& m : m_current_matches)
        referenced.insert(m.filament_index);
    for (const auto& old_entry : old_entries) {
        if (old_entry.kind != TextureFilamentKind::NewPhysical || referenced.count(old_entry.dialog_index) == 0 ||
            old_entry.dialog_index < 0 || old_entry.dialog_index >= (int)old_colors.size())
            continue;
        if (physical_filament_entry_count() >= MAX_PHYSICAL_FILAMENT_COUNT ||
            m_filament_entries.size() >= MAX_FILAMENT_COUNT)
            continue;
        m_reload_index_remap[old_entry.dialog_index] = add_virtual_filament(
            old_colors[old_entry.dialog_index], old_entry.color_hex, old_entry.preset_name, old_entry.name);
    }
}

void TextureImportDialog::rematch_after_project_filament_change()
{
    if (m_state != TextureImportState::Ready || m_painted.cluster_colors.empty()) {
        update_ui_for_state();
        return;
    }
    // Project filament list was rebuilt; carry each binding over to its new dialog index.
    for (auto& m : m_current_matches) {
        const int old_idx = m.filament_index;
        const int new_idx = (old_idx >= 0 && old_idx < (int)m_reload_index_remap.size()) ? m_reload_index_remap[old_idx] : -1;
        bind_match_inplace(m, new_idx, false);
    }
    m_reload_index_remap.clear();
    if (m_mix_enabled)
        apply_mix_from_existing_filaments();
    else {
        do_auto_match();
        rebuild_mapping_rows();
        update_ui_for_state();
    }
    Layout();
    Refresh();
}

size_t TextureImportDialog::pending_physical_filament_count() const
{
    std::set<int> referenced;
    for (const auto& m : m_current_matches)
        referenced.insert(m.filament_index);
    size_t count = 0;
    for (const auto& entry : m_filament_entries) {
        if (entry.kind == TextureFilamentKind::NewPhysical && referenced.count(entry.dialog_index) > 0)
            ++count;
    }
    return count;
}

void TextureImportDialog::add_pla_palette_cmykw_to_project(const OfficialMixingKitAddPlan& plan)
{
    // The kit takes priority over pending physical filaments of this dialog. Pending ones
    // that no longer fit are dropped by reload_existing_filaments_from_project().
    if (!add_official_mixing_kit_to_project(this, plan))
        return;

    wxBusyCursor wait;
    reload_existing_filaments_from_project(std::set<size_t>(plan.delete_indices.begin(), plan.delete_indices.end()));
    if (m_mix_cb)
        m_mix_cb->SetValue(true);
    m_mix_enabled = true;
    rematch_after_project_filament_change();
}

bool TextureImportDialog::mix_needs_supplement_filaments() const
{
    if (!wxGetApp().preset_bundle ||
        wxGetApp().preset_bundle->physical_filament_count() + pending_physical_filament_count() >= max_physical_filament_count() ||
        wxGetApp().preset_bundle->filament_presets.size() + pending_physical_filament_count() >= MAX_FILAMENT_COUNT)
        return false;
    if (has_complete_pla_palette_cmykw())
        return false;
    // Same pool apply_mix_from_existing_filaments() mixes from.
    const std::string family = preferred_mix_family();
    std::set<std::string> colors;
    for (const auto& entry : m_filament_entries) {
        if (entry.kind != TextureFilamentKind::ExistingPhysical)
            continue;
        if (texture_entry_family_type(entry, m_filament_entries) != family)
            continue;
        colors.insert(texture_normalize_color_hex(entry.color_hex));
        if (colors.size() >= 2)
            return false;
    }
    return true;
}

bool TextureImportDialog::prompt_supplement_filaments_for_mix()
{
    if (!wxGetApp().plater() || !wxGetApp().preset_bundle)
        return false;
    auto& pb = *wxGetApp().preset_bundle;

    // Suggest filaments of the family apply_mix_from_existing_filaments() mixes from,
    // so adding them never moves the mixing pool to another family.
    const std::string family = preferred_mix_family();
    int family_src_idx = -1;
    int physical_src_idx = -1;
    std::set<std::string> family_colors;
    std::vector<std::string> used_colors;
    for (const auto& entry : m_filament_entries) {
        if (entry.kind != TextureFilamentKind::ExistingPhysical || entry.project_config_index >= pb.filament_presets.size())
            continue;
        const std::string color = texture_normalize_color_hex(entry.color_hex);
        used_colors.push_back(color);
        physical_src_idx = (int)entry.project_config_index;
        if (texture_entry_family_type(entry, m_filament_entries) != family)
            continue;
        family_colors.insert(color);
        family_src_idx = (int)entry.project_config_index;
    }
    // New slots copy the nozzle diameter / flow and, without an official preset, the
    // preset of the last filament of the family.
    const int inherit_idx = family_src_idx >= 0 ? family_src_idx : physical_src_idx;
    const std::string fallback_preset = inherit_idx >= 0 ? pb.filament_presets[inherit_idx] : std::string();

    std::string basic_type;
    if (family == kDecomposePlaShortType)
        basic_type = kDecomposePlaBasicType;
    else if (family == kDecomposePetgShortType)
        basic_type = kDecomposePetgBasicType;
    std::string basic_preset;
    if (!basic_type.empty()) {
        ensure_official_series_installed(basic_type);
        basic_preset = find_decompose_standard_preset_name(inherit_idx >= 0 ? (size_t)inherit_idx : 0, basic_type);
    }
    // Other families have no official series; their color codes are still looked up by type.
    const std::string lookup_type = basic_type.empty() ? family : basic_type;

    // Red / yellow / blue exist for most materials in the color-code table, cyan and magenta only for PLA.
    static const DecomposeBaseColor kRybBases[] = {
        DecomposeBaseColor::Red,
        DecomposeBaseColor::Yellow,
        DecomposeBaseColor::Blue
    };
    static const wxColour kRybFallbacks[] = {
        wxColour(255, 0, 0),
        wxColour(255, 255, 0),
        wxColour(0, 0, 255)
    };

    std::vector<DecomposeOfficialComponent> officials;
    std::vector<MixSupplementFilamentDialog::Row> rows;
    for (size_t i = 0; i < 3; ++i) {
        const DecomposeOfficialComponent official = lookup_decompose_official_component(
            lookup_type, kRybBases[i], kRybFallbacks[i]);
        if (family_colors.count(texture_normalize_color_hex(official.color_hex)) > 0)
            continue;
        MixSupplementFilamentDialog::Row row;
        row.preset_name = basic_preset.empty() ? fallback_preset : basic_preset;
        row.color_hex = official.color_hex;
        // Without an official Basic preset the row is not an official component.
        row.tag = basic_preset.empty() ? -1 : (int)officials.size();
        officials.push_back(official);
        rows.push_back(std::move(row));
    }

    const size_t pending_physical = pending_physical_filament_count();
    const size_t used = pb.physical_filament_count() + pending_physical;
    const size_t total = pb.filament_presets.size() + pending_physical;
    if (used >= max_physical_filament_count() || total >= MAX_FILAMENT_COUNT)
        return false;
    const size_t capacity = std::min(max_physical_filament_count() - used, MAX_FILAMENT_COUNT - total);
    MixSupplementFilamentDialog dlg(this, rows, capacity, fallback_preset, used_colors, !family_colors.empty());
    if (dlg.ShowModal() != wxID_OK)
        return false;
    const std::vector<MixSupplementFilamentDialog::Row> result = dlg.get_rows();
    if (result.empty())
        return false;

    std::vector<std::pair<wxColour, std::string>> to_add;
    to_add.reserve(result.size());
    for (const auto& row : result) {
        wxColour col(row.color_hex);
        if (!col.IsOk())
            col = *wxWHITE;
        to_add.emplace_back(col, row.preset_name);
    }

    wxBusyCursor wait;
    Sidebar& sidebar = wxGetApp().plater()->sidebar();
    const size_t insert_pos = sidebar.add_custom_filaments(to_add, inherit_idx);
    if (insert_pos == size_t(-1))
        return false;
    const size_t added = std::min(to_add.size(), sidebar.combos_filament().size() - insert_pos);
    for (size_t i = 0; i < added; ++i) {
        const int tag = result[i].tag;
        if (tag >= 0 && tag < (int)officials.size())
            set_created_standard_component_metadata(insert_pos + i, officials[tag]);
    }

    reload_existing_filaments_from_project();
    return true;
}

std::string TextureImportDialog::preferred_mix_family() const
{
    // Same family vote as auto-match, so enabling mixing never switches to a family
    // the project has no filaments of.
    return pick_auto_match_filament_type(m_filament_entries, m_existing_filament_count);
}

bool TextureImportDialog::has_complete_pla_palette_cmykw() const
{
    return find_complete_official_mixing_kit(collect_official_mixing_kit_groups()) != nullptr;
}

struct TextureMixPool {
    bool                                                kit_complete = false;
    std::vector<int>                                    kit_dialog_indices;
    std::string                                         family;
    std::vector<int>                                    allowed_dialog_indices;
    std::vector<Slic3r::ColorDecomposePhysicalFilament> family_filaments;
};

static int find_texture_mixed_by_recipe(const std::vector<TextureFilamentEntry>& entries,
                                        const std::vector<int>& component_indices,
                                        const std::vector<int>& ratios)
{
    for (const auto& entry : entries) {
        if (!texture_entry_is_mixed(entry.kind) || entry.mixed_components.size() != component_indices.size() ||
            entry.mixed_ratios.size() != ratios.size())
            continue;
        bool same = true;
        for (size_t i = 0; i < component_indices.size(); ++i) {
            if (entry.mixed_components[i] != (unsigned int)(component_indices[i] + 1) ||
                entry.mixed_ratios[i] != ratios[i]) {
                same = false;
                break;
            }
        }
        if (same)
            return entry.dialog_index;
    }
    return -1;
}

void TextureImportDialog::build_mix_pool(TextureMixPool& pool) const
{
    const std::vector<OfficialMixingKitGroup> kit_groups = collect_official_mixing_kit_groups();
    const OfficialMixingKitGroup* kit_group = find_complete_official_mixing_kit(kit_groups);
    pool.kit_complete = kit_group != nullptr;
    pool.kit_dialog_indices.clear();
    if (kit_group) {
        for (int cfg_idx : kit_group->config_indices) {
            const auto entry = std::find_if(m_filament_entries.begin(), m_filament_entries.end(),
                [cfg_idx](const TextureFilamentEntry& e) {
                    return e.kind == TextureFilamentKind::ExistingPhysical && e.project_config_index == size_t(cfg_idx);
                });
            if (entry == m_filament_entries.end()) {
                pool.kit_complete = false;
                pool.kit_dialog_indices.clear();
                break;
            }
            pool.kit_dialog_indices.push_back(entry->dialog_index);
        }
    }
    pool.family = pool.kit_complete ? std::string(kDecomposePlaShortType) : preferred_mix_family();
    pool.allowed_dialog_indices.clear();
    pool.family_filaments.clear();
    for (const auto& entry : m_filament_entries) {
        if (entry.kind != TextureFilamentKind::ExistingPhysical)
            continue;
        if (pool.kit_complete) {
            if (std::find(pool.kit_dialog_indices.begin(), pool.kit_dialog_indices.end(), entry.dialog_index)
                == pool.kit_dialog_indices.end())
                continue;
        } else if (texture_entry_family_type(entry, m_filament_entries) != pool.family) {
            continue;
        }
        pool.allowed_dialog_indices.push_back(entry.dialog_index);
        Slic3r::ColorDecomposePhysicalFilament filament;
        filament.color_hex = entry.color_hex;
        filament.name = entry.name;
        filament.type = texture_entry_material_type(entry);
        filament.filament_index = (unsigned int)(entry.dialog_index + 1);
        pool.family_filaments.push_back(std::move(filament));
    }
}

int TextureImportDialog::resolve_mix_color(const TextureMixPool& pool,
                                           const std::array<std::size_t, 3>& color,
                                           bool allow_create, bool& created, bool& needs_slot)
{
    created = false;
    needs_slot = false;

    Slic3r::ColorDecomposeRgb target_rgb;
    target_rgb.r = (unsigned char)color[0];
    target_rgb.g = (unsigned char)color[1];
    target_rgb.b = (unsigned char)color[2];

    Slic3r::ColorDecomposeRecipeResult recipe;
    if (pool.kit_complete)
        recipe = Slic3r::lookup_standard_recipe(target_rgb, Slic3r::ColorDecomposeRecipeMode::CMYKW,
                                                kDecomposePlaPaletteType);
    else
        recipe = Slic3r::recommend_from_physical_filaments(target_rgb, pool.family_filaments, pool.family);

    std::vector<int> component_dialog_indices;
    std::vector<int> ratios;
    if (recipe.valid) {
        for (const auto& comp : recipe.components) {
            int dialog_idx = -1;
            if (pool.kit_complete) {
                const std::vector<DecomposeOfficialComponent> official_components = pla_palette_cmykw_official_components();
                const std::string color_hex = decompose_normalize_color_hex(comp.color_hex);
                const auto component = std::find_if(official_components.begin(), official_components.end(),
                    [&](const DecomposeOfficialComponent& official) { return official.color_hex == color_hex; });
                const size_t component_idx = size_t(component - official_components.begin());
                if (component != official_components.end() && component_idx < pool.kit_dialog_indices.size())
                    dialog_idx = pool.kit_dialog_indices[component_idx];
            } else if (comp.filament_index >= 1) {
                dialog_idx = (int)comp.filament_index - 1;
                if (dialog_idx < 0 || dialog_idx >= (int)m_filament_entries.size() ||
                    m_filament_entries[dialog_idx].kind != TextureFilamentKind::ExistingPhysical) {
                    dialog_idx = -1;
                }
            }
            if (dialog_idx < 0) {
                component_dialog_indices.clear();
                break;
            }
            component_dialog_indices.push_back(dialog_idx);
            ratios.push_back(comp.ratio);
        }
    }

    if (component_dialog_indices.size() >= 2 && component_dialog_indices.size() == ratios.size()) {
        int mixed_idx = find_texture_mixed_by_recipe(m_filament_entries, component_dialog_indices, ratios);
        if (mixed_idx < 0) {
            if (!allow_create) {
                needs_slot = true;
                return -1;
            }
            std::string mix_hex = recipe.matched_color_hex;
            if (mix_hex.empty()) {
                std::vector<std::string> hexes;
                hexes.reserve(component_dialog_indices.size());
                for (int idx : component_dialog_indices)
                    hexes.push_back(m_filament_entries[idx].color_hex);
                mix_hex = Slic3r::lookup_measured_blend_color(hexes, ratios);
            }
            if (mix_hex.empty())
                mix_hex = rgb_to_hex(color).ToStdString();
            mixed_idx = add_virtual_mixed_filament(mix_hex, component_dialog_indices, ratios);
            created = mixed_idx >= 0;
        }
        if (mixed_idx >= 0) {
            texture_sync_mixed_filament_name(m_filament_entries, m_filament_names,
                                             mixed_idx, component_dialog_indices);
            return mixed_idx;
        }
    }

    if (component_dialog_indices.size() == 1)
        return component_dialog_indices.front();
    return -1;
}

std::map<std::array<std::size_t, 3>, double> TextureImportDialog::cluster_color_areas() const
{
    std::map<std::array<std::size_t, 3>, double> areas;
    const auto& mesh = m_painted;
    const size_t face_n = std::min(mesh.indices.size(), mesh.face_colors.size());
    for (size_t fi = 0; fi < face_n; ++fi) {
        const auto& idx = mesh.indices[fi];
        if (idx[0] < 0 || idx[1] < 0 || idx[2] < 0 ||
            (size_t)idx[0] >= mesh.vertices.size() ||
            (size_t)idx[1] >= mesh.vertices.size() ||
            (size_t)idx[2] >= mesh.vertices.size())
            continue;
        const auto& a = mesh.vertices[idx[0]];
        const auto& b = mesh.vertices[idx[1]];
        const auto& c = mesh.vertices[idx[2]];
        const double ux = double(b[0]) - a[0], uy = double(b[1]) - a[1], uz = double(b[2]) - a[2];
        const double vx = double(c[0]) - a[0], vy = double(c[1]) - a[1], vz = double(c[2]) - a[2];
        const double nx = uy * vz - uz * vy;
        const double ny = uz * vx - ux * vz;
        const double nz = ux * vy - uy * vx;
        areas[mesh.face_colors[fi]] += 0.5 * std::sqrt(nx * nx + ny * ny + nz * nz);
    }
    return areas;
}

std::vector<size_t> TextureImportDialog::unmatched_match_indices_by_area() const
{
    const auto areas = cluster_color_areas();
    auto area_of = [&areas](const std::array<std::size_t, 3>& color) {
        const auto it = areas.find(color);
        return it != areas.end() ? it->second : 0.0;
    };
    std::vector<std::pair<size_t, double>> unmatched;
    for (size_t i = 0; i < m_current_matches.size(); ++i) {
        if (m_current_matches[i].filament_index < 0)
            unmatched.emplace_back(i, area_of(m_current_matches[i].cluster_color));
    }
    std::stable_sort(unmatched.begin(), unmatched.end(),
                     [](const auto& lhs, const auto& rhs) { return lhs.second > rhs.second; });
    std::vector<size_t> indices;
    indices.reserve(unmatched.size());
    for (const auto& item : unmatched)
        indices.push_back(item.first);
    return indices;
}

size_t TextureImportDialog::remaining_physical_filament_capacity()
{
    compact_used_virtual_filaments();
    const size_t physical = physical_filament_entry_count();
    const size_t total = m_filament_entries.size();
    if (physical >= MAX_PHYSICAL_FILAMENT_COUNT || total >= MAX_FILAMENT_COUNT)
        return 0;
    return std::min(MAX_PHYSICAL_FILAMENT_COUNT - physical, MAX_FILAMENT_COUNT - total);
}

size_t TextureImportDialog::remaining_mixed_filament_capacity()
{
    compact_used_virtual_filaments();
    const size_t mixed = mixed_filament_entry_count();
    const size_t total = m_filament_entries.size();
    if (mixed >= MAX_MIXED_FILAMENT_COUNT || total >= MAX_FILAMENT_COUNT)
        return 0;
    return std::min(MAX_MIXED_FILAMENT_COUNT - mixed, MAX_FILAMENT_COUNT - total);
}

int TextureImportDialog::closest_filament_ignoring_threshold(const std::array<std::size_t, 3>& color,
                                                             const std::string& family_type) const
{
    auto search = [&](bool same_family_only) -> int {
        int best_idx = -1;
        double best_delta = std::numeric_limits<double>::max();
        const size_t filament_count = std::min(m_filament_entries.size(), m_filament_colors_rgba.size());
        for (size_t i = 0; i < filament_count; ++i) {
            const TextureFilamentKind kind = m_filament_entries[i].kind;
            if (texture_entry_is_mixed(kind) ? !m_mix_enabled : !texture_entry_is_physical(kind))
                continue;
            if (same_family_only) {
                const std::string ft = texture_entry_family_type(m_filament_entries[i], m_filament_entries);
                if (!ft.empty() && ft != family_type)
                    continue;
            }
            const double delta = Slic3r::compute_delta_e(color, m_filament_colors_rgba[i]);
            if (delta < best_delta) {
                best_delta = delta;
                best_idx = (int)i;
            }
        }
        return best_idx;
    };

    const int idx = search(true);
    return idx >= 0 ? idx : search(false);
}

void TextureImportDialog::apply_mix_from_existing_filaments()
{
    if (m_painted.cluster_colors.empty())
        return;
    // Re-entrancy gate: the loop below mutates m_current_matches and
    // m_filament_entries. Toggling Color mixing or pressing Reset while it runs
    // would rebuild both from under it.
    if (m_mix_applying)
        return;
    m_mix_applying = true;
    Slic3r::ScopeGuard mix_guard([this]() { m_mix_applying = false; });

    bool rebuilt_matches = false;
    if (m_current_matches.size() != m_painted.cluster_colors.size()) {
        m_current_matches.clear();
        m_current_matches.reserve(m_painted.cluster_colors.size());
        for (size_t i = 0; i < m_painted.cluster_colors.size(); ++i) {
            Slic3r::FilamentMatch fm;
            fm.cluster_index = (int)i;
            fm.cluster_color = m_painted.cluster_colors[i];
            fm.filament_index = -1;
            m_current_matches.push_back(fm);
        }
        rebuilt_matches = true;
    }

    // Compact first: the pool and the dialog indices below must not shift afterwards.
    size_t capacity = remaining_mixed_filament_capacity();

    TextureMixPool pool;
    build_mix_pool(pool);

    auto closest_allowed = [this, &pool](const std::array<std::size_t, 3>& color) -> int {
        int best_idx = -1;
        double best_delta = 1e9;
        for (int idx : pool.allowed_dialog_indices) {
            if (idx < 0 || idx >= (int)m_filament_colors_rgba.size())
                continue;
            const double delta = Slic3r::compute_delta_e(color, m_filament_colors_rgba[idx]);
            if (delta < best_delta) {
                best_delta = delta;
                best_idx = idx;
            }
        }
        return best_idx;
    };

    // Larger colors get the free slots for new mixed filaments first. Colors whose
    // recipe needs a slot after they run out take the closest filament once every
    // new mixed slot is placed, so they can reuse those.
    std::vector<size_t> deferred;
    for (size_t match_idx : unmatched_match_indices_by_area()) {
        // Already-matched rows stay as the user left them.
        if (match_idx >= m_current_matches.size() || m_current_matches[match_idx].filament_index >= 0)
            continue;
        const std::array<std::size_t, 3> cluster_color = m_current_matches[match_idx].cluster_color;

        bool created = false;
        bool needs_slot = false;
        int bind_idx = resolve_mix_color(pool, cluster_color, capacity > 0, created, needs_slot);
        if (created && capacity > 0)
            --capacity;
        if (needs_slot) {
            deferred.push_back(match_idx);
            continue;
        }
        if (bind_idx < 0)
            bind_idx = closest_allowed(cluster_color);

        if (match_idx >= m_current_matches.size())
            break;
        bind_match_inplace(m_current_matches[match_idx], bind_idx);
    }
    for (size_t match_idx : deferred) {
        if (match_idx >= m_current_matches.size())
            continue;
        Slic3r::FilamentMatch& m = m_current_matches[match_idx];
        bind_match_inplace(m, closest_filament_ignoring_threshold(m.cluster_color, pool.family));
    }

    drop_unused_new_filaments_and_refresh();
    if (rebuilt_matches || m_mapping_rows.size() != m_current_matches.size())
        rebuild_mapping_rows();
}

void TextureImportDialog::reset_auto_mix()
{
    if (m_state != TextureImportState::Ready)
        return;
    // See apply_mix_from_existing_filaments(): both rebuild m_current_matches.
    if (m_mix_applying)
        return;
    m_mix_applying = true;
    Slic3r::ScopeGuard mix_guard([this]() { m_mix_applying = false; });

    const std::string match_type = pick_auto_match_filament_type(m_filament_entries, m_existing_filament_count);
    const bool had_rows = !m_mapping_rows.empty();

    std::vector<int> candidates;
    std::vector<int> any_physical;
    const size_t filament_count = std::min(m_filament_entries.size(), m_filament_colors_rgba.size());
    candidates.reserve(filament_count);
    any_physical.reserve(filament_count);
    for (size_t i = 0; i < filament_count; ++i) {
        if (!texture_entry_is_physical(m_filament_entries[i].kind))
            continue;
        any_physical.push_back((int)i);
        const std::string ft = texture_entry_family_type(m_filament_entries[i], m_filament_entries);
        if (ft == match_type)
            candidates.push_back((int)i);
    }
    if (candidates.empty())
        candidates = std::move(any_physical);

    for (size_t match_idx = 0; match_idx < m_current_matches.size(); ++match_idx) {
        Slic3r::FilamentMatch& m = m_current_matches[match_idx];
        if (m.filament_index < 0 || m.filament_index >= (int)m_filament_entries.size())
            continue;
        if (!texture_entry_is_mixed(m_filament_entries[m.filament_index].kind))
            continue;
        bind_match_inplace(m, select_filament_for_color(m.cluster_color, candidates));
    }

    drop_unused_new_filaments_and_refresh();
    if (!had_rows)
        rebuild_mapping_rows();
}

void TextureImportDialog::dismiss_filament_popup_on_wheel(wxMouseEvent& evt)
{
    dismiss_filament_popup();
    evt.Skip();
}

int TextureImportDialog::filament_popup_align_bottom() const
{
    auto bottom_of = [](wxWindow* w) -> int {
        if (!w || !w->IsShown())
            return 0;
        const wxRect r = w->GetScreenRect();
        return r.GetY() + r.GetHeight();
    };
    int y = bottom_of(m_unmatched_warning);
    if (y > 0)
        return y;
    wxWindow* footer_btn = nullptr;
    if (m_btn_ok && m_btn_ok->IsShown())
        footer_btn = m_btn_ok;
    else if (m_btn_reset && m_btn_reset->IsShown())
        footer_btn = m_btn_reset;
    else if (m_btn_next && m_btn_next->IsShown())
        footer_btn = m_btn_next;
    if (footer_btn)
        return bottom_of(footer_btn);
    const wxRect dlg = GetScreenRect();
    return dlg.GetY() + dlg.GetHeight() - FromDIP(16);
}

void TextureImportDialog::show_filament_popup(size_t row_index)
{
    if (row_index >= m_mapping_rows.size()) return;

    hide_match_threshold_popup();

    if (m_filament_popup) {
        // Opening is idempotent. Closing the current row is decided by the
        // anchor gesture, not by a second call racing this one.
        if (m_filament_popup->IsShown() && m_filament_popup_row == (int)row_index)
            return;
        dismiss_filament_popup();
    }

    const auto display_numbers = compute_display_numbers();

    auto on_select = [this, row_index](int idx) {
        if (row_index >= m_mapping_rows.size()) return;
        m_mapping_rows[row_index].target_filament_idx = idx;
        if (row_index < m_current_matches.size())
            m_current_matches[row_index].filament_index = idx;
        drop_unused_new_filaments_and_refresh();
    };

    auto on_add_filament = [this, row_index]() {
        // Dismiss() already queued the popup's Destroy on this turn. Opening the
        // modal here would start a nested loop before that Destroy (and the macOS
        // orderOut) runs, so the filament list stays on screen until the dialog ends.
        CallAfter([this, row_index]() {
            if (IsBeingDeleted())
                return;
            if (physical_filament_entry_count() >= MAX_PHYSICAL_FILAMENT_COUNT ||
                m_filament_entries.size() >= MAX_FILAMENT_COUNT)
                return;

            wxWindow* color_anchor = nullptr;
            if (row_index < m_mapping_rows.size())
                color_anchor = m_mapping_rows[row_index].target_panel;
            if (!color_anchor)
                color_anchor = this;

            int next_physical_display = 1;
            for (const auto& entry : m_filament_entries) {
                if (entry.kind == TextureFilamentKind::ExistingPhysical ||
                    entry.kind == TextureFilamentKind::NewPhysical)
                    ++next_physical_display;
            }

            TextureImportAddFilamentDialog dlg(this, m_default_virtual_filament_preset_name, next_physical_display);
            auto move_dialog = [&dlg, color_anchor]() {
                dlg.Move(constrained_dialog_position(color_anchor, dlg.GetBestSize()));
            };
            dlg.Bind(wxEVT_SHOW, [move_dialog](wxShowEvent& e) {
                e.Skip();
                if (e.IsShown())
                    move_dialog();
            });
            move_dialog();
            if (dlg.ShowModal() != wxID_OK)
                return;

            wxColour clr = dlg.GetColour();
            if (!clr.IsOk())
                return;
            std::array<float, 4> rgba = {clr.Red() / 255.f, clr.Green() / 255.f,
                                         clr.Blue() / 255.f, 1.0f};
            std::string hex = wxString::Format("#%02X%02X%02X",
                clr.Red(), clr.Green(), clr.Blue()).ToStdString();
            int new_idx = add_virtual_filament(rgba, hex, dlg.GetPresetName());
            if (new_idx < 0)
                return;

            if (row_index < m_mapping_rows.size()) {
                m_mapping_rows[row_index].target_filament_idx = new_idx;
                if (row_index < m_current_matches.size())
                    bind_match_inplace(m_current_matches[row_index], new_idx);
            }
            drop_unused_new_filaments_and_refresh();
        });
    };

    auto on_delete_filament = [this](int dialog_index) {
        CallAfter([this, dialog_index]() {
            if (IsBeingDeleted())
                return;
            if (!remove_virtual_filament(dialog_index)) {
                wxBell();
                return;
            }
            drop_unused_new_filaments_and_refresh();
        });
    };

    wxPanel* tp = m_mapping_rows[row_index].target_panel;
    if (!tp) return;

    auto on_close = [this, row_index](FilamentSelectPopup* popup) {
        if (IsBeingDeleted())
            return;
        m_last_filament_popup_dismiss_row = (int)row_index;
        m_filament_popup_dismiss_time = std::chrono::steady_clock::now();
        if (m_filament_popup == popup) {
            m_filament_popup = nullptr;
            m_filament_popup_row = -1;
        }
    };

    auto* popup = new FilamentSelectPopup(
        this, m_filament_entries, m_filament_colors_rgba, m_filament_names,
        m_existing_filament_count, tp->GetSize().x, tp, on_select, on_add_filament,
        on_delete_filament,
        on_close,
        display_numbers);

    wxPoint pos = texture_import_side_popup_position(this, filament_popup_align_bottom(), popup->GetSize());
    popup->Position(pos, wxSize(0, 0));
    popup->Bind(wxEVT_SHOW, [this, popup](wxShowEvent& e) {
        e.Skip();
        if (!e.IsShown() || !popup)
            return;
        popup->Move(texture_import_side_popup_position(this, filament_popup_align_bottom(), popup->GetSize()));
    });
    popup->Bind(wxEVT_DESTROY, [this, popup](wxWindowDestroyEvent& e) {
        e.Skip();
        // Destroy events bubble up from child rows rebuilt in place.
        if (e.GetEventObject() != popup)
            return;
        if (m_filament_popup == popup) {
            m_filament_popup = nullptr;
            m_filament_popup_row = -1;
        }
    });
    m_filament_popup = popup;
    m_filament_popup_row = (int)row_index;
    popup->Popup(popup);
}

void TextureImportDialog::drop_virtual_filaments_keep_project()
{
    m_current_matches.clear();
    if (m_filament_colors_rgba.size() > m_existing_filament_count)
        m_filament_colors_rgba.resize(m_existing_filament_count);
    if (m_filament_color_strs.size() > m_existing_filament_count)
        m_filament_color_strs.resize(m_existing_filament_count);
    if (m_filament_names.size() > m_existing_filament_count)
        m_filament_names.resize(m_existing_filament_count);
    if (m_filament_entries.size() > m_existing_filament_count)
        m_filament_entries.resize(m_existing_filament_count);
    while (m_filament_entries.size() < m_existing_filament_count) {
        TextureFilamentEntry entry;
        entry.kind = TextureFilamentKind::ExistingPhysical;
        entry.dialog_index = (int)m_filament_entries.size();
        entry.project_config_index = m_filament_entries.size();
        m_filament_entries.push_back(entry);
    }
    for (size_t i = 0; i < m_filament_entries.size(); ++i) {
        m_filament_entries[i].dialog_index = (int)i;
        m_filament_entries[i].color_hex = i < m_filament_color_strs.size() ?
            texture_normalize_color_hex(m_filament_color_strs[i]) : "#808080";
        m_filament_entries[i].name = i < m_filament_names.size() ?
            m_filament_names[i] : default_filament_stored_name((int)i + 1);
    }
    m_new_filament_colors.clear();
    m_new_filament_preset_names.clear();
    m_new_mixed_filaments.clear();
}

void TextureImportDialog::reset_to_project_filaments_and_auto_match()
{
    drop_virtual_filaments_keep_project();
    do_auto_match();
    compact_used_virtual_filaments();
    sort_current_matches_by_filament_index();
    update_filament_color_map();
    if (m_mapping_scroll)
        rebuild_mapping_rows();
}

void TextureImportDialog::match_clusters_to_physical_filaments()
{
    if (m_painted.cluster_colors.empty())
        return;

    const std::string match_type = pick_auto_match_filament_type(m_filament_entries, m_existing_filament_count);

    auto collect = [&](bool same_type_only) {
        std::vector<int> indices;
        const size_t existing_n = std::min(m_existing_filament_count, m_filament_colors_rgba.size());
        indices.reserve(existing_n);
        for (size_t i = 0; i < existing_n && i < m_filament_entries.size(); ++i) {
            // Never auto-bind mixed slots; Reset / auto-match only pair physicals.
            if (texture_entry_is_mixed(m_filament_entries[i].kind))
                continue;
            if (same_type_only) {
                const std::string ft = texture_entry_family_type(m_filament_entries[i], m_filament_entries);
                if (!ft.empty() && ft != match_type)
                    continue;
            }
            indices.push_back((int)i);
        }
        return indices;
    };

    std::vector<int> candidates = collect(true);
    if (candidates.empty())
        candidates = collect(false);

    m_current_matches.clear();
    m_current_matches.reserve(m_painted.cluster_colors.size());
    for (size_t ci = 0; ci < m_painted.cluster_colors.size(); ++ci) {
        Slic3r::FilamentMatch match;
        match.cluster_index = (int)ci;
        match.cluster_color = m_painted.cluster_colors[ci];
        const int bind = select_filament_for_color(match.cluster_color, candidates);
        if (bind >= 0 && bind < (int)m_filament_colors_rgba.size()) {
            match.filament_index = bind;
            match.filament_color = m_filament_colors_rgba[bind];
            match.delta_e = Slic3r::compute_delta_e(match.cluster_color, match.filament_color);
        }
        m_current_matches.push_back(match);
    }
}

void TextureImportDialog::do_auto_match()
{
    if (m_painted.cluster_colors.empty()) return;

    // Drop leftover virtual filaments that the current mapping no longer
    // references, so they do not inflate the slot count for a later add.
    compact_used_virtual_filaments();

    match_clusters_to_physical_filaments();

    update_filament_color_map();
}

void TextureImportDialog::rebuild_mapping_rows()
{
    if (!m_mapping_scroll || !m_mapping_sizer)
        return;

    dismiss_filament_popup();
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

    auto display_number = [this](int idx) -> int {
        const auto nums = compute_display_numbers();
        return (idx >= 0 && idx < (int)nums.size() && nums[idx] > 0) ? nums[idx] : idx + 1;
    };

    auto get_filament_label = [this, display_number](int idx) -> wxString {
        return texture_filament_label_wx(m_filament_entries, m_filament_names, idx, display_number(idx));
    };

    const wxColour dash_clr   = dark_or(wxColour(179, 179, 179), wxColour(100, 100, 106));
    const wxColour card_bg    = dark_or(wxColour(235, 235, 235), wxColour(0x3C, 0x3C, 0x42));
    const wxColour card_bd    = dark_or(wxColour(224, 224, 224), wxColour(0x46, 0x46, 0x4C));
    const wxColour name_fg    = texture_import_text_colour();
    const wxColour chev_clr   = dark_or(wxColour(107, 107, 107), wxColour(0xB3, 0xB3, 0xB5));
    const wxColour unmatched_bd = wxColour(225, 71, 71);
    const wxColour unmatched_bg = dark_or(wxColour(248, 248, 248), wxColour(0x2D, 0x2D, 0x31));
    const wxColour unmatched_fg = dark_or(wxColour(0x5C, 0x5C, 0x5C), wxColour(0xB3, 0xB3, 0xB5));
    const wxString unmatched_label = _L("Click to select a matching filament");

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
        row.source_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);

        row.source_panel->Bind(wxEVT_PAINT, [this, ci, src_wx_color, dash_clr](wxPaintEvent& e) {
            auto* p = static_cast<wxPanel*>(e.GetEventObject());
            texture_import_paint(p, [this, p, ci, src_wx_color, dash_clr](wxDC& dc) {
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
                dc.SetFont(Label::Body_12);
                const wxColour hex_fg = blend_towards(texture_import_text_colour(),
                                                      p->GetParent()->GetBackgroundColour(), 0.6);
                dc.SetTextForeground(hex_fg);
                wxString hex_str = wxString::Format("# %s", m_mapping_rows[ci].source_hex.substr(1));
                wxSize tsz = dc.GetTextExtent(hex_str);
                dc.DrawText(hex_str, cx + cd + p->FromDIP(6), (sz.y - tsz.y) / 2);
            }
            });
        });
        row.source_panel->Bind(wxEVT_SIZE, [](wxSizeEvent& e) {
            e.Skip();
            static_cast<wxWindow*>(e.GetEventObject())->Refresh();
        });
        row.source_panel->Bind(wxEVT_MOUSEWHEEL, &TextureImportDialog::dismiss_filament_popup_on_wheel, this);

        row_sizer->Add(row.source_panel, 1, wxEXPAND);

        // --- Arrow panel (dashed arrow) ---
        const int arrow_w = FromDIP(24);
        wxPanel* arrow_panel = new wxPanel(row_panel, wxID_ANY, wxDefaultPosition, wxSize(arrow_w, row_h));
        arrow_panel->SetMinSize(wxSize(arrow_w, row_h));
        arrow_panel->SetMaxSize(wxSize(arrow_w, row_h));
        arrow_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
        arrow_panel->Bind(wxEVT_MOUSEWHEEL, &TextureImportDialog::dismiss_filament_popup_on_wheel, this);
        arrow_panel->Bind(wxEVT_PAINT, [dash_clr](wxPaintEvent& e) {
            auto* p = static_cast<wxPanel*>(e.GetEventObject());
            texture_import_paint(p, [p, dash_clr](wxDC& dc) {
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
        });

        row_sizer->Add(arrow_panel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(4));

        // --- Target card (numbered square + material name + chevron) ---
        row.target_panel = new wxPanel(row_panel, wxID_ANY, wxDefaultPosition, wxSize(-1, row_h),
                                       wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE);
        row.target_panel->SetMinSize(wxSize(target_min_w, row_h));
        if (row.target_filament_idx < 0)
            row.target_panel->SetToolTip(wxString());
        else
            row.target_panel->SetToolTip(get_filament_label(row.target_filament_idx));
        row.target_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
        row.target_panel->SetCursor(wxCursor(wxCURSOR_HAND));

        row.target_panel->Bind(wxEVT_PAINT, [this, ci, get_target_wxcolor, get_filament_label,
                                              display_number, card_bg, card_bd, name_fg, chev_clr,
                                              unmatched_bd, unmatched_bg, unmatched_fg,
                                              unmatched_label](wxPaintEvent& e) {
            auto* p = static_cast<wxPanel*>(e.GetEventObject());
            texture_import_paint(p, [this, p, ci, get_target_wxcolor, get_filament_label,
                                     display_number, card_bg, card_bd, name_fg, chev_clr,
                                     unmatched_bd, unmatched_bg, unmatched_fg,
                                     unmatched_label](wxDC& dc) {
            wxSize sz = p->GetClientSize();

            dc.SetBrush(wxBrush(p->GetParent()->GetBackgroundColour()));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, sz.x, sz.y);

            if (ci >= m_mapping_rows.size()) return;
            int fil_idx = m_mapping_rows[ci].target_filament_idx;

            int r = p->FromDIP(8);
            auto draw_chevron = [&]() {
                int chev_cx = sz.x - p->FromDIP(14);
                int chev_cy = sz.y / 2;
                int hw = p->FromDIP(3);
                int hh = p->FromDIP(2);
                dc.SetPen(wxPen(chev_clr, p->FromDIP(1) > 0 ? p->FromDIP(1) : 1));
                dc.DrawLine(chev_cx - hw, chev_cy - hh, chev_cx, chev_cy + hh);
                dc.DrawLine(chev_cx, chev_cy + hh, chev_cx + hw, chev_cy - hh);
            };

            if (fil_idx < 0) {
                dc.SetBrush(wxBrush(unmatched_bg));
                dc.SetPen(wxPen(unmatched_bd, 1));
                dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, r);

                const wxSize icon_sz = m_bmp_unmatched.GetBmpSize();
                int icon_x = p->FromDIP(10);
                int icon_y = (sz.y - icon_sz.y) / 2;
                if (m_bmp_unmatched.bmp().IsOk())
                    dc.DrawBitmap(m_bmp_unmatched.bmp(), icon_x, icon_y);

                dc.SetFont(Label::Body_12);
                dc.SetTextForeground(unmatched_fg);
                wxSize tsz = dc.GetTextExtent(unmatched_label);
                int text_x = icon_x + icon_sz.x + p->FromDIP(6);
                int max_text_w = sz.x - text_x - p->FromDIP(24);
                wxString hint = unmatched_label;
                if (max_text_w > 0)
                    hint = ellipsize_text(dc, hint, max_text_w);
                dc.DrawText(hint, text_x, (sz.y - tsz.y) / 2);

                draw_chevron();
                return;
            }

            dc.SetBrush(wxBrush(card_bg));
            dc.SetPen(wxPen(card_bd, 1));
            dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, r);

            if (fil_idx >= 0 && fil_idx < (int)m_filament_entries.size() &&
                texture_entry_is_mixed(m_filament_entries[fil_idx].kind)) {
                const TextureFilamentEntry& entry = m_filament_entries[fil_idx];
                dc.SetFont(Label::Body_12);

                int x = p->FromDIP(10);
                const int sw = p->FromDIP(28);
                const int sw_r = p->FromDIP(6);
                const int sw_y = (sz.y - sw) / 2;
                {
                    wxColour self_clr("#D9D9D9");
                    if (fil_idx < (int)m_filament_colors_rgba.size()) {
                        const auto& c = m_filament_colors_rgba[fil_idx];
                        self_clr = wxColour((unsigned char)(c[0] * 255.f),
                                            (unsigned char)(c[1] * 255.f),
                                            (unsigned char)(c[2] * 255.f));
                    }
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.SetBrush(wxBrush(self_clr));
                    dc.DrawRoundedRectangle(x, sw_y, sw, sw, sw_r);
                    draw_filament_swatch_border(dc, self_clr, x, sw_y, sw, sw, sw_r);

                    wxString num_str = wxString::Format("%d", display_number(fil_idx));
                    wxSize nsz = dc.GetTextExtent(num_str);
                    dc.SetTextForeground(self_clr.GetLuminance() < 0.6 ? *wxWHITE : texture_import_gray9000());
                    dc.DrawText(num_str, x + (sw - nsz.x) / 2, sw_y + (sw - nsz.y) / 2);
                    x += sw + p->FromDIP(5);

                    dc.SetTextForeground(name_fg);
                    wxString eq = "=";
                    wxSize esz = dc.GetTextExtent(eq);
                    dc.DrawText(eq, x, (sz.y - esz.y) / 2);
                    x += esz.x + p->FromDIP(5);
                }
                for (size_t mi = 0; mi < entry.mixed_components.size() && mi < entry.mixed_ratios.size(); ++mi) {
                    if (mi > 0) {
                        dc.SetTextForeground(name_fg);
                        wxString plus = "+";
                        wxSize psz = dc.GetTextExtent(plus);
                        dc.DrawText(plus, x, (sz.y - psz.y) / 2);
                        x += psz.x + p->FromDIP(4);
                    }

                    const unsigned int comp_id = entry.mixed_components[mi];
                    const int comp_idx = comp_id >= 1 ? (int)comp_id - 1 : -1;
                    wxColour comp_clr("#D9D9D9");
                    if (comp_idx >= 0 && comp_idx < (int)m_filament_colors_rgba.size()) {
                        const auto& c = m_filament_colors_rgba[comp_idx];
                        comp_clr = wxColour((unsigned char)(c[0] * 255.f),
                                            (unsigned char)(c[1] * 255.f),
                                            (unsigned char)(c[2] * 255.f));
                    }

                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.SetBrush(wxBrush(comp_clr));
                    dc.DrawRoundedRectangle(x, sw_y, sw, sw, sw_r);
                    draw_filament_swatch_border(dc, comp_clr, x, sw_y, sw, sw, sw_r);

                    wxString num_str = wxString::Format("%d", display_number(comp_idx));
                    wxSize nsz = dc.GetTextExtent(num_str);
                    dc.SetTextForeground(comp_clr.GetLuminance() < 0.6 ? *wxWHITE : texture_import_gray9000());
                    dc.DrawText(num_str, x + (sw - nsz.x) / 2, sw_y + (sw - nsz.y) / 2);
                    x += sw + p->FromDIP(5);

                    dc.SetTextForeground(name_fg);
                    wxString pct = wxString::Format("%d%%", entry.mixed_ratios[mi]);
                    wxSize pct_sz = dc.GetTextExtent(pct);
                    dc.DrawText(pct, x, (sz.y - pct_sz.y) / 2);
                    x += pct_sz.x + p->FromDIP(5);
                    if (x > sz.x - p->FromDIP(34))
                        break;
                }

                int chev_cx = sz.x - p->FromDIP(14);
                int chev_cy = sz.y / 2;
                int hw = p->FromDIP(3);
                int hh = p->FromDIP(2);
                dc.SetPen(wxPen(chev_clr, p->FromDIP(1) > 0 ? p->FromDIP(1) : 1));
                dc.DrawLine(chev_cx - hw, chev_cy - hh, chev_cx, chev_cy + hh);
                dc.DrawLine(chev_cx, chev_cy + hh, chev_cx + hw, chev_cy - hh);
                return;
            }

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
                dc.SetFont(Label::Body_12);
                dc.SetTextForeground(fil_clr.GetLuminance() < 0.6 ? *wxWHITE : texture_import_gray9000());
                wxString num_str = wxString::Format("%d", display_number(fil_idx));
                wxSize nsz = dc.GetTextExtent(num_str);
                dc.DrawText(num_str, sq_x + (sq - nsz.x) / 2, sq_y + (sq - nsz.y) / 2);
            }

            // Brand icon + material name
            {
                dc.SetFont(Label::Body_12);
                dc.SetTextForeground(name_fg);
                wxString name_str = get_filament_label(fil_idx);
                int text_x = draw_brand_icon_and_strip(dc, p, name_str, sq_x + sq + p->FromDIP(8), sz.y / 2, &m_bmp_brand);
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
        });

        row.target_panel->Bind(wxEVT_LEFT_DOWN, [this, ci](wxMouseEvent&) {
            if (m_filament_popup && m_filament_popup_row == (int)ci) {
                dismiss_filament_popup();
                return;
            }

            const auto elapsed = std::chrono::steady_clock::now() - m_filament_popup_dismiss_time;
            if (m_last_filament_popup_dismiss_row == (int)ci &&
                elapsed < std::chrono::milliseconds(20))
                return;
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

    m_mapping_scroll->Thaw();
    layout_mapping_rows();
}

void TextureImportDialog::layout_mapping_rows()
{
    if (!m_mapping_scroll || !m_mapping_sizer || m_mapping_scroll->IsFrozen())
        return;

    static bool s_in_layout = false;
    if (s_in_layout)
        return;
    s_in_layout = true;

    const int client_w = m_mapping_scroll->GetClientSize().GetWidth();
    if (client_w > 0)
        m_mapping_sizer->SetMinSize(wxSize(client_w, -1));
    m_mapping_scroll->Layout();
    m_mapping_scroll->FitInside();

    s_in_layout = false;
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

void TextureImportDialog::set_color_count_value(int value, bool update_spin)
{
    m_param_color_count = std::clamp(value, 1, max_color_count());
    if (m_color_slider)
        m_color_slider->SetValue(m_param_color_count);
    if (update_spin && m_color_spin)
        set_text_input_int(m_color_spin, m_param_color_count);
    set_color_count_exceeded(false);
}

int TextureImportDialog::visible_simplified_color_count() const
{
    if (m_gap_preview && !m_gap_preview->display_face_colors.empty())
        return unique_face_color_count(m_gap_preview->display_face_colors);
    if (m_gap_preview && !m_gap_preview->simplified_face_colors.empty())
        return unique_face_color_count(m_gap_preview->simplified_face_colors);
    return (int)m_painted.cluster_colors.size();
}

void TextureImportDialog::sync_color_count_from_preview()
{
    if (m_wizard_step != TextureImportWizardStep::SimplifyColors)
        return;
    if (m_state == TextureImportState::Computing)
        return;
    const int visible = visible_simplified_color_count();
    if (visible < 1 || visible == m_param_color_count)
        return;
    m_updating_params = true;
    set_color_count_value(visible, true);
    m_updating_params = false;
}

bool TextureImportDialog::can_restore_applied_color_count(int count) const
{
    if (count != m_applied_color_count || m_applied_color_count < 1)
        return false;
    if (m_applied_smooth != m_param_smooth)
        return false;
    if (m_painted.face_colors.empty())
        return false;
    return true;
}

void TextureImportDialog::restore_applied_color_result()
{
    if (m_recompute_timer && m_recompute_timer->IsRunning())
        m_recompute_timer->Stop();
    m_advance_to_matching_when_ready = false;
    const bool in_dialog_compute = m_state == TextureImportState::Computing && !m_current_computation_initial;
    m_updating_params = true;
    set_gap_value(0.0, true);
    m_updating_params = false;
    apply_gap_area();
    if (in_dialog_compute)
        show_last_result_after_cancel();
}

void TextureImportDialog::request_color_count(int count, int delay_ms)
{
    if (m_updating_params)
        return;
    m_auto_preset_selected = false;
    const int clamped = std::clamp(count, 1, max_color_count());
    if (can_restore_applied_color_count(clamped)) {
        set_color_count_value(clamped, true);
        restore_applied_color_result();
        return;
    }
    if (clamped == m_param_color_count) {
        set_color_count_value(clamped, true);
        return;
    }
    set_color_count_value(clamped, true);
    m_updating_params = true;
    set_gap_value(0.0, true);
    m_updating_params = false;
    schedule_recompute(false, delay_ms);
}

void TextureImportDialog::set_smooth_value(int value, bool update_spin)
{
    m_param_smooth = std::clamp(value, 0, 10);
    m_smooth_slider->SetValue(m_param_smooth);
    if (update_spin)
        set_text_input_int(m_smooth_spin, m_param_smooth);
}

void TextureImportDialog::set_gap_value(double value, bool update_spin)
{
    double minv = 0.0;
    double maxv = kGapAreaMaxPercent;
    if (m_gap_slider) {
        minv = m_gap_slider->GetMin();
        maxv = m_gap_slider->GetMax();
    }
    if (maxv < minv)
        maxv = minv;
    value = std::round(std::clamp(value, minv, maxv) * 100.0) / 100.0;
    m_param_gap_area = value;
    if (m_gap_slider)
        m_gap_slider->SetValue(m_param_gap_area);
    if (update_spin)
        set_text_input_double(m_gap_spin, m_param_gap_area);
}

GapPreviewState& TextureImportDialog::gap_preview()
{
    if (!m_gap_preview)
        m_gap_preview = std::make_unique<GapPreviewState>();
    return *m_gap_preview;
}

const GapPreviewState& TextureImportDialog::gap_preview() const
{
    static const GapPreviewState empty;
    return m_gap_preview ? *m_gap_preview : empty;
}

void TextureImportDialog::clear_color_patches()
{
    GapPreviewState& state = gap_preview();
    state.color_patches.clear();
    state.merge_events.clear();
    state.merge_members.clear();
    state.applied_events = 0;
    state.gap_total_area = 0.0;
    state.display_face_colors.clear();
}

int TextureImportDialog::gap_event_count_for_threshold(double area_threshold) const
{
    const GapPreviewState& state = gap_preview();
    auto it = std::lower_bound(state.merge_events.begin(), state.merge_events.end(), area_threshold,
        [](const GapMergeEvent& ev, double threshold) { return ev.key_area < threshold; });
    return (int)std::distance(state.merge_events.begin(), it);
}

void TextureImportDialog::refresh_gap_preview()
{
    const GapPreviewState& state = gap_preview();
    if (m_preview_canvas_right && !state.display_face_colors.empty())
        m_preview_canvas_right->set_face_colors(state.display_face_colors);
}

void TextureImportDialog::apply_gap_area()
{
    GapPreviewState& state = gap_preview();
    if (state.color_patches.empty())
        return;
    if (state.display_face_colors.size() != state.simplified_face_colors.size()) {
        state.display_face_colors = state.simplified_face_colors;
        state.applied_events = 0;
    }
    if (state.display_face_colors.size() != m_painted.indices.size())
        return;

    // Regions whose merged area is below this share of the mesh are absorbed by a neighbor.
    const double area_threshold = (m_param_gap_area / 100.0) * state.gap_total_area;
    const int target = gap_event_count_for_threshold(area_threshold);
    const int npatches = (int)state.color_patches.size();
    auto write_event = [&](const GapMergeEvent& ev, const std::array<std::size_t, 3>& color) {
        for (int m = ev.member_begin; m < ev.member_end; ++m) {
            const int pid = state.merge_members[m];
            if (pid >= 0 && pid < npatches)
                write_patch_display_color(state, state.color_patches[pid], color);
        }
    };

    // Replay forward when the threshold grows and undo in reverse when it shrinks,
    // so any threshold reproduces the same colors regardless of slider history.
    for (int i = state.applied_events; i < target; ++i)
        write_event(state.merge_events[i], state.merge_events[i].to_color);
    for (int i = state.applied_events - 1; i >= target; --i)
        write_event(state.merge_events[i], state.merge_events[i].from_color);
    state.applied_events = target;

    refresh_gap_preview();
    if (m_wizard_step == TextureImportWizardStep::SimplifyColors)
        update_color_captions();
    sync_color_count_from_preview();
}

void TextureImportDialog::install_gap_preview(std::unique_ptr<GapPreviewState> preview)
{
    if (preview)
        m_gap_preview = std::move(preview);
    else if (!m_gap_preview)
        m_gap_preview = std::make_unique<GapPreviewState>();
    GapPreviewState& state = gap_preview();
    if (!state.simplified_face_colors.empty()) {
        state.display_face_colors = state.simplified_face_colors;
        state.applied_events = 0;
    }
    apply_gap_area();
}

void TextureImportDialog::update_color_patches()
{
    GapPreviewState& state = gap_preview();
    if (!state.color_patches.empty() &&
        state.simplified_face_colors.size() == m_painted.indices.size()) {
        apply_gap_area();
        return;
    }
    schedule_color_patches_rebuild();
}

void TextureImportDialog::schedule_color_patches_rebuild()
{
    GapPreviewState& state = gap_preview();
    if (m_state == TextureImportState::Computing || m_patch_building)
        return;
    if (m_painted.indices.empty() ||
        state.simplified_face_colors.size() != m_painted.indices.size())
        return;
    if (!state.color_patches.empty()) {
        apply_gap_area();
        return;
    }

    const int gen = m_patch_generation.fetch_add(1) + 1;
    m_patch_building = true;
    update_ui_for_state();

    if (m_worker.joinable())
        m_worker.join();

    PaintedMesh mesh_copy = m_painted;
    auto simplified_colors = state.simplified_face_colors;
    auto simplified_clusters = state.simplified_cluster_colors;
    wxEvtHandler* handler = this;
    m_worker = create_thread([this, handler, gen,
                              mesh_copy = std::move(mesh_copy),
                              simplified_colors = std::move(simplified_colors),
                              simplified_clusters = std::move(simplified_clusters)]() mutable {
        auto preview = std::make_unique<GapPreviewState>();
        preview->simplified_face_colors = std::move(simplified_colors);
        preview->simplified_cluster_colors = std::move(simplified_clusters);
        preview->display_face_colors = preview->simplified_face_colors;
        build_gap_preview_regions(*preview, mesh_copy, preview->simplified_face_colors);
        {
            std::lock_guard<std::mutex> lock(m_result_mutex);
            m_pending_gap_preview = std::move(preview);
        }
        auto* evt = new wxCommandEvent(EVT_TEXTURE_PATCH_DONE);
        evt->SetInt(gen);
        wxQueueEvent(handler, evt);
    });
}

void TextureImportDialog::on_patch_build_complete(wxCommandEvent& evt)
{
    if (evt.GetInt() != m_patch_generation.load())
        return;

    std::unique_ptr<GapPreviewState> preview;
    {
        std::lock_guard<std::mutex> lock(m_result_mutex);
        preview = std::move(m_pending_gap_preview);
    }
    m_patch_building = false;
    install_gap_preview(std::move(preview));
    update_ui_for_state();
}

void TextureImportDialog::commit_gap_area_to_painted()
{
    GapPreviewState& state = gap_preview();
    if (state.display_face_colors.empty()) {
        if (!state.simplified_face_colors.empty() &&
            state.simplified_face_colors.size() == m_painted.face_colors.size())
            state.display_face_colors = state.simplified_face_colors;
        else
            return;
    }
    if (state.display_face_colors.size() != m_painted.face_colors.size())
        return;

    m_painted.face_colors = state.display_face_colors;
    m_painted.cluster_colors = remaining_cluster_colors(
        state.simplified_cluster_colors, state.display_face_colors);

    const auto previous_matches = m_current_matches;
    do_auto_match();

    std::vector<Slic3r::FilamentMatch> ordered;
    ordered.reserve(m_current_matches.size());
    std::set<std::array<std::size_t, 3>> used;
    for (const auto& prev : previous_matches) {
        auto it = std::find_if(m_current_matches.begin(), m_current_matches.end(),
            [&](const Slic3r::FilamentMatch& match) {
                return match.cluster_color == prev.cluster_color;
            });
        if (it == m_current_matches.end())
            continue;
        it->filament_index = prev.filament_index;
        it->filament_color = prev.filament_color;
        it->delta_e = prev.delta_e;
        ordered.push_back(*it);
        used.insert(it->cluster_color);
    }
    for (const auto& match : m_current_matches) {
        if (used.count(match.cluster_color))
            continue;
        ordered.push_back(match);
    }
    m_current_matches = std::move(ordered);
    compact_used_virtual_filaments();

    for_each_preview([&](TexturePreviewCanvas* canvas) {
        canvas->set_face_colors(m_painted.face_colors);
    });
    update_filament_color_map();
    update_color_captions();
    if (m_mapping_scroll)
        rebuild_mapping_rows();
}

void TextureImportDialog::toggle_advanced_design()
{
    m_advanced_expanded = !m_advanced_expanded;
    if (auto* header = dynamic_cast<AdvancedFoldHeader*>(m_advanced_header))
        header->set_expanded(m_advanced_expanded);
    if (m_advanced_body) {
        m_advanced_body->Show(m_advanced_expanded);
        if (wxSizer* sizer = m_advanced_body->GetContainingSizer())
            sizer->Show(m_advanced_body, m_advanced_expanded);
    }
    if (m_advanced_expanded) {
        if (gap_preview().color_patches.empty())
            schedule_color_patches_rebuild();
        else
            apply_gap_area();
    }
    if (m_params_panel)
        m_params_panel->Layout();
    // The card only gets a real width once it is laid out visible, so break the hints
    // after that Layout rather than from the card's own constructor.
    layout_advanced_hints();
    Layout();
    update_dialog_min_size();
    recenter_preview_tags();
}

void TextureImportDialog::on_color_preset_clicked(wxCommandEvent& evt)
{
    int id = evt.GetId();
    int color_count = m_param_color_count;
    if (id == ID_COLOR_4)  { color_count = 4; }
    if (id == ID_COLOR_8)  { color_count = 8; }
    if (id == ID_COLOR_16) { color_count = 16; }

    if (id == ID_COLOR_AUTO) {
        m_auto_preset_selected = true;
        set_color_count_value(m_param_color_count, true);
        schedule_recompute(true, 0);
        return;
    }

    request_color_count(color_count, 0);
}

void TextureImportDialog::on_color_slider_changed(wxCommandEvent&)
{
    request_color_count(m_color_slider->GetValue(), 400);
}

void TextureImportDialog::on_color_spin_text_changed(wxCommandEvent& evt)
{
    long parsed = 0;
    if (!evt.GetString().ToLong(&parsed))
        return;
    set_color_count_exceeded(parsed > max_color_count());
}

void TextureImportDialog::on_color_spin_commit(bool from_enter)
{
    (void)from_enter;
    if (m_updating_params || !m_color_spin || !m_color_spin->GetTextCtrl())
        return;
    const wxString text = m_color_spin->GetTextCtrl()->GetValue();
    long parsed = 0;
    if (!text.ToLong(&parsed)) {
        set_text_input_int(m_color_spin, m_param_color_count);
        set_color_count_exceeded(false);
        return;
    }
    const int clamped = std::clamp((int)parsed, 1, max_color_count());
    request_color_count(clamped, 0);
}

void TextureImportDialog::on_smooth_slider_changed(wxCommandEvent&)
{
    set_smooth_value(m_smooth_slider->GetValue(), true);
    schedule_recompute(false, 400);
}

void TextureImportDialog::on_smooth_spin_commit()
{
    if (m_updating_params || !m_smooth_spin || !m_smooth_spin->GetTextCtrl())
        return;
    const wxString text = m_smooth_spin->GetTextCtrl()->GetValue();
    long parsed = 0;
    if (!text.ToLong(&parsed)) {
        set_text_input_int(m_smooth_spin, m_param_smooth);
        return;
    }
    const int clamped = std::clamp((int)parsed, 0, 10);
    if (clamped == m_param_smooth) {
        set_smooth_value(clamped, true);
        return;
    }
    set_smooth_value(clamped, true);
    schedule_recompute(false, 0);
}

void TextureImportDialog::on_gap_slider_changed(wxCommandEvent&)
{
    if (m_state == TextureImportState::Computing || m_patch_building)
        return;
    if (!m_gap_slider)
        return;
    set_gap_value(m_gap_slider->GetValue(), true);
    apply_gap_area();
}

void TextureImportDialog::on_gap_spin_commit()
{
    if (m_state == TextureImportState::Computing || m_patch_building)
        return;
    if (m_updating_params || !m_gap_spin || !m_gap_spin->GetTextCtrl())
        return;
    const wxString text = m_gap_spin->GetTextCtrl()->GetValue();
    double parsed = 0.0;
    if (!text.ToDouble(&parsed)) {
        set_text_input_double(m_gap_spin, m_param_gap_area);
        return;
    }
    set_gap_value(parsed, true);
    apply_gap_area();
}

void TextureImportDialog::retarget_filament_slot(int from_index, int to_index)
{
    if (from_index < 0 || from_index == to_index)
        return;
    for (auto& match : m_current_matches) {
        if (match.filament_index == from_index)
            bind_match_inplace(match, to_index, false);
    }
    for (auto& row : m_mapping_rows) {
        if (row.target_filament_idx == from_index)
            row.target_filament_idx = to_index;
    }
}

int TextureImportDialog::fold_new_mixed_components(int mixed_idx)
{
    if (mixed_idx < 0 || mixed_idx >= (int)m_filament_entries.size())
        return -1;
    TextureFilamentEntry& entry = m_filament_entries[mixed_idx];
    if (entry.kind != TextureFilamentKind::NewMixed ||
        entry.mixed_components.size() != entry.mixed_ratios.size())
        return -1;

    std::vector<unsigned int> components;
    std::vector<int>          ratios;
    components.reserve(entry.mixed_components.size());
    ratios.reserve(entry.mixed_ratios.size());
    for (size_t i = 0; i < entry.mixed_components.size(); ++i) {
        const auto it = std::find(components.begin(), components.end(), entry.mixed_components[i]);
        if (it != components.end()) {
            ratios[it - components.begin()] += entry.mixed_ratios[i];
            continue;
        }
        components.push_back(entry.mixed_components[i]);
        ratios.push_back(entry.mixed_ratios[i]);
    }
    entry.mixed_components = std::move(components);
    entry.mixed_ratios = std::move(ratios);

    for (auto& mixed : m_new_mixed_filaments) {
        if (mixed.dialog_index != mixed_idx)
            continue;
        mixed.ratios = entry.mixed_ratios;
        mixed.component_dialog_indices.clear();
        mixed.component_dialog_indices.reserve(entry.mixed_components.size());
        for (unsigned int comp : entry.mixed_components)
            mixed.component_dialog_indices.push_back(comp >= 1 ? (int)comp - 1 : -1);
    }

    if (entry.mixed_components.size() == 1 && entry.mixed_components.front() >= 1)
        return (int)entry.mixed_components.front() - 1;
    return -1;
}

void TextureImportDialog::on_skip_clicked(wxCommandEvent&)
{
    m_skipped = true;
    m_new_filament_colors.clear();
    m_new_filament_preset_names.clear();
    m_new_mixed_filaments.clear();
    m_current_matches.clear();
    cancel_computation();
    EndModal(wxID_CANCEL);
}

void TextureImportDialog::on_next_clicked(wxCommandEvent&)
{
    if (m_color_count_exceeded)
        return;
    if (m_recompute_timer && m_recompute_timer->IsRunning()) {
        m_recompute_timer->Stop();
        m_advance_to_matching_when_ready = true;
        start_computation(m_pending_auto_color);
        return;
    }
    if (m_state == TextureImportState::Computing) {
        m_advance_to_matching_when_ready = true;
        return;
    }
    if (m_state != TextureImportState::Ready || m_painted.face_colors.empty())
        return;
    if (m_patch_building)
        return;
    if (m_applied_smooth != m_param_smooth) {
        m_advance_to_matching_when_ready = true;
        start_computation(false);
        return;
    }
    if (m_applied_color_count != m_param_color_count &&
        m_param_color_count != visible_simplified_color_count()) {
        m_advance_to_matching_when_ready = true;
        start_computation(false);
        return;
    }
    commit_gap_area_to_painted();
    set_wizard_step(TextureImportWizardStep::FilamentMatching);
}

void TextureImportDialog::on_prev_clicked(wxCommandEvent&)
{
    set_wizard_step(TextureImportWizardStep::SimplifyColors);
    refresh_gap_preview();
}

void TextureImportDialog::on_reset_clicked(wxCommandEvent&)
{
    if (m_wizard_step != TextureImportWizardStep::FilamentMatching)
        return;
    if (m_state == TextureImportState::Computing)
        return;

    hide_match_threshold_popup();
    dismiss_filament_popup();
    m_match_delta_threshold = kMatchDeltaThresholdDefault;
    m_match_threshold_drop_pending = false;
    if (m_match_threshold_popup)
        m_match_threshold_popup->set_value(kMatchDeltaThresholdDefault);
    m_mix_enabled = false;
    if (m_mix_cb)
        m_mix_cb->SetValue(false);
    reset_to_project_filaments_and_auto_match();
    update_confirm_button_state();
    if (GetSizer())
        GetSizer()->Layout();
    Layout();
    recenter_preview_tags();
}

bool TextureImportDialog::has_valid_result() const
{
    if (m_painted.face_colors.empty() || m_current_matches.empty() || m_mapping_rows.empty())
        return false;

    if (m_mapping_rows.size() != m_current_matches.size())
        return false;

    const int filament_count = (int)m_filament_colors_rgba.size();
    for (const auto& row : m_mapping_rows) {
        if (row.target_filament_idx < 0 || row.target_filament_idx >= filament_count)
            return false;
    }
    if (has_unmatched_mapping())
        return false;
    return true;
}

bool TextureImportDialog::has_unmatched_mapping() const
{
    if (!m_mapping_rows.empty()) {
        for (const auto& row : m_mapping_rows) {
            if (row.target_filament_idx < 0)
                return true;
        }
        return false;
    }
    for (const auto& m : m_current_matches) {
        if (m.filament_index < 0)
            return true;
    }
    return false;
}

void TextureImportDialog::wrap_unmatched_warning_label()
{
    if (!m_unmatched_warning_label)
        return;
    const wxString text = _L("Some colors have no matching filament. Please match them before importing.");
    m_unmatched_warning_label->SetLabel(text);
    int wrap_width = GetClientSize().x - FromDIP(48);
    if (m_unmatched_warning_icon)
        wrap_width -= m_unmatched_warning_icon->GetBestSize().x + FromDIP(4);
    if (m_unmatched_add_link)
        wrap_width -= m_unmatched_add_link->GetBestSize().x + FromDIP(4);
    wrap_width = std::max(wrap_width, FromDIP(200));
    m_unmatched_warning_label->Wrap(wrap_width);
    m_unmatched_warning_label->InvalidateBestSize();
    m_unmatched_warning_label->SetMinSize(wxDefaultSize);
    m_unmatched_warning_label->SetMinSize(m_unmatched_warning_label->GetBestSize());
    if (m_unmatched_warning)
        m_unmatched_warning->Layout();
}

void TextureImportDialog::update_unmatched_warning_visibility()
{
    if (!m_unmatched_warning) return;
    const bool show = (m_wizard_step == TextureImportWizardStep::FilamentMatching)
        && (m_state == TextureImportState::Ready) && has_unmatched_mapping();
    if (show)
        wrap_unmatched_warning_label();
    if (m_unmatched_warning->IsShown() == show) return;
    m_unmatched_warning->Show(show);
    Layout();
}

void TextureImportDialog::complete_unmatched_filaments()
{
    if (m_wizard_step != TextureImportWizardStep::FilamentMatching)
        return;
    if (m_state != TextureImportState::Ready)
        return;
    if (!has_unmatched_mapping())
        return;
    if (m_mix_applying)
        return;
    m_mix_applying = true;
    Slic3r::ScopeGuard mix_guard([this]() { m_mix_applying = false; });

    // Compact first: the pool and the dialog indices below must not shift afterwards.
    size_t physical_capacity = remaining_physical_filament_capacity();
    size_t mixed_capacity = remaining_mixed_filament_capacity();

    const std::string match_type = pick_auto_match_filament_type(m_filament_entries, m_existing_filament_count);
    const std::string match_preset = resolve_auto_match_preset_name(
        m_filament_entries, m_existing_filament_count, match_type, m_default_virtual_filament_preset_name);

    auto find_virtual_filament_by_color = [this, &match_type](const std::array<std::size_t, 3>& color) -> int {
        const std::string hex = rgb_to_hex(color).ToStdString();
        for (size_t i = m_existing_filament_count; i < m_filament_color_strs.size(); ++i) {
            if (m_filament_color_strs[i] == hex &&
                i < m_filament_entries.size() && texture_entry_is_physical(m_filament_entries[i].kind) &&
                texture_entry_family_type(m_filament_entries[i], m_filament_entries) == match_type)
                return (int)i;
        }
        return -1;
    };

    TextureMixPool mix_pool;
    if (m_mix_enabled)
        build_mix_pool(mix_pool);

    // Larger colors get the free slots first. With Color Mixing on a color tries a
    // mixed recipe before a new physical slot. Colors left without a slot take the
    // closest filament once every slot is placed, so they can reuse the new ones.
    std::vector<size_t> deferred;
    for (size_t match_idx : unmatched_match_indices_by_area()) {
        if (match_idx >= m_current_matches.size())
            continue;
        Slic3r::FilamentMatch& m = m_current_matches[match_idx];
        if (m.filament_index >= 0)
            continue;
        const std::array<std::size_t, 3> color = m.cluster_color;

        int idx = find_virtual_filament_by_color(color);
        if (idx < 0 && m_mix_enabled) {
            bool created = false;
            bool needs_slot = false;
            idx = resolve_mix_color(mix_pool, color, mixed_capacity > 0, created, needs_slot);
            if (created) {
                if (mixed_capacity > 0)
                    --mixed_capacity;
                if (physical_capacity > 0 && m_filament_entries.size() + physical_capacity > MAX_FILAMENT_COUNT)
                    --physical_capacity;
            }
        }
        if (idx < 0 && physical_capacity > 0) {
            const std::array<float, 4> rgba = {
                color[0] / 255.f,
                color[1] / 255.f,
                color[2] / 255.f,
                1.f
            };
            idx = add_virtual_filament(rgba, rgb_to_hex(color).ToStdString(), match_preset);
            if (idx >= 0) {
                --physical_capacity;
                if (mixed_capacity > 0 && m_filament_entries.size() + mixed_capacity > MAX_FILAMENT_COUNT)
                    --mixed_capacity;
            }
        }
        if (idx < 0) {
            deferred.push_back(match_idx);
            continue;
        }
        bind_match_inplace(m, idx, false);
    }
    for (size_t match_idx : deferred) {
        if (match_idx >= m_current_matches.size())
            continue;
        Slic3r::FilamentMatch& m = m_current_matches[match_idx];
        bind_match_inplace(m, closest_filament_ignoring_threshold(m.cluster_color, match_type), false);
    }

    compact_used_virtual_filaments();
    update_filament_color_map();
    if (m_mapping_scroll)
        rebuild_mapping_rows();
    update_unmatched_warning_visibility();
    update_confirm_button_state();
    if (GetSizer())
        GetSizer()->Layout();
    Layout();
}

bool TextureImportDialog::can_confirm() const
{
    return m_wizard_step == TextureImportWizardStep::FilamentMatching &&
           m_state == TextureImportState::Ready &&
           has_valid_result() &&
           !filament_count_exceeded();
}

void TextureImportDialog::update_confirm_button_state()
{
    update_ui_for_state();
}

void TextureImportDialog::on_ok_clicked(wxCommandEvent&)
{
    if (m_wizard_step != TextureImportWizardStep::FilamentMatching)
        return;
    if (m_state != TextureImportState::Ready)
        return;
    if (filament_count_exceeded())
        return;
    if (has_unmatched_mapping()) {
        const wxString msg = _L("Some colors have no matching filament. Please match them before importing.");
        Slic3r::GUI::MessageDialog dlg(this, msg, _L("Error Prompt"), wxOK | wxICON_WARNING);
        dlg.ShowModal();
        return;
    }
    if (!has_valid_result())
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
    m_bmp_unmatched.msw_rescale();
    m_bmp_brand.msw_rescale();
    if (m_match_threshold_gear)
        m_match_threshold_gear->msw_rescale();
    hide_match_threshold_popup();
    update_dialog_min_size();

    for (Button* btn : {m_btn_color_4, m_btn_color_8, m_btn_color_16, m_btn_color_auto}) {
        if (btn) {
            const bool is_auto = (btn == m_btn_color_auto);
            btn->SetMinSize(wxSize(FromDIP(is_auto ? 52 : 46), FromDIP(21)));
            style_color_count_preset_button(btn);
        }
    }
    style_advanced_settings_card();
    for (Label* hint : {m_lbl_smooth_hint, m_lbl_gap_hint}) {
        if (hint)
            init_advanced_hint(hint);
    }
    layout_advanced_hints();
    style_param_value_input(m_color_spin);
    style_param_value_input(m_smooth_spin);
    style_param_value_input(m_gap_spin);
    update_gap_param_lock();

    if (m_mapping_scroll) {
        const bool step1 = (m_wizard_step == TextureImportWizardStep::SimplifyColors);
        m_mapping_scroll->SetMinSize(wxSize(-1, step1 ? -1 : FromDIP(252)));
        m_mapping_scroll->SetScrollRate(0, FromDIP(10));
    }
    if (m_preview_container)
        m_preview_container->SetMinSize(wxSize(-1, FromDIP(351)));
    if (auto* overlay = dynamic_cast<UpdatingOverlayPanel*>(m_updating_overlay))
        overlay->Rescale();
    if (auto* warning = dynamic_cast<ColorCountWarningPanel*>(m_color_count_warning))
        warning->Rescale();
    style_primary_button(m_btn_next);
    style_primary_button(m_btn_ok);

    auto style_footer = [this](Button* btn, int width) {
        if (!btn) return;
        btn->SetCornerRadius(FromDIP(12));
        btn->SetMinSize(wxSize(FromDIP(width), FromDIP(24)));
    };
    style_footer(m_btn_skip, 60);
    style_footer(m_btn_next, 60);
    style_footer(m_btn_prev, 72);
    style_footer(m_btn_reset, 72);
    style_footer(m_btn_ok, 60);

    rebuild_mapping_rows();

    if (wxSizer* sizer = GetSizer())
        sizer->Layout();
    Layout();
    recenter_preview_tags();
    Refresh();
    wxGetApp().UpdateDlgDarkUI(this);
    style_primary_button(m_btn_next);
    style_primary_button(m_btn_ok);
    for (Button* btn : {m_btn_color_4, m_btn_color_8, m_btn_color_16, m_btn_color_auto})
        style_color_count_preset_button(btn);
    style_advanced_settings_card();
    if (m_unmatched_warning_icon) {
        m_unmatched_warning_icon->SetBitmap(m_bmp_unmatched.bmp());
        m_unmatched_warning_icon->SetBackgroundColour(
            m_unmatched_warning ? m_unmatched_warning->GetBackgroundColour() : GetBackgroundColour());
    }
    if (m_unmatched_warning_label) {
        m_unmatched_warning_label->SetFont(texture_import_warning_body_font(this));
        m_unmatched_warning_label->SetForegroundColour(wxColour(225, 71, 71));
    }
    if (m_unmatched_add_link) {
        m_unmatched_add_link->SetForegroundColour(wxColour(0, 174, 66));
        m_unmatched_add_link->SetFont(texture_import_action_link_font(this));
    }
    wrap_unmatched_warning_label();
    if (auto* warning = dynamic_cast<ColorCountWarningPanel*>(m_color_count_warning))
        warning->Rescale();
    set_color_spin_error_border(m_color_spin, m_color_count_exceeded
        && m_wizard_step == TextureImportWizardStep::SimplifyColors);
}

}} // namespace Slic3r::GUI
