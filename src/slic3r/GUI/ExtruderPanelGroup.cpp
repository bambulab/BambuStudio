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

#include "GUI_App.hpp"
#include "ExtruderHelpers.hpp"
#include "wxExtensions.hpp"
#include "Widgets/StateColor.hpp"

namespace Slic3r::GUI {

namespace {
constexpr int HOVER_HIDE_DELAY_MS = 150; // grace after the pointer leaves, so a re-enter cancels the hide
}

ExtruderPanelGroup::ExtruderPanelGroup(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    SetBackgroundColour(ThemeColor::Grey200);
    Bind(wxEVT_PAINT, &ExtruderPanelGroup::OnPaint, this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent &) { SetHoverShown(false); }); // grace elapsed -> hide
}

void ExtruderPanelGroup::Build()
{
    m_main_sizer = new wxBoxSizer(wxHORIZONTAL);

    CreateExtruders(m_main_sizer);

    auto separator = new wxPanel(this, wxID_ANY);
    separator->SetBackgroundColour(ThemeColor::Grey400);
    separator->SetMinSize(wxSize(FromDIP(1), -1));
    m_main_sizer->Add(separator, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(4));

    auto arrow = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("arrow_right", this, 20), wxDefaultPosition, wxSize(FromDIP(20), FromDIP(20)), 0);
    arrow->SetMinSize(wxSize(FromDIP(20), FromDIP(20)));
    m_main_sizer->Add(arrow, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(10));

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
    if (gc) {
        dc.Clear();
        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
        wxGraphicsPath path   = gc->CreatePath();
        double         radius = FromDIP(2);

        path.AddRoundedRectangle(0, 0, size.x - 1, size.y - 1, radius);

        auto  color = StateColor::darkModeColorFor(ThemeColor::Grey400);
        wxPen pen(color, FromDIP(1));
        gc->SetPen(pen);
        gc->StrokePath(path);

        delete gc;
    }

#ifdef __WXMSW__
    if (m_badge.bmp().IsOk()) {
        auto s = m_badge.bmp().GetScaledSize();
        dc.DrawBitmap(m_badge.bmp(), GetSize().x - s.x, 0);
    }
#endif
}

// Bind LEFT_DOWN on every non-interactive descendant so the whole card body opens the settings
// dialog (mouse events don't bubble to parents, so each surface must carry its own handler).
// Unbind first so re-arming after a content rebuild (RebindClick) can't stack duplicate handlers,
// which would otherwise open the modal once per stacked bind.
void ExtruderPanelGroup::BindClickEvents()
{
    std::function<void(wxWindow *)> bind_recursive = [this, &bind_recursive](wxWindow *window) {
        if (!window) return;

        if (dynamic_cast<wxButton *>(window) || dynamic_cast<wxComboBox *>(window) || dynamic_cast<ScalableButton *>(window) || dynamic_cast<wxScrollBar *>(window) ||
            dynamic_cast<wxTextCtrl *>(window) || dynamic_cast<wxSpinCtrl *>(window)) {
            return;
        }

        window->Unbind(wxEVT_LEFT_DOWN, &ExtruderPanelGroup::OnPanelClick, this); // avoid stacking on a RebindClick
        window->Bind(wxEVT_LEFT_DOWN, &ExtruderPanelGroup::OnPanelClick, this);

        for (auto *child : window->GetChildren()) { bind_recursive(child); }
    };

    bind_recursive(this);
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
    if (shown && (!IsEnabled() || !wxGetApp().IsActive())) shown = false;
    if (shown == m_hover_shown) return;
    m_hover_shown = shown;
    if (m_on_hover) m_on_hover(shown);
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
        m_badge->SetBackgroundColour(ThemeColor::Grey200);

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

    int x = panel_size.x - badge_size.x - FromDIP(8);
    int y = FromDIP(8);

    m_badge->SetPosition(wxPoint(x, y));
}

void ExtruderPanelGroup::OnSize(wxSizeEvent &evt)
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
    auto single = new SingleExtruderPanel(this);
    content->Add(single, 1, wxEXPAND | wxALL, FromDIP(2));
    m_panels.push_back(single);
}

} // namespace Slic3r::GUI
