#ifndef slic3r_GUI_SwitchButton_hpp_
#define slic3r_GUI_SwitchButton_hpp_

#include <wx/tglbtn.h>

class SwitchButton : public wxBitmapToggleButton
{
public:
	SwitchButton(wxWindow * parent = NULL);

public:
	void SetValue(bool value) override;

private:
	void update();
};

#endif // !slic3r_GUI_SwitchButton_hpp_
