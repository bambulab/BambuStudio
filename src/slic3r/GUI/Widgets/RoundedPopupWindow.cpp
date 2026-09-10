#include "RoundedPopupWindow.hpp"

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/dcmemory.h>
#include <wx/gdicmn.h>
#include <wx/graphics.h>
#include <wx/region.h>
#ifdef __WIN32__
#include <wx/msw/wrapwin.h>
#endif

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"

namespace Slic3r::GUI {

RoundedPopupWindow::RoundedPopupWindow(wxWindow *parent, int corner_radius, bool with_shadow)
    : PopupWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS)
    , m_radius(corner_radius)
    , m_with_shadow(with_shadow)
    , m_border_light(ThemeColor::Grey300)
    , m_shadow(WindowShadowParams{6, 0, corner_radius, 0.12})
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(ThemeColor::White);
    Bind(wxEVT_PAINT, &RoundedPopupWindow::OnPaint, this);
    // Re-clip whenever the popup resizes so the rounded shape always tracks the fitted size.
    Bind(wxEVT_SIZE, [this](wxSizeEvent &e) {
        ApplyShape();
        e.Skip();
    });
    if (m_with_shadow)
        Bind(wxEVT_SHOW, [this](wxShowEvent &e) {
            m_shadow.Sync(this, e.IsShown());
            e.Skip();
        });
}

void RoundedPopupWindow::Finalize()
{
    Layout();
    Fit();
    ApplyShape();
}

void RoundedPopupWindow::ApplyShape()
{
    const wxSize sz = GetSize();
    if (sz.GetWidth() <= 0 || sz.GetHeight() <= 0) return;
    const int d = FromDIP(m_radius * 2); // GDI ellipse size = 2*radius

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

// Border drawn as a solid fill cleared to the border colour, then a card-colour rounded rect inset
// by the border width
void RoundedPopupWindow::OnPaint(wxPaintEvent &)
{
    const bool dark = wxGetApp().dark_mode();
    auto       t    = [dark](const wxColour &c) { return dark ? StateColor::darkModeColorFor(c) : c; };

    wxBufferedPaintDC dc(this);
    wxGCDC            gdc(dc);
    gdc.SetBackground(wxBrush(t(m_border_light)));
    gdc.Clear();

    wxGraphicsContext *gc = gdc.GetGraphicsContext();
    if (gc == nullptr) return;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    const double r  = FromDIP(m_radius);
    const double bw = FromDIP(1);
    const wxSize sz = GetClientSize();
    const double ir = r > bw ? r - bw : 0.0;

    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(t(GetBackgroundColour())));
    gc->DrawRoundedRectangle(bw, bw, sz.GetWidth() - 2.0 * bw, sz.GetHeight() - 2.0 * bw, ir);
}

} // namespace Slic3r::GUI
