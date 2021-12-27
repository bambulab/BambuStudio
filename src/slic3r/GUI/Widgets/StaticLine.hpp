#ifndef slic3r_GUI_StaticLine_hpp_
#define slic3r_GUI_StaticLine_hpp_

#include "wx/window.h"

class StaticLine : public wxWindow
{
public:
    StaticLine(wxWindow* parent, bool vertical = false);

public:
    void SetLineColour(wxColour color);
    
private:
    wxPen pen;

private:
    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);
};

#endif // !slic3r_GUI_StaticLine_hpp_
