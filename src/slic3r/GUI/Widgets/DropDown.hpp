#ifndef slic3r_GUI_DropDown_hpp_
#define slic3r_GUI_DropDown_hpp_

#include <wx/stattext.h>
#include "../wxExtensions.hpp"
#include "StateHandler.hpp"

class DropDown : public wxPopupTransientWindow
{
    std::vector<wxString> &       texts;
    std::vector<wxBitmap> &     icons;
    bool                          need_sync  = false;
    int                         selection = -1;
    int                         hover_item = -1;

    double radius = 0;
    wxSize textSize;
    wxSize iconSize;
    wxSize rowSize;

    StateHandler state_handler;
    StateColor   text_color;
    StateColor   border_color;
    StateColor   background_color;
    ScalableBitmap check_bitmap;

    bool pressedDown = false;
    boost::posix_time::ptime dismissTime;
    wxPoint                  offset; // x not used
    wxPoint                  dragStart;

    static const int DropDownWidth = 200;
    static const int DropDownHeight = 50;

public:
    DropDown(wxWindow *     parent,
             std::vector<wxString> &texts,
             std::vector<wxBitmap> &icons,
             long           style     = 0);

public:
    void Invalidate(bool clear = false);

    int GetSelection() const { return selection; }

    void SetSelection(int n);

    wxString GetValue() const;
    void     SetValue(const wxString &value);

public:
    void SetCornerRadius(double radius);

    bool SetForegroundColour(wxColour const & colour) override;

    bool SetBackgroundColour(wxColour const & color) override;

    void SetBorderColor(StateColor const & color);

    void SetForegroundColor(StateColor const &color);

    void SetBackgroundColor(StateColor const &color);
    
public:
    void Rescale();

    bool HasDismissLongTime();

private:
    void paintEvent(wxPaintEvent& evt);
    void paintNow();

    void render(wxDC& dc);

    friend class ComboBox;
    void messureSize();
    void autoPosition();

    // some useful events
    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent &event);
    void mouseMove(wxMouseEvent &event);
    void mouseWheelMoved(wxMouseEvent &event);

    void sendDropDownEvent();

    void OnDismiss() override;

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_DropDown_hpp_
