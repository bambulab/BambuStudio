#include "ExtruderPanel.hpp"

#include <wx/bitmap.h>
#include <wx/event.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/settings.h>
#include <wx/dcbuffer.h>
#include <wx/scrolbar.h>
#include <wx/eventfilter.h>
#include <wx/utils.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

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
static const wxColour SelectedTextColor    = ThemeColor::WhiteFixed;
static const wxColour HasNozzleBgColor     = ThemeColor::White;
static const wxColour HasNozzleBorderColor = ThemeColor::Grey400;
static const wxColour HasNozzleTextColor   = ThemeColor::TextPrimary;
static const wxColour NoNozzleBgColor      = ThemeColor::White;
static const wxColour NoNozzleBorderColor  = ThemeColor::Grey300;
static const wxColour NoNozzleTextColor    = ThemeColor::Grey400;

// Per-extruder AMS caps, matching the ones show_ams_add_menu enforces when adding a unit.
constexpr size_t MAX_AMS_4_SLOT = 4;
constexpr size_t MAX_AMS_1_SLOT = 8;
constexpr int    AMS_DIVIDER_H  = 16; // half the height of an AMS unit

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
    ApplyMode();
}

void DiameterButtonPanel::ApplyMode()
{
    UpdateSelectorVisibility();
    // A hosting card puts the panel inside its scrolling body, so the card is an ancestor, not the parent.
    for (wxWindow *window = GetParent(); window && !window->IsTopLevel(); window = window->GetParent()) {
        if (auto *card = dynamic_cast<NozzleRackPanelGroup *>(window)) {
            card->ShowDiameterPanel(ShowsDiameterButtons());
            break;
        }
    }
}

bool DiameterButtonPanel::ShowsDiameterButtons() const
{
    return !(m_show_multi_diameter_mode && m_mode_selector_visible && m_multi_diameter_mode_enabled);
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
    if (m_mode_selector_visible != show) {
        m_mode_selector_visible = show; // remembered so a later AttachModePill replays the state
        ApplyMode();                    // a hidden selector also takes the mode out of effect
    }
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
    const bool show = ShowsDiameterButtons();
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

    // A grey (NoNozzle) button cannot be picked; its tooltip says why. It stays enabled, because a
    // disabled window gets no mouse events and so would never show that tooltip.
    if (clicked_btn->IsGrayed()) return;

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
        : wxString::Format(_L("This printer isn’t configured with a %s mm nozzle. Please configure it in “Settings” before slicing."), btn->GetLabel());
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

// Prepare-page nozzle-slot tile geometry, taken from the design: a narrow card holding the
// hotend icon with its slot number stamped on it, then the diameter over the flow type.
constexpr int    TILE_W            = 37; // design minimum; overflow scrolls instead of shrinking tiles
constexpr int    TILE_GAP          = 4;
constexpr int    TILE_RADIUS       = 4;
constexpr int    TILE_ICON_H       = 38; // the hotend images' own height, so they draw 16 wide
constexpr int    TILE_ICON_TOP     = 4;  // tile top to the icon
constexpr int    TILE_BADGE_D      = 16;
constexpr int    TILE_BADGE_TEXT_W = 12; // widest label inside the badge circle; longer labels get a smaller font
constexpr int    TILE_TEXT_GAP     = 2;  // icon bottom to the diameter line
constexpr int    TILE_EMPTY_ICON   = 16; // the "no nozzle" glyph that stands in for both text lines
constexpr int    TILE_UNKNOWN_D    = 12; // the "?" badge on an unknown tool, below the slot label
constexpr int    TILE_UNKNOWN_TOP  = 16; // icon top to the "?" badge, clear of the slot label
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
 *
 * \return the font now set on \p gc
 */
static const wxFont &set_fitting_font(wxGraphicsContext &gc, const wxString &text, double max_width, const wxColour &colour)
{
    const wxFont *fonts[] = {&Label::Body_12, &Label::Body_11, &Label::Body_10, &Label::Body_9, &Label::Body_8};
    for (const wxFont *font : fonts) {
        gc.SetFont(*font, colour);
        double width = 0.0;
        gc.GetTextExtent(text, &width, nullptr);
        if (width <= max_width)
            return *font;
    }
    return *fonts[std::size(fonts) - 1];
}

/**
 * \brief One physical nozzle slot: the hotend icon carrying its slot label, then diameter + flow.
 *
 * A slot tile stands for exactly one slot, so the card can address nozzles by the label the user
 * sees on the machine. Painted whole rather than assembled from child windows: the badge has to sit
 * on top of the icon, and the dimmed state composites the entire tile at one opacity, which a tree
 * of labels and static bitmaps cannot do.
 */
class NozzleSlotTile : public wxPanel
{
public:
    /**
     * \param icon      hotend image name; the image must have the height of the single-head icon
     * \param label     short slot name stamped on the icon, such as "3", "L" or "R1"
     * \param diameter  first text line; ignored for an empty face, which shows a glyph instead
     * \param flow      second text line; ignored for an empty face
     * \param face      what the slot holds
     */
    NozzleSlotTile(wxWindow *parent, const std::string &icon, const wxString &label, const wxString &diameter, const wxString &flow, SlotFace face)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
        , m_icon(this, icon, TILE_ICON_H)
        , m_label(label)
        , m_diameter(diameter)
        , m_flow(flow)
    {
        if (face == SlotFace::Empty) m_empty_glyph.emplace(this, "nozzle_slot_empty", TILE_EMPTY_ICON);
        if (face == SlotFace::Unknown) m_unknown_badge.emplace(this, "hotend_unknown_badge", TILE_UNKNOWN_D);
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(parent->GetBackgroundColour()); // the surface the rounded corners reveal
        update_min_size();
        Bind(wxEVT_PAINT, &NozzleSlotTile::OnPaint, this);
    }

    /**
     * \brief Mark the slot as unusable in this mode, or usable again.
     *
     * \param reason  why the slot cannot be used, shown on hover; empty means usable.
     *                An unusable tile is faded out.
     */
    void SetUnavailable(const wxString &reason)
    {
        const bool dimmed = !reason.empty();
        reason.empty() ? UnsetToolTip() : SetToolTip(reason);
        if (m_dimmed == dimmed) return;
        m_dimmed = dimmed;
        Refresh();
    }

    /// \brief Reload the icon for the current DPI and light/dark theme.
    void Rescale()
    {
        m_icon.msw_rescale();
        if (m_empty_glyph) m_empty_glyph->msw_rescale();
        if (m_unknown_badge) m_unknown_badge->msw_rescale();
        update_min_size();
        Refresh();
    }

private:
    void OnPaint(wxPaintEvent &evt);

    /// \brief Size the tile to fit its icon and both text rows at any font, language or DPI.
    void update_min_size()
    {
        wxClientDC dc(this);
        dc.SetFont(Label::Body_12);
        const int line_h = dc.GetFontMetrics().height;
        const int height = FromDIP(TILE_ICON_TOP) + m_icon.GetBmpHeight() + FromDIP(TILE_TEXT_GAP) + 2 * line_h;
        SetMinSize(wxSize(FromDIP(TILE_W), height));
    }

    ScalableBitmap                m_icon;
    std::optional<ScalableBitmap> m_empty_glyph;   // set for an empty slot, drawn in place of the text lines
    std::optional<ScalableBitmap> m_unknown_badge; // set for an unknown tool, drawn over the hotend
    wxString                      m_label;
    wxString                      m_diameter;
    wxString                      m_flow;
    bool                          m_dimmed{false};
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
    auto badge_colour = StateColor::darkModeColorFor(wxColour("#ADADAD"));
    auto text_colour = StateColor::darkModeColorFor(ThemeColor::TextPrimary);
    // The badge stays light in both themes, so its label keeps the dark text colour.
    auto badge_text_colour = ThemeColor::TextPrimary;
    if (m_dimmed) {
        // BeginLayer(opacity) is not implemented by wxWidgets' default GDI+ renderer on Windows.
        // Blend every visual with the card surface instead, producing the same 30% result on every
        // graphics backend.
        tile_colour       = faded_tile_colour(tile_colour, background);
        badge_colour      = faded_tile_colour(badge_colour, background);
        text_colour       = faded_tile_colour(text_colour, background);
        badge_text_colour = faded_tile_colour(badge_text_colour, background);
    }

    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(tile_colour));
    gc->DrawRoundedRectangle(0, 0, size.x, size.y, FromDIP(TILE_RADIUS));

    const wxSize icon_size = m_icon.GetBmpSize();
    const double icon_x    = (size.x - icon_size.x) / 2.0;
    const double icon_y    = FromDIP(TILE_ICON_TOP);
    const wxBitmap icon = m_dimmed ? faded_tile_bitmap(m_icon.bmp(), background) : m_icon.bmp();
    gc->DrawBitmap(icon, icon_x, icon_y, icon_size.x, icon_size.y);
    if (m_unknown_badge) {
        const wxSize   mark_size = m_unknown_badge->GetBmpSize();
        const wxBitmap mark      = m_dimmed ? faded_tile_bitmap(m_unknown_badge->bmp(), background) : m_unknown_badge->bmp();
        gc->DrawBitmap(mark, (size.x - mark_size.x) / 2.0, icon_y + FromDIP(TILE_UNKNOWN_TOP), mark_size.x, mark_size.y);
    }

    // The label goes on the icon, not beside it: the tile is barely wider than the icon itself.
    // The badge stays a circle for every label; labels like "R1" take a smaller font instead.
    const double badge_d = FromDIP(TILE_BADGE_D);
    const double badge_x = (size.x - badge_d) / 2.0;
    const double badge_y = icon_y; // stamped on the icon's top edge
    gc->SetBrush(wxBrush(badge_colour));
    gc->DrawEllipse(badge_x, badge_y, badge_d, badge_d);

    // dc mirrors the label font so its metrics centre the ink on the badge.
    dc.SetFont(set_fitting_font(*gc, m_label, FromDIP(TILE_BADGE_TEXT_W), badge_text_colour));
    double text_w = 0.0, text_h = 0.0, descent = 0.0, leading = 0.0;
    gc->GetTextExtent(m_label, &text_w, &text_h, &descent, &leading);
    const wxFontMetrics label_metrics = dc.GetFontMetrics();
    const double        ink_mid       = (label_metrics.internalLeading + label_metrics.ascent) / 2.0;
    gc->DrawText(m_label, badge_x + (badge_d - text_w) / 2.0, badge_y + badge_d / 2.0 - ink_mid);

    // Rows keep the Body_12 line height so a shrunk line does not shift the one below it.
    dc.SetFont(Label::Body_12);
    const double line_h = dc.GetFontMetrics().height;
    double       y      = icon_y + icon_size.y + FromDIP(TILE_TEXT_GAP);
    if (m_empty_glyph) {
        // The glyph takes the whole text area, so an empty tile keeps the height of the others.
        const wxSize   glyph_size = m_empty_glyph->GetBmpSize();
        const wxBitmap glyph      = m_dimmed ? faded_tile_bitmap(m_empty_glyph->bmp(), background) : m_empty_glyph->bmp();
        gc->DrawBitmap(glyph, (size.x - glyph_size.x) / 2.0, y + (2 * line_h - glyph_size.y) / 2.0, glyph_size.x, glyph_size.y);
        return;
    }
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
// NozzleRackPanel — every extruder's slot tiles in one row, their AMS units below
// ----------------------------------------------------------------------------

namespace {
wxString volume_short(NozzleVolumeType volume) { return wxString::FromUTF8(DevNozzle::ToNozzleVolumeShortString(volume)); }

/**
 * \brief The label stamped on a slot tile.
 *
 * One extruder counts its slots 1..N. With two, a slot carries its side, L or R, followed by its
 * position unless it is the only slot on that side.
 */
wxString slot_label(int extruder_count, int extruder, int slot, int slots_on_extruder)
{
    if (extruder_count < 2) return wxString::Format("%d", slot + 1);

    const wxString side = extruder == 0 ? "L" : "R";
    return slots_on_extruder == 1 ? side : wxString::Format("%s%d", side, slot + 1);
}

// Why a slot cannot print at the selected diameter; empty when it can.
wxString unavailable_reason(SlotFace face, bool usable)
{
    if (usable) return {};
    switch (face) {
    case SlotFace::Cutter:
    case SlotFace::Pen: return _L("Settings for CutTool and DrawTool are not supported yet.");
    case SlotFace::Empty: return _L("No nozzle is installed in this slot.");
    case SlotFace::Unknown: return _L("Hotend information is unknown. Please refresh nozzle information before use.");
    default: return _L("Nozzle diameter mismatch. This nozzle cannot be used.");
    }
}

// The tile image for a face. The design has 2D tool art for the single-head tile only.
std::string tile_icon(SlotFace face, int extruder_count)
{
    if (extruder_count > 1) return "prepare_nozzle_normal_dua";
    switch (face) {
    case SlotFace::Cutter: return "prepare_nozzle_cutter_short";
    case SlotFace::Pen: return "prepare_nozzle_pen_short";
    default: return "prepare_nozzle_normal_short";
    }
}
} // namespace

NozzleRackPanel::NozzleRackPanel(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    SetForegroundColour(ThemeColor::Grey400);
    // The panel is a region of the card that frames it, so it takes the card's surface rather than
    // naming one of its own.
    SetBackgroundColour(parent->GetBackgroundColour());

    auto *main_sizer = new wxBoxSizer(wxVERTICAL);
    // Floor the panel on the sizer, not on the window: a min height on the window would also cap it.
    main_sizer->SetMinSize(wxSize(-1, FromDIP(40)));

    // The rack stays on one line at the design's minimum tile width, so the panel is as wide as the
    // whole rack; the card scrolls it rather than shrinking or clipping the trailing slots.
    m_tile_row = new wxBoxSizer(wxHORIZONTAL);
    main_sizer->Add(m_tile_row, 0, wxEXPAND | wxTOP, FromDIP(8));

    main_sizer->Add(CreateAmsRow(), 0, wxEXPAND | wxTOP, FromDIP(4));

    SetSizer(main_sizer);
    Layout();

    wxGetApp().UpdateDarkUIWin(this);
    CallAfter([this]() { update_ams(); });
}

// L and the left units, a divider, then R and the right units. Each extruder gets a fixed pool sized to the per-extruder
// caps the AMS-count menu enforces, so changing the counts only opens and closes previews.
wxSizer *NozzleRackPanel::CreateAmsRow()
{
    auto *row = new wxBoxSizer(wxHORIZONTAL);

    for (int extruder = 0; extruder < MAX_EXTRUDERS; ++extruder) {
        if (extruder > 0) {
            auto *line = new wxPanel(this, wxID_ANY);
            line->SetBackgroundColour(ThemeColor::Grey400);
            line->SetMinSize(wxSize(FromDIP(1), FromDIP(AMS_DIVIDER_H)));
            // The left gap is narrower: an AMS preview already draws inside its window with some
            // blank margin, so equal gaps would look wider on the left.
            auto *divider = new wxBoxSizer(wxHORIZONTAL);
            divider->AddSpacer(FromDIP(8));
            divider->Add(line, 0, wxALIGN_CENTER_VERTICAL);
            divider->AddSpacer(FromDIP(12));
            m_ams_divider = row->Add(divider, 0, wxEXPAND);
        }

        ExtruderAms &ams = m_ams[extruder];
        ams.side         = new Label(this, extruder == 0 ? "L" : "R");
        ams.side->SetFont(Label::Body_12);
        ams.side->SetForegroundColour(ThemeColor::TextPrimary);
        ams.side->SetBackgroundColour(GetBackgroundColour());
        ams.side->Hide();
        row->Add(ams.side, 0, wxALIGN_CENTER_VERTICAL);

        ams.sizer = new wxBoxSizer(wxHORIZONTAL);
        row->Add(ams.sizer, 0, wxEXPAND);

        auto seed_previews = [this, &ams](size_t count, DevAmsType type) {
            for (size_t i = 0; i < count; ++i) {
                AMSinfo info;
                info.ams_type = type;
                auto *preview = new AMSPreview(this, wxID_ANY, info);
                // The preview draws over whatever was erased beneath it, and a window never inherits
                // its parent's colour, so without this nothing is erased and it renders on black.
                preview->SetBackgroundColour(GetBackgroundColour());
                preview->Close();
                ams.previews.push_back(preview);
            }
        };
        seed_previews(MAX_AMS_4_SLOT, DevAmsType::AMS);
        seed_previews(MAX_AMS_1_SLOT, DevAmsType::N3S);
    }

    return row;
}

void NozzleRackPanel::UpdateNozzles(const std::vector<std::vector<NozzleConfig>> &extruders, const wxString &selected_diameter)
{
    m_extruders         = extruders;
    m_selected_diameter = selected_diameter;
    rebuild_tiles();
}

void NozzleRackPanel::sync_machine_slots(int extruder, std::vector<MachineSlot> slots)
{
    if (extruder < 0 || extruder >= MAX_EXTRUDERS) return;
    if (m_machine_slots[extruder] == slots) return; // runs on every printer message; redraw on change only
    m_machine_slots[extruder] = std::move(slots);
    if (!m_extruders.empty()) rebuild_tiles(); // else UpdateNozzles() applies the slots when it builds the tiles
}

void NozzleRackPanel::rebuild_tiles()
{
    m_tile_row->Clear(true); // destroy the tiles from the previous projection

    const int count  = std::min(static_cast<int>(m_extruders.size()), MAX_EXTRUDERS);
    m_extruder_count = std::max(count, 1);

    // Multi-diameter mode exists for single-extruder printers only.
    const bool multi_diameter = m_extruder_count == 1 && multi_nozzle_diameter_mode_enabled(wxGetApp().preset_bundle->project_config);
    m_ams_enabled.fill(true);
    const DynamicPrintConfig &printer_config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    if (m_extruder_count == 1 && is_single_ext_multi_nozzle(printer_config)) {
        const int ams_nozzle_idx = printer_config.opt_int("ams_nozzle_idx", -1);
        const bool valid_ams_nozzle = ams_nozzle_idx >= 0 && !m_extruders.empty() &&
                                      ams_nozzle_idx < static_cast<int>(m_extruders[0].size());
        m_ams_enabled[0] = valid_ams_nozzle && !m_extruders[0][ams_nozzle_idx].is_empty() &&
                           (multi_diameter || m_extruders[0][ams_nozzle_idx].diameter == m_selected_diameter);
    }

    for (int extruder = 0; extruder < count; ++extruder)
        add_extruder_tiles(extruder, multi_diameter);

    update_ams(); // the extruder count decides which AMS halves show; this also lays out the new tiles
}

void NozzleRackPanel::add_extruder_tiles(int extruder, bool multi_diameter)
{
    const std::vector<NozzleConfig> &configs = m_extruders[extruder];
    const std::vector<MachineSlot>  &machine = m_machine_slots[extruder];
    const int                        slots   = static_cast<int>(configs.size());
    for (int slot = 0; slot < slots; ++slot) {
        const NozzleConfig &cfg  = configs[slot];
        const auto          kind = slot < static_cast<int>(machine.size()) ? machine[slot] : MachineSlot::Blank;
        const bool on_selected = !cfg.is_empty() && cfg.diameter == m_selected_diameter;

        const SlotFace face = slot_face(cfg, kind);
        // A 2D tool or an unknown tool has no diameter or flow; an empty slot shows a glyph in place of both lines.
        const bool     tool     = face == SlotFace::Cutter || face == SlotFace::Pen || face == SlotFace::Unknown;
        const wxString diameter = tool ? wxString("-") : cfg.diameter;
        const wxString flow     = tool ? wxString() : volume_short(cfg.volume);
        const wxString label    = slot_label(m_extruder_count, extruder, slot, slots);
        auto          *tile     = new NozzleSlotTile(this, tile_icon(face, m_extruder_count), label, diameter, flow, face);
        // Single-diameter mode dims every slot that cannot print at the selected diameter and says why on hover.
        tile->SetUnavailable(unavailable_reason(face, multi_diameter || on_selected));

        if (!m_tile_row->IsEmpty()) m_tile_row->AddSpacer(FromDIP(TILE_GAP));
        m_tile_row->Add(tile, 1, wxALIGN_CENTER_VERTICAL);
        wxGetApp().UpdateDarkUIWin(tile); // tiles are created after the constructor's dark-mode pass
    }
}

void NozzleRackPanel::Rescale()
{
    for (wxSizerItem *item : m_tile_row->GetChildren())
        if (auto *tile = dynamic_cast<NozzleSlotTile *>(item->GetWindow())) tile->Rescale();

    Layout();
    Refresh();
}

void NozzleRackPanel::set_ams_count(int extruder, int n4, int n1)
{
    if (extruder < 0 || extruder >= MAX_EXTRUDERS) return;

    ExtruderAms &ams = m_ams[extruder];
    if (n4 == ams.n4 && n1 == ams.n1) return;

    ams.n4 = n4;
    ams.n1 = n1;
    update_ams();
}

std::pair<int, int> NozzleRackPanel::ams_count(int extruder) const
{
    if (extruder < 0 || extruder >= MAX_EXTRUDERS) return {0, 0};
    return {m_ams[extruder].n4, m_ams[extruder].n1};
}

void NozzleRackPanel::update_ams()
{
    auto       has_units = [](const ExtruderAms &ams) { return ams.n4 > 0 || ams.n1 > 0; };
    const bool dual      = m_extruder_count > 1;
    for (int extruder = 0; extruder < MAX_EXTRUDERS; ++extruder) {
        ExtruderAms &ams = m_ams[extruder];
        show_ams_units(extruder, ams, extruder < m_extruder_count, m_ams_enabled[extruder]);
        ams.side->Show(dual && has_units(ams)); // a side label names units, so it needs some
    }
    // The divider separates the left units from the right ones, so it needs units on both sides.
    m_ams_divider->Show(dual && has_units(m_ams[0]) && has_units(m_ams[1]));

    Layout();
    Refresh();
}

// A unit the machine has not reported yet still gets a slot, drawn from an empty placeholder, so the
// row matches the count the user configured instead of appearing one short.
void NozzleRackPanel::show_ams_units(int extruder, ExtruderAms &ams, bool visible, bool enabled)
{
    ams.sizer->Clear(false); // detach only: the pool outlives every refresh
    for (auto *preview : ams.previews) preview->Close();
    if (!visible) return;

    std::vector<std::string> ids4;
    std::vector<std::string> ids1;
    for (const AMSinfo &info : ams.ams_4) ids4.push_back(info.ams_id);
    for (const AMSinfo &info : ams.ams_1) ids1.push_back(info.ams_id);
    if (!ids4.empty() || !ids1.empty())
        wxGetApp().preset_bundle->initialize_ams_selection(extruder, ams.device_id, ids4, ids1);
    const ExtruderAmsSelection *selection = wxGetApp().preset_bundle->get_ams_selection(extruder);

    auto show_units = [this, &ams, enabled](size_t pool_offset, size_t wanted, const std::vector<std::string> *selected,
                                            const std::vector<AMSinfo> &known, DevAmsType type) {
        AMSinfo placeholder;
        placeholder.ams_type = type;
        placeholder.cans.resize(type == DevAmsType::AMS ? 4 : 1);
        if (!enabled) {
            for (Caninfo &can : placeholder.cans)
                can.material_colour = AMS_CONTROL_DISABLE_COLOUR;
        }

        for (size_t i = 0; i < wanted && pool_offset + i < ams.previews.size(); ++i) {
            auto *preview = ams.previews[pool_offset + i];
            const AMSinfo *content = nullptr;
            if (enabled) {
                if (selected && i < selected->size() && !(*selected)[i].empty()) {
                    const auto found = std::find_if(known.begin(), known.end(), [&](const AMSinfo &info) {
                        return info.ams_id == (*selected)[i];
                    });
                    if (found != known.end()) content = &*found;
                } else if (!selected && i < known.size()) {
                    content = &known[i];
                }
            }
            preview->Update(content ? *content : placeholder);
            preview->Enable(enabled);
            preview->Refresh();
            preview->Open();
            ams.sizer->Add(preview, 0, wxALL, FromDIP(2));
        }
    };
    const auto *selected4 = selection && selection->initialized ? &selection->ams4 : nullptr;
    const auto *selected1 = selection && selection->initialized ? &selection->ams1 : nullptr;
    show_units(0, ams.n4, selected4, ams.ams_4, DevAmsType::AMS);
    show_units(MAX_AMS_4_SLOT, ams.n1, selected1, ams.ams_1, DevAmsType::N3S);
}

void NozzleRackPanel::sync_ams(int                            extruder,
                               MachineObject const           *obj,
                               std::vector<DevAms *> const   &ams_internal,
                               std::vector<DevAms *> const   &ams4,
                               std::vector<DevAms *> const   &ams1,
                               std::vector<DevAmsTray> const &ext)
{
    if (extruder < 0 || extruder >= MAX_EXTRUDERS) return;

    ExtruderAms &state = m_ams[extruder];
    const bool device_changed = obj && state.device_id != obj->get_dev_id();
    if (obj) state.device_id = obj->get_dev_id();
    if (!device_changed && state.ams_4.empty() && ams4.empty() && state.ams_1.empty() && ams1.empty() &&
        state.ext.empty() && ext.empty())
        return;
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
            BOOST_LOG_TRIVIAL(info) << "NozzleRackPanel::sync_ams - AMS Info: "
                                    << "ams_id = \"" << ams_info.ams_id << "\""
                                    << ", ams_type = " << static_cast<int>(ams_info.ams_type) << ", cans_count = " << ams_info.cans.size();

            for (size_t can_index = 0; can_index < ams_info.cans.size(); ++can_index) {
                const auto &can_info = ams_info.cans[can_index];

                BOOST_LOG_TRIVIAL(info) << "NozzleRackPanel::sync_ams - Can[" << can_index << "]: "
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
                    BOOST_LOG_TRIVIAL(info) << "NozzleRackPanel::sync_ams - Can[" << can_index << "] MultiColors: " << multi_colors_str;
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

        BOOST_LOG_TRIVIAL(info) << "NozzleRackPanel::sync_ams - External material count: " << infos2.size();
        for (size_t i = 0; i < infos2.size(); ++i) {
            const auto &ext_info = infos2[i];
            BOOST_LOG_TRIVIAL(info) << "NozzleRackPanel::sync_ams - Ext[" << i << "]: "
                                    << "tray_id = \"" << ext_info.ams_id << "\"";
            if (!ext_info.cans.empty()) {
                const auto &can = ext_info.cans[0];
                BOOST_LOG_TRIVIAL(info) << "  material_name = \"" << can.material_name << "\""
                                        << ", colour = " << can.material_colour.GetAsString(2).ToStdString();
            }
        }

        infos.swap(infos2);
        return true;
    };

    // Every list is re-read even after one of them changed, so none is left stale until the next sync.
    bool changed = device_changed;
    changed |= sync(state.ams_4, ams4);
    changed |= sync(state.ams_1, ams1);
    changed |= sync(state.ams_internal, ams_internal);
    changed |= sync_ext(state.ext, ext);
    if (changed) update_ams();
}

CustomSeparator::CustomSeparator(wxWindow *parent, bool is_small) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    SetBackgroundColour(ThemeColor::Grey200);
    int size = is_small ? 9 : 20;

    SetMinSize(wxSize(FromDIP(size * 2), -1));

    m_normal_bitmap = ScalableBitmap(this, "fila_switch", size);
    m_error_bitmap  = ScalableBitmap(this, "fila_switch_error", size);

    // Line and icon are painted in one pass, not as overlapping child windows: wxMSW does not clip
    // siblings, so a child icon over a child line only stays on top by repaint-order luck.
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &CustomSeparator::on_paint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent &evt) {
        Refresh(); // the line spans the full height and the icon may be centred
        evt.Skip();
    });
    Bind(wxEVT_MOTION, [this](wxMouseEvent &evt) {
        SetCursor(icon_rect().Contains(evt.GetPosition()) ? wxCURSOR_HAND : wxCURSOR_ARROW);
        evt.Skip();
    });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &evt) {
        if (icon_rect().Contains(evt.GetPosition())) show_filament_switcher_tip(m_state == Normal);
        evt.Skip();
    });
    Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent &evt) {
        m_normal_bitmap.msw_rescale();
        m_error_bitmap.msw_rescale();
        SetMinSize(wxSize(2 * m_normal_bitmap.GetBmpWidth(), -1));
        evt.Skip();
    });
}

void CustomSeparator::SetState(State state)
{
    if (m_state == state) return;
    m_state = state;
    Refresh();
}

void CustomSeparator::SetIconYOffset(int offset)
{
    if (m_icon_y_offset == offset) return;
    m_icon_y_offset = offset;
    Refresh();
}

wxRect CustomSeparator::icon_rect() const
{
    if (m_state == Hidden) return {};

    const wxSize panel = GetClientSize();
    const wxSize icon  = m_normal_bitmap.GetBmpSize();
    const int    x     = (panel.x - icon.x) / 2;
    int          y     = m_icon_y_offset < 0 ? (panel.y - icon.y) / 2 : m_icon_y_offset - icon.y / 2;
    y                  = std::max(0, std::min(y, panel.y - icon.y));
    return {wxPoint(x, y), icon};
}

void CustomSeparator::on_paint(wxPaintEvent &)
{
    wxAutoBufferedPaintDC dc(this);
    // Colours are read here, not cached, so a theme switch shows on the next repaint.
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();

    const wxSize panel = GetClientSize();
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(StateColor::darkModeColorFor(ThemeColor::Grey400)));
    dc.DrawRectangle(panel.x / 2, 0, std::max(1, FromDIP(1)), panel.y);

    const wxRect icon = icon_rect();
    if (!icon.IsEmpty()) dc.DrawBitmap((m_state == Normal ? m_normal_bitmap : m_error_bitmap).bmp(), icon.GetTopLeft(), true);
}

} // namespace Slic3r::GUI
