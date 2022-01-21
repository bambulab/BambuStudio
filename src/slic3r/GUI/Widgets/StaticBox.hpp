#ifndef slic3r_GUI_StaticBox_hpp_
#define slic3r_GUI_StaticBox_hpp_

#include <wx/window.h>
#include "StateHandler.hpp"

class StaticBox : public wxWindow
{
protected:
    double radius;

    StateHandler state_handler;
    StateColor   border_color;
    StateColor   background_color;

public:
    StaticBox(wxWindow* parent, long style = 0);

    void SetCornerRadius(double radius);

    void SetBorderColor(StateColor const & color);

    void SetBackgroundColor(StateColor const &color);
    
protected:
    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_StaticBox_hpp_
