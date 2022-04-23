#include "AMSHumidity.hpp"
#include "../I18N.hpp"
#include <wx/dcgraph.h>
#include "Label.hpp"



wxDEFINE_EVENT(wxCUSTOMEVT_SET_TEMP_FINISH, wxCommandEvent);
BEGIN_EVENT_TABLE(AMSHumidity, wxPanel)
EVT_PAINT(AMSHumidity::paintEvent)
END_EVENT_TABLE()

AMSHumidity::AMSHumidity(wxWindow *parent, wxWindowID id,const wxPoint &pos, const wxSize &size)
{
    SetBackgroundColour(wxColour(255,255,255));
    SetFont(Label::Head_12);
    create(parent, id, pos, size);
}


AMSHumidity::~AMSHumidity() {}


void AMSHumidity::create(wxWindow *parent, wxWindowID id, const wxPoint &pos,  const wxSize &size)
{
    wxWindow::Create(parent, id, pos, size);
    SetSize({82,20});
    SetBackgroundColour(wxColour(238, 238, 238));
}


void AMSHumidity::SetValue(int step) 
{ 

}

void AMSHumidity::paintEvent(wxPaintEvent &evt)
{

    wxPaintDC dc(this);
    render(dc);
}

void AMSHumidity::render(wxDC &dc)
{
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
}

void AMSHumidity::doRender(wxDC &dc)
{
    wxSize size   = GetSize();
    dc.SetPen(wxPen(wxColour(206,206,206), 1));
    dc.SetBrush(wxBrush(wxColour(206,206,206)));
    dc.DrawRoundedRectangle(0, 0, size.x, size.y, 10);
}


void AMSHumidity::DoSetSize(int x, int y, int width, int height, int sizeFlags) 
{ 
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
}
