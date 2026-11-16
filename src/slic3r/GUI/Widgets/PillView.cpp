#include "PillView.hpp"

#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/graphics.h>

#include "slic3r/GUI/Widgets/Label.hpp"

namespace Slic3r::GUI {

namespace {
constexpr int kPadX   = 16;
constexpr int kPadY   = 8;
constexpr int kRadius = 4;
constexpr int kIconGap = 4;  // between the text and the chevron
constexpr int kIconD   = 12; // chevron box

bool is_clear(const wxColour &c) { return !c.IsOk() || c.Alpha() == wxALPHA_TRANSPARENT; }

/**
 * \brief Stroke a small downward chevron centred in \p box.
 */
void draw_chevron(wxGCDC &gdc, const wxRect &box, const wxColour &colour, int stroke)
{
    wxGraphicsContext *gc = gdc.GetGraphicsContext();
    if (!gc) return;
    wxPen pen(colour, stroke);
    pen.SetCap(wxCAP_ROUND);
    pen.SetJoin(wxJOIN_ROUND);
    gc->SetPen(pen);
    const double cx = box.x + box.width / 2.0;
    const double cy = box.y + box.height / 2.0;
    const double hw = box.width / 4.0; // half the chevron width
    const double hh = box.height / 8.0;
    wxGraphicsPath path = gc->CreatePath();
    path.MoveToPoint(cx - hw, cy - hh);
    path.AddLineToPoint(cx, cy + hh);
    path.AddLineToPoint(cx + hw, cy - hh);
    gc->StrokePath(path);
}
} // namespace

PillView::PillView(wxWindow *parent, const wxColour &surface)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE)
    , m_fg(*wxBLACK)
    , m_fill(wxTransparentColour)
    , m_border(wxTransparentColour)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(surface);
    SetFont(::Label::Body_11);
    Bind(wxEVT_PAINT, &PillView::on_paint, this);
}

void PillView::set_text(const wxString &text)
{
    if (m_text == text) return;
    m_text = text;
    InvalidateBestSize();
    Refresh();
}

void PillView::set_colors(const wxColour &fg, const wxColour &fill, const wxColour &border)
{
    if (m_fg == fg && m_fill == fill && m_border == border) return;
    m_fg     = fg;
    m_fill   = fill;
    m_border = border;
    Refresh();
}

void PillView::set_shape(Shape shape)
{
    if (m_shape == shape) return;
    m_shape = shape;
    Refresh();
}

void PillView::set_dropdown(bool dropdown)
{
    if (m_dropdown == dropdown) return;
    m_dropdown = dropdown;
    InvalidateBestSize();
    Refresh();
}

wxSize PillView::DoGetBestSize() const
{
    // Height comes from the font's line height, not the text, so an empty pill keeps its row height.
    wxClientDC dc(const_cast<PillView *>(this));
    dc.SetFont(GetFont());
    int w = m_text.empty() ? 0 : dc.GetTextExtent(m_text).x;
    if (m_dropdown) w += FromDIP(kIconGap + kIconD);
    return wxSize(w + FromDIP(kPadX), dc.GetCharHeight() + FromDIP(kPadY));
}

void PillView::on_paint(wxPaintEvent &)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    wxGCDC       gdc(dc);
    const wxSize sz = GetClientSize();
    wxRect       r(0, 0, sz.x, sz.y);
    if (!is_clear(m_border)) r.Deflate(FromDIP(1)); // keep the stroke inside the widget
    gdc.SetBrush(is_clear(m_fill) ? *wxTRANSPARENT_BRUSH : wxBrush(m_fill));
    gdc.SetPen(is_clear(m_border) ? *wxTRANSPARENT_PEN : wxPen(m_border, FromDIP(1)));
    const double radius = m_shape == Shape::Capsule ? r.height / 2.0 : FromDIP(kRadius);
    gdc.DrawRoundedRectangle(r, radius);
    gdc.SetFont(GetFont());
    gdc.SetTextForeground(m_fg);
    const wxSize ts = gdc.GetTextExtent(m_text);
    if (!m_dropdown) {
        gdc.DrawText(m_text, (sz.x - ts.x) / 2, (sz.y - ts.y) / 2);
        return;
    }
    // Text and chevron centre as one group.
    const int icon_d = FromDIP(kIconD);
    const int x      = (sz.x - (ts.x + FromDIP(kIconGap) + icon_d)) / 2;
    gdc.DrawText(m_text, x, (sz.y - ts.y) / 2);
    draw_chevron(gdc, wxRect(x + ts.x + FromDIP(kIconGap), (sz.y - icon_d) / 2, icon_d, icon_d), m_fg, FromDIP(1));
}

} // namespace Slic3r::GUI
