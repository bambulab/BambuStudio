#ifndef slic3r_GUI_SwitchButton_hpp_
#define slic3r_GUI_SwitchButton_hpp_

#include "../wxExtensions.hpp"

#include <wx/tglbtn.h>

class SwitchButton : public wxBitmapToggleButton
{
public:
	SwitchButton(wxWindow * parent = NULL);

public:
	void SetValue(bool value) override;

	void Rescale();

private:
	void update();

private:
	ScalableBitmap m_on;
	ScalableBitmap m_off;
};

#endif // !slic3r_GUI_SwitchButton_hpp_
