#include "RoundedRectangle.hpp"
#include "../wxExtensions.hpp"
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(RoundedRectangle, wxWindow)
EVT_PAINT(RoundedRectangle::OnPaint)
END_EVENT_TABLE()

RoundedRectangle::RoundedRectangle(wxWindow *parent, const wxColour &fill, const wxColour &border, double radius, wxPoint pos, wxSize size)
    : wxWindow(parent, wxID_ANY, pos, size, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE), m_radius(radius), m_fill(fill), m_border(border)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(parent->GetBackgroundColour()); // the corners blend into the parent
}

void RoundedRectangle::OnPaint(wxPaintEvent &)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();
    wxGCDC       gdc(dc);
    const wxSize sz = GetClientSize();
    wxRect       r(0, 0, sz.x, sz.y);
    if (m_border.IsOk()) {
        r.Deflate(FromDIP(1)); // keep the stroke inside the widget
        gdc.SetPen(wxPen(m_border, FromDIP(1)));
    } else {
        gdc.SetPen(*wxTRANSPARENT_PEN);
    }
    gdc.SetBrush(m_fill.IsOk() ? wxBrush(m_fill) : *wxTRANSPARENT_BRUSH);
    gdc.DrawRoundedRectangle(r, m_radius);
}

void RoundedRectangle::SetFillColour(const wxColour &fill)
{
    if (m_fill == fill)
        return;
    m_fill = fill;
    Refresh();
}
