#include "StaticBox.hpp"

BEGIN_EVENT_TABLE(StaticBox, wxPanel)

// catch paint events
EVT_PAINT(StaticBox::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

StaticBox::StaticBox(wxWindow* parent, long stlye)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, stlye)
    , state_handler(this)
    , border_color(0x303A3C)
    , background_color(*wxWHITE)
{
    if (auto box = dynamic_cast<StaticBox*>(parent))
        wxWindow::SetBackgroundColour(box->background_color.defaultColor());
    else if (parent)
        wxWindow::SetBackgroundColour(parent->GetBackgroundColour());
    radius = 8;
    state_handler.attach({&border_color, &background_color});
    state_handler.update_binds();
}

void StaticBox::SetCornerRadius(double radius)
{
    this->radius = radius;
    Refresh();
}

void StaticBox::SetBorderColor(StateColor const &color)
{
    border_color = color;
    state_handler.update_binds();
    Refresh();
}

void StaticBox::SetBackgroundColor(StateColor const &color)
{
    background_color = color;
    state_handler.update_binds();
    Refresh();
}

void StaticBox::paintEvent(wxPaintEvent& evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxPaintDC dc(this);
    render(dc);
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void StaticBox::render(wxDC& dc)
{
    int states = state_handler.states();
    dc.SetPen(wxPen(border_color.colorForStates(states)));
    dc.SetBrush(wxBrush(background_color.colorForStates(states)));
    if (GetWindowStyle() & wxBORDER_NONE)
        dc.SetPen(wxPen(background_color.colorForStates(states)));

    wxSize size = GetSize();
    if (radius == 0)
        dc.DrawRectangle(0, 0, size.x, size.y);
    else
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, radius);
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
}
