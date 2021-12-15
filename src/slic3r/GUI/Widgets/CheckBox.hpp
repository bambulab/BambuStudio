#ifndef slic3r_GUI_CheckBox_hpp_
#define slic3r_GUI_CheckBox_hpp_

#include <wx/tglbtn.h>

class CheckBox : public wxBitmapToggleButton
{
public:
	CheckBox(wxWindow * parent = NULL);

public:
	void SetValue(bool value) override;

private:
	void update();
};

#endif // !slic3r_GUI_CheckBox_hpp_
