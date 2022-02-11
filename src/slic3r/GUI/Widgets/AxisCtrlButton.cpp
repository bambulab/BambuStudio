#include "AxisCtrlButton.hpp"
#include "Label.hpp"

#include <wx/dcgraph.h>

static const wxColour bd = wxColour(0x00AE42);
static const wxColour bg = wxColour(0xD1D1D1);
static const wxColour inner_bg = wxColour(0xE5E5E5);
static const wxColour blank_bg = wxColour(0xFFFFFF);
static const wxColour text_xy_color = wxColour(0x352F2D);
static const wxColour text_num_color = wxColour(0x898989);
static const double sqrt2 = std::sqrt(2);

BEGIN_EVENT_TABLE(AxisCtrlButton, wxPanel)
EVT_LEFT_DOWN(AxisCtrlButton::mouseDown)
EVT_LEFT_UP(AxisCtrlButton::mouseReleased)
EVT_MOTION(AxisCtrlButton::mouseMoving)
EVT_PAINT(AxisCtrlButton::paintEvent)   
END_EVENT_TABLE()

AxisCtrlButton::AxisCtrlButton(wxWindow* parent, long stlye)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, stlye)
    , r_outer(105.0), r_inner(60.0), r_blank(15.0), gap(5.0), last_pos(8), current_pos(8)//don't change init value
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

    state_handler.attach({ &border_color, &background_color });
    state_handler.update_binds();

    SetMinSize(wxSize(-1,-1));
}

void AxisCtrlButton::updateParams() {
    double stretch = std::min(stretch_x, stretch_y);
	r_outer *= stretch;
    r_inner *= stretch;
    r_blank *= stretch;
    gap *= stretch;
}

void AxisCtrlButton::SetMinSize(const wxSize& size)
{
    if (size.GetWidth() > 0 && size.GetHeight() > 0) {
        stretch_x = size.GetWidth() / 212.0;
        stretch_y = size.GetHeight() / 212.0;
		minSize = size;
        updateParams();
    }
    else if (size.GetWidth() > 0) {
        stretch_x = size.GetWidth() / 212.0;
		minSize.x = size.x;
        updateParams();
    }
    else if (size.GetHeight() > 0) {
        stretch_y = size.GetHeight() / 212.0;
		minSize.y = size.y;
        updateParams();
    }
    else {
        stretch_x = 1.0;
        stretch_y = 1.0;
        minSize = wxSize(212, 212);
    }
    wxWindow::SetMinSize(minSize);
    center = wxPoint(minSize.x / 2, minSize.y / 2);
}

void AxisCtrlButton::SetBorderColor(StateColor const& color)
{
    border_color = color;
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
    wxGCDC gcdc(dc);
    render(gcdc);
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void AxisCtrlButton::render(wxDC& dc)
{
    wxGraphicsContext* gc = dc.GetGraphicsContext();

    int states = state_handler.states();
	wxSize size = GetSize();

    gc->PushState();
    gc->Translate(center.x, center.y);
	
	//draw the outer ring
    wxGraphicsPath outer_path = gc->CreatePath();
    outer_path.AddCircle(0, 0, r_outer);
    outer_path.AddCircle(0, 0, r_inner);
    gc->SetPen(bg);
    gc->SetBrush(bg);
    gc->DrawPath(outer_path);

	//draw the inner ring
    wxGraphicsPath inner_path = gc->CreatePath();
    inner_path.AddCircle(0, 0, r_inner);
    inner_path.AddCircle(0, 0, r_blank);
	gc->SetPen(inner_bg);
	gc->SetBrush(inner_bg);
	gc->DrawPath(inner_path);

	//draw an arc in corresponding position
	if (current_pos != CurrentPos::UNDEFINED) {
		wxGraphicsPath path = gc->CreatePath();
		if (current_pos < 4) {
			path.AddArc(0, 0, r_outer, (5 - 2 * current_pos) * PI / 4, (7 - 2 * current_pos) * PI / 4, true);
			path.AddArc(0, 0, r_inner, (7 - 2 * current_pos) * PI / 4, (5 - 2 * current_pos) * PI / 4, false);
			path.CloseSubpath();
			gc->SetBrush(wxBrush(background_color.colorForStates(states)));
		}
		else if (current_pos < 8) {
			path.AddArc(0, 0, r_inner, (5 - 2 * current_pos) * PI / 4, (7 - 2 * current_pos) * PI / 4, true);
			path.AddArc(0, 0, r_blank, (7 - 2 * current_pos) * PI / 4, (5 - 2 * current_pos) * PI / 4, false);
			path.CloseSubpath();
			gc->SetBrush(wxBrush(inner_background_color.colorForStates(states)));
		}
		gc->SetPen(wxPen(border_color.colorForStates(states),2));
		gc->DrawPath(path);
	}

	//draw rectangle gap
	gc->SetPen(blank_bg);
	gc->SetBrush(blank_bg);
	gc->PushState();
	gc->Rotate(-PI / 4);
	gc->DrawRectangle(-sqrt2 * size.x / 2, -sqrt2 * gap / 2, sqrt2 * size.x, sqrt2 * gap);
	gc->Rotate(-PI / 2);
	gc->DrawRectangle(-sqrt2 * size.x / 2, -sqrt2 * gap / 2, sqrt2 * size.x, sqrt2 * gap);
	gc->PopState();

	//draw linear border of the arc
	if (current_pos != CurrentPos::UNDEFINED) {
		wxGraphicsPath line_path1 = gc->CreatePath();
		wxGraphicsPath line_path2 = gc->CreatePath();
		if (current_pos < 4) {
			line_path1.MoveToPoint(r_inner, -sqrt2 * gap / 2); line_path1.AddLineToPoint(r_outer, -sqrt2 * gap / 2);
			line_path2.MoveToPoint(-r_inner, -sqrt2 * gap / 2); line_path2.AddLineToPoint(-r_outer, -sqrt2 * gap / 2);
		}
		else if (current_pos < 8) {
			line_path1.MoveToPoint(r_blank, -sqrt2 * gap / 2); line_path1.AddLineToPoint(r_inner, -sqrt2 * gap / 2);
			line_path2.MoveToPoint(-r_blank, -sqrt2 * gap / 2); line_path2.AddLineToPoint(-r_inner, -sqrt2 * gap / 2);
		}
		gc->PushState();
		gc->Rotate(-(1 + 2 * current_pos) * PI / 4);
		gc->SetPen(wxPen(border_color.colorForStates(states),2));
		gc->StrokePath(line_path1);
		gc->Rotate(PI / 2);
		gc->StrokePath(line_path2);
		gc->PopState();
	}

	//draw text
	gc->SetFont(Label::Body_12, text_xy_color);
	wxDouble w, h;

	gc->GetTextExtent("Y", &w, &h);
	gc->DrawText(wxT("Y"), -w / 2, -r_outer + (r_outer - r_inner) / 2 - h / 2);
	gc->GetTextExtent("-X", &w, &h);
	gc->DrawText(wxT("-X"), -r_outer + (r_outer - r_inner) / 2 - w / 2, - h / 2);
	gc->GetTextExtent("-Y", &w, &h);
	gc->DrawText(wxT("-Y"), -w / 2, r_outer - (r_outer - r_inner) / 2 - h / 2);
	gc->GetTextExtent("X", &w, &h);
	gc->DrawText(wxT("X"), r_outer - (r_outer - r_inner) / 2 - w / 2, -h / 2);

	gc->SetFont(Label::Body_12, text_num_color);

	gc->PushState();
	gc->Rotate(PI / 4);
	gc->GetTextExtent("+10", &w, &h);
	gc->DrawText(wxT("+10"), sqrt2 * gap, -r_outer + (r_outer - r_inner) / 2 - h / 2);
	gc->GetTextExtent("+1", &w, &h);
	gc->DrawText(wxT("+1"), sqrt2 * gap, -r_inner + (r_inner - r_blank) / 2 - h / 2);
	gc->GetTextExtent("-1", &w, &h);
	gc->DrawText(wxT("-1"), sqrt2 * gap, r_inner - (r_inner - r_blank) / 2 - h / 2);
	gc->GetTextExtent("-10", &w, &h);
	gc->DrawText(wxT("-10"), sqrt2 * gap, r_outer - (r_outer - r_inner) / 2 - h / 2);
	gc->PopState();


	gc->PopState();
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
    if (pressedDown)
        return;
	wxPoint mouse_pos(event.GetX(), event.GetY());
	wxPoint transformed_mouse_pos = mouse_pos - center;
	double r_temp = transformed_mouse_pos.x * transformed_mouse_pos.x + transformed_mouse_pos.y * transformed_mouse_pos.y;
	if (r_temp > r_outer * r_outer || r_temp < r_blank * r_blank) {
		current_pos = CurrentPos::UNDEFINED;
	}
	else if (r_temp > r_inner * r_inner) {
		if (transformed_mouse_pos.y < transformed_mouse_pos.x - gap && transformed_mouse_pos.y < -transformed_mouse_pos.x - gap)
		{
			current_pos = CurrentPos::OUTER_UP;
		}
		else if (transformed_mouse_pos.y > transformed_mouse_pos.x + gap && transformed_mouse_pos.y < -transformed_mouse_pos.x - gap)
		{
			current_pos = CurrentPos::OUTER_LEFT;
		}
		else if (transformed_mouse_pos.y > transformed_mouse_pos.x + gap && transformed_mouse_pos.y > -transformed_mouse_pos.x + gap)
		{
			current_pos = CurrentPos::OUTER_DOWN;
		}
		else if (transformed_mouse_pos.y < transformed_mouse_pos.x - gap && transformed_mouse_pos.y > -transformed_mouse_pos.x + gap)
		{
			current_pos = CurrentPos::OUTER_RIGHT;
		}
        else {
            current_pos = CurrentPos::UNDEFINED;
        }
	}
	else if (r_temp > r_blank * r_blank) {
		if (transformed_mouse_pos.y < transformed_mouse_pos.x - gap && transformed_mouse_pos.y < -transformed_mouse_pos.x - gap)
		{
			current_pos = CurrentPos::INNER_UP;
		}
		else if (transformed_mouse_pos.y > transformed_mouse_pos.x + gap && transformed_mouse_pos.y < -transformed_mouse_pos.x - gap)
		{
			current_pos = CurrentPos::INNER_LEFT;
		}
		else if (transformed_mouse_pos.y > transformed_mouse_pos.x + gap && transformed_mouse_pos.y > -transformed_mouse_pos.x + gap)
		{
			current_pos = CurrentPos::INNER_DOWN;
		}
		else if (transformed_mouse_pos.y < transformed_mouse_pos.x - gap && transformed_mouse_pos.y > -transformed_mouse_pos.x + gap)
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