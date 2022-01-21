#include "Button.hpp"
#include "Label.hpp"

#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(Button, wxPanel)

EVT_LEFT_DOWN(Button::mouseDown)
EVT_LEFT_UP(Button::mouseReleased)

// catch paint events
EVT_PAINT(Button::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

Button::Button(wxWindow* parent, wxString text, wxString icon, long stlye, int iconSize)
    : StaticBox(parent, stlye)
    , text_color(*wxBLACK)
{
    background_color = StateColor(
        std::make_pair(*wxLIGHT_GREY, (int) StateColor::Hovered),
        std::make_pair(*wxLIGHT_GREY, (int) StateColor::Checked),
        std::make_pair(*wxWHITE, (int) StateColor::Normal));
    state_handler.attach({&text_color});
    state_handler.update_binds();
    //BBS set default font
    SetFont(Label::Body_14);
    wxWindow::SetLabel(text);
    if (!icon.IsEmpty()) {
        //BBS set button icon default size to 20
        this->icon = ScalableBitmap(this, icon.ToStdString(), iconSize > 0 ? iconSize : 20);
    }
    messureSize();
}

void Button::SetLabel(const wxString& label)
{
    wxWindow::SetLabel(label);
    messureSize();
    Refresh();
}

void Button::SetIcon(const wxString& icon)
{
    if (!icon.IsEmpty()) {
        //BBS set button icon default size to 20
        this->icon = ScalableBitmap(this, icon.ToStdString(), this->icon.px_cnt());
    }
    else
    {
        this->icon = ScalableBitmap();
    }
    Refresh();
}

void Button::SetMinSize(const wxSize& size)
{
    minSize = size;
    messureSize();
}

void Button::SetTextColor(StateColor const &color)
{
    text_color = color;
    state_handler.update_binds();
    Refresh();
}

void Button::Rescale()
{
    if (this->icon.bmp().IsOk())
        this->icon.msw_rescale();
    messureSize();
}

void Button::paintEvent(wxPaintEvent& evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxPaintDC dc(this);
    render(dc);
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void Button::render(wxDC& dc)
{
    StaticBox::render(dc);
    int states = state_handler.states();
    wxSize size = GetSize();
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    // calc content size
    wxSize szIcon;
    wxSize szContent = textSize;
    if (icon.bmp().IsOk()) {
        if (szContent.y > 0) {
            //BBS norrow size between text and icon
            szContent.x += 5;
        }
        szIcon = icon.bmp().GetSize();
        szContent.x += szIcon.x;
        if (szIcon.y > szContent.y)
            szContent.y = szIcon.y;
    }
    // move to center
    wxRect rcContent = { {0, 0}, size };
    wxSize offset = (size - szContent) / 2;
    rcContent.Deflate(offset.x, offset.y);
    // start draw
    wxPoint pt = rcContent.GetLeftTop();
    if (icon.bmp().IsOk()) {
        pt.y += (rcContent.height - szIcon.y) / 2;
        dc.DrawBitmap(icon.bmp(), pt);
        //BBS norrow size between text and icon
        pt.x += szIcon.x + 5;
        pt.y = rcContent.y;
    }
    auto text = GetLabel();
    if (!text.IsEmpty()) {
        pt.y += (rcContent.height - textSize.y) / 2;
        dc.SetFont(GetFont());
        dc.SetTextForeground(text_color.colorForStates(states));
        dc.DrawText(text, pt);
    }
}

void Button::messureSize()
{
    textSize = GetTextExtent(GetLabel());
    if (minSize.GetWidth() > 0) {
        wxWindow::SetMinSize(minSize);
        return;
    }
    wxSize szContent = textSize;
    if (this->icon.bmp().IsOk()) {
        if (szContent.y > 0) {
            //BBS norrow size between text and icon
            szContent.x += 5;
        }
        wxSize szIcon = this->icon.bmp().GetSize();
        szContent.x += szIcon.x;
        if (szIcon.y > szContent.y)
            szContent.y = szIcon.y;
    }
    wxWindow::SetMinSize(szContent + wxSize{ 20, 16 });
}

void Button::mouseDown(wxMouseEvent& event)
{
    event.Skip();
    pressedDown = true;
    SetFocus();
}

void Button::mouseReleased(wxMouseEvent& event)
{
    event.Skip();
    if (pressedDown) {
        pressedDown = false;
        sendButtonEvent();
    }
}

void Button::sendButtonEvent()
{
    wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
}
