#ifndef slic3r_GUI_SpinInput_hpp_
#define slic3r_GUI_SpinInput_hpp_

#include <wx/textctrl.h>
#include "../wxExtensions.hpp"

class Button;

class SpinInput : public wxWindow
{

    bool hover;
    wxSize labelSize;
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
    Button * button_inc;
    Button * button_dec;
    wxTimer timer;

    int val;
    int min;
    int max;
    int initail;
    int delta;

    static const int SpinInputWidth = 200;
    static const int SpinInputHeight = 50;

public:
    SpinInput(wxWindow *     parent,
              wxString       text,
              wxString       label = "",
              const wxPoint &pos   = wxDefaultPosition,
              const wxSize & size  = wxDefaultSize,
              long           style = 0,
              int min = 0, int max = 100, int initial = 0);

    void SetCornerRadius(double radius);

    void SetLabel(const wxString &label) wxOVERRIDE;

    bool SetForegroundColour(const wxColour& colour) override;

    bool SetBackgroundColour(wxColour const & color) override;

    void SetBorderColor(wxColor normal, wxColor hover, wxColor pressed);

    void SetForegroundColor(wxColor normal, wxColor hover, wxColor pressed);

    void SetBackgroundColor(wxColor normal, wxColor hover, wxColor pressed);

    void SetSize(wxSize const &size);

    void Rescale();

    virtual bool Enable(bool enable = true) wxOVERRIDE;

    wxTextCtrl * GetTextCtrl() { return text_ctrl; }

    void SetValue(const wxString &text);

    void SetValue (int value);

    int GetValue () const;

    void SetRange(int min, int max);

private:
    void paintEvent(wxPaintEvent& evt);
    void paintNow();

    void render(wxDC& dc);

    void messureSize();

    Button *createButton(bool inc);

    // some useful events
    void mouseMoved(wxMouseEvent& event);
    void mouseWheelMoved(wxMouseEvent& event);
    void mouseEnterWindow(wxMouseEvent& event);
    void mouseLeaveWindow(wxMouseEvent& event);
    void keyPressed(wxKeyEvent& event);
    void keyReleased(wxKeyEvent& event);
    void onTimer(wxTimerEvent &evnet);
    void onTextLostFocus(wxEvent &event);
    void onTextEnter(wxCommandEvent &event);

    void sendSpinEvent();

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_SpinInput_hpp_
