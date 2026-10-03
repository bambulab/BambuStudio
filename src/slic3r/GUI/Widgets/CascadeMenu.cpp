#include "CascadeMenu.hpp"

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
        ? CascadeMenu::Pad + CascadeMenu::TickD + CascadeMenu::Gap +
              CascadeMenu::Gap + CascadeMenu::ChevronD + CascadeMenu::ItemPadR
        : CascadeMenu::Pad + CascadeMenu::TickD + CascadeMenu::Gap + CascadeMenu::ItemPadR;
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
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &e) {
        m_hover = false;
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
}

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
        // whole interaction; only the pointer's position tells the two apart.
        if (!m_main->GetScreenRect().Contains(wxGetMousePosition()))
            m_main->DismissAndNotify();
#ifdef __WIN32__
        else
            ::SetActiveWindow((HWND) m_main->GetHandle()); // take activation back off this popup
#endif
        return;
    }
    if (m_sub && m_sub->IsShown()) return;
    RoundedPopupWindow::OnDismiss();
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
