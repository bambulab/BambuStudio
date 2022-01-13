#ifndef slic3r_GUI_SideButton_hpp_
#define slic3r_GUI_SideButton_hpp_

#include <wx/stattext.h>
#include "../wxExtensions.hpp"
#include "StateHandler.hpp"


class SideButton : public wxWindow
{
    wxSize textSize;
    wxSize minSize;
    ScalableBitmap icon;
    double radius;
    wxSize extra_size;
    int icon_offset;

    StateHandler    state_handler;
    StateColor      text_color;
    StateColor      border_color;
    StateColor      background_color;

	bool pressedDown = false;

public:
    SideButton(wxWindow* parent, wxString text, wxString icon = "", long style = 0, int iconSize = 0);

    void SetCornerRadius(double radius);

    void SetLabel(const wxString& label);

    bool SetForegroundColour(wxColour const & colour) override;

    bool SetBackgroundColour(wxColour const & color) override;

    void SetMinSize(const wxSize& size) override;
    
    void SetBorderColor(StateColor const & color);

    void SetForegroundColor(StateColor const &color);

    void SetBackgroundColor(StateColor const &color);

    bool Enable(bool enable = true);

    void Rescale();

    void SetExtraSize(const wxSize& size);

    void SetIconOffset(const int offset);

private:
    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

    void messureSize();

    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent& event);
    void mouseLeave(wxMouseEvent& event);

    void sendButtonEvent();

	DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_Button_hpp_
