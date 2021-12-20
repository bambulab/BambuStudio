#ifndef slic3r_GUI_Button_hpp_
#define slic3r_GUI_Button_hpp_

#include <wx/stattext.h>
#include "../wxExtensions.hpp"
#include "StateHandler.hpp"

class Button : public wxWindow
{
    wxSize textSize;
    wxSize minSize; // set by outer
    ScalableBitmap icon;
    double radius;

    StateHandler state_handler;
    StateColor   text_color;
    StateColor   border_color;
    StateColor   background_color;

    bool pressedDown = false;

    static const int buttonWidth = 200;
    static const int buttonHeight = 50;

public:
    Button(wxWindow* parent, wxString text, wxString icon = "", long style = 0, int iconSize = 0);

    void SetCornerRadius(double radius);

    void SetLabel(const wxString& label);

    bool SetForegroundColour(wxColour const & colour) override;

    bool SetBackgroundColour(wxColour const & color) override;

    void SetMinSize(const wxSize& size) override;
    
    void SetBorderColor(StateColor const & color);

    void SetForegroundColor(StateColor const &color);

    void SetBackgroundColor(StateColor const &color);
    
    void Rescale();

private:
    void paintEvent(wxPaintEvent& evt);
    void paintNow();

    void render(wxDC& dc);

    void messureSize();

    // some useful events
    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent& event);

    void sendButtonEvent();

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_Button_hpp_
