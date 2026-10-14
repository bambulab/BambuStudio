#include "ExtruderPanel.hpp"

#include <wx/bitmap.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/settings.h>
#include <wx/dcbuffer.h>
#include <wx/scrolbar.h>
#include <wx/timer.h>
#include <wx/eventfilter.h>
#include <wx/utils.h>

#include <algorithm>
#include <functional>
#include <memory>

#include "I18N.hpp"
#include "GUI_App.hpp"
#include "ExtruderHelpers.hpp"
#include "ExtruderPanelGroup.hpp"
#include "libslic3r/PresetBundle.hpp" // project_config, for reading multi-diameter mode
#include "wxExtensions.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/StateColor.hpp"
#include "wx/graphics.h"
#include "libslic3r/Log.hpp"

namespace Slic3r::GUI {

static const wxColour SelectedBgColor      = ThemeColor::BrandGreen;
static const wxColour SelectedBorderColor  = SelectedBgColor;
static const wxColour SelectedTextColor    = ThemeColor::White;
static const wxColour HasNozzleBgColor     = ThemeColor::White;
static const wxColour HasNozzleBorderColor = ThemeColor::Grey400;
static const wxColour HasNozzleTextColor   = ThemeColor::TextPrimary;
static const wxColour NoNozzleBgColor      = ThemeColor::White;
static const wxColour NoNozzleBorderColor  = ThemeColor::Grey300;
static const wxColour NoNozzleTextColor    = ThemeColor::Grey400;

// Per-extruder AMS caps, matching the ones show_ams_add_menu enforces when adding a unit.
constexpr size_t MAX_AMS_4_SLOT = 4;
constexpr size_t MAX_AMS_1_SLOT = 8;

static const wxColour DiameterNormalColor   = ThemeColor::TextMuted;
static const wxColour FlowNormalColor       = ThemeColor::Grey500;
static const wxColour DiameterDisabledColor = ThemeColor::Grey400;
static const wxColour FlowDisabledColor     = ThemeColor::Grey350;

DiameterButtonPanel::DiameterButtonPanel(wxWindow *parent, const std::vector<wxString> &diameter_choices, bool show_title,
                                         DisabledTooltipContext tooltip_context, bool show_multi_diameter_mode)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_choices(diameter_choices)
    , m_show_title(show_title)
    , m_show_multi_diameter_mode(show_multi_diameter_mode)
    , m_tooltip_context(tooltip_context)
{
    SetBackgroundColour(ThemeColor::White);
    if (!m_choices.empty()) {
        m_selected_diameter = m_choices[0];
    }

    CreateLayout();

    wxGetApp().UpdateDarkUIWin(this);
}

void DiameterButtonPanel::CreateLayout()
{
    if (GetSizer()) { GetSizer()->Clear(true); }
    auto main_sizer = new wxBoxSizer(wxVERTICAL);

    // The mode selector itself lives on the nozzle card (see AttachModePill); only its state is
    // mirrored here. The project config is the source of truth, so re-read it on every rebuild
    // rather than trusting a local value that a preset change may have invalidated.
    if (m_show_multi_diameter_mode) {
        m_multi_diameter_mode_enabled = multi_nozzle_diameter_mode_enabled(wxGetApp().preset_bundle->project_config);
        if (m_multi_diameter_mode_combo)
            m_multi_diameter_mode_combo->SetSelection(m_multi_diameter_mode_enabled ? 1 : 0);
    }

    m_title_sizer = nullptr;
    if (m_show_title) {
        auto title = new Label(this, _L("Set nozzle diameter"));
        title->SetFont(Label::Body_13);
        title->SetForegroundColour(ThemeColor::Grey500);

        m_title_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_title_sizer->Add(title, 0, wxALIGN_CENTER_VERTICAL);
        main_sizer->Add(m_title_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    }
    m_button_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_buttons.clear();
    for (const auto &diameter : m_choices) {
        auto btn = new Button(this, diameter);
        const int button_width = m_tooltip_context == FilamentContext ? 40 : 54;
        btn->SetMinSize(wxSize(FromDIP(button_width), FromDIP(24)));
        btn->SetMaxSize(wxSize(FromDIP(button_width), FromDIP(24)));
        btn->SetFont(Label::Body_12);
        btn->SetCornerRadius(FromDIP(6));

        UpdateSingleButtonState(btn, diameter);
        btn->Bind(wxEVT_BUTTON, &DiameterButtonPanel::OnButtonClicked, this);
        m_buttons.push_back(btn);
        m_button_sizer->Add(btn, 0, wxRIGHT, FromDIP(4));
    }

    main_sizer->Add(m_button_sizer, 0, wxALIGN_LEFT);
    SetSizer(main_sizer);
    UpdateSelectorVisibility();
    Layout();
}

void DiameterButtonPanel::FitModeComboToWidestItem()
{
    if (!m_multi_diameter_mode_combo)
        return;

    int text_w = 0;
    for (unsigned int i = 0; i < m_multi_diameter_mode_combo->GetCount(); ++i)
        text_w = std::max(text_w, m_multi_diameter_mode_combo->GetTextExtent(m_multi_diameter_mode_combo->GetString(i)).x);

    // Everything the pill draws beside the label: an inset on each side, the gap, and the chevron.
    const int reserve = 2 * MODE_PILL_PAD_X + MODE_PILL_GAP + MODE_PILL_ICON_D;
    m_multi_diameter_mode_combo->SetMinSize(wxSize(text_w + FromDIP(reserve), FromDIP(MODE_PILL_H)));
}

void DiameterButtonPanel::AttachModePill(ComboBox *pill)
{
    if (!pill || m_multi_diameter_mode_combo == pill) return;
    m_multi_diameter_mode_combo = pill;

    pill->Append(_L("Single-diameter mode"), {}); // item 0 = single, 1 = multi
    pill->Append(_L("Multi-diameter mode"), {});
    FitModeComboToWidestItem();

    pill->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &event) {
        const bool enabled = m_multi_diameter_mode_combo->GetSelection() == 1;
        SetMultiDiameterMode(enabled);
        if (m_multi_diameter_mode_callback)
            m_multi_diameter_mode_callback(enabled);
        event.Skip();
    });

    // The mode may already have been pushed in before the card existed, so replay it onto the pill.
    SetMultiDiameterMode(m_multi_diameter_mode_enabled);
    ShowModeSelector(m_mode_selector_visible);
}

void DiameterButtonPanel::RefreshLayout(const std::vector<wxString> &choices)
{
    if (choices == m_choices) { return; }
    m_choices            = choices;
    bool selected_exists = std::find(m_choices.begin(), m_choices.end(), m_selected_diameter) != m_choices.end();
    if (!selected_exists && !m_choices.empty()) { m_selected_diameter = m_choices[0]; }

    CreateLayout();

    Refresh();
    Update();
}

void DiameterButtonPanel::SetMultiDiameterMode(bool enabled)
{
    m_multi_diameter_mode_enabled = enabled;
    if (m_multi_diameter_mode_combo)
        m_multi_diameter_mode_combo->SetSelection(enabled ? 1 : 0);
    UpdateSelectorVisibility();
    // A hosting card puts the panel inside its scrolling body, so the card is an ancestor, not the parent.
    for (wxWindow *window = GetParent(); window && !window->IsTopLevel(); window = window->GetParent()) {
        if (auto *card = dynamic_cast<SingleExtruderPanelGroup *>(window)) {
            card->ShowDiameterPanel(!enabled);
            break;
        }
    }
}

void DiameterButtonPanel::SetShowTitle(bool show)
{
    if (m_show_title == show)
        return;

    m_show_title = show;
    CreateLayout();
}

void DiameterButtonPanel::ShowModeSelector(bool show)
{
    m_mode_selector_visible = show; // remembered so a later AttachModePill replays the state
    if (!m_multi_diameter_mode_combo || m_multi_diameter_mode_combo->IsShown() == show)
        return;
    m_multi_diameter_mode_combo->Show(show);
    // The pill is the card's child, so it is the card that has to re-lay out around it -- hiding it
    // also collapses the strip the card reserved for it and pulls the top border back up, which
    // changes the card's height and so the sidebar's layout too.
    if (wxWindow *card = m_multi_diameter_mode_combo->GetParent()) {
        card->Layout();
        card->Refresh();
        if (wxWindow *sidebar = card->GetParent()) sidebar->Layout();
    }
}

void DiameterButtonPanel::UpdateSelectorVisibility()
{
    if (!GetSizer()) return;
    // Only the sidebar panel (the one carrying the mode selector) drops its diameter buttons in
    // multi-diameter mode; every other context keeps them visible.
    const bool show = !(m_show_multi_diameter_mode && m_multi_diameter_mode_enabled);
    if (m_title_sizer) GetSizer()->Show(m_title_sizer, show, true);
    if (m_button_sizer) GetSizer()->Show(m_button_sizer, show, true);
    GetSizer()->Layout();
    Layout();
    if (wxWindow *parent = GetParent()) parent->Layout();
}

void DiameterButtonPanel::OnButtonClicked(wxCommandEvent &event)
{
    Button *clicked_btn = dynamic_cast<Button *>(event.GetEventObject());
    if (!clicked_btn) return;

    wxString diameter = clicked_btn->GetLabel();

    // A grey (NoNozzle) button has no nozzle configured for this diameter. Instead of
    // switching to it, open the extruder settings dialog so the user can set one up.
    if (clicked_btn->IsGrayed()) {
        if (m_no_nozzle_click_callback) m_no_nozzle_click_callback(diameter);
        return;
    }

    SetSelectedDiameter(diameter);

    event.Skip();
}

void DiameterButtonPanel::SetSelectedDiameter(const wxString &diameter)
{
    if (m_selected_diameter == diameter) {
        UpdateButtonStates();
        return;
    }

    wxString original_diameter = m_selected_diameter;
    m_selected_diameter        = diameter;
    UpdateButtonStates();

    /*wxCommandEvent evt(EVT_NOZZLE_DIAMETER_SELECTED, GetId());
    evt.SetEventObject(this);
    evt.SetString(diameter);
    evt.SetClientData(new wxString(original_diameter));
    GetEventHandler()->ProcessEvent(evt);*/
}

void DiameterButtonPanel::UpdateButtonStates()
{
    assert(m_buttons.size() == m_choices.size());
    for (size_t i = 0; i < m_buttons.size() && i < m_choices.size(); ++i) {
        UpdateSingleButtonState(m_buttons[i], m_choices[i]);
    }
}

void DiameterButtonPanel::Rescale()
{
    FitModeComboToWidestItem();
    for (auto *btn : m_buttons) {
        if (btn) {
            const int button_width = m_tooltip_context == FilamentContext ? 40 : 54;
            btn->SetMinSize(wxSize(FromDIP(button_width), FromDIP(24)));
            btn->SetMaxSize(wxSize(FromDIP(button_width), FromDIP(24)));
            btn->SetFont(Label::Body_12);
            btn->SetCornerRadius(FromDIP(6));
            btn->Rescale();
            btn->SetCenter(true);
            btn->InvalidateBestSize();
            btn->SetSize(btn->GetMinSize());
            btn->Layout();
            btn->Refresh();
        }
    }
    if (GetSizer()) {
        GetSizer()->Layout();
    }
    Layout();
    Refresh();
}

void DiameterButtonPanel::UpdateSingleButtonState(Button *btn, const wxString &diameter)
{
    if (!btn) return;

    ButtonState state = NoNozzle;

    if (diameter == m_selected_diameter) {
        state = Selected;
    } else {
        if (m_nozzle_query_callback) {
            bool has_nozzle = m_nozzle_query_callback(diameter);
            state           = has_nozzle ? HasNozzle : NoNozzle;
        } else {
            state = NoNozzle;
        }
    }

    StateColor bg_color, text_color, border_color;

    switch (state) {
    case Selected:
        bg_color.append(SelectedBgColor, StateColor::Normal);
        text_color.append(SelectedTextColor, StateColor::Normal);
        border_color.append(SelectedBorderColor, StateColor::Normal);
        break;
    case HasNozzle:
        bg_color.append(HasNozzleBgColor, StateColor::Normal);
        text_color.append(HasNozzleTextColor, StateColor::Normal);
        border_color.append(HasNozzleBorderColor, StateColor::Normal);
        break;
    case NoNozzle:
        bg_color.append(NoNozzleBgColor, StateColor::Normal);
        text_color.append(NoNozzleTextColor, StateColor::Normal);
        border_color.append(NoNozzleBorderColor, StateColor::Normal);
        break;
    }

    btn->SetBackgroundColor(bg_color);
    btn->SetTextColor(text_color);
    btn->SetBorderColor(border_color);
    btn->SetGrayed(state == NoNozzle);

    wxString tip = m_tooltip_context == FilamentContext
        ? wxString::Format(_L("This filament does not support the %smm nozzle, or no matching nozzle is configured."), btn->GetLabel())
        : wxString::Format(_L("Please set a %smm diameter nozzle for the printer in the section below first before using this diameter for slicing."), btn->GetLabel());
    state == NoNozzle ? btn->SetToolTip(tip) : btn->UnsetToolTip();
    btn->Refresh();
}

namespace {
// wxOSX builds a scrolled window's scrollbars as real child windows (wxWindowMac::MacCreateScrollBars),
// created visible, and the only thing that ever hides them again is wxWindowMac::SetScrollbar, through
// DoUpdateScrollbarVisibility. The cards here either draw their own bar (SlimScrollbar) or scroll
// without one, so the native pair has to be taken off by hand -- the same thing the Windows branches
// below do through ShowScrollBar().
//
// At construction the scrolled window owns nothing but those bars and wx's grow box, so hiding every
// child is safe there and takes the grow box with it. Afterwards only the bars matter, and each
// SetScrollbar call puts them back on screen as soon as the content overflows, so every such call
// needs another pass.
#ifdef __WXOSX__
void hide_native_scrollbars(wxWindow *window, bool include_grow_box = false)
{
    if (window == nullptr) return;
    for (auto *child : window->GetChildren())
        if (include_grow_box || dynamic_cast<wxScrollBar *>(child)) child->Hide();
}
#else
inline void hide_native_scrollbars(wxWindow *, bool = false) {}
#endif
} // namespace

HorizontalScrollablePanel::HorizontalScrollablePanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style)
    : wxScrolledWindow(parent, id, pos, size, style)
{
    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    SetSizer(sizer);

    SetScrollRate(FromDIP(1), 0);
    SetBackgroundColour(ThemeColor::Grey200);

    hide_native_scrollbars(this, true);
}

void HorizontalScrollablePanel::Clear()
{
    GetSizer()->Clear(true);
    //UpdateScrollbars();
}

/**
 * \brief One nozzle's summary tile: icon + diameter + flow + count.
 *
 * Stamped once for a single-nozzle extruder, or repeated per nozzle for a
 * single-extruder multi-nozzle setup.
 */
class NozzleSummary : public wxPanel
{
public:
    explicit NozzleSummary(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    {
        SetBackgroundColour(ThemeColor::Grey200);

        auto *main_sizer = new wxBoxSizer(wxHORIZONTAL);

        auto  icon        = create_scaled_bitmap("prepare_nozzle_normal_short", this, 24);
        auto *nozzle_icon = new wxStaticBitmap(this, wxID_ANY, icon);

        auto *label_sizer = new wxBoxSizer(wxVERTICAL);
        m_diameter        = new Label(this, "0.4");
        m_diameter->SetFont(Label::Body_12);
        m_diameter->SetForegroundColour(DiameterNormalColor);
        m_diameter->SetBackgroundColour(ThemeColor::Grey200);

        m_flow = new Label(this, "Std");
        m_flow->SetFont(Label::Body_12);
        m_flow->SetForegroundColour(FlowNormalColor);
        m_flow->SetBackgroundColour(ThemeColor::Grey200);

        label_sizer->Add(m_diameter, 0, wxALIGN_LEFT | wxBOTTOM, FromDIP(2));
        label_sizer->Add(m_flow, 0, wxALIGN_LEFT);

        m_nozzle_number = new Label(this, "x1");
        m_nozzle_number->SetFont(Label::Body_15);
        m_nozzle_number->SetForegroundColour(DiameterNormalColor);
        m_nozzle_number->SetBackgroundColour(ThemeColor::Grey200);

        main_sizer->Add(nozzle_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(8));
        main_sizer->Add(label_sizer, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        main_sizer->Add(m_nozzle_number, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        SetSizer(main_sizer);
        Layout();
    }

    void SetDiameter(const wxString &diameter) { m_diameter->SetLabel(diameter); }
    void SetFlow(const wxString &flow) { m_flow->SetLabel(flow); }
    void SetNozzleNumber(int num) { m_nozzle_number->SetLabel(wxString::Format("x%d", num)); }
    void ShowNozzleNumber(bool show) { m_nozzle_number->Show(show); }

private:
    Label *m_diameter{};
    Label *m_flow{};
    Label *m_nozzle_number{};
};

// Prepare-page nozzle-slot tile geometry, taken from the design: a narrow card holding the
// hotend icon with its slot number stamped on it, then the diameter over the flow type.
constexpr int    TILE_W            = 37; // design minimum; overflow scrolls instead of shrinking tiles
constexpr int    TILE_H            = 80;
constexpr int    TILE_GAP          = 4;
constexpr int    TILE_RADIUS       = 4;
constexpr int    TILE_ICON_H       = 48;
constexpr int    TILE_BADGE_D      = 16;
constexpr int    TILE_BADGE_Y      = 14; // badge top, measured down from the tile's top
constexpr int    TILE_TEXT_GAP     = 2;  // icon bottom to the diameter line
constexpr double TILE_DIM_OPACITY  = 0.3;

static wxColour faded_tile_colour(const wxColour &colour, const wxColour &background)
{
    auto blend = [](unsigned char foreground, unsigned char backdrop) {
        return static_cast<unsigned char>(foreground * TILE_DIM_OPACITY + backdrop * (1.0 - TILE_DIM_OPACITY) + 0.5);
    };
    return wxColour(blend(colour.Red(), background.Red()),
                    blend(colour.Green(), background.Green()),
                    blend(colour.Blue(), background.Blue()));
}

static wxBitmap faded_tile_bitmap(const wxBitmap &bitmap, const wxColour &background)
{
    wxImage image = bitmap.ConvertToImage();
    for (int y = 0; y < image.GetHeight(); ++y) {
        for (int x = 0; x < image.GetWidth(); ++x) {
            const wxColour colour(image.GetRed(x, y), image.GetGreen(x, y), image.GetBlue(x, y));
            const wxColour faded = faded_tile_colour(colour, background);
            image.SetRGB(x, y, faded.Red(), faded.Green(), faded.Blue());
        }
    }
    return wxBitmap(image, -1, bitmap.GetScaleFactor());
}

/**
 * \brief Set on \p gc the largest body font, 12 down to 8, at which \p text fits in \p max_width.
 *
 * Falls back to the smallest size when even that overflows.
 */
static void set_fitting_font(wxGraphicsContext &gc, const wxString &text, double max_width, const wxColour &colour)
{
    for (const wxFont *font : {&Label::Body_12, &Label::Body_11, &Label::Body_10, &Label::Body_9, &Label::Body_8}) {
        gc.SetFont(*font, colour);
        double width = 0.0;
        gc.GetTextExtent(text, &width, nullptr);
        if (width <= max_width)
            return;
    }
}

/**
 * \brief One physical nozzle slot: the hotend icon carrying its slot number, then diameter + flow.
 *
 * Where \ref NozzleSummary folds every nozzle sharing a (diameter, flow) pair into one tile, a slot
 * tile stands for exactly one slot, so the card can address nozzles by the number the user sees on
 * the machine. Painted whole rather than assembled from child windows: the badge has to sit on top
 * of the icon, and the dimmed state composites the entire tile at one opacity, which a tree of
 * labels and static bitmaps cannot do.
 */
class NozzleSlotTile : public wxPanel
{
public:
    NozzleSlotTile(wxWindow *parent, int number, const wxString &diameter, const wxString &flow, NozzleStatus status)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
        , m_icon(this, "prepare_nozzle_normal_short", TILE_ICON_H)
        , m_number(number)
        , m_diameter(diameter)
        , m_flow(flow)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(parent->GetBackgroundColour()); // the surface the rounded corners reveal
        SetMinSize(FromDIP(wxSize(TILE_W, TILE_H)));
        Bind(wxEVT_PAINT, &NozzleSlotTile::OnPaint, this);
    }

    /// \brief Fade the whole tile out: the slot is configured but not printed at in this mode.
    void SetDimmed(bool dimmed)
    {
        if (m_dimmed == dimmed) return;
        m_dimmed = dimmed;
        Refresh();
    }

private:
    void OnPaint(wxPaintEvent &evt);

    ScalableBitmap m_icon;
    int            m_number{0};
    wxString       m_diameter;
    wxString       m_flow;
    bool           m_dimmed{false};
};

void NozzleSlotTile::OnPaint(wxPaintEvent &evt)
{
    wxAutoBufferedPaintDC dc(this);
    // wxBG_STYLE_PAINT leaves the whole client area to us and the rounded card never covers its
    // corners, so the surrounding surface has to go down first or those corners keep stale pixels.
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    const wxSize size = GetSize();
    const wxColour background = GetBackgroundColour();
    auto tile_colour = StateColor::darkModeColorFor(ThemeColor::Grey350);
    auto badge_colour = StateColor::darkModeColorFor(ThemeColor::Grey400);
    auto text_colour = StateColor::darkModeColorFor(ThemeColor::TextPrimary);
    if (m_dimmed) {
        // BeginLayer(opacity) is not implemented by wxWidgets' default GDI+ renderer on Windows.
        // Blend every visual with the card surface instead, producing the same 30% result on every
        // graphics backend.
        tile_colour  = faded_tile_colour(tile_colour, background);
        badge_colour = faded_tile_colour(badge_colour, background);
        text_colour  = faded_tile_colour(text_colour, background);
    }

    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(tile_colour));
    gc->DrawRoundedRectangle(0, 0, size.x, size.y, FromDIP(TILE_RADIUS));

    const wxSize icon_size = m_icon.GetBmpSize();
    const double icon_x    = (size.x - icon_size.x) / 2.0;
    const double   icon_y    = 0;
    const wxBitmap icon = m_dimmed ? faded_tile_bitmap(m_icon.bmp(), background) : m_icon.bmp();
    gc->DrawBitmap(icon, icon_x, icon_y, icon_size.x, icon_size.y);

    // The number goes on the icon, not beside it: the tile is barely wider than the icon itself.
    const double badge_d = FromDIP(TILE_BADGE_D);
    const double badge_x = (size.x - badge_d) / 2.0;
    const double badge_y = FromDIP(0);
    gc->SetBrush(wxBrush(badge_colour));
    gc->DrawEllipse(badge_x, 0, badge_d, badge_d);

    gc->SetFont(Label::Body_12, text_colour);

    double text_w = 0.0, text_h = 0.0, descent = 0.0, leading = 0.0;
    const wxString number = wxString::Format("%d", m_number);
    gc->GetTextExtent(number, &text_w, &text_h, &descent, &leading);
    // Digits have no descenders and start below the internal leading, so centring the line box
    // leaves them visibly high; centre their ink band [internalLeading, ascent] instead.
    dc.SetFont(Label::Body_12);
    const wxFontMetrics metrics  = dc.GetFontMetrics();
    const double        ink_mid  = (metrics.internalLeading + metrics.ascent) / 2.0;
    gc->DrawText(number, badge_x + (badge_d - text_w) / 2.0, badge_y + badge_d / 2.0 - ink_mid);

    // Rows keep the 12px line height so a shrunk line does not shift the one below it.
    const double line_h = metrics.height;
    double       y      = icon_y + icon_size.y + FromDIP(TILE_TEXT_GAP);
    for (const wxString &line : {m_diameter, m_flow}) {
        set_fitting_font(*gc, line, size.x, text_colour);
        gc->GetTextExtent(line, &text_w, &text_h, &descent, &leading);
        gc->DrawText(line, (size.x - text_w) / 2.0, y + (line_h - text_h) / 2.0);
        y += line_h;
    }
}

/**
 * \brief Thin draggable track and thumb that scrolls \p target sideways, in place of the native bar.
 */
class SlimScrollbar : public wxPanel
{
public:
    SlimScrollbar(wxWindow *parent, wxScrolledWindow *target) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_target(target)
    {
        SetMinSize(wxSize(-1, FromDIP(6)));
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(parent->GetBackgroundColour());

        Bind(wxEVT_PAINT, &SlimScrollbar::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &SlimScrollbar::OnLeftDown, this);
        Bind(wxEVT_LEFT_UP, &SlimScrollbar::OnLeftUp, this);
        Bind(wxEVT_MOTION, &SlimScrollbar::OnMotion, this);
        Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &) { m_dragging = false; });
    }

private:
    struct ThumbGeometry
    {
        int x{0};
        int width{0};
    };

    ThumbGeometry GetThumbGeometry() const
    {
        const int width = GetClientSize().x;
        const int client_width = m_target->GetClientSize().x;
        const int virtual_width = m_target->GetVirtualSize().x;
        if (width <= 0 || client_width <= 0 || virtual_width <= client_width)
            return {0, width};

        const int thumb_width = std::max(FromDIP(24), width * client_width / virtual_width);
        int view_x = 0;
        int view_y = 0;
        int unit_x = 0;
        int unit_y = 0;
        m_target->GetViewStart(&view_x, &view_y);
        m_target->GetScrollPixelsPerUnit(&unit_x, &unit_y);
        const int scroll_x = view_x * std::max(1, unit_x);
        const int thumb_x = (width - thumb_width) * scroll_x / (virtual_width - client_width);
        return {thumb_x, thumb_width};
    }

    void ScrollToThumb(int thumb_x)
    {
        const int width = GetClientSize().x;
        const int client_width = m_target->GetClientSize().x;
        const int virtual_width = m_target->GetVirtualSize().x;
        const ThumbGeometry thumb = GetThumbGeometry();
        const int thumb_range = width - thumb.width;
        if (thumb_range <= 0 || virtual_width <= client_width) return;

        const int clamped_x = std::clamp(thumb_x, 0, thumb_range);
        const int scroll_x = clamped_x * (virtual_width - client_width) / thumb_range;
        int unit_x = 0;
        int unit_y = 0;
        m_target->GetScrollPixelsPerUnit(&unit_x, &unit_y);
        m_target->Scroll(scroll_x / std::max(1, unit_x), -1);
        Refresh();
    }

    void OnPaint(wxPaintEvent &)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();

        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (!gc) return;

        const wxSize size = GetClientSize();
        const ThumbGeometry thumb = GetThumbGeometry();
        const double radius = size.y / 2.0;
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(wxBrush(StateColor::darkModeColorFor(ThemeColor::Grey250)));
        gc->DrawRoundedRectangle(0, 0, size.x, size.y, radius);
        gc->SetBrush(wxBrush(StateColor::darkModeColorFor(ThemeColor::Grey400)));
        gc->DrawRoundedRectangle(thumb.x, 0, thumb.width, size.y, radius);
    }

    void OnLeftDown(wxMouseEvent &event)
    {
        const ThumbGeometry thumb = GetThumbGeometry();
        m_drag_offset = event.GetX() >= thumb.x && event.GetX() < thumb.x + thumb.width
            ? event.GetX() - thumb.x
            : thumb.width / 2;
        m_dragging = true;
        CaptureMouse();
        ScrollToThumb(event.GetX() - m_drag_offset);
    }

    void OnLeftUp(wxMouseEvent &)
    {
        m_dragging = false;
        if (HasCapture()) ReleaseMouse();
    }

    void OnMotion(wxMouseEvent &event)
    {
        if (m_dragging && event.LeftIsDown())
            ScrollToThumb(event.GetX() - m_drag_offset);
    }

    wxScrolledWindow *m_target{nullptr};
    bool              m_dragging{false};
    int               m_drag_offset{0};
};

// ----------------------------------------------------------------------------
// HorizontalScrollView
// ----------------------------------------------------------------------------

namespace {
constexpr int SCROLLBAR_GAP     = 4;  // content bottom to the slim scrollbar
constexpr int WHEEL_SCROLL_STEP = 40; // pixels per wheel notch

/// \brief A scrolled window that never shows the native scrollbar; SlimScrollbar stands in for it.
class BarlessScrolledWindow : public wxScrolledWindow
{
public:
    explicit BarlessScrolledWindow(wxWindow *parent) : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    {
        // wxScrolled::Create turns a style naming neither direction into both, so wxOSX builds a
        // horizontal and a vertical bar here even though this window never asks for either, and the
        // empty SetScrollbar below leaves them on screen for good. Nothing shows them again once
        // they are hidden: MacChildAdded only raises them.
        hide_native_scrollbars(this, true);
    }

    // wxScrollHelper keeps the scroll state itself and only mirrors it onto the native bar here, so
    // dropping the mirror hides the bar while scrolling keeps working. Hiding it through
    // ShowScrollbars(wxSHOW_SB_NEVER) instead would also clamp the content to the client width.
    void SetScrollbar(int, int, int, int, bool) override {}
};

/**
 * \brief Hands every wheel turn made over \p window to \p handler, whichever window Windows sent it to.
 *
 * Windows sends the wheel to the focused window -- usually the 3D canvas -- or, depending on a system
 * setting, to the innermost window under the pointer, so binding the event on one window catches it
 * only some of the time. An app-wide filter sees it in every case. A window covering this one, such
 * as a popup, keeps its wheel: only turns whose pointer is over this window or its children count.
 */
class WheelOverWindowFilter : public wxEventFilter
{
public:
    WheelOverWindowFilter(wxWindow *window, std::function<bool(const wxMouseEvent &)> handler) : m_window(window), m_handler(std::move(handler))
    { wxEvtHandler::AddFilter(this); }
    ~WheelOverWindowFilter() override { wxEvtHandler::RemoveFilter(this); }

    int FilterEvent(wxEvent &event) override
    {
        if (event.GetEventType() != wxEVT_MOUSEWHEEL || !m_window->IsShownOnScreen()) return Event_Skip;

        for (wxWindow *hit = wxFindWindowAtPoint(wxGetMousePosition()); hit; hit = hit->GetParent()) {
            if (hit == m_window) return m_handler(static_cast<const wxMouseEvent &>(event)) ? Event_Processed : Event_Skip;
            if (hit->IsTopLevel()) break;
        }
        return Event_Skip;
    }

private:
    wxWindow                                 *m_window;
    std::function<bool(const wxMouseEvent &)> m_handler;
};
} // namespace

HorizontalScrollView::HorizontalScrollView(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    SetBackgroundColour(parent->GetBackgroundColour());

    m_content = new BarlessScrolledWindow(this);
    m_content->SetBackgroundColour(GetBackgroundColour());
    m_content->SetSizer(new wxBoxSizer(wxVERTICAL));
    m_content->SetScrollRate(1, 0); // horizontal only, so the content still sets the view's height

    // A wheel the filter lets through while nothing overflows would be swallowed by wxScrollHelper
    // once it reached the content; pass it on to the enclosing windows instead.
    m_content->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent &evt) {
        for (wxWindow *window = GetParent(); window && !window->IsTopLevel(); window = window->GetParent())
            if (window->GetEventHandler()->ProcessEvent(evt)) return;
    });
    m_wheel_filter = std::make_unique<WheelOverWindowFilter>(this, [this](const wxMouseEvent &evt) { return ScrollByWheel(evt); });

    m_scrollbar = new SlimScrollbar(this, m_content);
    m_scrollbar->Hide();

    auto *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_content, 0, wxEXPAND);
    sizer->Add(m_scrollbar, 0, wxEXPAND | wxTOP, FromDIP(SCROLLBAR_GAP));
    SetSizer(sizer);
}

HorizontalScrollView::~HorizontalScrollView() = default;

wxWindow *HorizontalScrollView::Content() const { return m_content; }

wxBoxSizer *HorizontalScrollView::ContentSizer() const { return static_cast<wxBoxSizer *>(m_content->GetSizer()); }

// Overflow is judged from the content's own minimum rather than from the scrolled window, whose
// virtual size is only refreshed after it has been resized. Showing the bar here, before the view
// is laid out, lets a parent that reads the view's height right after laying it out see the bar.
bool HorizontalScrollView::Layout()
{
    m_scrollbar->Show(ContentSizer()->GetMinSize().x > GetClientSize().x);

    const bool laid_out = wxPanel::Layout();
    m_content->FitInside();
    m_content->Layout();
    m_scrollbar->Refresh();
    return laid_out;
}

bool HorizontalScrollView::ScrollByWheel(const wxMouseEvent &evt)
{
    if (!m_scrollbar->IsShown()) return false;

    const int notches = evt.GetWheelDelta() == 0 ? 0 : evt.GetWheelRotation() / evt.GetWheelDelta();
    // A horizontal wheel turns positive towards the right, a vertical one positive away from the user.
    const int direction = evt.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL ? 1 : -1;
    int       view_x = 0, view_y = 0;
    m_content->GetViewStart(&view_x, &view_y);
    m_content->Scroll(std::max(0, view_x + direction * notches * FromDIP(WHEEL_SCROLL_STEP)), -1);
    m_scrollbar->Refresh();
    return true;
}

// ----------------------------------------------------------------------------
// ExtruderPanel (abstract base)
// ----------------------------------------------------------------------------

ExtruderPanel::ExtruderPanel(wxWindow *parent, GroupType type) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_type(type)
{
    SetForegroundColour(ThemeColor::Grey400);
    // The panel is a region of the card that frames it, so it takes the card's surface rather than
    // naming one of its own -- the single-nozzle card is white where the dual card is grey.
    SetBackgroundColour(parent->GetBackgroundColour());
}

void ExtruderPanel::Build()
{
    m_main_sizer = new wxBoxSizer(wxVERTICAL);
    // Floor the card at a comfortable height on the sizer, not on the window: a min height set on the
    // window would also cap it, leaving no room for the tile row to wrap onto extra lines.
    m_main_sizer->SetMinSize(wxSize(-1, FromDIP(40)));

    // No wxEXTEND_LAST_ON_EACH_LINE: that raises the last tile's proportion, so it would stretch to
    // fill its line instead of keeping its own width.
    m_summary_sizer = new wxWrapSizer(wxHORIZONTAL, 0); // NozzleSummary tiles, filled by UpdateNozzles

    m_content_row = new wxBoxSizer(wxHORIZONTAL);
    m_content_row->Add(m_summary_sizer, 0, wxALIGN_CENTER_VERTICAL);

    m_content_sizer = new wxBoxSizer(wxHORIZONTAL);
    CreateContent(m_content_sizer);

    // Trailing warning icon; UpdateNozzles shows it when the selected diameter has no matching nozzle.
    m_warning_icon = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("warning", this, 20), wxDefaultPosition, wxSize(FromDIP(20), FromDIP(20)), 0);
    m_warning_icon->Hide();
    m_content_sizer->Add(m_warning_icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    m_content_row->Add(m_content_sizer, 1, wxEXPAND);
    m_main_sizer->Add(m_content_row, 0, wxEXPAND);

    CreateExtraRows(m_main_sizer);

    SetSizer(m_main_sizer);
    Bind(wxEVT_SIZE, &ExtruderPanel::OnSize, this);
    Layout();

    wxGetApp().UpdateDarkUIWin(this);
    CallAfter([this]() { update_ams(); });
}

// Set (or clear) a tooltip on a window and its whole descendant tree.
static void set_tooltip_recursive(wxWindow *window, const wxString &tooltip, bool clear)
{
    if (!window) return;
    clear ? window->UnsetToolTip() : window->SetToolTip(tooltip);
    for (auto *child : window->GetChildren()) set_tooltip_recursive(child, tooltip, clear);
}

static wxString volume_short(NozzleVolumeType volume) { return wxString::FromUTF8(DevNozzle::ToNozzleVolumeShortString(volume)); }

void ExtruderPanel::add_summary_tile(const wxString &diameter, const wxString &flow, int count, bool show_count)
{
    auto *tile = new NozzleSummary(this);
    tile->SetDiameter(diameter);
    tile->SetFlow(flow);
    tile->SetNozzleNumber(count);
    tile->ShowNozzleNumber(show_count);
    m_summary_sizer->Add(tile, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(2));
    m_summaries.push_back(tile);
    wxGetApp().UpdateDarkUIWin(tile); // tiles are created after Build()'s one-time dark-mode pass
}

// Width the tiles occupy when they are all kept on a single line. Measured from the tiles rather
// than from the wrapping sizer, whose own min size shrinks to the wrapped width once it wraps and
// would otherwise make the two placements below flip back and forth.
int ExtruderPanel::tiles_single_line_width() const
{
    int width = 0;
    if (!m_summary_sizer) return width;

    for (const auto *item : m_summary_sizer->GetChildren()) {
        const wxWindow *tile = item->GetWindow();
        if (tile && tile->IsShown()) width += tile->GetBestSize().x + item->GetBorder();
    }

    return width;
}

// A wrap sizer only wraps when its parent hands it a width, which the horizontal row holding it
// never does; the card's own vertical sizer does. So the row stays beside the content while the
// tiles still fit there on one line, and moves below it once they do not, where it can wrap.
void ExtruderPanel::update_tile_row_line()
{
    if (!m_content_row || !m_content_sizer) return;

    const wxSize available = GetClientSize();
    if (available.x <= 0) return; // not shown yet; size events re-run this once it is

    // Keep the tiles beside the content while that leaves room for them on one line, and give the
    // tiles a line of their own once it does not, so the extra tiles wrap there instead of clipping.
    const int  beside    = available.x - m_content_sizer->GetMinSize().x - FromDIP(8);
    const bool fits_wide = tiles_single_line_width() <= beside;
    if (m_tiles_on_own_line == !fits_wide) return; // already placed this way

    m_tiles_on_own_line = !fits_wide;

    // The row may currently sit in either the content row or the card's vertical sizer, so both
    // have to be cleared before it is placed again.
    m_content_row->Detach(m_summary_sizer);
    m_main_sizer->Detach(m_summary_sizer);
    m_content_row->Detach(m_content_sizer);

    if (fits_wide) {
        m_content_row->Add(m_summary_sizer, 0, wxALIGN_CENTER_VERTICAL);
        m_content_row->Add(m_content_sizer, 1, wxEXPAND);
    } else {
        m_content_row->Add(m_content_sizer, 1, wxEXPAND);
        // A line of its own, directly under the content row (index 0) and so still above whatever
        // CreateExtraRows appended -- an Add() here would drop it below those rows instead.
        m_main_sizer->Insert(1, m_summary_sizer, 0, wxEXPAND | wxTOP, FromDIP(2));
    }

    m_main_sizer->Layout();
    Layout();
}

void ExtruderPanel::OnSize(wxSizeEvent &evt)
{
    update_tile_row_line();
    evt.Skip();
}

void ExtruderPanel::finish_projection(bool enabled)
{
    if (m_warning_icon) m_warning_icon->Show(!enabled);
    UpdateScrollButtons();

    auto tooltip = enabled ? wxString() :
                             _L("This extruder has a different nozzle diameter from the selected printing diameter. "
                                "Printing with mixed nozzle diameters is experimental.");
    set_tooltip_recursive(this, tooltip, enabled);

    update_tile_row_line();
    Refresh();
}

// Single-diameter mode: the default projection for every extruder. Shows the nozzle(s) at the
// selected diameter as one tile. SingleExtruderPanel overrides this to add the experimental multi mode.
void ExtruderPanel::UpdateNozzles(const std::vector<NozzleConfig> &configs, const wxString &selected_diameter)
{
    m_summary_sizer->Clear(true); // destroy the tiles from the previous projection
    m_summaries.clear();

    // One pass to count and characterize the nozzles at the selected diameter.
    int              matching     = 0;
    NozzleVolumeType first_volume{};
    bool             mixed_volume = false;
    for (const auto &c : configs) {
        if (c.diameter != selected_diameter) continue;
        if (matching == 0)
            first_volume = c.volume;
        else if (c.volume != first_volume)
            mixed_volume = true;
        matching++;
    }

    if (configs.size() == 1) {
        // A single-nozzle extruder shows its own diameter/volume, unfiltered.
        add_summary_tile(configs[0].diameter, volume_short(configs[0].volume), 0, false);
    } else {
        wxString flow = matching == 0 ? wxString("--") : mixed_volume ? wxString("Mix") : volume_short(first_volume);
        add_summary_tile(selected_diameter, flow, matching, matching > 1 || (matching == 0 && m_type == RightExtruder));
    }

    finish_projection(matching > 0);
}

void ExtruderPanel::Rescale()
{
    if (GetSizer()) {
        GetSizer()->Layout();
    }

    Layout();
    Refresh();
}

void ExtruderPanel::set_ams_count(int n4, int n1)
{
    if (n4 == m_ams_n4 && n1 == m_ams_n1)
        return;

    m_ams_n4 = n4;
    m_ams_n1 = n1;
    update_ams();
}

// ----------------------------------------------------------------------------
// DualExtruderPanel — one side (left/right) of a dual card: scroll + AMS previews
// ----------------------------------------------------------------------------

void DualExtruderPanel::CreateContent(wxBoxSizer *main_sizer)
{
    m_scroll_timer = new wxTimer(this);
    Bind(wxEVT_TIMER, &DualExtruderPanel::OnScrollTimer, this);

    m_scroll = new HorizontalScrollablePanel(this);
    m_scroll->SetMinSize(wxSize(FromDIP(50), FromDIP(40)));
    m_ams_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_scroll->SetSizer(m_ams_sizer);

    for (size_t i = 0; i < 4; ++i) {
        AMSinfo info;
        info.ams_type = DevAmsType::AMS;
        auto preview  = new AMSPreview(m_scroll, wxID_ANY, info);
        preview->Close();
        m_ams_previews.push_back(preview);
        m_ams_sizer->Add(preview, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(2));
    }

    for (size_t i = 0; i < 8; ++i) {
        AMSinfo info;
        info.ams_type = DevAmsType::N3S;
        auto preview  = new AMSPreview(m_scroll, wxID_ANY, info);
        preview->Close();
        m_ams_previews.push_back(preview);
        m_ams_sizer->Add(preview, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(2));
    }

    m_left_scroll  = new ScalableButton(this, wxID_ANY, "left_arrow", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 12);
    m_right_scroll = new ScalableButton(this, wxID_ANY, "right_arrow", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 12);

    m_left_scroll->Bind(wxEVT_BUTTON, &DualExtruderPanel::OnLeftScrollClick, this);
    m_right_scroll->Bind(wxEVT_BUTTON, &DualExtruderPanel::OnRightScrollClick, this);

    m_left_scroll->Bind(wxEVT_LEFT_DOWN, &DualExtruderPanel::OnLeftScrollDown, this);
    m_left_scroll->Bind(wxEVT_LEFT_UP, &DualExtruderPanel::OnLeftScrollUp, this);
    m_left_scroll->Bind(wxEVT_LEAVE_WINDOW, &DualExtruderPanel::OnLeftScrollUp, this);

    m_right_scroll->Bind(wxEVT_LEFT_DOWN, &DualExtruderPanel::OnRightScrollDown, this);
    m_right_scroll->Bind(wxEVT_LEFT_UP, &DualExtruderPanel::OnRightScrollUp, this);
    m_right_scroll->Bind(wxEVT_LEAVE_WINDOW, &DualExtruderPanel::OnRightScrollUp, this);

    m_left_scroll->Show(false);
    m_right_scroll->Show(false);

    main_sizer->Add(m_scroll, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    main_sizer->Add(m_left_scroll, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(0));
    main_sizer->Add(m_right_scroll, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
}

void DualExtruderPanel::update_ams()
{
    if (!m_ams_sizer) return;

    static AMSinfo info4;
    static AMSinfo info1;
    static AMSinfo info_ext;
    if (info4.cans.empty()) {
        for (size_t i = 0; i < 4; ++i) {
            info4.cans.push_back({});
        }
        info1.ams_type = DevAmsType::N3S;
        info1.cans.push_back({});

        info_ext.ams_type = DevAmsType::EXT_SPOOL;
        info_ext.cans.push_back({});
    }

    m_ams_sizer->Clear(false);

    for (auto preview : m_ams_previews) {
        preview->Close();
    }

    size_t preview_idx = 0;

    // update 4-slot AMS（first 4 previews）
    for (size_t i = 0; i < m_ams_n4 && i < 4; ++i) {
        AMSinfo &ams_info = i < m_ams_4.size() ? m_ams_4[i] : info4;

        m_ams_previews[preview_idx]->Update(ams_info);
        m_ams_previews[preview_idx]->Refresh();
        m_ams_previews[preview_idx]->Open();

        m_ams_sizer->Add(m_ams_previews[preview_idx], 0, wxALL, FromDIP(2));
        preview_idx++;
    }

    // update 1-slot AMS（last 8 previews）
    size_t ams1_start_idx = 4;
    for (size_t i = 0; i < m_ams_n1 && i < 8; ++i) {
        AMSinfo &ams_info = i < m_ams_1.size() ? m_ams_1[i] : info1;

        size_t idx = ams1_start_idx + i;
        m_ams_previews[idx]->Update(ams_info);
        m_ams_previews[idx]->Refresh();
        m_ams_previews[idx]->Open();

        m_ams_sizer->Add(m_ams_previews[idx], 0, wxALL, FromDIP(2));
    }

    // wxWindow *parent_window = m_scroll ? m_scroll : m_ams_sizer->GetContainingWindow();
    // if (!m_ext.empty()) {
    //     while (m_ext_previews.size() < m_ext.size()) {
    //         AMSinfo info;
    //         info.ams_type = DevAmsType::N3S;
    //         auto ext_preview = new AMSPreview(parent_window, wxID_ANY, info);
    //         ext_preview->Close();
    //         m_ext_previews.push_back(ext_preview);
    //     }

    //     for (size_t i = 0; i < m_ext.size(); ++i) {
    //         if (i < m_ext_previews.size()) {
    //             AMSinfo &ext_info = m_ext[i];
    //             m_ext_previews[i]->Update(ext_info);
    //             m_ext_previews[i]->Refresh();
    //             m_ext_previews[i]->Open();

    //             m_ams_sizer->Add(m_ext_previews[i], 0, wxALL, FromDIP(2));
    //         }
    //     }
    // } else {
    //     if (m_ext_previews.empty()) {
    //         AMSinfo info;
    //         info.ams_type    = DevAmsType::EXT_SPOOL;
    //         auto ext_preview = new AMSPreview(parent_window, wxID_ANY, info);
    //         ext_preview->Close();
    //         m_ext_previews.push_back(ext_preview);
    //     }

    //     m_ext_previews[0]->Update(info_ext);
    //     m_ext_previews[0]->Refresh();
    //     m_ext_previews[0]->Open();
    //     m_ams_sizer->Add(m_ext_previews[0], 0, wxALL, FromDIP(2));
    // }

    Layout();

    if (m_scroll) {
        m_scroll->FitInside();
        int virtual_width = m_scroll->GetVirtualSize().GetWidth();
        int client_width = m_scroll->GetClientSize().GetWidth();

        if (virtual_width > client_width) {
            m_scroll->SetScrollbar(wxHORIZONTAL, 0, client_width, virtual_width, false);
        }

#ifdef __WXMSW__
        HWND hwnd = (HWND)m_scroll->GetHandle();
        if (hwnd) {
            ShowScrollBar(hwnd, SB_HORZ, FALSE);
            ShowScrollBar(hwnd, SB_VERT, FALSE);
        }
#endif
        hide_native_scrollbars(m_scroll); // the macOS half of the same cleanup
    }

    Refresh();

    CallAfter([this]() {
        UpdateScrollButtons();
    });
}

void DualExtruderPanel::OnLeftScrollClick(wxCommandEvent &evt)
{
    if (!m_scroll) return;

    int scroll_unit = FromDIP(5);
    int current_pos = m_scroll->GetScrollPos(wxHORIZONTAL);
    int new_pos     = std::max(0, current_pos - scroll_unit);

    m_scroll->Scroll(new_pos, -1);
    UpdateScrollButtons();
}

void DualExtruderPanel::OnRightScrollClick(wxCommandEvent &evt)
{
    if (!m_scroll) return;

    int scroll_unit = FromDIP(5);
    int current_pos = m_scroll->GetScrollPos(wxHORIZONTAL);
    int max_pos     = m_scroll->GetScrollRange(wxHORIZONTAL);
    int new_pos     = std::min(max_pos, current_pos + scroll_unit);

    m_scroll->Scroll(new_pos, -1);
    UpdateScrollButtons();
}

void DualExtruderPanel::OnScrollSizeChanged(wxSizeEvent &evt)
{
    UpdateScrollButtons();
    evt.Skip();
}

void DualExtruderPanel::UpdateScrollButtons()
{
    if (!m_scroll || !m_left_scroll || !m_right_scroll) return;

    int virtual_width = m_scroll->GetVirtualSize().GetWidth();
    int client_width  = m_scroll->GetClientSize().GetWidth();

    bool need_scroll = virtual_width > client_width;

    if (!need_scroll) {
        m_left_scroll->Show(false);
        m_right_scroll->Show(false);

        Layout();
        Refresh();
        return;
    }

    int current_pos = m_scroll->GetScrollPos(wxHORIZONTAL);
    int max_pos     = m_scroll->GetScrollRange(wxHORIZONTAL) - m_scroll->GetScrollThumb(wxHORIZONTAL);

    //m_left_scroll->Show(current_pos > 0);
    //m_left_scroll->Enable(current_pos > 0);

    //m_right_scroll->Show(current_pos < max_pos);
    //m_right_scroll->Enable(current_pos < max_pos);

    m_scroll->SetScrollbar(wxHORIZONTAL, m_scroll->GetScrollPos(wxHORIZONTAL), client_width, virtual_width, false);

#ifdef __WXMSW__
    HWND hwnd = (HWND) m_scroll->GetHandle();
    if (hwnd) {
        ShowScrollBar(hwnd, SB_HORZ, FALSE);
        ShowScrollBar(hwnd, SB_VERT, FALSE);
    }
#endif
    hide_native_scrollbars(m_scroll); // the macOS half of the same cleanup

    m_left_scroll->Show(true);
    m_right_scroll->Show(true);

    Layout();
    Refresh();
}

void DualExtruderPanel::OnLeftScrollDown(wxMouseEvent &evt)
{
    m_scroll_left = true;
    m_scroll_right = false;

    if (m_scroll) {
        int scroll_unit = FromDIP(5);
        int current_pos = m_scroll->GetScrollPos(wxHORIZONTAL);
        int new_pos = std::max(0, current_pos - scroll_unit);
        m_scroll->Scroll(new_pos, -1);
        UpdateScrollButtons();
    }

    m_scroll_timer->Start(50);
    evt.Skip();
}

void DualExtruderPanel::OnLeftScrollUp(wxMouseEvent &evt)
{
    m_scroll_left = false;
    if (!m_scroll_right) {
        m_scroll_timer->Stop();
    }
    evt.Skip();
}

void DualExtruderPanel::OnRightScrollDown(wxMouseEvent &evt)
{
    m_scroll_right = true;
    m_scroll_left = false;

    if (m_scroll) {
        int scroll_unit = FromDIP(5);
        int current_pos = m_scroll->GetScrollPos(wxHORIZONTAL);
        int max_pos = m_scroll->GetScrollRange(wxHORIZONTAL);
        int new_pos = std::min(max_pos, current_pos + scroll_unit);
        m_scroll->Scroll(new_pos, -1);
        UpdateScrollButtons();
    }

    m_scroll_timer->Start(50);
    evt.Skip();
}

void DualExtruderPanel::OnRightScrollUp(wxMouseEvent &evt)
{
    m_scroll_right = false;
    if (!m_scroll_left) {
        m_scroll_timer->Stop();
    }
    evt.Skip();
}

void DualExtruderPanel::OnScrollTimer(wxTimerEvent &evt)
{
    if (!m_scroll) return;

    int scroll_unit = FromDIP(5);
    int current_pos = m_scroll->GetScrollPos(wxHORIZONTAL);

    if (m_scroll_left) {
        int new_pos = std::max(0, current_pos - scroll_unit);
        if (new_pos != current_pos) {
            m_scroll->Scroll(new_pos, -1);
            UpdateScrollButtons();
        } else {
            m_scroll_timer->Stop();
            m_scroll_left = false;
        }
    } else if (m_scroll_right) {
        int max_pos = m_scroll->GetScrollRange(wxHORIZONTAL);
        int new_pos = std::min(max_pos, current_pos + scroll_unit);
        if (new_pos != current_pos) {
            m_scroll->Scroll(new_pos, -1);
            UpdateScrollButtons();
        } else {
            m_scroll_timer->Stop();
            m_scroll_right = false;
        }
    }
}

void DualExtruderPanel::Rescale()
{
    for (auto *ams : m_ams_previews) {
        if (ams) { ams->msw_rescale(); }
    }

    for (auto *ext : m_ext_previews) {
        if (ext) { ext->msw_rescale(); }
    }

    if (m_scroll) {
        m_scroll->SetScrollRate(FromDIP(5), FromDIP(5));
        m_scroll->FitInside();
    }

    ExtruderPanel::Rescale();
}

// ----------------------------------------------------------------------------
// ExtruderPanel — AMS state, parsed the same way for every card variant
// ----------------------------------------------------------------------------

void ExtruderPanel::sync_ams(
    MachineObject const *obj, std::vector<DevAms *> const &ams_internal, std::vector<DevAms *> const &ams4, std::vector<DevAms *> const &ams1, std::vector<DevAmsTray> const &ext)
{
    if (m_ams_4.empty() && ams4.empty() && m_ams_1.empty() && ams1.empty() && m_ext.empty() && ext.empty()) return;
    auto sync = [obj](std::vector<AMSinfo> &infos, std::vector<DevAms *> const &ams) -> bool {
        std::vector<AMSinfo> infos2;
        for (auto a : ams) {
            AMSinfo ams_info;
            ams_info.parse_ams_info(const_cast<MachineObject *>(obj), a, obj->GetFilaSystem()->IsDetectRemainEnabled(), obj->is_support_ams_humidity);
            infos2.push_back(ams_info);
        }
        if (infos == infos2) return false;

        for (size_t ams_index = 0; ams_index < infos2.size(); ++ams_index) {
            auto ams_info = infos2[ams_index];
            BOOST_LOG_TRIVIAL(info) << "ExtruderPanel::sync_ams - AMS Info: "
                                    << "ams_id = \"" << ams_info.ams_id << "\""
                                    << ", ams_type = " << static_cast<int>(ams_info.ams_type) << ", cans_count = " << ams_info.cans.size();

            for (size_t can_index = 0; can_index < ams_info.cans.size(); ++can_index) {
                const auto &can_info = ams_info.cans[can_index];

                BOOST_LOG_TRIVIAL(info) << "ExtruderPanel::sync_ams - Can[" << can_index << "]: "
                                        << "can_id = \"" << can_info.can_id << "\""
                                        << ", material_name = \"" << can_info.material_name.ToStdString() << "\""
                                        << ", material_colour = " << can_info.material_colour.GetAsString(2).ToStdString()
                                        << ", material_state = " << static_cast<int>(can_info.material_state) << ", multi_colours_count = " << can_info.material_cols.size();

                if (can_info.material_cols.size() > 1) {
                    std::string multi_colors_str;
                    for (size_t i = 0; i < can_info.material_cols.size(); ++i) {
                        if (i > 0) multi_colors_str += ",";
                        const auto &color = can_info.material_cols[i];
                        multi_colors_str += color.GetAsString(2).ToStdString();
                    }
                    BOOST_LOG_TRIVIAL(info) << "ExtruderPanel::sync_ams - Can[" << can_index << "] MultiColors: " << multi_colors_str;
                }
            }
        }

        infos.swap(infos2);
        return true;
    };

    auto sync_ext = [obj](std::vector<AMSinfo> &infos, std::vector<DevAmsTray> const &trays) -> bool {
        std::vector<AMSinfo> infos2;
        for (auto tray : trays) {
            AMSinfo ext_info;
            ext_info.parse_ext_info(const_cast<MachineObject *>(obj), tray);
            infos2.push_back(ext_info);
        }
        if (infos == infos2) return false;

        BOOST_LOG_TRIVIAL(info) << "ExtruderPanel::sync_ams - External material count: " << infos2.size();
        for (size_t i = 0; i < infos2.size(); ++i) {
            const auto &ext_info = infos2[i];
            BOOST_LOG_TRIVIAL(info) << "ExtruderPanel::sync_ams - Ext[" << i << "]: "
                                    << "tray_id = \"" << ext_info.ams_id << "\"";
            if (!ext_info.cans.empty()) {
                const auto &can = ext_info.cans[0];
                BOOST_LOG_TRIVIAL(info) << "  material_name = \"" << can.material_name.ToStdString() << "\""
                                        << ", colour = " << can.material_colour.GetAsString(2).ToStdString();
            }
        }

        infos.swap(infos2);
        return true;
    };

    if (sync(m_ams_4, ams4) || sync(m_ams_1, ams1) || sync(m_ams_internal, ams_internal) || sync_ext(m_ext, ext)) update_ams();
}

// ----------------------------------------------------------------------------
// SingleExtruderPanel — single head with multiple nozzles: one tile per nozzle slot
// ----------------------------------------------------------------------------

// Nothing but tiles: every slot tile carries its own number, so the card needs no content column
// next to them. The base still appends the trailing warning icon to this sizer.
void SingleExtruderPanel::CreateContent(wxBoxSizer * /*main_sizer*/) {}

// The nozzle rack stays on one line at the design's minimum tile width, so the panel is as wide as
// the whole rack; the card scrolls it rather than shrinking or clipping the trailing slots.
void SingleExtruderPanel::CreateExtraRows(wxBoxSizer *main_sizer)
{
    m_tile_row = new wxBoxSizer(wxHORIZONTAL);
    main_sizer->Add(m_tile_row, 0, wxEXPAND | wxTOP, FromDIP(8));

    m_ams_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Fixed pool sized to the per-extruder caps the AMS-count menu enforces, as on the dual card.
    // Previews are only opened and closed from here on, so changing the counts costs no rebuild.
    auto seed_previews = [this](size_t count, DevAmsType type) {
        for (size_t i = 0; i < count; ++i) {
            AMSinfo info;
            info.ams_type = type;
            auto *preview = new AMSPreview(this, wxID_ANY, info);
            // The preview draws over whatever was erased beneath it, and a window never inherits its
            // parent's colour, so without this nothing is erased and it renders on black.
            preview->SetBackgroundColour(GetBackgroundColour());
            preview->Close();
            m_ams_previews.push_back(preview);
        }
    };
    seed_previews(MAX_AMS_4_SLOT, DevAmsType::AMS);
    seed_previews(MAX_AMS_1_SLOT, DevAmsType::N3S);

    main_sizer->Add(m_ams_sizer, 0, wxEXPAND | wxTOP, FromDIP(4));
}

void SingleExtruderPanel::update_ams()
{
    if (!m_ams_sizer) return;

    m_ams_sizer->Clear(false); // detach only: the pool outlives every refresh
    for (auto *preview : m_ams_previews) preview->Close();

    // A unit the machine has not reported yet still gets a slot, drawn from an empty placeholder,
    // so the row matches the count the user configured instead of appearing one short.
    auto show_units = [this](size_t pool_offset, size_t wanted, const std::vector<AMSinfo> &known, DevAmsType type) {
        AMSinfo placeholder;
        placeholder.ams_type = type;
        placeholder.cans.resize(type == DevAmsType::AMS ? 4 : 1);

        for (size_t i = 0; i < wanted && pool_offset + i < m_ams_previews.size(); ++i) {
            auto *preview = m_ams_previews[pool_offset + i];
            preview->Update(i < known.size() ? known[i] : placeholder);
            preview->Refresh();
            preview->Open();
            m_ams_sizer->Add(preview, 0, wxALL, FromDIP(2));
        }
    };
    show_units(0, m_ams_n4, m_ams_4, DevAmsType::AMS);
    show_units(MAX_AMS_4_SLOT, m_ams_n1, m_ams_1, DevAmsType::N3S);

    Layout();
    Refresh();
}

// One tile per nozzle slot, numbered by its position on the machine. An empty slot stays visible
// and reads the same way the nozzle-settings card does: "Empty" over "--". Single-diameter mode
// dims every slot that cannot print at the selected diameter, including empty slots.
void SingleExtruderPanel::UpdateNozzles(const std::vector<NozzleConfig> &configs, const wxString &selected_diameter)
{
    m_tile_row->Clear(true); // destroy the tiles from the previous projection
    m_nozzle_tiles.clear();

    const bool multi_diameter = multi_nozzle_diameter_mode_enabled(wxGetApp().preset_bundle->project_config);

    int matching = 0;
    for (int i = 0; i < static_cast<int>(configs.size()); ++i) {
        const NozzleConfig &cfg = configs[i];
        const bool          empty = cfg.diameter.IsEmpty();
        const bool          on_selected_diameter = !empty && cfg.diameter == selected_diameter;
        if (on_selected_diameter) matching++;

        // Same copy as NozzleCard::refresh_values(): an empty slot has no diameter and no flow type.
        const wxString diameter = empty ? _L("Empty") : cfg.diameter;
        const wxString flow     = empty ? wxString("--") : volume_short(cfg.volume);
        auto          *tile     = new NozzleSlotTile(this, i + 1, diameter, flow, cfg.status);
        tile->SetDimmed(!multi_diameter && !on_selected_diameter);
        if (!m_nozzle_tiles.empty()) m_tile_row->AddSpacer(FromDIP(TILE_GAP));
        m_tile_row->Add(tile, 1, wxALIGN_CENTER_VERTICAL);
        wxGetApp().UpdateDarkUIWin(tile); // tiles are created after Build()'s one-time dark-mode pass
        m_nozzle_tiles.push_back(tile);
    }
    // Place the new tiles now: the card re-lays itself out next, but a panel whose size it leaves
    // unchanged is not laid out again.
    Layout();

    // Every configured diameter is valid in multi-diameter mode, so the mixed-diameter warning
    // only applies once a single diameter has been picked for the whole plate.
    finish_projection(multi_diameter || matching > 0);
}

CustomSeparator::CustomSeparator(wxWindow *parent, bool is_small) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    SetBackgroundColour(ThemeColor::Grey200);
    int size = is_small ? 9 : 20;

    SetMinSize(wxSize(FromDIP(size * 2), -1));

    m_normal_bitmap = ScalableBitmap(this, "fila_switch", size);
    m_error_bitmap  = ScalableBitmap(this, "fila_switch_error", size);

    CreateLayout();
}

void CustomSeparator::CreateLayout()
{
    auto main_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_separator_line = new wxPanel(this, wxID_ANY);
    m_separator_line->SetBackgroundColour(ThemeColor::Grey400);
    m_separator_line->SetSize(wxSize(FromDIP(1), -1));

    m_icon = new wxStaticBitmap(this, wxID_ANY, m_normal_bitmap.bmp(),
                                wxDefaultPosition, m_normal_bitmap.GetBmpSize());
    m_icon->SetBackgroundColour(GetBackgroundColour());
    m_icon->Hide();

    auto overlay_sizer = new wxBoxSizer(wxVERTICAL);
    overlay_sizer->AddStretchSpacer();
    overlay_sizer->Add(m_icon, 0, wxALIGN_CENTER_HORIZONTAL);
    overlay_sizer->AddStretchSpacer();

    main_sizer->AddStretchSpacer(1);
    main_sizer->Add(m_separator_line, 0, wxEXPAND);
    main_sizer->AddStretchSpacer(1);

    SetSizer(main_sizer);

    Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) {
        UpdatePosition();
        evt.Skip();
    });
}

void CustomSeparator::SetState(State state)
{
    if (m_state == state) return;

    m_state = state;

    switch (state) {
    case Normal:
        m_icon->SetBitmap(m_normal_bitmap.bmp());
        m_icon->Show();
        Show();
        break;

    case Error:
        m_icon->SetBitmap(m_error_bitmap.bmp());
        m_icon->Show();
        Show();
        break;

    case Hidden:
        m_icon->Hide();
        Show();
        break;
    }

    UpdatePosition();
    Refresh();
}

void CustomSeparator::UpdatePosition()
{
    if (!m_icon) return;

    wxSize panel_size = GetSize();
    wxSize icon_size  = m_icon->GetSize();

    int x = (panel_size.x - icon_size.x) / 2;
    int y;
    if (m_icon_y_offset < 0) {
        y = (panel_size.y - icon_size.y) / 2;
    } else {
        y = m_icon_y_offset - icon_size.y / 2;
    }

    y = std::max(0, std::min(y, panel_size.y - icon_size.y));

    m_icon->SetPosition(wxPoint(x, y));
    m_icon->Raise();
}

} // namespace Slic3r::GUI
