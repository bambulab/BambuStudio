#ifndef slic3r_GUI_AxisCtrlButton_hpp_
#define slic3r_GUI_AxisCtrlButton_hpp_

#include <wx/stattext.h>
#include <wx/vlbox.h>
#include <wx/combo.h>
#include "../wxExtensions.hpp"
#include "StateHandler.hpp"


class AxisCtrlButton : public wxWindow
{
    wxSize minSize;
    double
        stretch_x,
        stretch_y,
        r_outer,
        r_inner,
        r_blank,
        gap;
	wxPoint center;

    StateHandler    state_handler;
    StateColor      border_color;
    StateColor      background_color;
	StateColor      inner_background_color;

	bool pressedDown = false;

    unsigned char last_pos;
    unsigned char current_pos;
    enum CurrentPos {
        OUTER_UP = 0,
        OUTER_LEFT = 1,
        OUTER_DOWN = 2,
        OUTER_RIGHT = 3,
        INNER_UP = 4,
        INNER_LEFT = 5,
        INNER_DOWN = 6,
        INNER_RIGHT = 7,
        UNDEFINED = 8
    };

public:
    AxisCtrlButton(wxWindow* parent, long style = 0);

    void SetMinSize(const wxSize& size) override;

    void SetBorderColor(StateColor const& color);

    void SetBackgroundColor(StateColor const& color);

    void SetInnerBackgroundColor(StateColor const& color);

private:
    void updateParams();

    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent& event);
    void mouseMoving(wxMouseEvent& event);

    void sendButtonEvent();

    DECLARE_EVENT_TABLE()
};
#endif // !slic3r_GUI_Button_hpp_
