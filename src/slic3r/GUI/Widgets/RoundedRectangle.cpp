#include "RoundedRectangle.hpp"
#include "../wxExtensions.hpp"
#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(RoundedRectangle, wxPanel)
EVT_PAINT(RoundedRectangle::OnPaint)
END_EVENT_TABLE()

 RoundedRectangle::RoundedRectangle(wxWindow *parent, wxColour col, wxPoint pos, wxSize size, double radius) 
     : wxWindow(parent, wxID_ANY, pos, size, wxBORDER_NONE) 
{ 
    SetBackgroundColour(wxColour(255,255,255));
    m_color  = col;
    m_radius = radius;
}

void RoundedRectangle::OnPaint(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(m_color));
    dc.DrawRoundedRectangle(0, 0, GetSize().GetWidth(), GetSize().GetHeight(), m_radius);
}