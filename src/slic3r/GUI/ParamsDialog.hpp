#ifndef slic3r_GUI_ParamsDialog_hpp_
#define slic3r_GUI_ParamsDialog_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/collpane.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

namespace Slic3r { 
namespace GUI {

class ParamsPanel;

class ParamsDialog : public DPIDialog
{
public:
    ParamsDialog(wxWindow * parent);

    ParamsPanel * panel() { return m_panel; }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    void submit();

private:
    ParamsPanel * m_panel;
    wxButton* m_btn_submit;

    wxWindowDisabler * m_winDisabler = nullptr;
};

} // namespace GUI
} // namespace Slic3r

#endif
