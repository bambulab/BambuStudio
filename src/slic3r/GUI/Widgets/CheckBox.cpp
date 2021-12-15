#include "CheckBox.hpp"

#include "../wxExtensions.hpp"

static wxBitmap& CheckBoxBitmap(int index)
{
	static wxBitmap bmOn = create_scaled_bitmap("check_on");
	static wxBitmap bmOff = create_scaled_bitmap("check_off");
	return index == 0 ? bmOn : bmOff;

}
CheckBox::CheckBox(wxWindow* parent)
	: wxBitmapToggleButton(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(16 * em_unit(parent) / 10, 16 * em_unit(parent) / 10), wxBORDER_NONE)
{
	//SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
	if (parent)
		SetBackgroundColour(parent->GetBackgroundColour());
	Bind(wxEVT_TOGGLEBUTTON, [this](auto& e) { update(); e.Skip(); });
    Bind(wxEVT_MOVE, [this](auto &e) {
		e.Skip();
	});
	update();
}

void CheckBox::SetValue(bool value)
{
	wxBitmapToggleButton::SetValue(value);
	update();
}

void CheckBox::update()
{
	SetBitmap(CheckBoxBitmap(GetValue() ? 0 : 1));
	auto em = em_unit(GetParent());
	SetMinSize({ 4 * em, 16 * em / 10 });
}