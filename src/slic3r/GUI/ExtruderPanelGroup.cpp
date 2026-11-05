#include "ExtruderPanelGroup.hpp"

#include <algorithm>

#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/button.h>
#include <wx/combobox.h>
#include <wx/scrolbar.h>
#include <wx/textctrl.h>
#include <wx/spinctrl.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>

#include "I18N.hpp"
#include "GUI_App.hpp"
#include "ExtruderHelpers.hpp"
#include "wxExtensions.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/IconView.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"

namespace Slic3r::GUI {

namespace {
constexpr int HOVER_HIDE_DELAY_MS = 150; // grace after the pointer leaves, so a re-enter cancels the hide
constexpr int MODE_PILL_MARGIN_X  = 16;  // gap between the mode pill and the card's left edge
constexpr int HEADER_TOP         = 20;  // top border to the header row
constexpr int HEADER_GAP_PILL    = 8;   // the same, less the pill's lower half inside the card

/**
 * \brief The single-/multi-diameter mode selector, drawn as the pill the design puts on the
 *        nozzle card's top border.
 *
 * Only the contents are custom: the drop-down list, its events and the rounded border all come from
 * ComboBox. TextInput always draws its icon ahead of the label, while the design puts the chevron
 * after it, so this paints the label and chevron itself on top of the inherited StaticBox chrome.
 */
class ModePill : public ComboBox
{
public:
    explicit ModePill(wxWindow *parent)
        : ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, parent->FromDIP(wxSize(120, MODE_PILL_H)), 0, nullptr, wxCB_READONLY)
        , m_chevron(this, "drop_down_green", MODE_PILL_ICON_D)
    {
        // The pill is sized for the current label only; the list must fit its longest option.
        GetDropDown().SetUseContentWidth(true);
        GetDropDown().SetTrailingPadding(14);
    }

protected:
    void render(wxDC &dc) override
    {
        StaticBox::render(dc); // the rounded brand-green outline and the opaque fill behind it

        const wxSize size  = GetSize();
        const wxSize arrow = m_chevron.GetBmpSize();

        dc.SetFont(GetFont());
        dc.SetTextForeground(ThemeColor::BrandGreen);
        // wxWindow::GetLabel, as TextInput::render reads it: ComboBox::GetLabel would answer from
        // the hidden text control instead once this stopped being a read-only combo.
        const wxString label = wxWindow::GetLabel();
        const wxSize   text  = dc.GetTextExtent(label);
        dc.DrawText(label, FromDIP(MODE_PILL_PAD_X), (size.y - text.y) / 2);
        dc.DrawBitmap(m_chevron.bmp(), size.x - FromDIP(MODE_PILL_PAD_X) - arrow.x, (size.y - arrow.y) / 2, true);
    }

private:
    ScalableBitmap m_chevron;
};
} // namespace


NozzleRackPanelGroup::NozzleRackPanelGroup(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    // The design draws this card on white rather than the sidebar grey, which is what gives its
    // nozzle tiles their contrast. Set first: the header and body read it back.
    SetBackgroundColour(ThemeColor::White);
    Bind(wxEVT_PAINT, &NozzleRackPanelGroup::OnPaint, this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent &) { SetHoverShown(false); }); // grace elapsed -> hide
    Bind(wxEVT_SIZE, &NozzleRackPanelGroup::OnSize, this);
#ifdef __WXOSX__
    Bind(wxEVT_SIZE, &NozzleRackPanelGroup::OnSizeBadge, this);
#endif

    m_main_sizer = new wxBoxSizer(wxVERTICAL);
    CreateHeader(m_main_sizer);
    CreateBody(m_main_sizer);
    m_main_sizer->AddSpacer(FromDIP(4));

    SetSizer(m_main_sizer);
    Layout();
    Fit();

    BindClickEvents();
    BindHoverEvents();
    ShowBadge(false);

    wxGetApp().UpdateDarkUIWin(this);
}

// Two rows: the mode pill straddling the top border, then "Nozzle" on the left with the settings
// entry on the right.
void NozzleRackPanelGroup::CreateHeader(wxBoxSizer *outer)
{
    // Only the widget is ours; DiameterButtonPanel::AttachModePill fills it in and owns the mode.
    m_mode_pill = new ModePill(this);
    StyleModePill();
    outer->Add(m_mode_pill, 0, wxLEFT, FromDIP(MODE_PILL_MARGIN_X));

    auto *row = new wxBoxSizer(wxHORIZONTAL);

    auto *title = new Label(this, _L("Nozzle"));
    title->SetFont(Label::Body_12);
    title->SetForegroundColour(ThemeColor::TextPrimary);
    row->Add(title, 0, wxALIGN_CENTER_VERTICAL);

    row->AddStretchSpacer();

    row->Add(CreateSettingsEntry(), 0, wxALIGN_CENTER_VERTICAL);

    // Sized by Layout, which knows whether the pill is shown.
    m_header_gap = outer->AddSpacer(FromDIP(HEADER_GAP_PILL));
    outer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));
}

// Gear, label and chevron share one panel, so the gaps between them are clickable too. The panel
// is the card's only click surface.
wxWindow *NozzleRackPanelGroup::CreateSettingsEntry()
{
    m_settings_entry = new wxPanel(this);
    m_settings_entry->SetBackgroundColour(GetBackgroundColour());
    auto *sizer = new wxBoxSizer(wxHORIZONTAL);

    auto *gear = new IconView(m_settings_entry, "settings", 16);
    sizer->Add(gear, 0, wxALIGN_CENTER_VERTICAL);

    auto *label = new Label(m_settings_entry, _L("Settings"));
    label->SetFont(Label::Body_13);
    label->SetForegroundColour(ThemeColor::TextPrimary);
    label->SetBackgroundColour(GetBackgroundColour());
    sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(2));

    // right_arrow, not arrow_right: the design ends the row with a thin chevron, where arrow_right
    // is a full shafted arrow.
    auto *arrow = new IconView(m_settings_entry, "right_arrow", 16);
    sizer->Add(arrow, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(2));

    m_settings_entry->SetSizer(sizer);
    return m_settings_entry;
}

void NozzleRackPanelGroup::CreateBody(wxBoxSizer *outer)
{
    // The spacers widen the side inset to the header row's 6px (4 here plus the body's own 2), so
    // the tiles line up under "Nozzle" instead of starting 4px further left. A wider border on the
    // item itself would push the top and bottom out with them, since a sizer item has only one.
    // The header stays put; everything below it scrolls as one when the sidebar gets too narrow.
    auto *content = new wxBoxSizer(wxHORIZONTAL);
    m_body        = new HorizontalScrollView(this);
    content->AddSpacer(FromDIP(4));
    content->Add(m_body, 1, wxEXPAND | wxALL, FromDIP(2));
    content->AddSpacer(FromDIP(4));

    m_panel = new NozzleRackPanel(m_body->Content());
    m_body->ContentSizer()->Add(m_panel, 0, wxEXPAND);

    outer->Add(content, 1, wxEXPAND);
}

bool NozzleRackPanelGroup::Layout()
{
    // The header row sits a fixed distance below the top border. A shown pill already covers part of
    // that distance with its lower half, so the gap shrinks by that much while the pill shows.
    if (m_header_gap && m_mode_pill) {
        const int gap = FromDIP(m_mode_pill->IsShown() ? HEADER_GAP_PILL : HEADER_TOP);
        if (m_header_gap->GetMinSize().y != gap) {
            m_header_gap->AssignSpacer(wxSize(0, gap));
            RequestFit(); // the card's height changes with the gap
        }
    }

    if (m_body) m_body->Layout();
    return wxPanel::Layout();
}

int NozzleRackPanelGroup::BorderTop() const { return m_mode_pill && m_mode_pill->IsShown() ? m_mode_pill->GetSize().y / 2 : 0; }

void NozzleRackPanelGroup::OnPaint(wxPaintEvent &evt)
{
    wxAutoBufferedPaintDC dc(this);
    wxGraphicsContext    *gc   = wxGraphicsContext::Create(dc);
    wxSize                size = GetSize();

    // The pill paints after us and its own fill covers the segment of border behind it, which leaves
    // the fieldset-legend look.
    const int border_top = BorderTop();

    if (gc) {
        dc.Clear();
        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
        wxGraphicsPath path   = gc->CreatePath();
        double         radius = FromDIP(4);

        path.AddRoundedRectangle(0, border_top, size.x - 1, size.y - 1 - border_top, radius);

        auto  color = StateColor::darkModeColorFor(ThemeColor::Grey400);
        wxPen pen(color, FromDIP(1));
        gc->SetPen(pen);
        gc->StrokePath(path);

        delete gc;
    }

#ifdef __WXMSW__
    if (m_badge.bmp().IsOk()) {
        auto s = m_badge.bmp().GetScaledSize();
        dc.DrawBitmap(m_badge.bmp(), size.x - s.x, border_top); // pinned to the card's own top corner
    }
#endif
}

// Bind LEFT_DOWN on every non-interactive window under the settings entry so they open the settings
// dialog (mouse events don't bubble to parents, so each window must carry its own handler).
// Unbind first so a second call can't stack duplicate handlers, which would open the modal once per bind.
void NozzleRackPanelGroup::BindClickEvents()
{
    std::function<void(wxWindow *)> bind_recursive = [this, &bind_recursive](wxWindow *window) {
        if (!window) return;

        // ::ComboBox and ::Button are StaticBoxes, not their wx namesakes, so they need their own
        // tests -- without them they would open the settings dialog instead of acting on their own click.
        if (dynamic_cast<wxButton *>(window) || dynamic_cast<::Button *>(window) ||
            dynamic_cast<wxComboBox *>(window) || dynamic_cast<::ComboBox *>(window) ||
            dynamic_cast<ScalableButton *>(window) || dynamic_cast<wxScrollBar *>(window) ||
            dynamic_cast<wxTextCtrl *>(window) || dynamic_cast<wxSpinCtrl *>(window)) {
            return;
        }

        window->Unbind(wxEVT_LEFT_DOWN, &NozzleRackPanelGroup::OnPanelClick, this);
        window->Bind(wxEVT_LEFT_DOWN, &NozzleRackPanelGroup::OnPanelClick, this);

        for (auto *child : window->GetChildren()) { bind_recursive(child); }
    };

    bind_recursive(m_settings_entry);
}

void NozzleRackPanelGroup::OnPanelClick(wxMouseEvent &evt)
{
    // Hide the hover card before opening the modal dialog. A modal steals the mouse, so the leave
    // event that normally hides the card never arrives.
    m_hover_timer.Stop();
    SetHoverShown(false);

    if (m_on_click) { m_on_click(); }
}

// Bind enter/leave on every descendant (unlike BindClickEvents, interactive controls are NOT
// pruned): enter/leave don't propagate to parents, so the whole card surface must be covered
// for the hover card to stay up while the pointer moves across combos, buttons and previews.
// Unbind first so re-arming after a tile rebuild can't stack duplicate handlers.
void NozzleRackPanelGroup::BindHoverEvents()
{
    std::function<void(wxWindow *)> bind_recursive = [this, &bind_recursive](wxWindow *window) {
        if (!window) return;
        for (auto evt : {wxEVT_ENTER_WINDOW, wxEVT_LEAVE_WINDOW}) {
            window->Unbind(evt, &NozzleRackPanelGroup::OnHover, this);
            window->Bind(evt, &NozzleRackPanelGroup::OnHover, this);
        }
        for (auto *child : window->GetChildren()) bind_recursive(child);
    };
    bind_recursive(this);
}

void NozzleRackPanelGroup::UpdateNozzles(const std::vector<std::vector<NozzleConfig>> &extruders, const wxString &selected_diameter)
{
    const bool all_empty = std::all_of(extruders.begin(), extruders.end(), [](const auto &configs) { return configs.empty(); });
    if (all_empty) return;

    m_panel->UpdateNozzles(extruders, selected_diameter);
    // The panel just rebuilt its tiles as new windows; re-arm hover so the whole card stays live.
    BindHoverEvents();
    FitToPanels();
}

// The card frames its content, so whenever the content's height changes the card has to be re-fitted
// and the sidebar re-laid out to make room for the new height.
void NozzleRackPanelGroup::FitToPanels()
{
    // Hand the content its current width first, so it reports the height it needs at that width.
    Layout();

    const int height = m_main_sizer->GetMinSize().y;
    if (height <= 0 || GetMinSize().y == height) return;

    SetMinSize(wxSize(-1, height));

    if (wxWindow *parent = GetParent()) {
        parent->Layout();
        if (wxWindow *grand_parent = parent->GetParent()) grand_parent->Layout();
    }

    Layout();
    Refresh();
}

void NozzleRackPanelGroup::RequestFit()
{
    if (m_fit_pending) return;

    m_fit_pending = true;
    CallAfter([this]() {
        m_fit_pending = false;
        FitToPanels();
    });
}

// The sidebar can be resized at any time, so the card re-fits itself whenever it is resized. Deferred
// and merged: re-fitting resizes this window again.
void NozzleRackPanelGroup::OnSize(wxSizeEvent &evt)
{
    evt.Skip();
    RequestFit();
}

// Enter shows the tip synchronously; a leave that truly exits the card arms a short grace timer that a
// re-enter cancels. Crossing between the card's own children keeps the pointer inside the rect, so it
// does nothing.
void NozzleRackPanelGroup::OnHover(wxMouseEvent &evt)
{
    evt.Skip();
    if (evt.GetEventType() == wxEVT_ENTER_WINDOW) {
        m_hover_timer.Stop(); // cancel a pending hide
        SetHoverShown(true);
    } else if (!GetScreenRect().Contains(wxGetMousePosition())) {
        m_hover_timer.StartOnce(HOVER_HIDE_DELAY_MS);
    }
}

// The show is never deferred, so a stray enter can't surface the tip over a modal; it is also dropped
// while a settings dialog owns the card (disabled) or the app is inactive.
void NozzleRackPanelGroup::SetHoverShown(bool shown)
{
    return;
    if (shown && (!IsEnabled() || !wxGetApp().IsActive())) shown = false;
    if (shown == m_hover_shown) return;
    m_hover_shown = shown;
    // if (m_on_hover) m_on_hover(shown);
}

void NozzleRackPanelGroup::Rescale()
{
    StyleModePill();
    m_panel->Rescale();

    Layout();
    Fit();
    FitToPanels();
    Refresh();
}

void NozzleRackPanelGroup::ShowBadge(bool show)
{
#ifdef __WXMSW__
    if (show && m_badge.name() != "badge") {
        m_badge = ScalableBitmap(this, "badge", 18);
        Refresh();
    } else if (!show && !m_badge.name().empty()) {
        m_badge = ScalableBitmap{};
        Refresh();
    }
#endif
#ifdef __WXOSX__
    if (show && m_badge == nullptr) {
        m_badge = new ScalableButton(this, wxID_ANY, "badge", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, false, 18);
        m_badge->SetSize(m_badge->GetBestSize());
        m_badge->SetBackgroundColour(GetBackgroundColour());

        LayoutBadge();
    }
    if (m_badge) { m_badge->Show(show); }
#endif
}

#ifdef __WXOSX__
void NozzleRackPanelGroup::LayoutBadge()
{
    if (!m_badge) return;

    wxSize panel_size = GetSize();
    wxSize badge_size = m_badge->GetBestSize();

    m_badge->SetPosition(wxPoint(panel_size.x - badge_size.x, BorderTop())); // the card's own top edge, see OnPaint
}

void NozzleRackPanelGroup::OnSizeBadge(wxSizeEvent &evt)
{
    LayoutBadge();
    evt.Skip();
}
#endif

// Measured even while the pill is hidden, so the minimum stays put when the printer changes.
int NozzleRackPanelGroup::MinWidth() const
{
    return m_mode_pill ? m_mode_pill->GetEffectiveMinSize().x + 2 * FromDIP(MODE_PILL_MARGIN_X) : 0;
}

// Fully rounded with the brand outline, and opaque in the card's own background so it hides the
// stretch of border OnPaint runs behind it -- that hidden stretch is what makes the pill read as
// sitting on the border rather than below it.
void NozzleRackPanelGroup::StyleModePill()
{
    if (!m_mode_pill) return;

    m_mode_pill->SetCornerRadius(FromDIP(MODE_PILL_H) / 2.0);
    m_mode_pill->SetBorderColor(ThemeColor::BrandGreen); // brand colour, identical in both themes
    m_mode_pill->SetBackgroundColor(ThemeColor::White);
    m_mode_pill->SetFont(Label::Body_13);
}

void NozzleRackPanelGroup::AttachDiameterPanel(DiameterButtonPanel *panel)
{
    if (!panel || m_diameter_panel == panel)
        return;

    panel->Reparent(m_body->Content());
    panel->SetShowTitle(false);

    // Above the rack panel, inside the body so the buttons scroll with the tiles. The body already
    // supplies the side inset, and the rack panel's tile row supplies the gap below.
    m_diameter_sizer = new wxBoxSizer(wxVERTICAL);
    m_diameter_sizer->AddSpacer(FromDIP(8));
    m_diameter_sizer->Add(panel, 0, wxEXPAND);
    m_body->ContentSizer()->Insert(0, m_diameter_sizer, 0, wxEXPAND);
    m_diameter_panel = panel;

    ShowDiameterPanel(panel->ShowsDiameterButtons());
}

void NozzleRackPanelGroup::ShowDiameterPanel(bool show)
{
    if (!m_diameter_sizer)
        return;

    m_body->ContentSizer()->Show(m_diameter_sizer, show);
    Layout();
    FitToPanels();
}

void NozzleRackPanelGroup::DetachDiameterPanel(wxWindow *new_parent)
{
    if (!m_diameter_panel || !new_parent)
        return;

    m_diameter_sizer->Detach(m_diameter_panel);
    m_body->ContentSizer()->Detach(m_diameter_sizer);
    delete m_diameter_sizer;
    m_diameter_sizer = nullptr;

    // Outside the card no sizer places the panel, so a shown one would paint over its new parent.
    m_diameter_panel->Hide();
    m_diameter_panel->Reparent(new_parent);
    m_diameter_panel->SetShowTitle(true);
    m_diameter_panel = nullptr;

    Layout();
    FitToPanels();
}

} // namespace Slic3r::GUI
