#ifndef slic3r_GUI_SwitchButton_hpp_
#define slic3r_GUI_SwitchButton_hpp_

#include "../wxExtensions.hpp"

#include <wx/tglbtn.h>

class SwitchButton : public wxBitmapToggleButton
{
public:
	SwitchButton(wxWindow * parent = NULL, bool isBlue = true);

public:
	void SetValue(bool value) override;

	void Rescale();

private:
	void update();

private:
	ScalableBitmap m_on;
	ScalableBitmap m_off;
	ScalableBitmap m_on_monitor;
	ScalableBitmap m_off_monitor;
	bool color;
};

#endif // !slic3r_GUI_SwitchButton_hpp_
