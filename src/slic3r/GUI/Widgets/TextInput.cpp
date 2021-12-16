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

TextInput::TextInput(wxWindow *     parent,
                     wxString       text,
                     wxString       label,
                     wxString       icon,
                     const wxPoint &pos,
                     const wxSize & size,
                     long           style)
    : wxWindow(parent, wxID_ANY, pos, size)
{
    hover = false;
    radius = 0;
    border_normal = wxColour("#DBDBDB");
    border_disabled = wxColour("#DBDBDB");
    border_focused = wxColour("#1F8EEA");
    text_normal = text_focused = *wxBLACK;
    text_disabled = wxColour("#6D6D6D");
    background_normal = *wxWHITE_BRUSH;
    background_disabled = wxBrush(wxColour("#F0F0F0"));
    background_focused = *wxWHITE_BRUSH;
    SetFont(Label::Body_12);
    wxWindow::SetLabel(label);
    text_ctrl = new wxTextCtrl(this, wxID_ANY, text, {5, 5}, wxDefaultSize,
                               style | wxBORDER_NONE);
    text_ctrl->Bind(wxEVT_SET_FOCUS, [this](auto &e) {
        e.Skip();
        paintNow();
    });
    text_ctrl->Bind(wxEVT_KILL_FOCUS, [this](auto &e) {
        e.Skip();
        paintNow();
    });
    text_ctrl->SetFont(Label::Body_14);
    if (!icon.IsEmpty()) {
        this->icon = ScalableBitmap(this, icon.ToStdString(), 20);
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

bool TextInput::SetForegroundColour(const wxColour& colour)
{
    SetForegroundColor(colour, colour, colour);
    return true;
}

bool TextInput::SetBackgroundColour(wxColour const& color)
{
    SetBackgroundColor(color, color, color);
    return true;
}

void TextInput::SetBorderColor(wxColor normal, wxColor hover, wxColor pressed)
{
    border_normal = normal;
    border_disabled = hover;
    border_focused = pressed;
    paintNow();
}

void TextInput::SetForegroundColor(wxColor normal, wxColor hover, wxColor pressed)
{
    text_normal = normal;
    text_disabled = hover;
    text_focused = pressed;
    paintNow();
}

void TextInput::SetBackgroundColor(wxColor normal, wxColor hover, wxColor pressed)
{
    background_normal = normal;
    border_disabled = hover;
    background_focused = pressed;
    paintNow();
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
    paintNow();
    return result;
}

void TextInput::paintEvent(wxPaintEvent& evt)
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
    if (!text_ctrl->IsEnabled()) {
        dc.SetPen(border_disabled);
        dc.SetBrush(background_disabled);
        dc.SetTextForeground(text_disabled);
    }
    else if (text_ctrl->HasFocus()) {
        dc.SetPen(border_focused);
        dc.SetBrush(background_focused);
        dc.SetTextForeground(text_focused);
    }
    else{
        dc.SetPen(border_normal);
        dc.SetBrush(background_normal);
        dc.SetTextForeground(text_normal);
    }

    wxSize size = GetSize();
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
    auto text = GetLabel();
    if (!text.IsEmpty()) {
        wxSize textSize = text_ctrl->GetSize();
        pt.x += textSize.x;
        pt.y = (size.y + textSize.y) / 2 - labelSize.y;
        dc.SetFont(GetFont());
        dc.DrawText(text, pt);
    }
}

void TextInput::messureSize()
{
    wxSize size     = GetSize();
    wxSize textSize = text_ctrl->GetSize();
    int    h        = textSize.y * 24 / 14;
    if (size.y < h) {
        size.y = h;
        SetSize(size);
    } else {
        textSize.y = size.y * 14 / 24;
    }
    labelSize = GetTextExtent(GetLabel());
    wxPoint textPos = {5, 0};
    if (this->icon.bmp().IsOk()) {
        wxSize szIcon = this->icon.bmp().GetSize();
        textPos.x += szIcon.x;
    }
    textSize.x = size.x - textPos.x - labelSize.x - 5;
    text_ctrl->SetSize(textSize);
    text_ctrl->SetPosition({textPos.x, (size.y - textSize.y) / 2});
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
