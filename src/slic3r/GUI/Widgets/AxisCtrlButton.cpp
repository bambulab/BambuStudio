#include "AxisCtrlButton.hpp"
#include "Label.hpp"

#include <wx/dcgraph.h>

const wxColour bd = wxColour(0x00AE42);
const wxColour bg = wxColour(0xD1D1D1);
const wxColour inner_bg = wxColour(0xE5E5E5);
const wxColour blank_bg = wxColour(0xFFFFFF);
const wxColour text_xy_color = wxColour(0x352F2D);
const wxColour text_num_color = wxColour(0x898989);

BEGIN_EVENT_TABLE(AxisCtrlButton, wxPanel)
EVT_LEFT_DOWN(AxisCtrlButton::mouseDown)
EVT_LEFT_UP(AxisCtrlButton::mouseReleased)
EVT_MOTION(AxisCtrlButton::mouseMoving)
EVT_PAINT(AxisCtrlButton::paintEvent)
END_EVENT_TABLE()

AxisCtrlButton::AxisCtrlButton(wxWindow* parent, long stlye)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, stlye)
    , r(105.0), r_inner(61.0), r_blank(16.0), space(5.0), last_pos(8), current_pos(8)//don't change init value
	, state_handler(this)
{
    wxWindow::SetBackgroundColour(parent->GetBackgroundColour());

    border_color.append(bd, StateColor::Hovered);

    background_color.append(bg, StateColor::Disabled);
    background_color.append(0xACACAC, StateColor::Pressed);
    background_color.append(bg, StateColor::Hovered);
    background_color.append(bg, StateColor::Normal);
    background_color.append(bg, StateColor::Enabled);

    inner_background_color.append(inner_bg, StateColor::Disabled);
    inner_background_color.append(0xACACAC, StateColor::Pressed);
    inner_background_color.append(inner_bg, StateColor::Hovered);
    inner_background_color.append(inner_bg, StateColor::Normal);
    inner_background_color.append(inner_bg, StateColor::Enabled);

    state_handler.attach({ &border_color, &text_color, &background_color });
    state_handler.update_binds();

    measureSize();
}

void AxisCtrlButton::updateParams() {
    double stretch = std::min(stretch_x, stretch_y);
    r *= stretch;
    r_inner *= stretch;
    r_blank *= stretch;
    space *= stretch;
}

bool AxisCtrlButton::SetForegroundColour(wxColour const& color)
{
    text_color = StateColor(color);
    state_handler.update_binds();
    return true;
}

bool AxisCtrlButton::SetBackgroundColour(wxColour const& color)
{
    background_color = StateColor(color);
    state_handler.update_binds();
    return true;
}

void AxisCtrlButton::SetMinSize(const wxSize& size)
{
    minSize = size;
    measureSize();
}

void AxisCtrlButton::SetBorderColor(StateColor const& color)
{
    border_color = color;
    state_handler.update_binds();
    Refresh();
}

void AxisCtrlButton::SetForegroundColor(StateColor const& color)
{
    text_color = color;
    state_handler.update_binds();
    Refresh();
}

void AxisCtrlButton::SetBackgroundColor(StateColor const& color)
{
    background_color = color;
    state_handler.update_binds();
    Refresh();
}

void AxisCtrlButton::SetInnerBackgroundColor(StateColor const& color)
{
    inner_background_color = color;
    state_handler.update_binds();
    Refresh();
}

void AxisCtrlButton::paintEvent(wxPaintEvent& evt)
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
void AxisCtrlButton::render(wxDC& dc)
{
    double x = circle_center_pos.x - r, y = circle_center_pos.y - r;
    double sqrt_two = std::sqrt(2);
    int states = state_handler.states();

    dc.SetPen(bg);
    dc.SetBrush(bg);
    dc.DrawEllipticArc(x, y, 2 * r, 2 * r, 0, 0);

    dc.SetPen(inner_bg);
    dc.SetBrush(inner_bg);
    dc.DrawEllipticArc(x - r_inner + r, y - r_inner + r, 2 * r_inner, 2 * r_inner, 0, 0);
    
    dc.SetPen(wxPen(border_color.colorForStates(states),2));
    if (current_pos < 4) {
        dc.SetBrush(wxBrush(background_color.colorForStates(states)));
        switch (current_pos) {
        case OUTER_UP:
            dc.DrawEllipticArc(x, y, 2 * r, 2 * r, 45, 135);
            dc.SetBrush(wxColour(inner_bg));
            dc.DrawEllipticArc(x - r_inner + r, y - r_inner + r, 2 * r_inner, 2 * r_inner, 45, 135);
            break;
        case OUTER_LEFT:
            dc.DrawEllipticArc(x, y, 2 * r, 2 * r, 135, 225);
            dc.SetBrush(wxColour(inner_bg));
            dc.DrawEllipticArc(x - r_inner + r, y - r_inner + r, 2 * r_inner, 2 * r_inner, 135, 225);
            break;
        case OUTER_DOWN:
            dc.DrawEllipticArc(x, y, 2 * r, 2 * r, 225, 315);
            dc.SetBrush(wxColour(inner_bg));
            dc.DrawEllipticArc(x - r_inner + r, y - r_inner + r, 2 * r_inner, 2 * r_inner, 225, 315);
            break;
        case OUTER_RIGHT:
            dc.DrawEllipticArc(x, y, 2 * r, 2 * r, 315, 45);
            dc.SetBrush(wxColour(inner_bg));
            dc.DrawEllipticArc(x - r_inner + r, y - r_inner + r, 2 * r_inner, 2 * r_inner, 315, 45);
            break;
        }
    }
    else {
        dc.SetBrush(wxBrush(inner_background_color.colorForStates(states)));
        switch (current_pos) {
        case INNER_UP:
            dc.DrawEllipticArc(x - r_inner + r, y - r_inner + r, 2 * r_inner, 2 * r_inner, 45, 135);
            dc.SetBrush(wxColour(blank_bg));
            dc.DrawEllipticArc(x - r_blank + r, y - r_blank + r, 2 * r_blank, 2 * r_blank, 45, 135);
            break;
        case INNER_LEFT:
            dc.DrawEllipticArc(x - r_inner + r, y - r_inner + r, 2 * r_inner, 2 * r_inner, 135, 225);
            dc.SetBrush(wxColour(blank_bg));
            dc.DrawEllipticArc(x - r_blank + r, y - r_blank + r, 2 * r_blank, 2 * r_blank, 135, 225);
            break;
        case INNER_DOWN:
            dc.DrawEllipticArc(x - r_inner + r, y - r_inner + r, 2 * r_inner, 2 * r_inner, 225, 315);
            dc.SetBrush(wxColour(blank_bg));
            dc.DrawEllipticArc(x - r_blank + r, y - r_blank + r, 2 * r_blank, 2 * r_blank, 225, 315);
            break;
        case INNER_RIGHT:
            dc.DrawEllipticArc(x - r_inner + r, y - r_inner + r, 2 * r_inner, 2 * r_inner, 315, 45);
            dc.SetBrush(wxColour(blank_bg));
            dc.DrawEllipticArc(x - r_blank + r, y - r_blank + r, 2 * r_blank, 2 * r_blank, 315, 45);
            break;
        }
    }

	int pen_width = dc.GetPen().GetWidth();
	dc.SetPen(wxColour(blank_bg));
	dc.SetBrush(wxColour(blank_bg));
	dc.DrawEllipticArc(x - r_blank + r + pen_width / 2, y - r_blank + r + pen_width / 2, 2 * (r_blank - pen_width), 2 * (r_blank - pen_width), 0, 0);
	wxSize size = GetSize();
	wxPoint rect_blank[4];
	rect_blank[0] = wxPoint(space, 0);
	rect_blank[1] = wxPoint(0, space);
	rect_blank[2] = wxPoint(size.x - space, size.y);
	rect_blank[3] = wxPoint(size.x, size.y - space);
	dc.DrawPolygon(WXSIZEOF(rect_blank), rect_blank);
    wxPoint rect_blank2[4];
	rect_blank2[0] = wxPoint(size.x - space, 0);
	rect_blank2[1] = wxPoint(0, size.y - space);
	rect_blank2[2] = wxPoint(space, size.y);
	rect_blank2[3] = wxPoint(size.x, space);
	dc.DrawPolygon(WXSIZEOF(rect_blank2), rect_blank2);

	dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
	dc.SetFont(Label::Body_12);
	dc.SetTextForeground(text_xy_color);
	wxSize text_size;
	text_size = dc.GetMultiLineTextExtent("Y");
	dc.DrawText(wxT("Y"), circle_center_pos.x - text_size.x / 2, circle_center_pos.y - r + (r - r_inner) / 2 - text_size.y / 2);
	text_size = dc.GetMultiLineTextExtent("-X");
	dc.DrawText(wxT("-X"), x + (r - r_inner) / 2 - text_size.x / 2, circle_center_pos.y - text_size.y / 2);
    text_size = dc.GetMultiLineTextExtent("-Y");
	dc.DrawText(wxT("-Y"), circle_center_pos.x - text_size.x / 2, circle_center_pos.y + r - (r - r_inner) / 2);
    text_size = dc.GetMultiLineTextExtent("X");
	dc.DrawText(wxT("X"), x + 2.0 * r - (r - r_inner) / 2 - text_size.x / 2, circle_center_pos.y - text_size.y / 2);

	dc.SetTextForeground(text_num_color);
	text_size = dc.GetMultiLineTextExtent("+10");
	dc.DrawRotatedText(wxT("+10"), circle_center_pos.x + 2 * space + (r_inner + (r - r_inner) / 2) / sqrt_two, circle_center_pos.y - (r_inner + (r - r_inner) / 2) / sqrt_two + space, -45);
	dc.DrawRotatedText(wxT(" +1"), circle_center_pos.x + space + (r_inner / 1.5) / sqrt_two, circle_center_pos.y - (r_inner / 2) / sqrt_two - text_size.y / 2 + space, -45);
	dc.DrawRotatedText(wxT(" -1"), circle_center_pos.x + space - (r_inner / 1.5) / sqrt_two + text_size.x / 2, circle_center_pos.y + (r_inner / 2) / sqrt_two, -45);
    dc.DrawRotatedText(wxT("-10"), circle_center_pos.x + space - (r_inner + (r - r_inner) / 2) / sqrt_two + text_size.x / 2, circle_center_pos.y + (r_inner + (r - r_inner) / 2) / sqrt_two , -45);

}

void AxisCtrlButton::measureSize()
{
	if (minSize.GetWidth() > 0 && minSize.GetHeight() > 0) {
		stretch_x = minSize.GetWidth() / 212.0;
		stretch_y = minSize.GetHeight() / 212.0;
        updateParams();
    }
	else if (minSize.GetWidth() > 0) {
        stretch_x = minSize.GetWidth() / 212.0;
        updateParams();
	}
	else if (minSize.GetHeight() > 0) {
		stretch_y = minSize.GetHeight() / 212.0;
		updateParams();
	}
	else {
		stretch_x = 1;
		stretch_y = 1;
		minSize = wxSize(212, 212);
	}
	wxWindow::SetMinSize(minSize);
	circle_center_pos = wxPoint(minSize.x / 2, minSize.y / 2);
}

void AxisCtrlButton::mouseDown(wxMouseEvent& event)
{
    event.Skip();
    pressedDown = true;
    SetFocus();
    CaptureMouse();
}

void AxisCtrlButton::mouseReleased(wxMouseEvent& event)
{
    event.Skip();
    if (pressedDown) {
        pressedDown = false;
        ReleaseMouse();
        if (wxRect({ 0, 0 }, GetSize()).Contains(event.GetPosition()))
            sendButtonEvent();
    }
}

void AxisCtrlButton::mouseMoving(wxMouseEvent& event)
{
	wxPoint mouse_pos(event.GetX(), event.GetY());
	wxPoint transformed_mouse_pos = mouse_pos - circle_center_pos;
	double r_temp = transformed_mouse_pos.x * transformed_mouse_pos.x + transformed_mouse_pos.y * transformed_mouse_pos.y;
	if (r_temp > r * r || r_temp < r_blank * r_blank) {
		current_pos = CurrentPos::UNDEFINED;
	}
	else if (r_temp > r_inner * r_inner) {
		if (transformed_mouse_pos.y < transformed_mouse_pos.x - space && transformed_mouse_pos.y < -transformed_mouse_pos.x - space)
		{
			current_pos = CurrentPos::OUTER_UP;
		}
		else if (transformed_mouse_pos.y > transformed_mouse_pos.x + space && transformed_mouse_pos.y < -transformed_mouse_pos.x - space)
		{
			current_pos = CurrentPos::OUTER_LEFT;
		}
		else if (transformed_mouse_pos.y > transformed_mouse_pos.x + space && transformed_mouse_pos.y > -transformed_mouse_pos.x + space)
		{
			current_pos = CurrentPos::OUTER_DOWN;
		}
		else if (transformed_mouse_pos.y < transformed_mouse_pos.x - space && transformed_mouse_pos.y > -transformed_mouse_pos.x + space)
		{
			current_pos = CurrentPos::OUTER_RIGHT;
		}
        else {
            current_pos = CurrentPos::UNDEFINED;
        }
	}
	else if (r_temp > r_blank * r_blank) {
		if (transformed_mouse_pos.y < transformed_mouse_pos.x - space && transformed_mouse_pos.y < -transformed_mouse_pos.x - space)
		{
			current_pos = CurrentPos::INNER_UP;
		}
		else if (transformed_mouse_pos.y > transformed_mouse_pos.x + space && transformed_mouse_pos.y < -transformed_mouse_pos.x - space)
		{
			current_pos = CurrentPos::INNER_LEFT;
		}
		else if (transformed_mouse_pos.y > transformed_mouse_pos.x + space && transformed_mouse_pos.y > -transformed_mouse_pos.x + space)
		{
			current_pos = CurrentPos::INNER_DOWN;
		}
		else if (transformed_mouse_pos.y < transformed_mouse_pos.x - space && transformed_mouse_pos.y > -transformed_mouse_pos.x + space)
		{
			current_pos = CurrentPos::INNER_RIGHT;
		}
        else {
            current_pos = CurrentPos::UNDEFINED;
        }
	}
	if (last_pos != current_pos) {
		last_pos = current_pos;
		Refresh();
	}
}

void AxisCtrlButton::sendButtonEvent()
{
    wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
    event.SetEventObject(this);
    event.SetInt(current_pos);
    GetEventHandler()->ProcessEvent(event);
}