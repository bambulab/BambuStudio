#include "UpgradePanel.hpp"

namespace Slic3r {
namespace GUI {

UpgradePanel::UpgradePanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style)
    :wxPanel(parent, id, pos, size, style)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__
    auto m_main_sizer = new wxBoxSizer(wxHORIZONTAL);
    SetSizerAndFit(m_main_sizer);
}

UpgradePanel::~UpgradePanel()
{
    ;
}



}
}