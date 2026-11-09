#ifndef slic3r_GUI_StaticBox_hpp_
#define slic3r_GUI_StaticBox_hpp_

#include "../wxExtensions.hpp"
#include "StateHandler.hpp"

#include <wx/window.h>

class StaticBox : public wxWindow
{
public:
    StaticBox();

    StaticBox(wxWindow* parent,
             wxWindowID      id        = wxID_ANY,
             const wxPoint & pos       = wxDefaultPosition,
             const wxSize &  size      = wxDefaultSize,
             long style = 0);

    bool Create(wxWindow* parent,
        wxWindowID      id        = wxID_ANY,
        const wxPoint & pos       = wxDefaultPosition,
        const wxSize &  size      = wxDefaultSize,
        long style = 0);

    void SetCornerRadius(double radius);

    void SetBorderWidth(int width);

    void SetBorderColor(StateColor const & color);

    void SetBorderColorNormal(wxColor const &color);

    void SetBorderStyle(wxPenStyle style);

    void SetBackgroundColor(StateColor const &color);

    void SetBackgroundColorNormal(wxColor const &color);

    void SetBackgroundColor2(StateColor const &color);

    static wxColor GetParentBackgroundColor(wxWindow * parent);

    void ShowBadge(bool show);

    // The badge is a corner ribbon cut to sit flush in this control's top-right corner, so its own
    // top-right has to be cut the same way the control is: the default asset is rounded, and a square
    // host leaves that rounding as an unfilled notch. Hosts that are not rounded name a square-cut
    // asset here instead of rounding themselves to match a shape shared with unrelated controls.
    // Takes effect on the next ShowBadge(true).
    void SetBadgeIcon(const std::string &icon_name) { badge_icon_name = icon_name; }

    // Lets an owning widget avoid positioning other content over the badge, which is always
    // drawn flush at this control's own top-right corner and must not be moved from there.
    bool HasBadge() const { return badge.bmp().IsOk(); }
    int GetBadgeWidth() const { return badge.bmp().IsOk() ? badge.bmp().GetScaledSize().x : 0; }

protected:
    void eraseEvent(wxEraseEvent& evt);

    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

    virtual void doRender(wxDC& dc);

protected:
    double radius;
    int border_width = 1;
    wxPenStyle border_style = wxPENSTYLE_SOLID;
    StateHandler state_handler;
    StateColor   border_color;
    StateColor   background_color;
    StateColor   background_color2;
    ScalableBitmap badge;
    std::string    badge_icon_name = "badge";

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_StaticBox_hpp_
