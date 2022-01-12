#include "SideButton.hpp"
#include "Label.hpp"

#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(SideButton, wxPanel)

EVT_LEFT_DOWN(SideButton::mouseDown)
EVT_LEFT_UP(SideButton::mouseReleased)
EVT_LEAVE_WINDOW(SideButton::mouseLeave)
// catch paint events
EVT_PAINT(SideButton::paintEvent)

END_EVENT_TABLE()

SideButton::SideButton(wxWindow* parent, wxString text, wxString icon, long stlye, int iconSize)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, stlye)
    , state_handler(this)
{
    radius = 12;
    extra_size = wxSize(38, 10);

    border_color.append(0x6B6B6B, StateColor::Disabled);
    border_color.append(0x1B8844, StateColor::Pressed);
    border_color.append(0xFFFFF, StateColor::Hovered);
    border_color.append(0x00AE42, StateColor::Normal);
    border_color.append(0x00AE42, StateColor::Enabled);

    text_color.append(0xACACAC, StateColor::Disabled);
    text_color.append(0xFFFFFF, StateColor::Pressed);
    text_color.append(0xFFFFFF, StateColor::Hovered);
    text_color.append(0xFFFFFF, StateColor::Normal);
    text_color.append(0xFFFFFF, StateColor::Enabled);

    background_color.append(0x6B6B6B, StateColor::Disabled);
    background_color.append(0x1B8844, StateColor::Pressed);
    background_color.append(0x00AE42, StateColor::Hovered);
    background_color.append(0x00AE42, StateColor::Normal);
    background_color.append(0x00AE42, StateColor::Enabled);

    state_handler.attach({ &border_color, &text_color, &background_color });
    state_handler.update_binds();

    // icon only
    if (!icon.IsEmpty()) {
        this->icon = ScalableBitmap(this, icon.ToStdString(), iconSize > 0 ? iconSize : 14);
    }

    SetFont(Label::Body_14);
    wxWindow::SetLabel(text);

    messureSize();
}

void SideButton::SetCornerRadius(double radius)
{
    this->radius = radius;
    Refresh();
}

void SideButton::SetLabel(const wxString& label)
{
    wxWindow::SetLabel(label);
    messureSize();
    Refresh();
}

bool SideButton::SetForegroundColour(wxColour const &color)
{
    text_color = StateColor(color);
    state_handler.update_binds();
    return true;
}

bool SideButton::SetBackgroundColour(wxColour const& color)
{
    background_color = StateColor(color);
    state_handler.update_binds();
    return true;
}

void SideButton::SetMinSize(const wxSize& size)
{
    minSize = size;
    messureSize();
}

void SideButton::SetBorderColor(StateColor const &color)
{
    border_color = color;
    state_handler.update_binds();
    Refresh();
}

void SideButton::SetForegroundColor(StateColor const &color)
{
    text_color = color;
    state_handler.update_binds();
    Refresh();
}

void SideButton::SetBackgroundColor(StateColor const &color)
{
    background_color = color;
    state_handler.update_binds();
    Refresh();
}

bool SideButton::Enable(bool enable)
{
    if (enable) {
        state_handler.set_state(StateHandler::State::Enabled);
        state_handler.clean_state(StateHandler::State::Disabled);
    }
    else {
        state_handler.set_state(StateHandler::State::Disabled);
        state_handler.clean_state(StateHandler::State::Enabled);
    }
    Refresh();
    return wxWindow::Enable(enable);
}

void SideButton::Rescale()
{
    if (this->icon.bmp().IsOk())
        this->icon.msw_rescale();
    messureSize();
}

void SideButton::paintEvent(wxPaintEvent& evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxPaintDC dc(this);
    wxGCDC dc2(dc);
    render(dc2);
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void SideButton::render(wxDC& dc)
{
    wxSize size = GetSize();

    // draw background
    dc.SetPen(wxNullPen);
    dc.SetBrush(wxColour(0x3B4446));
    dc.DrawRectangle(0, 0, size.x, size.y);

    int states = state_handler.states();
    dc.SetBrush(wxBrush(background_color.colorForStates(states)));

    dc.SetPen(wxPen(border_color.colorForStates(states)));
    int pen_width = dc.GetPen().GetWidth();

    
    // draw icon style
    if (icon.bmp().IsOk()) {
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, radius);
        dc.DrawRectangle(radius, 0, size.x - radius, size.y);
        dc.SetPen(wxNullPen);
        dc.DrawRectangle(radius - pen_width, pen_width, radius, size.y - 2 * pen_width);
    }
    // draw text style
    else {
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, radius);
        dc.DrawRectangle(0, 0, size.x - radius, size.y);
        dc.SetPen(wxNullPen);
        dc.DrawRectangle(size.x - radius - pen_width, pen_width, 2 * pen_width, size.y - 2 * pen_width);
    }

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
        //BBS extra 2 pixels for icon
        pt.x += 2;
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

void SideButton::messureSize()
{
    textSize = GetTextExtent(GetLabel());
    if (minSize.GetWidth() > 0) {
        wxWindow::SetMinSize(minSize);
        return;
    }

    wxSize szContent = textSize;
    if (this->icon.bmp().IsOk()) {
        if (szContent.y > 0) {
            szContent.x += 5;
        }
        wxSize szIcon = this->icon.bmp().GetSize();
        szContent.x += szIcon.x;
        if (szIcon.y > szContent.y)
            szContent.y = szIcon.y;
        //BBS icon only
        wxWindow::SetMinSize(szContent + wxSize{ 10, 10 });
    }
    else {
        if (minSize.GetHeight() > 0) {
            //BBS with text size
            wxWindow::SetMinSize(wxSize(szContent.GetX() + extra_size.GetX(), minSize.GetHeight()));
        } else {
            //BBS with text size
            wxWindow::SetMinSize(szContent + extra_size);
        }
    }
}

void SideButton::mouseDown(wxMouseEvent& event)
{
    //event.Skip();
    pressedDown = true;
    state_handler.set_state(StateHandler::State::Pressed);
    Refresh();
    SetFocus();
}

void SideButton::mouseReleased(wxMouseEvent& event)
{
    if (pressedDown) {
        pressedDown = false;
        state_handler.clean_state(StateHandler::State::Pressed);
        Refresh();
        sendButtonEvent();
    }
}

void SideButton::mouseLeave(wxMouseEvent& event)
{
    if (pressedDown) {
        pressedDown = false;
        state_handler.clean_state(StateHandler::State::Pressed);
        Refresh();
    }
}

void SideButton::sendButtonEvent()
{
    wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
}
