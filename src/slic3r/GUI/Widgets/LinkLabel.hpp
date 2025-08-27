#ifndef slic3r_GUI_LinkLabel_hpp_
#define slic3r_GUI_LinkLabel_hpp_

#include <wx/panel.h>
#include "Label.hpp"

class LinkLabel : public wxWindow
{
private:
    wxString m_url;
    Label *  m_txt{nullptr};
    wxPanel* m_underline{nullptr};

public:
    LinkLabel(wxWindow *parent, wxString const &text, std::string url, long style = 0, wxSize size = wxDefaultSize);
    ~LinkLabel(){};

    void link(wxMouseEvent &evt);
    Label *getLabel(){return m_txt;};
    bool SeLinkLabelFColour(const wxColour &colour);
    bool SeLinkLabelBColour(const wxColour &colour);
};

#endif // !slic3r_GUI_LinkLabel_hpp_
