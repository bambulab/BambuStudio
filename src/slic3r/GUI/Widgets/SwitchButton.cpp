#include "SwitchButton.hpp"

#include "../wxExtensions.hpp"

static wxBitmap& switchButtonBitmap(int index)
{
	static wxBitmap bmOn = create_scaled_bitmap("toggle_on");
	static wxBitmap bmOff = create_scaled_bitmap("toggle_off");
	return index == 0 ? bmOn : bmOff;

}
SwitchButton::SwitchButton(wxWindow* parent)
	: wxBitmapToggleButton(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(4 * em_unit(parent), -1), wxBORDER_NONE)
{
	//SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
	if (parent)
		SetBackgroundColour(parent->GetBackgroundColour());
	Bind(wxEVT_TOGGLEBUTTON, [this](auto& e) { update(); e.Skip(); });
	update();
}

void SwitchButton::SetValue(bool value)
{
	wxBitmapToggleButton::SetValue(value);
	update();
}

void SwitchButton::update()
{
	SetBitmap(switchButtonBitmap(GetValue() ? 0 : 1));
}