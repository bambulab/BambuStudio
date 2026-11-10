#ifndef slic3r_GUI_SpinInput_hpp_
#define slic3r_GUI_SpinInput_hpp_

#include <wx/dcclient.h>
#include <wx/event.h>
#include <wx/timer.h>
#include <wx/textctrl.h>
#include <wx/valtext.h>
#include <functional>
#include "StaticBox.hpp"

wxDECLARE_EVENT(EVT_SPINCTRL_TEXT, wxCommandEvent);

class Button;

class SpinInput : public wxNavigationEnabled<StaticBox>
{
    wxSize labelSize;
    StateColor   label_color;
    StateColor   text_color;
    wxTextCtrl * text_ctrl;
    Button * button_inc;
    Button * button_dec;
    wxTimer timer;

    int val;
    int min;
    int max;
    int delta;
    bool text_updating;
    std::function<void(long)> m_out_of_range_handler;
    bool m_warning{false};
    StateColor m_normal_border_color;
    StateColor m_normal_text_color;

    static const int SpinInputWidth = 200;
    static const int SpinInputHeight = 50;

public:
    SpinInput();

    SpinInput(wxWindow *     parent,
              wxString       text,
              wxString       label = "",
              const wxPoint &pos   = wxDefaultPosition,
              const wxSize & size  = wxDefaultSize,
              long           style = 0,
              int min = 0, int max = 100, int initial = 0);

    void Create(wxWindow *     parent,
              wxString       text,
              wxString       label   = "",
              const wxPoint &pos     = wxDefaultPosition,
              const wxSize & size    = wxDefaultSize,
              long           style   = 0,
              int            min     = 0,
              int            max     = 100,
              int            initial = 0);

    void SetCornerRadius(double radius);

    void SetLabel(const wxString &label) wxOVERRIDE;

    void SetLabelColor(StateColor const &color);

    void SetTextColor(StateColor const &color);

    void SetSize(wxSize const &size);

    void Rescale();

    virtual bool Enable(bool enable = true) wxOVERRIDE;

    wxTextCtrl * GetTextCtrl() { return text_ctrl; }

    void SetValue(const wxString &text);

    void SetValue (int value);

    int GetValue () const;

    void SetRange(int min, int max);

    /**
     * \brief Set a handler called on commit with the typed value when it is outside the range
     *
     * \param handler receives the typed value; the value is clamped after the handler returns
     * \note The handler may show a modal dialog
     */
    void SetOutOfRangeHandler(std::function<void(long)> handler) { m_out_of_range_handler = std::move(handler); }

    /**
     * \brief Highlight the value as needing attention
     *
     * \param warning true draws border and text in the warning colour, false restores the normal look
     */
    void SetWarning(bool warning);

protected:
    void DoSetToolTipText(wxString const &tip) override;

private:
    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

    void messureSize();

    Button *createButton(bool inc);

    // some useful events
    void mouseWheelMoved(wxMouseEvent& event);
    void keyPressed(wxKeyEvent& event);
    void onTimer(wxTimerEvent &evnet);
    void onTextChanged(wxCommandEvent &event);
    void onTextLostFocus(wxEvent &event);
    void onTextEnter(wxCommandEvent &event);

    void sendSpinEvent();

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_SpinInput_hpp_
