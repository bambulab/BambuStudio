#ifndef slic3r_UpgradePanel_hpp_
#define slic3r_UpgradePanel_hpp_

#include <wx/panel.h>

namespace Slic3r {
namespace GUI {

class UpgradePanel : public wxPanel
{
private:
public:
    UpgradePanel(wxWindow *parent, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~UpgradePanel();
    void msw_rescale() {}
};

}
}

#endif
