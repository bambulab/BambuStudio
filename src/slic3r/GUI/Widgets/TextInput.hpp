#ifndef slic3r_GUI_TextInput_hpp_
#define slic3r_GUI_TextInput_hpp_

#include <wx/textctrl.h>
#include "../wxExtensions.hpp"

class TextInput : public wxWindow
{

    bool hover;
    wxSize labelSize;
    ScalableBitmap icon;
    double radius;
    wxColor text_normal;
    wxColor text_disabled;
    wxColor text_focused;
    wxPen border_normal;
    wxPen border_disabled;
    wxPen border_focused;
    wxBrush background_normal;
    wxBrush background_disabled;
    wxBrush background_focused;
    wxTextCtrl * text_ctrl;

    static const int TextInputWidth = 200;
    static const int TextInputHeight = 50;

public:
    TextInput(wxWindow *     parent,
              wxString       text,
              wxString       label = "",
              wxString       icon  = "",
              const wxPoint &pos   = wxDefaultPosition,
              const wxSize & size  = wxDefaultSize,
              long           style = 0);

    void SetCornerRadius(double radius);

    void SetLabel(const wxString& label);

    bool SetForegroundColour(const wxColour& colour) override;

    bool SetBackgroundColour(wxColour const & color) override;

    void SetBorderColor(wxColor normal, wxColor hover, wxColor pressed);

    void SetForegroundColor(wxColor normal, wxColor hover, wxColor pressed);

    void SetBackgroundColor(wxColor normal, wxColor hover, wxColor pressed);

    void Rescale();

    virtual bool Enable(bool enable = true);

    wxTextCtrl * GetTextCtrl() { return text_ctrl; }

private:
    void paintEvent(wxPaintEvent& evt);
    void paintNow();

    void render(wxDC& dc);

    void messureSize();

    // some useful events
    void mouseMoved(wxMouseEvent& event);
    void mouseWheelMoved(wxMouseEvent& event);
    void mouseEnterWindow(wxMouseEvent& event);
    void mouseLeaveWindow(wxMouseEvent& event);
    void keyPressed(wxKeyEvent& event);
    void keyReleased(wxKeyEvent& event);

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_TextInput_hpp_
