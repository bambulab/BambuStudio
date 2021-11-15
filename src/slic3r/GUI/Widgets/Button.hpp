#ifndef slic3r_GUI_Button_hpp_
#define slic3r_GUI_Button_hpp_

#include <wx/stattext.h>

class Button : public wxWindow
{

    bool hover;
    bool pressedDown;
    wxSize textSize;
    wxSize minSize; // set by outer
    wxBitmap icon;
    double radius;
    wxColor text_normal;
    wxColor text_hover;
    wxColor text_pressed;
    wxPen border_normal;
    wxPen border_hover;
    wxPen border_pressed;
    wxBrush background_normal;
    wxBrush background_hover;
    wxBrush background_pressed;

    static const int buttonWidth = 200;
    static const int buttonHeight = 50;

public:
    Button(wxWindow* parent, wxString text, wxString icon = "", long style = 0);

    void SetCornerRadius(double radius);

    void SetLabel(const wxString& label);

    bool SetForegroundColour(const wxColour& colour) override;

    bool SetBackgroundColour(wxColour const & color) override;

    void SetMinSize(const wxSize& size) override;
    
    void SetBorderColor(wxColor normal, wxColor hover, wxColor pressed);

    void SetForegroundColor(wxColor normal, wxColor hover, wxColor pressed);

    void SetBackgroundColor(wxColor normal, wxColor hover, wxColor pressed);

private:
    void paintEvent(wxPaintEvent& evt);
    void paintNow();

    void render(wxDC& dc);

    void messureSize();

    // some useful events
    void mouseMoved(wxMouseEvent& event);
    void mouseDown(wxMouseEvent& event);
    void mouseWheelMoved(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent& event);
    void rightClick(wxMouseEvent& event);
    void mouseEnterWindow(wxMouseEvent& event);
    void mouseLeaveWindow(wxMouseEvent& event);
    void keyPressed(wxKeyEvent& event);
    void keyReleased(wxKeyEvent& event);

    void sendButtonEvent();

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_Button_hpp_
