#include "StaticBox.hpp"
#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(StaticBox, wxWindow)

// catch paint events
//EVT_ERASE_BACKGROUND(StaticBox::eraseEvent)
EVT_PAINT(StaticBox::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

StaticBox::StaticBox()
    : state_handler(this)
    , border_color(0x303A3C)
    , background_color(*wxWHITE)
    , radius(8)
{
}

StaticBox::StaticBox(wxWindow* parent,
                   wxWindowID      id,
                   const wxPoint & pos,
                   const wxSize &  size, long style)
    : StaticBox()
{
    Create(parent, id, pos, size, style);
}

bool StaticBox::Create(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
{
    wxWindow::Create(parent, id, pos, size, style);
    state_handler.attach({&border_color, &background_color});
    state_handler.update_binds();
    if (auto box = dynamic_cast<StaticBox*>(parent))
        wxWindow::SetBackgroundColour(box->background_color.defaultColor());
    else if (parent)
        wxWindow::SetBackgroundColour(parent->GetBackgroundColour());
    return true;
}

void StaticBox::SetCornerRadius(double radius)
{
    this->radius = radius;
    Refresh();
}

void StaticBox::SetBorderWidth(int width)
{
    border_width = width;
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

void StaticBox::eraseEvent(wxEraseEvent& evt)
{
    // for transparent background, but not work
#ifdef __WIN32__
    wxDC *dc = evt.GetDC();
    wxSize size = GetSize();
    wxClientDC dc2(GetParent());
    dc->Blit({0, 0}, size, &dc2, GetPosition());
#endif
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
	wxSize size = GetSize();
    wxMemoryDC memdc;
    wxBitmap bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
	dc.DrawBitmap(bmp, 0, 0);
}

void StaticBox::doRender(wxDC& dc)
{
    wxSize size = GetSize();
    int states = state_handler.states();
    dc.SetPen(wxPen(border_color.colorForStates(states), border_width));
    dc.SetBrush(wxBrush(background_color.colorForStates(states)));
    if (GetWindowStyle() & wxBORDER_NONE)
        dc.SetPen(wxPen(background_color.colorForStates(states)));

    if (radius == 0) {
        dc.DrawRectangle(0, 0, size.x, size.y);
    }
    else {
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, radius);
    }
}
