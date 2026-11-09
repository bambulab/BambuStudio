#include "IconView.hpp"

#include <wx/dcbuffer.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

namespace Slic3r::GUI {

IconView::IconView(wxWindow *parent, const std::string &icon, int px)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_icon(icon), m_px(px)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetInitialSize();
    Bind(wxEVT_PAINT, &IconView::on_paint, this);
}

wxSize IconView::DoGetBestSize() const { return ScalableBitmap::GetBmpSize(fresh_bitmap()); }

// The loader recolours the asset for the theme at load time, so reload when the theme or the scale
// is no longer the one the bitmap was made for.
const wxBitmap &IconView::fresh_bitmap() const
{
    const bool dark = wxGetApp().dark_mode();
    const int  px   = FromDIP(m_px);
    if (!m_bmp.IsOk() || dark != m_bmp_dark || px != m_bmp_px) {
        m_bmp      = create_scaled_bitmap(m_icon, const_cast<IconView *>(this), m_px);
        m_bmp_dark = dark;
        m_bmp_px   = px;
    }
    return m_bmp;
}

void IconView::on_paint(wxPaintEvent &)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();

    const wxBitmap &bmp = fresh_bitmap();
    if (!bmp.IsOk()) return;
    const wxSize sz = GetClientSize();
    const wxSize bs = ScalableBitmap::GetBmpSize(bmp);
    dc.DrawBitmap(bmp, (sz.x - bs.x) / 2, (sz.y - bs.y) / 2, true);
}

} // namespace Slic3r::GUI
