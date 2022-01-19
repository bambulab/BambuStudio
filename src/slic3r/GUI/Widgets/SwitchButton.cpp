#include "SwitchButton.hpp"

#include "../wxExtensions.hpp"

SwitchButton::SwitchButton(wxWindow* parent)
	: wxBitmapToggleButton(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
	, m_on(this, "toggle_on", 16)
	, m_off(this, "toggle_off", 16)
{
	//SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
	if (parent)
		SetBackgroundColour(parent->GetBackgroundColour());
	Bind(wxEVT_TOGGLEBUTTON, [this](auto& e) { update(); e.Skip(); });
	SetSize(m_on.GetBmpSize());
	update();
}

void SwitchButton::SetValue(bool value)
{
	wxBitmapToggleButton::SetValue(value);
	update();
}

void SwitchButton::Rescale()
{
	m_on.msw_rescale();
	m_off.msw_rescale();
	SetSize(m_on.GetBmpSize());
	update();
}

void SwitchButton::update()
{
	SetBitmap((GetValue() ? m_on : m_off).bmp());
}