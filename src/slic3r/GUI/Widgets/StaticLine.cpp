#include "StaticLine.hpp"

#include <wx/dcgraph.h>

StaticLine::StaticLine(wxWindow* parent, bool vertical)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    this->pen = wxPen(wxColour("#C4C4C4"));
    if (vertical)
        SetMinSize({1, -1});
    else
        SetMinSize({-1, 1});
}

void StaticLine::SetLineColour(wxColour color)
{
    this->pen = wxPen(color);
}

void StaticLine::paintEvent(wxPaintEvent& evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxPaintDC dc(this);
    render(dc);
}

/*
 * Alternatively, you can use a clientDC to paint on the panel
 * at any time. Using this generally does not free you from
 * catching paint events, since it is possible that e.g. the window
 * manager throws away your drawing when the window comes to the
 * background, and expects you will redraw it when the window comes
 * back (by sending a paint event).
 */
void StaticLine::paintNow()
{
    // depending on your system you may need to look at double-buffered dcs
    wxClientDC dc(this);
    render(dc);
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void StaticLine::render(wxDC& dc)
{
    dc.SetPen(pen);
    dc.DrawLine(0, 0, GetSize().x, 0);
}
