#include "CascadeMenu.hpp"

#include <utility>

#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/dcmemory.h>
#include <wx/event.h>
#include <wx/graphics.h>
#include <wx/region.h>
#include <wx/sizer.h>
#ifdef __WIN32__
#include <wx/msw/wrapwin.h> // SetActiveWindow, for handing activation back across the cascade
#endif

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"

namespace Slic3r::GUI {
namespace {

// Fill/border/text as ThemeColor tokens; mapped through StateColor's dark map at paint time.
inline wxColour themed(const wxColour &c) { return wxGetApp().dark_mode() ? StateColor::darkModeColorFor(c) : c; }

// The design fades a whole row to 30%, but the default MSW renderer behind wxGCDC is GDI+, whose
// BeginLayer(opacity) is a no-op -- so blend the row's own colours toward the panel fill by hand
// instead of drawing the row through a transparency layer.
wxColour blended(const wxColour &fg, const wxColour &bg, double alpha)
{
    auto mix = [alpha](unsigned char f, unsigned char b) { return static_cast<unsigned char>(f * alpha + b * (1.0 - alpha) + 0.5); };
    return wxColour(mix(fg.Red(), bg.Red()), mix(fg.Green(), bg.Green()), mix(fg.Blue(), bg.Blue()));
}

constexpr double DIMMED_ALPHA = 0.3;

// The spec's 0.5px stroke. One device pixel: FromDIP(1) becomes two or three once the window is scaled.
constexpr int HAIRLINE_PX = 1;

// The hover tip chip, from the design: black fill, 4px corners, 10/4 padding, 12px text on a 24px
// line. Fixed colours rather than ThemeColor tokens -- the design specifies a black chip, and it
// already reads correctly against a dark surface.
constexpr int  TIP_PAD_X  = 10;
constexpr int  TIP_PAD_Y  = 4;
constexpr int  TIP_LINE_H = 24;
constexpr int  TIP_RADIUS = 4;
const wxColour TIP_BG{"#000000"};
const wxColour TIP_FG{"#D2D2D2"};

} // namespace

// ---------------------------------------------------------------------------
// CascadeMenuTip
// ---------------------------------------------------------------------------

CascadeMenuTip::CascadeMenuTip(wxWindow *parent) : wxPopupWindow(parent, wxBORDER_NONE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetFont(Label::Body_12);
    Bind(wxEVT_PAINT, &CascadeMenuTip::OnPaint, this);
}

void CascadeMenuTip::ShowBelow(const wxWindow *anchor, const wxString &text)
{
    if (anchor == nullptr) return;

    m_text = text;
    SetSize(GetTextExtent(m_text).x + FromDIP(2 * TIP_PAD_X), FromDIP(2 * TIP_PAD_Y + TIP_LINE_H));
    ApplyShape();

    // Under the row it belongs to, left edges aligned, separated by the menu's own row gap.
    const wxRect row = anchor->GetScreenRect();
    SetPosition(wxPoint(row.GetLeft(), row.GetBottom() + FromDIP(CascadeMenu::Gap)));

    Show();
    Refresh();
}

void CascadeMenuTip::ApplyShape()
{
    const wxSize sz = GetSize();
    if (sz.GetWidth() <= 0 || sz.GetHeight() <= 0) return;
    const int d = FromDIP(TIP_RADIUS * 2); // GDI ellipse size = 2*radius

#ifdef __WIN32__
    if (HWND hwnd = (HWND) GetHWND()) {
        HRGN body = CreateRoundRectRgn(0, 0, sz.GetWidth() + 1, sz.GetHeight() + 1, d, d);
        SetWindowRgn(hwnd, body, TRUE); // system takes ownership of body
    }
#else
    wxBitmap mask(sz.GetWidth(), sz.GetHeight());
    {
        wxMemoryDC dc(mask);
        dc.SetBackground(*wxBLACK_BRUSH);
        dc.Clear();
        dc.SetBrush(*wxWHITE_BRUSH);
        dc.SetPen(*wxWHITE_PEN);
        dc.DrawRoundedRectangle(0, 0, sz.GetWidth(), sz.GetHeight(), d / 2);
    }
    SetShape(wxRegion(mask, *wxBLACK));
#endif
}

void CascadeMenuTip::OnPaint(wxPaintEvent &)
{
    wxPaintDC          pdc(this);
    wxGCDC             dc(pdc);
    wxGraphicsContext *gc = dc.GetGraphicsContext();
    if (gc == nullptr) return;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    const wxSize sz = GetClientSize();
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(TIP_BG));
    gc->DrawRoundedRectangle(0, 0, sz.GetWidth(), sz.GetHeight(), FromDIP(TIP_RADIUS));

    gc->SetFont(GetFont(), TIP_FG);
    double tw = 0, th = 0;
    gc->GetTextExtent(m_text, &tw, &th);
    gc->DrawText(m_text, FromDIP(TIP_PAD_X), (sz.GetHeight() - th) / 2.0);
}

// ---------------------------------------------------------------------------
// CascadeMenuItem
// ---------------------------------------------------------------------------

CascadeMenuItem::CascadeMenuItem(wxWindow *parent, const wxString &text, bool enabled, bool checked, bool has_submenu)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_text(text)
    , m_enabled(enabled)
    , m_checked(checked)
    , m_has_submenu(has_submenu)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetFont(Label::Body_14);
    // The design sizes a panel to its widest row, so measure the label rather than reserving a
    // fixed width that a longer translation of a flow-type name would overflow.
    const int chrome = has_submenu
        ? CascadeMenu::Pad + CascadeMenu::TickD + CascadeMenu::Gap + CascadeMenu::LabelPadR +
              CascadeMenu::Gap + CascadeMenu::ChevronD + CascadeMenu::ItemPadR
        : CascadeMenu::Pad + CascadeMenu::TickD + CascadeMenu::Gap + CascadeMenu::LabelPadR + CascadeMenu::ItemPadR;
    SetMinSize(wxSize(GetTextExtent(m_text).x + FromDIP(chrome), FromDIP(CascadeMenu::ItemH)));

    if (m_checked) m_tick = create_scaled_bitmap("checked", this, CascadeMenu::TickD);
    if (m_has_submenu) m_chevron = create_scaled_bitmap("right_arrow", this, CascadeMenu::ChevronD);
    Bind(wxEVT_PAINT, &CascadeMenuItem::OnPaint, this);

    // Bound even on a disabled row, which a faded one always is: that is precisely the row that has
    // a tip to show. The highlight is suppressed in the paint path instead.
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &e) {
        m_hover = true;
        if (!m_tip.IsEmpty())
            if (auto *menu = dynamic_cast<CascadeMenuPopup *>(GetParent())) menu->ShowTip(this, m_tip);
        Refresh();
        e.Skip();
    });
    // Consumed on every row, disabled ones too: a press left unhandled bubbles into
    // wxPopupTransientWindow, which reads the completed click as a reason to dismiss the menu.
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { m_pressed = true; });
    Bind(wxEVT_LEFT_UP, &CascadeMenuItem::OnLeftUp, this);
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &e) {
        m_hover   = false;
        m_pressed = false; // dragging off the row cancels the pick, as in a native menu
        if (!m_tip.IsEmpty())
            if (auto *menu = dynamic_cast<CascadeMenuPopup *>(GetParent())) menu->HideTip();
        Refresh();
        e.Skip();
    });
}

void CascadeMenuItem::SetDimmed(bool dimmed)
{
    m_dimmed = dimmed;
    if (dimmed) m_enabled = false;
    Refresh();
}

void CascadeMenuItem::SetTip(const wxString &tip) { m_tip = tip; }

void CascadeMenuItem::OnLeftUp(wxMouseEvent &)
{
    // A release without a press on this row is the tail of some other click -- typically the one
    // that opened this menu under the pointer -- and must not pick.
    const bool pressed = std::exchange(m_pressed, false);
    if (!pressed || !m_enabled) return;

    // Screen position rather than the event's: on macOS a release can reach the row routed across
    // the cascade's levels, and each hop rewrites the event coordinates.
    if (!GetClientRect().Contains(ScreenToClient(wxGetMousePosition()))) return;

    // Not propagated: the popup's parent is the owning window, whose own wxEVT_BUTTON handlers
    // must never see a menu pick. Nothing is read after dispatch, since a pick may close the menu.
    wxCommandEvent evt(wxEVT_BUTTON, GetId());
    evt.SetEventObject(this);
    evt.StopPropagation();
    HandleWindowEvent(evt);
}

void CascadeMenuItem::OnPaint(wxPaintEvent &)
{
    wxPaintDC          pdc(this);
    wxGCDC             dc(pdc);
    wxGraphicsContext *gc = dc.GetGraphicsContext();
    if (gc == nullptr) return;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    const wxSize   sz   = GetClientSize();
    const wxColour fill = themed(ThemeColor::White);

    gc->SetPen(*wxTRANSPARENT_PEN);
    // The design marks the current row with a green wash rather than a highlight bar, so hover
    // paints over that wash instead of replacing it -- the row stays recognisably the current one.
    wxColour row_bg = m_checked ? themed(CascadeMenu::SelectedBg) : fill;
    if (m_dimmed) row_bg = blended(row_bg, fill, DIMMED_ALPHA);
    gc->SetBrush(wxBrush(row_bg));
    gc->DrawRectangle(0, 0, sz.GetWidth(), sz.GetHeight());

    // A row disabled only after construction still has its hover handlers bound, so the enabled
    // check has to be here rather than left to whether m_hover can ever become true.
    if (m_hover && m_enabled) {
        gc->SetBrush(wxBrush(themed(ThemeColor::Grey300)));
        gc->DrawRectangle(0, 0, sz.GetWidth(), sz.GetHeight());
    }

    // The tick column is reserved whether or not this row is the current one, so the labels of a
    // panel line up with each other.
    if (m_tick.IsOk()) {
        const wxSize ts = ScalableBitmap::GetBmpSize(m_tick);
        gc->DrawBitmap(m_tick, FromDIP(CascadeMenu::Pad), (sz.GetHeight() - ts.y) / 2.0, ts.x, ts.y);
    }

    wxColour fg = m_enabled ? themed(ThemeColor::TextPrimary) : themed(CascadeMenu::Hairline);
    if (m_dimmed) fg = blended(themed(ThemeColor::TextPrimary), fill, DIMMED_ALPHA);
    gc->SetFont(GetFont(), fg);
    double text_w = 0, text_h = 0;
    gc->GetTextExtent(m_text, &text_w, &text_h);
    (void) text_w;
    const double text_x = FromDIP(CascadeMenu::Pad + CascadeMenu::TickD + CascadeMenu::Gap);
    gc->DrawText(m_text, text_x, (sz.GetHeight() - text_h) / 2.0);

    if (m_chevron.IsOk()) {
        const wxSize cs = ScalableBitmap::GetBmpSize(m_chevron);
        gc->DrawBitmap(m_chevron, sz.GetWidth() - cs.x - FromDIP(CascadeMenu::ItemPadR), (sz.GetHeight() - cs.y) / 2.0, cs.x, cs.y);
    }
}

// ---------------------------------------------------------------------------
// CascadeMenuPopup
// ---------------------------------------------------------------------------

CascadeMenuPopup::CascadeMenuPopup(wxWindow *parent) : RoundedPopupWindow(parent, 0, true)
{
    // Square card: 0.5px #ACACAC stroke, drop shadow 0 / 4 / blur 2 at 25% black. Lengths on the
    // shadow are DIP and get scaled when it is painted.
    SetBorderColour(CascadeMenu::Hairline);
    SetBorderWidthPx(HAIRLINE_PX);
    m_shadow.SetParams(WindowShadowParams{2, 4, 0, 0.25});

#ifdef __WXOSX__
    // Take wx's capture juggling off this window and route between the levels ourselves.
    //
    // wxPopupTransientWindow::OnIdle releases the mouse capture while the pointer is over the
    // popup and takes it back when it leaves -- fine for one popup, ruinous for two. With both
    // levels up, the level the pointer is NOT on ends up holding the capture (idle events reach
    // the levels in creation order and the handler's position gate is a shared static, so only the
    // first level ever acts), and since cocoa forwards every mouse event in the application to
    // whatever holds the capture, the level under the pointer receives nothing: no hover on either
    // level once the submenu opens. The capture also accumulates, because CaptureMouse() happily
    // pushes a window that is already on wx's capture stack -- the guard against that is a debug
    // assert -- and those entries outlive the windows.
    //
    // A dynamic binding is searched before the class's own event table, so not skipping the event
    // here means wxPopupTransientWindow::OnIdle never runs. The capture then stays where Popup()
    // and Dismiss() put it, strictly last-in-first-out, and the routing below carries the events
    // from whichever level holds it to the one the pointer is actually on.
    Bind(wxEVT_IDLE, &CascadeMenuPopup::OnIdleNoop, this);
    for (auto evt : {wxEVT_MOTION, wxEVT_LEFT_UP, wxEVT_LEFT_DCLICK, wxEVT_MOUSEWHEEL})
        Bind(evt, &CascadeMenuPopup::OnCascadeMouse, this);
#endif
}

CascadeMenuPopup *CascadeMenuPopup::OtherLevel() const
{
    // Exactly one of the two is ever set: m_sub on a first-level menu, m_main on a second.
    return m_sub ? m_sub.get() : m_main.get();
}

bool CascadeMenuPopup::RouteToOtherLevel(wxMouseEvent &evt)
{
    // An event that got here by being routed is handled where it landed, never handed back.
    if (m_routed_in) return false;

    auto *other = OtherLevel();
    if (other == nullptr || !other->IsShown()) return false;

    // Coordinates are relative to whatever wx delivered the event to. Popup() keeps that this
    // popup, but ProcessLeftDown() is called on behalf of wx's own handler, so translate from the
    // window the event actually names rather than assuming it is this one.
    auto *source = dynamic_cast<wxWindow *>(evt.GetEventObject());
    if (source == nullptr) source = this;

    const wxPoint screen = source->ClientToScreen(evt.GetPosition());

    // The levels normally sit side by side, but wxPopupWindowBase::Position() flips a popup that
    // does not fit on the display back over its origin, shifted by its own width -- so a cascade
    // opened near the right edge has its submenu lying on top of the first level instead of beside
    // it. A point in that overlap is inside both rectangles and geometry cannot say whose it is;
    // the submenu's, being the level on top and the one holding the capture. Leaving such a point
    // where it arrived is also what stops the two levels routing it to each other for ever.
    const bool route = other->GetScreenRect().Contains(screen) && !GetScreenRect().Contains(screen);

#ifdef __WXOSX__
    // Whichever level the pointer is not on has to drop its highlight: it will not get a leave
    // event of its own, having stopped receiving events the moment the pointer left it. Only macOS
    // needs this -- it is the only port where a popup synthesizes the hover at all, the others
    // letting the rows get their own native enter and leave events.
    if (evt.GetEventType() == wxEVT_MOTION) (route ? this : other)->ClearHovered();
#endif

    if (!route) return false;

    wxMouseEvent copy(evt);
    copy.SetPosition(other->ScreenToClient(screen));
    copy.SetEventObject(other);

    // Scoped, so a handler that throws cannot leave the other level refusing to route for good.
    struct Routed {
        CascadeMenuPopup *level;
        explicit Routed(CascadeMenuPopup *l) : level(l) { level->m_routed_in = true; }
        ~Routed() { level->m_routed_in = false; }
    } routed(other);

    // Locally: this bypasses the popup handlers wx pushed onto the other level, whose click
    // handling would read a routed click as a click outside itself.
    other->ProcessEventLocally(copy);
    return true;
}

bool CascadeMenuPopup::ProcessLeftDown(wxMouseEvent &evt)
{
    return RouteToOtherLevel(evt);
}

#ifdef __WXOSX__

void CascadeMenuPopup::OnCascadeMouse(wxMouseEvent &evt)
{
    // Not routed means the pointer is over this level, or over neither: let PopupWindow's own
    // hit test have it. It is bound earlier, so skipping here passes the event straight to it.
    if (!RouteToOtherLevel(evt)) evt.Skip();
}

void CascadeMenuPopup::OnIdleNoop(wxIdleEvent &) {} // deliberately not skipped; see the ctor

#endif

void CascadeMenuPopup::ShowTip(const wxWindow *anchor, const wxString &text)
{
    if (anchor == nullptr || text.IsEmpty()) return;
    if (m_tip == nullptr) m_tip = new CascadeMenuTip(this);
    m_tip->ShowBelow(anchor, text);
}

void CascadeMenuPopup::HideTip()
{
    if (m_tip != nullptr) m_tip->Hide();
}

void CascadeMenuPopup::Popup(wxWindow *focus)
{
    // wxPopupTransientWindow::Popup() hands the mouse capture, and the event handler that drives
    // dismissal, to the popup's single child when it has exactly one. Here that is a plain row,
    // which carries none of the cascade's routing: cocoa would deliver every mouse event in the
    // application to that row, where a motion event is handled by nobody and does not propagate,
    // so neither level sees another one. Nothing recovers from that either -- the enter event that
    // opens the next submenu is itself synthesized from a motion event this popup no longer gets.
    // It would also misplace wx's own click test, which reads the child's rectangle as the popup's
    // and so treats a click on any other row as a click outside the menu.
    //
    // A flow menu with a single row is ordinary (a diameter made in one flow type only), so rather
    // than handle the one-child case, make sure it never arises. The filler is outside the sizer,
    // so it changes no layout, and hidden, so PopupWindow's hit test skips it.
    // Windows runs a different wxPopupTransientWindow that has no m_child and never moves the
    // capture off the popup, so it neither needs this nor should pay for an extra window.
#ifndef __WXMSW__
    if (GetChildren().GetCount() == 1) (new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(0, 0)))->Hide();
#endif

    RoundedPopupWindow::Popup(focus);
}

void CascadeMenuPopup::Dismiss()
{
    if (m_sub && m_sub->IsShown()) return; // the submenu holds the pointer, so this level stays
    HideTip();
    RoundedPopupWindow::Dismiss();
}

void CascadeMenuPopup::OnDismiss()
{
    HideTip();
    if (m_main) {
        // Closing the submenu either returns the user to the level that opened it or ends the
        // whole interaction; only the pointer's position tells the two apart. Ending it goes
        // through the one exit, so that this direction cleans up exactly like the other one --
        // with the submenu holding the capture, a click outside the cascade lands here.
        if (!m_main->GetScreenRect().Contains(wxGetMousePosition()))
            m_main->CloseCascade();
#ifdef __WIN32__
        else
            ::SetActiveWindow((HWND) m_main->GetHandle()); // take activation back off this popup
#endif
        return;
    }
    if (m_sub && m_sub->IsShown()) {
        // Dismiss() has just refused, and that refusal is meant for one caller only: the dismissal
        // wx sends this level when the submenu takes the activation (Windows) or the focus (macOS)
        // off it. The pointer is on this level's own row then, since that row is what opened the
        // submenu, so a pointer outside both levels means something else -- the user clicked away
        // from the whole cascade, and both levels have to go.
        //
        // This level cannot leave that to the click itself. On macOS the first level keeps the
        // mouse capture while the submenu is up, so a click outside arrives here, as a dismissal
        // the guard then swallows, and the submenu never hears about it at all. Windows only works
        // today because the click lands on the submenu instead, which does propagate upwards --
        // the branch above. This is that propagation in the other direction.
        const wxPoint pos = wxGetMousePosition();
        if (GetScreenRect().Contains(pos) || m_sub->GetScreenRect().Contains(pos)) return;

        CloseCascade();
        return;
    }
    RoundedPopupWindow::OnDismiss();
}

void CascadeMenuPopup::CloseCascade()
{
    auto *sub = m_sub.get();

    SetSubMenu(nullptr);     // release the guard, or neither Dismiss() below does anything
    if (sub) sub->Dismiss(); // the submenu popped last, so it goes first
    Dismiss();

    // Both levels are hidden now, but hidden is not enough -- see ReleaseCascadeCapture().
    ReleaseCascadeCapture(sub);

    if (m_destroy_on_close) {
        // Deferred to idle by wxPopupTransientWindowBase::Destroy(), so this is safe to call from
        // inside the click handler that brought us here. The capture is already clean, which it
        // has to be: the destructor will not clean it, and a freed window left on wx's capture
        // stack takes the whole application's mouse input down with it.
        if (sub) sub->Destroy();
        Destroy();
    } else {
        SetSubMenu(sub); // kept for the next gesture, so re-arm the guard
    }

    if (m_on_closed) m_on_closed();
}

// wx releases a capture by handing it to the next window on its stack, and on macOS
// wxPopupTransientWindow::OnIdle pushes a level back onto that stack every time the pointer leaves
// it -- so by the time a two-level menu closes, the stack holds several entries for the two levels
// and one Dismiss() per level pops at most one each. Whatever is left holding the capture swallows
// every mouse event in the application, shown or not: cocoa's WX_filterSendEvent forwards them all
// to wxWindow::GetCapture() and marks them handled.
void CascadeMenuPopup::ReleaseCascadeCapture(wxWindow *sub)
{
    // A level with a single row hands its capture to that row rather than keeping it, so what is
    // on the stack may be a descendant of either level, not the level itself.
    auto ours = [this, sub](wxWindow *win) {
        for (; win != nullptr; win = win->GetParent())
            if (win == this || win == sub) return true;
        return false;
    };

    // Each pass pops one entry, so the stack bounds the loop; the count is only a backstop against
    // an entry wx refuses to pop.
    for (int i = 0; i < 64; ++i) {
        wxWindow *captured = wxWindow::GetCapture();
        if (captured == nullptr || !ours(captured)) break;
        captured->ReleaseMouse();
    }
}

// ---------------------------------------------------------------------------
// Layout helpers
// ---------------------------------------------------------------------------

namespace CascadeMenu {

void AddItem(wxBoxSizer *box, CascadeMenuItem *item, bool first)
{
    auto *popup = item->GetParent();
    // The stroke is painted inside the client area, so the 4px inset starts inside that one pixel.
    const int edge = popup->FromDIP(Pad) + HAIRLINE_PX;
    box->AddSpacer(first ? edge : popup->FromDIP(Gap));
    box->Add(item, 0, wxEXPAND | wxLEFT | wxRIGHT, edge);
}

void Finish(CascadeMenuPopup *popup, wxBoxSizer *box)
{
    box->AddSpacer(popup->FromDIP(Pad) + HAIRLINE_PX);
    popup->SetSizer(box);
    popup->Finalize(); // sizes to content
}

void PositionSubMenu(CascadeMenuPopup *sub, const CascadeMenuPopup *main, const wxWindow *anchor)
{
    const wxRect main_rect = main->GetScreenRect();
    sub->Position(wxPoint(main_rect.GetRight() + sub->FromDIP(Gap),
                          anchor->GetScreenPosition().y - sub->FromDIP(Pad) - HAIRLINE_PX),
                  wxSize(0, 0));
}

} // namespace CascadeMenu

} // namespace Slic3r::GUI
