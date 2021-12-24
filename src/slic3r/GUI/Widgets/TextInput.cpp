#include "TextInput.hpp"
#include "Label.hpp"

#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(TextInput, wxPanel)

EVT_MOTION(TextInput::mouseMoved)
EVT_ENTER_WINDOW(TextInput::mouseEnterWindow)
EVT_LEAVE_WINDOW(TextInput::mouseLeaveWindow)
EVT_KEY_DOWN(TextInput::keyPressed)
EVT_KEY_UP(TextInput::keyReleased)
EVT_MOUSEWHEEL(TextInput::mouseWheelMoved)

// catch paint events
EVT_PAINT(TextInput::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

TextInput::TextInput()
    : state_handler(this)
    , border_color(std::make_pair(0xDBDBDB, (int) StateColor::Disabled),
                   std::make_pair(0x1F8EEA, (int) StateColor::Focused),
                   std::make_pair(0xDBDBDB, (int) StateColor::Normal))
    , text_color(std::make_pair(0xACACAC, (int) StateColor::Disabled),
                 std::make_pair(0x363636, (int) StateColor::Normal))
    , background_color(std::make_pair(0xF0F0F0, (int) StateColor::Disabled),
                 std::make_pair(*wxWHITE, (int) StateColor::Normal))
{
    hover  = false;
    radius = 0;
    SetFont(Label::Body_12);
}

TextInput::TextInput(wxWindow *     parent,
                     wxString       text,
                     wxString       label,
                     wxString       icon,
                     const wxPoint &pos,
                     const wxSize & size,
                     long           style)
    : TextInput()
{
    Create(parent, text, label, icon, pos, size, style);
}

void TextInput::Create(wxWindow *     parent,
                       wxString       text,
                       wxString       label,
                       wxString       icon,
                       const wxPoint &pos,
                       const wxSize & size,
                       long           style)
{
    wxWindow::Create(parent, wxID_ANY, pos, size, style);

    wxWindow::SetLabel(label);
    style &= ~wxRIGHT;
    state_handler.attach({&border_color, &text_color, &background_color});
    state_handler.update_binds();
    state_handler.Bind(EVT_STATE_CHANGED, [this](auto &e) { paintNow(); });
    text_ctrl = new wxTextCtrl(this, wxID_ANY, text, {5, 5}, wxDefaultSize,
                               style | wxBORDER_NONE);
    text_ctrl->Bind(wxEVT_SET_FOCUS, [this](auto &e) {
        e.SetId(GetId());
        ProcessEventLocally(e);
    });
    text_ctrl->Bind(wxEVT_KILL_FOCUS, [this](auto &e) {
        OnEdit();
        e.SetId(GetId());
        ProcessEventLocally(e);
    });
    text_ctrl->Bind(wxEVT_TEXT_ENTER, [this](auto &e) {
        OnEdit();
        e.SetId(GetId());
        ProcessEventLocally(e);
    });
    text_ctrl->SetFont(Label::Body_14);
    if (!icon.IsEmpty()) {
        this->icon = ScalableBitmap(this, icon.ToStdString(), 16);
    }
    messureSize();
}

void TextInput::SetCornerRadius(double radius)
{
    this->radius = radius;
    paintNow();
}

void TextInput::SetLabel(const wxString& label)
{
    wxWindow::SetLabel(label);
    messureSize();
    paintNow();
}

bool TextInput::SetForegroundColour(const wxColour &color)
{
    text_color = StateColor(color);
    state_handler.update_binds();
    return true;
}

bool TextInput::SetBackgroundColour(wxColour const& color)
{
    background_color = StateColor(color);
    state_handler.update_binds();
    return true;
}

void TextInput::Rescale()
{
    if (this->icon.bmp().IsOk())
        this->icon.msw_rescale();
    messureSize();
}

bool TextInput::Enable(bool enable)
{
    bool result = text_ctrl->Enable(enable) && wxWindow::Enable(enable);
    if (result) {
        wxCommandEvent e(EVT_ENABLE_CHANGED);
        GetEventHandler()->ProcessEvent(e);
    }
    return result;
}

void TextInput::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
    if (sizeFlags & wxSIZE_USE_EXISTING) return;
    wxSize size = GetSize();
    wxPoint textPos = {5, 0};
    if (this->icon.bmp().IsOk()) {
        wxSize szIcon = this->icon.bmp().GetSize();
        textPos.x += szIcon.x;
    }
    bool align_right = GetWindowStyle() & wxRIGHT;
    if (align_right) textPos.x += labelSize.x;
    wxSize textSize = text_ctrl->GetSize();
    textSize.x = size.x - textPos.x - labelSize.x - 5;
    text_ctrl->SetSize(textSize);
    text_ctrl->SetPosition({textPos.x, (size.y - textSize.y) / 2});
}

void TextInput::paintEvent(wxPaintEvent &evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxPaintDC dc(this);
    render(dc);
}

/*
 * Alternatively, you can use a clientDC to paint on the panel
 * at any time. Using this generally does not free you from
 * catching paint events, since it is possible that e.g. the window
 * manager throws away your drawing when the window comes to the
 * background, and expects you will redraw it when the window comes
 * back (by sending a paint event).
 */
void TextInput::paintNow()
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
void TextInput::render(wxDC& dc)
{
    int states = state_handler.states();
    char   buf[32];
    wxSize size = GetSize();
    bool   align_right = GetWindowStyle() & wxRIGHT;
    dc.SetPen(wxPen(border_color.colorForStates(states)));
    dc.SetBrush(wxBrush(background_color.colorForStates(states)));
    dc.DrawRoundedRectangle(0, 0, size.x, size.y, radius);
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    // start draw
    wxPoint pt = {5, 0};
    if (icon.bmp().IsOk()) {
        wxSize szIcon = icon.GetBmpSize();
        pt.y = (size.y - szIcon.y) / 2;
        dc.DrawBitmap(icon.bmp(), pt);
        pt.x += szIcon.x + 5;
    }
    auto text = wxWindow::GetLabel();
    if (!text.IsEmpty()) {
        wxSize textSize = text_ctrl->GetSize();
        if (!align_right)
            pt.x += textSize.x;
        pt.y = (size.y + textSize.y) / 2 - labelSize.y;
        dc.SetTextForeground(text_color.colorForStates(states));
        dc.SetFont(GetFont());
        dc.DrawText(text, pt);
    }
}

void TextInput::messureSize()
{
    wxSize size     = GetSize();
    wxSize textSize = text_ctrl->GetSize();
    bool   align_right    = GetWindowStyle() & wxRIGHT;
    int    h        = textSize.y * 24 / 14;
    if (size.y < h) {
        size.y = h;
    } else if (size.y > h) {
        textSize.y = size.y * 14 / 24;
    }
    labelSize = GetTextExtent(wxWindow::GetLabel());
    SetSize(size);
    SetMinSize(size);
}

void TextInput::mouseEnterWindow(wxMouseEvent& event)
{
    if (!hover)
    {
        hover = true;
        paintNow();
    }
}

void TextInput::mouseLeaveWindow(wxMouseEvent& event)
{
    if (hover)
    {
        hover = false;
        paintNow();
    }
}

// currently unused events
void TextInput::mouseMoved(wxMouseEvent& event) {}
void TextInput::mouseWheelMoved(wxMouseEvent& event) {}
void TextInput::keyPressed(wxKeyEvent& event) {}
void TextInput::keyReleased(wxKeyEvent& event) {}
