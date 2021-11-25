#include "Button.hpp"
#include "Label.hpp"

#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(Button, wxPanel)

EVT_MOTION(Button::mouseMoved)
EVT_LEFT_DOWN(Button::mouseDown)
EVT_LEFT_UP(Button::mouseReleased)
EVT_RIGHT_DOWN(Button::rightClick)
EVT_ENTER_WINDOW(Button::mouseEnterWindow)
EVT_LEAVE_WINDOW(Button::mouseLeaveWindow)
EVT_KEY_DOWN(Button::keyPressed)
EVT_KEY_UP(Button::keyReleased)
EVT_MOUSEWHEEL(Button::mouseWheelMoved)

// catch paint events
EVT_PAINT(Button::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

    Button::Button(wxWindow* parent, wxString text, wxString icon, long stlye)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, stlye)
{
    pressedDown = hover = false;
    radius = 8;
    border_normal = border_hover = border_pressed = wxColour("#303A3C");
    text_normal = text_hover = text_pressed = *wxBLACK;
    background_normal = *wxWHITE_BRUSH;
    background_hover = *wxLIGHT_GREY_BRUSH;
    background_pressed = *wxBLUE_BRUSH;
    SetFont(Label::Body_12);
    SetLabel(text);
    if (!icon.IsEmpty())
        this->icon = ScalableBitmap(this, icon.ToStdString());
    messureSize();
}

void Button::SetCornerRadius(double radius)
{
    this->radius = radius;
    paintNow();
}

void Button::SetLabel(const wxString& label)
{
    wxWindow::SetLabel(label);
    messureSize();
    paintNow();
}

bool Button::SetForegroundColour(const wxColour& colour)
{
    SetForegroundColor(colour, colour, colour);
    return true;
}

bool Button::SetBackgroundColour(wxColour const& color)
{
    SetBackgroundColor(color, color, color);
    return true;
}

void Button::SetMinSize(const wxSize& size)
{
    minSize = size;
    messureSize();
}

void Button::SetBorderColor(wxColor normal, wxColor hover, wxColor pressed)
{
    border_normal = normal;
    border_hover = hover;
    border_pressed = pressed;
    paintNow();
}

void Button::SetForegroundColor(wxColor normal, wxColor hover, wxColor pressed)
{
    text_normal = normal;
    text_hover = hover;
    text_pressed = pressed;
    paintNow();
}

void Button::SetBackgroundColor(wxColor normal, wxColor hover, wxColor pressed)
{
    background_normal = normal;
    background_hover = hover;
    background_pressed = pressed;
    paintNow();
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
    wxGCDC dc2(dc);
    render(dc2);
}

/*
 * Alternatively, you can use a clientDC to paint on the panel
 * at any time. Using this generally does not free you from
 * catching paint events, since it is possible that e.g. the window
 * manager throws away your drawing when the window comes to the
 * background, and expects you will redraw it when the window comes
 * back (by sending a paint event).
 */
void Button::paintNow()
{
    // depending on your system you may need to look at double-buffered dcs
    wxClientDC dc(this);
    render(dc);
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void Button::render(wxDC& dc)
{
    if (pressedDown) {
        //dc.SetPen(border_pressed);
        dc.SetBrush(background_pressed);
        dc.SetTextForeground(text_pressed);
    }
    else if (hover) {
        //dc.SetPen(border_hover);
        dc.SetBrush(background_hover);
        dc.SetTextForeground(text_hover);
    }
    else {
        //dc.SetPen(border_normal);
        dc.SetBrush(background_normal);
        dc.SetTextForeground(text_normal);
    }
    if (GetWindowStyle() & wxBORDER_NONE)
        dc.SetPen(wxNullPen);

    wxSize size = GetSize();
    dc.DrawRoundedRectangle(0, 0, size.x, size.y, radius);
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    // calc content size
    wxSize szIcon;
    wxSize szContent = textSize;
    if (icon.bmp().IsOk()) {
        if (szContent.y > 0)
            szContent.x +=10;
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
        pt.x += szIcon.x + 10;
        pt.y = rcContent.y;
    }
    auto text = GetLabel();
    if (!text.IsEmpty()) {
        pt.y += (rcContent.height - textSize.y) / 2;
        dc.SetFont(GetFont());
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
        if (szContent.y > 0)
            szContent.x += 10;
        wxSize szIcon = this->icon.bmp().GetSize();
        szContent.x += szIcon.x;
        if (szIcon.y > szContent.y)
            szContent.y = szIcon.y;
    }
    wxWindow::SetMinSize(szContent + wxSize{ 20, 16 });
}

void Button::mouseDown(wxMouseEvent& event)
{
    pressedDown = true;
    paintNow();
}

void Button::mouseReleased(wxMouseEvent& event)
{
    if (pressedDown) {
        pressedDown = false;
        paintNow();
        sendButtonEvent();
    }
}

void Button::mouseEnterWindow(wxMouseEvent& event)
{
    if (!hover)
    {
        hover = true;
        paintNow();
    }
}

void Button::mouseLeaveWindow(wxMouseEvent& event)
{
    if (pressedDown || hover)
    {
        pressedDown = false;
        hover = false;
        paintNow();
    }
}

// currently unused events
void Button::mouseMoved(wxMouseEvent& event) {}

void Button::mouseWheelMoved(wxMouseEvent& event) {}
void Button::rightClick(wxMouseEvent& event) {}
void Button::keyPressed(wxKeyEvent& event) {}
void Button::keyReleased(wxKeyEvent& event) {}

void Button::sendButtonEvent()
{
    wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
}
