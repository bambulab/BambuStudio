#include "Button.hpp"
#include "Label.hpp"

#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(Button, StaticBox)

EVT_LEFT_DOWN(Button::mouseDown)
EVT_LEFT_UP(Button::mouseReleased)
EVT_MOUSE_CAPTURE_LOST(Button::mouseCaptureLost)
EVT_KEY_DOWN(Button::keyDownUp)
EVT_KEY_UP(Button::keyDownUp)

// catch paint events
EVT_PAINT(Button::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

Button::Button()
    : paddingSize(10, 8)
{
    background_color = StateColor(
        std::make_pair(0xF0F0F1, (int) StateColor::Disabled),
        std::make_pair(0x37EE7C, (int) StateColor::Hovered | StateColor::Checked),
        std::make_pair(0x00AE42, (int) StateColor::Checked),
        std::make_pair(*wxLIGHT_GREY, (int) StateColor::Hovered), 
        std::make_pair(*wxWHITE, (int) StateColor::Normal));
    text_color       = StateColor(
        std::make_pair(*wxLIGHT_GREY, (int) StateColor::Disabled), 
        std::make_pair(*wxBLACK, (int) StateColor::Normal));
}

Button::Button(wxWindow* parent, wxString text, wxString icon, long style, int iconSize)
    : Button()
{
    Create(parent, text, icon, style, iconSize);
}

bool Button::Create(wxWindow* parent, wxString text, wxString icon, long style, int iconSize)
{
    StaticBox::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, style);
    state_handler.attach({&text_color});
    state_handler.update_binds();
    //BBS set default font
    SetFont(Label::Body_14);
    wxWindow::SetLabel(text);
    if (!icon.IsEmpty()) {
        //BBS set button icon default size to 20
        this->active_icon = ScalableBitmap(this, icon.ToStdString(), iconSize > 0 ? iconSize : 20);
    }
    messureSize();
    return true;
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
        this->active_icon = ScalableBitmap(this, icon.ToStdString(), this->active_icon.px_cnt());
    }
    else
    {
        this->active_icon = ScalableBitmap();
    }
    Refresh();
}

void Button::SetInactiveIcon(const wxString &icon)
{
    if (!icon.IsEmpty()) {
        // BBS set button icon default size to 20
        this->inactive_icon = ScalableBitmap(this, icon.ToStdString(), this->active_icon.px_cnt());
    } else {
        this->inactive_icon = ScalableBitmap();
    }
    Refresh();
}

void Button::SetMinSize(const wxSize& size)
{
    minSize = size;
    messureSize();
}

void Button::SetPaddingSize(const wxSize& size)
{
    paddingSize = size;
    messureSize();
}

void Button::SetTextColor(StateColor const& color)
{
    text_color = color;
    state_handler.update_binds();
    Refresh();
}

void Button::SetTextColorNormal(wxColor const &color)
{
    text_color.setColorForStates(color, 0);
    Refresh();
}

bool Button::Enable(bool enable)
{
    bool result = wxWindow::Enable(enable);
    if (result) {
        wxCommandEvent e(EVT_ENABLE_CHANGED);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(e);
    }
    return result;
}

void Button::SetCanFocus(bool canFocus) { this->canFocus = canFocus; }

void Button::SetValue(bool state)
{
    if (GetValue() == state) return;
    state_handler.set_state(state ? StateHandler::Checked : 0, StateHandler::Checked);
}

bool Button::GetValue() const { return state_handler.states() & StateHandler::Checked; }

void Button::Rescale()
{
    if (this->active_icon.bmp().IsOk())
        this->active_icon.msw_rescale();

    if (this->inactive_icon.bmp().IsOk())
        this->inactive_icon.msw_rescale();

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

    ScalableBitmap icon;
    if (m_selected || ((states & (int)StateColor::State::Hovered) != 0))
        icon = active_icon;
    else
        icon = inactive_icon;
    int padding = 5;
    if (icon.bmp().IsOk()) {
        if (szContent.y > 0) {
            //BBS norrow size between text and icon
            szContent.x += padding;
        }
        szIcon = icon.GetBmpSize();
        szContent.x += szIcon.x;
        if (szIcon.y > szContent.y)
            szContent.y = szIcon.y;
        if (szContent.x > size.x) {
            int d = std::min(padding, szContent.x - size.x);
            padding -= d;
            szContent.x -= d;
        }
    }
    // move to center
    wxRect rcContent = { {0, 0}, size };
    wxSize offset = (size - szContent) / 2;
    if (offset.x < 0) offset.x = 0;
    rcContent.Deflate(offset.x, offset.y);
    // start draw
    wxPoint pt = rcContent.GetLeftTop();
    if (icon.bmp().IsOk()) {
        pt.y += (rcContent.height - szIcon.y) / 2;
        dc.DrawBitmap(icon.bmp(), pt);
        //BBS norrow size between text and icon
        pt.x += szIcon.x + padding;
        pt.y = rcContent.y;
    }
    auto text = GetLabel();
    if (!text.IsEmpty()) {
        if (pt.x + textSize.x > size.x)
            text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, size.x - pt.x);
        pt.y += (rcContent.height - textSize.y) / 2;
        dc.SetFont(GetFont());
        dc.SetTextForeground(text_color.colorForStates(states));
        dc.DrawText(text, pt);
    }
}

void Button::messureSize()
{
    wxClientDC dc(this);
    textSize = dc.GetTextExtent(GetLabel());
    if (minSize.GetWidth() > 0) {
        wxWindow::SetMinSize(minSize);
        return;
    }
    wxSize szContent = textSize;
    if (this->active_icon.bmp().IsOk()) {
        if (szContent.y > 0) {
            //BBS norrow size between text and icon
            szContent.x += 5;
        }
        wxSize szIcon = this->active_icon.GetBmpSize();
        szContent.x += szIcon.x;
        if (szIcon.y > szContent.y)
            szContent.y = szIcon.y;
    }
    wxSize size = szContent + paddingSize * 2;
    if (minSize.GetHeight() > 0)
        size.SetHeight(minSize.GetHeight());
    wxWindow::SetMinSize(size);
}

void Button::mouseDown(wxMouseEvent& event)
{
    event.Skip();
    pressedDown = true;
    if (canFocus)
        SetFocus();
    if (!HasCapture())
        CaptureMouse();
}

void Button::mouseReleased(wxMouseEvent& event)
{
    event.Skip();
    if (pressedDown) {
        pressedDown = false;
        if (HasCapture())
            ReleaseMouse();
        if (wxRect({0, 0}, GetSize()).Contains(event.GetPosition()))
            sendButtonEvent();
    }
}

void Button::mouseCaptureLost(wxMouseCaptureLostEvent &event)
{
    wxMouseEvent evt;
    mouseReleased(evt);
}

void Button::keyDownUp(wxKeyEvent &event)
{
    if (event.GetKeyCode() == WXK_SPACE || event.GetKeyCode() == WXK_RETURN) {
        wxMouseEvent evt(event.GetEventType() == wxEVT_KEY_UP ? wxEVT_LEFT_UP : wxEVT_LEFT_DOWN);
        event.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        return;
    }
    if (event.GetEventType() == wxEVT_KEY_DOWN &&
        (event.GetKeyCode() == WXK_TAB || event.GetKeyCode() == WXK_LEFT || event.GetKeyCode() == WXK_RIGHT 
        || event.GetKeyCode() == WXK_UP || event.GetKeyCode() == WXK_DOWN))
        HandleAsNavigationKey(event);
    else
        event.Skip();
}

void Button::sendButtonEvent()
{
    wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
}

#ifdef __WIN32__

WXLRESULT Button::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    if (nMsg == WM_GETDLGCODE) { return DLGC_WANTMESSAGE; }
    if (nMsg == WM_KEYDOWN) {
        wxKeyEvent event(CreateKeyEvent(wxEVT_KEY_DOWN, wParam, lParam));
        switch (wParam) {
        case WXK_RETURN: { // WXK_RETURN key is handled by default button
            GetEventHandler()->ProcessEvent(event);
            return 0;
        }
        }
    }
    return wxWindow::MSWWindowProc(nMsg, wParam, lParam);
}

#endif

bool Button::AcceptsFocus() const { return canFocus; }
