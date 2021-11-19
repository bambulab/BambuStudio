#include "Label.hpp"

static wxFont sysFont(int size, bool bold) {
	//wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	//if (bold)
	//	font.MakeBold();
	//font.SetPointSize(size);
#ifdef __linux__
	return wxFont{};
#endif
	auto face = wxString::FromUTF8("HarmonyOS Sans SC");
#ifdef __WIN32__
	wxFont font{ {0, size * 5 / 4}, wxFONTFAMILY_SWISS, wxNORMAL, bold ? wxBOLD : wxNORMAL, false, face };
#else
	wxFont font{size, wxFONTFAMILY_SWISS, wxNORMAL, bold ? wxBOLD : wxNORMAL, false, face };
#endif
	font.SetFaceName(face);
	if (!font.IsOk()) {
		font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
		if (bold)
			font.MakeBold();
#ifdef __WIN32__
		font.SetPixelSize({ 0, size });
#else
		font.SetPointSize(size);
#endif
	}
	return font;
}
wxFont Label::Head_24 = sysFont(24, true);
wxFont Label::Head_18 = sysFont(18, true);
wxFont Label::Head_16 = sysFont(16, true);
wxFont Label::Head_14 = sysFont(14, true);
wxFont Label::Head_12 = sysFont(12, true);

wxFont Label::Body_16 = sysFont(16, false);
wxFont Label::Body_14 = sysFont(14, false);
wxFont Label::Body_12 = sysFont(12, false);
wxFont Label::Body_10 = sysFont(10, false);

Label::Label(wxString const & text, wxWindow* parent)
	: wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, 0)
{
	SetFont(Body_16);
}

Label::Label(wxFont const& font, wxWindow* parent)
	: wxStaticText(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0)
{
	SetFont(font);
}

Label::Label(wxFont const& font, wxString const& text, wxWindow* parent)
	: wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, 0)
{
	SetFont(font);
}
