#include "ExtruderPanelGroup.hpp"

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
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"

namespace Slic3r::GUI {

namespace {
constexpr int HOVER_HIDE_DELAY_MS = 150; // grace after the pointer leaves, so a re-enter cancels the hide
constexpr int MODE_PILL_MARGIN_X  = 16;  // gap between the mode pill and the card's left edge

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
        , m_chevron(this, "drop_down", MODE_PILL_ICON_D)
    {}

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

ExtruderPanelGroup::ExtruderPanelGroup(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    SetBackgroundColour(ThemeColor::Grey200);
    Bind(wxEVT_PAINT, &ExtruderPanelGroup::OnPaint, this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent &) { SetHoverShown(false); }); // grace elapsed -> hide
    Bind(wxEVT_SIZE, &ExtruderPanelGroup::OnSize, this);
#ifdef __WXOSX__
    Bind(wxEVT_SIZE, &ExtruderPanelGroup::OnSizeBadge, this);
#endif
}

void ExtruderPanelGroup::Build()
{
    m_main_sizer = new wxBoxSizer(wxVERTICAL);

    // A variant with a header puts its settings entry there, so it also drops the trailing arrow
    // that would otherwise be the card's only hint that it opens a dialog.
    const bool has_header = CreateHeader(m_main_sizer);

    auto *content = new wxBoxSizer(wxHORIZONTAL);
    CreateExtruders(content);

    if (!has_header) {
        auto separator = new wxPanel(this, wxID_ANY);
        separator->SetBackgroundColour(ThemeColor::Grey400);
        separator->SetMinSize(wxSize(FromDIP(1), -1));
        content->Add(separator, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(4));

        auto arrow = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("arrow_right", this, 20), wxDefaultPosition, wxSize(FromDIP(20), FromDIP(20)), 0);
        arrow->SetMinSize(wxSize(FromDIP(20), FromDIP(20)));
        content->Add(arrow, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(10));
    }

    m_main_sizer->Add(content, 1, wxEXPAND);

    SetSizer(m_main_sizer);
    Layout();
    Fit();

    BindClickEvents();
    BindHoverEvents();
    ShowBadge(false);

    wxGetApp().UpdateDarkUIWin(this);
}

void ExtruderPanelGroup::OnPaint(wxPaintEvent &evt)
{
    wxAutoBufferedPaintDC dc(this);
    wxGraphicsContext    *gc   = wxGraphicsContext::Create(dc);
    wxSize                size = GetSize();

    // A control hung across the top edge pushes the border down to its vertical centre; the control
    // paints after us and its own fill covers the segment behind it, leaving the fieldset-legend look.
    const wxWindow *straddling = BorderStraddlingWidget();
    const int       border_top = straddling ? straddling->GetSize().y / 2 : 0;

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

// Bind LEFT_DOWN on every non-interactive window under the ClickSurfaces so they open the settings
// dialog (mouse events don't bubble to parents, so each window must carry its own handler).
// Unbind first so re-arming after a content rebuild (RebindClick) can't stack duplicate handlers,
// which would otherwise open the modal once per stacked bind.
void ExtruderPanelGroup::BindClickEvents()
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

        window->Unbind(wxEVT_LEFT_DOWN, &ExtruderPanelGroup::OnPanelClick, this); // avoid stacking on a RebindClick
        window->Bind(wxEVT_LEFT_DOWN, &ExtruderPanelGroup::OnPanelClick, this);

        for (auto *child : window->GetChildren()) { bind_recursive(child); }
    };

    for (auto *surface : ClickSurfaces()) bind_recursive(surface);
}

void ExtruderPanelGroup::OnPanelClick(wxMouseEvent &evt)
{
    // Hide the hover card before opening the modal dialog. A modal steals the mouse, so the leave
    // event that normally hides the card never arrives.
    m_hover_timer.Stop();
    SetHoverShown(false);

    if (m_on_click) { m_on_click(IsDual()); }
}

// Bind enter/leave on every descendant (unlike BindClickEvents, interactive controls are NOT
// pruned): enter/leave don't propagate to parents, so the whole panel surface must be covered
// for the hover card to stay up while the pointer moves across combos, buttons and previews.
// Unbind first so re-arming after a content rebuild (RebindHover) can't stack duplicate handlers.
void ExtruderPanelGroup::BindHoverEvents()
{
    std::function<void(wxWindow *)> bind_recursive = [this, &bind_recursive](wxWindow *window) {
        if (!window) return;
        for (auto evt : {wxEVT_ENTER_WINDOW, wxEVT_LEAVE_WINDOW}) {
            window->Unbind(evt, &ExtruderPanelGroup::OnHover, this); // avoid stacking on a RebindHover
            window->Bind(evt, &ExtruderPanelGroup::OnHover, this);
        }
        for (auto *child : window->GetChildren()) bind_recursive(child);
    };
    bind_recursive(this);
}

void ExtruderPanelGroup::UpdateNozzles(int index, const std::vector<NozzleConfig> &configs, const wxString &selected_diameter)
{
    ExtruderPanel *panel = GetPanel(index);
    if (!panel || configs.empty()) return;

    panel->UpdateNozzles(configs, selected_diameter);
    // The panel just rebuilt its tiles as new windows; re-arm the whole card so the body stays live.
    BindClickEvents();
    BindHoverEvents();
    FitToPanels();
}

// A panel that wrapped its tiles onto extra lines is taller than the size the card was fitted to, so
// the card has to be re-fitted and the sidebar re-laid out to make room for the new height.
void ExtruderPanelGroup::FitToPanels()
{
    // Hand the panels their current width first: a wrapped tile row only reports its real height
    // once it knows how wide it may be.
    Layout();

    const int height = m_main_sizer->GetMinSize().y;
    if (height <= 0 || GetMinSize().y == height) return;

    SetMinSize(wxSize(-1, height)); // the card frames its panels, so it takes their height

    if (wxWindow *parent = GetParent()) {
        parent->Layout();
        if (wxWindow *grand_parent = parent->GetParent()) grand_parent->Layout();
    }

    Layout();
    Refresh();
}

// The sidebar can be resized at any time, which changes how many tiles fit on a line, so the card
// re-fits itself whenever it is resized. Deferred and guarded: re-fitting resizes this window again.
void ExtruderPanelGroup::OnSize(wxSizeEvent &evt)
{
    evt.Skip();

    if (m_fit_pending) return;

    m_fit_pending = true;
    CallAfter([this]() {
        m_fit_pending = false;
        FitToPanels();
    });
}

// Enter shows the tip synchronously; a leave that truly exits the card arms a short grace timer that a
// re-enter cancels. Crossing between the card's own children keeps the pointer inside the rect, so it
// does nothing. Replaces the old per-event CallAfter geometry debounce.
void ExtruderPanelGroup::OnHover(wxMouseEvent &evt)
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
// while a settings dialog owns the panel (disabled) or the app is inactive.
void ExtruderPanelGroup::SetHoverShown(bool shown)
{
    return;
    if (shown && (!IsEnabled() || !wxGetApp().IsActive())) shown = false;
    if (shown == m_hover_shown) return;
    m_hover_shown = shown;
    // if (m_on_hover) m_on_hover(shown);
}

ExtruderPanel *ExtruderPanelGroup::GetPanel(int index) const
{
    if (index >= 0 && index < static_cast<int>(m_panels.size())) return m_panels[index];
    return nullptr;
}

void ExtruderPanelGroup::Rescale()
{
    for (auto *panel : m_panels)
        if (panel) panel->Rescale();

    Layout();
    Fit();
    FitToPanels();
    Refresh();
}

void ExtruderPanelGroup::ShowBadge(bool show)
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
void ExtruderPanelGroup::LayoutBadge()
{
    if (!m_badge) return;

    wxSize panel_size = GetSize();
    wxSize badge_size = m_badge->GetBestSize();

    const wxWindow *straddling = BorderStraddlingWidget();

    int x = panel_size.x - badge_size.x;
    int y = straddling ? straddling->GetSize().y / 2 : 0; // the card's own top edge, see OnPaint

    m_badge->SetPosition(wxPoint(x, y));
}

void ExtruderPanelGroup::OnSizeBadge(wxSizeEvent &evt)
{
    LayoutBadge();
    evt.Skip();
}
#endif

// ----------------------------------------------------------------------------
// DualExtruderPanelGroup
// ----------------------------------------------------------------------------

void DualExtruderPanelGroup::CreateExtruders(wxBoxSizer *content)
{
    auto left = new DualExtruderPanel(this, ExtruderPanel::LeftExtruder);
    content->Add(left, 1, wxEXPAND | wxALL, FromDIP(2));
    m_panels.push_back(left);

    m_separator = new CustomSeparator(this, true);
    content->Add(m_separator, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(4));

    auto right = new DualExtruderPanel(this, ExtruderPanel::RightExtruder);
    content->Add(right, 1, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, FromDIP(2));
    m_panels.push_back(right);
}

void DualExtruderPanelGroup::SetSeparatorState(CustomSeparator::State state)
{
    if (m_separator) m_separator->SetState(state);
}

CustomSeparator::State DualExtruderPanelGroup::GetSeparatorState() const { return m_separator ? m_separator->GetState() : CustomSeparator::State::Hidden; }

// ----------------------------------------------------------------------------
// SingleExtruderPanelGroup
// ----------------------------------------------------------------------------

void SingleExtruderPanelGroup::CreateExtruders(wxBoxSizer *content)
{
    // The spacers widen the side inset to the header row's 6px (4 here plus the body's own 2), so
    // the tiles line up under "Nozzle" instead of starting 4px further left. A wider border on the
    // item itself would push the top and bottom out with them, since a sizer item has only one.
    // The header stays put; everything below it scrolls as one when the sidebar gets too narrow.
    m_body = new HorizontalScrollView(this);
    content->AddSpacer(FromDIP(4));
    content->Add(m_body, 1, wxEXPAND | wxALL, FromDIP(2));
    content->AddSpacer(FromDIP(4));

    auto single = new SingleExtruderPanel(m_body->Content());
    m_body->ContentSizer()->Add(single, 0, wxEXPAND);
    m_panels.push_back(single);
}

bool SingleExtruderPanelGroup::Layout()
{
    if (m_body) m_body->Layout();
    return ExtruderPanelGroup::Layout();
}

// The mode pill rides the card's top border, so OnPaint has to know how tall it is. Reported only
// while it is shown: on a printer without the mode selector the card has an ordinary top border.
// Measured even while the pill is hidden, so the minimum stays put when the printer changes.
int SingleExtruderPanelGroup::MinWidth() const
{
    return m_mode_pill ? m_mode_pill->GetEffectiveMinSize().x + 2 * FromDIP(MODE_PILL_MARGIN_X) : 0;
}

wxWindow *SingleExtruderPanelGroup::BorderStraddlingWidget() const { return m_mode_pill && m_mode_pill->IsShown() ? m_mode_pill : nullptr; }

// Fully rounded with the brand outline, and opaque in the card's own background so it hides the
// stretch of border OnPaint runs behind it -- that hidden stretch is what makes the pill read as
// sitting on the border rather than below it.
void SingleExtruderPanelGroup::StyleModePill()
{
    if (!m_mode_pill) return;

    m_mode_pill->SetCornerRadius(FromDIP(MODE_PILL_H) / 2.0);
    m_mode_pill->SetBorderColor(ThemeColor::BrandGreen); // brand colour, identical in both themes
    m_mode_pill->SetBackgroundColor(GetBackgroundColour());
    m_mode_pill->SetFont(Label::Body_13);
}

void SingleExtruderPanelGroup::AttachDiameterPanel(DiameterButtonPanel *panel)
{
    if (!panel || m_diameter_panel == panel)
        return;

    panel->Reparent(m_body->Content());
    panel->SetShowTitle(false);

    // Above the nozzle panel, inside the body so the buttons scroll with the tiles. The body already
    // supplies the side inset.
    m_diameter_sizer = new wxBoxSizer(wxVERTICAL);
    m_diameter_sizer->AddSpacer(FromDIP(8));
    m_diameter_sizer->Add(panel, 0, wxEXPAND);
    m_diameter_sizer->AddSpacer(FromDIP(8));
    m_body->ContentSizer()->Insert(0, m_diameter_sizer, 0, wxEXPAND);
    m_diameter_panel = panel;

    ShowDiameterPanel(!panel->IsMultiDiameterMode());
}

void SingleExtruderPanelGroup::ShowDiameterPanel(bool show)
{
    if (!m_diameter_sizer)
        return;

    m_body->ContentSizer()->Show(m_diameter_sizer, show);
    Layout();
    FitToPanels();
}

void SingleExtruderPanelGroup::DetachDiameterPanel(wxWindow *new_parent)
{
    if (!m_diameter_panel || !new_parent)
        return;

    m_diameter_sizer->Detach(m_diameter_panel);
    m_body->ContentSizer()->Detach(m_diameter_sizer);
    delete m_diameter_sizer;
    m_diameter_sizer = nullptr;

    m_diameter_panel->Reparent(new_parent);
    m_diameter_panel->SetShowTitle(true);
    m_diameter_panel = nullptr;

    Layout();
    FitToPanels();
}

void SingleExtruderPanelGroup::Rescale()
{
    StyleModePill();
    ExtruderPanelGroup::Rescale();
}

// Two rows: the mode pill straddling the top border, then "Nozzle" on the left with the settings
// entry (gear + label + arrow) on the right. The entry is not a control of its own; its three
// windows are the card's only ClickSurfaces, so clicking any of them opens the dialog.
bool SingleExtruderPanelGroup::CreateHeader(wxBoxSizer *outer)
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

    auto *gear = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("settings", this, 16));
    row->Add(gear, 0, wxALIGN_CENTER_VERTICAL);

    auto *entry = new Label(this, _L("Settings"));
    entry->SetFont(Label::Body_13);
    entry->SetForegroundColour(ThemeColor::TextPrimary);
    entry->SetBackgroundColour(ThemeColor::Grey200);
    row->Add(entry, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(2));

    // right_arrow, not arrow_right: the design ends the row with a thin chevron, where arrow_right
    // is a full shafted arrow.
    auto *arrow = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("right_arrow", this, 16));
    row->Add(arrow, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(2));

    m_settings_entry = {gear, entry, arrow};

    // The design puts this row 20px below the card's top border. The pill above already accounts for
    // 12 of those -- its lower half sits inside the card -- so only the remainder is added here.
    outer->AddSpacer(FromDIP(8));
    outer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));
    return true;
}

} // namespace Slic3r::GUI
