#ifndef slic3r_BindDialog_hpp_
#define slic3r_BindDialog_hpp_


#include "I18N.hpp"

#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/gauge.h>
#include <wx/button.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/dialog.h>
#include "wxExtensions.hpp"
#include "Plater.hpp"
#include "Widgets/StepCtrl.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/ProgressBar.hpp"
#include "Jobs/BindJob.hpp"
#include "BBLStatusBar.hpp"


namespace Slic3r {
namespace GUI {


class BindDialog : public DPIDialog
{
private:
    wxArrayString printer_list_item;
protected:
    wxButton *    btn_start_ssdp;
    wxButton *    btn_stop_ssdp;
    wxStaticText *text_sn;
    wxButton *    btn_refresh;
    wxComboBox *  cb_machine_sn;
    wxStaticText *text_result;
    wxButton *    btn_bind;
    wxButton *    btn_unbind;
    Plater *      m_plater{nullptr};

    std::shared_ptr<BindJob> m_bind_job;
    std::shared_ptr<BBLStatusBar> m_status_bar;

    void on_start_ssdp(wxCommandEvent &event);
    void on_stop_ssdp(wxCommandEvent &event);
    void on_bind_printer(wxCommandEvent &event);
    void on_unbind_printer(wxCommandEvent &event);
    void on_refresh(wxCommandEvent &event);

public:
    BindDialog(Plater *plater = nullptr);

    ~BindDialog();

    void on_dpi_changed(const wxRect &suggested_rect) override;
};

} // GUI
} // Slic3r


#endif  /* slic3r_BindDialog_hpp_ */
