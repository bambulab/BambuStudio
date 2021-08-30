#ifndef slic3r_GUI_PrintResultDialog_hpp_
#define slic3r_GUI_PrintResultDialog_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/collpane.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

namespace Slic3r { 
namespace GUI {

#define FAIL_REASON_NUM     13
#define AMS_FAIL_REASON_NUM 6

class PrintSummary {
public:
    std::string username;
    std::string device_id;
    std::string device_ip;
    std::string host_ip;
    std::string user_id;
    std::string slicer_version;
    std::string device_version;
    std::string start_time;
    int start;
    std::string end_time;
    std::string print_filename;
    bool has_time_start;
    std::time_t time_start;
    int duration;
    
    PrintSummary() {
        username = "";
        device_id = "";
        user_id = "";
        slicer_version = "";
        start_time = "";
        end_time = "";
        print_filename = "";
        device_ip = "192.168.0.1";
        host_ip = "192.168.0.1";
        duration = 0;
        has_time_start = false;
        start = 0;
    }
};


class PrintResultDialog : public DPIDialog
{
private:
    PrintSummary* summary;
    enum {
        DIALOG_MARGIN = 15,
        MIN_HEIGHT = 35,
        MAX_HEIGHT = 35,
        MIN_HEIGHT_EXPANDED = 50,
        WIN_WIDTH = 35,
        SPACING = 10,
    };

    void fit_no_shrink();
    void on_close(SimpleEvent& evt);

    wxStaticText* m_label_title;
    wxRadioButton* m_radio_single;
    wxRadioButton* m_radio_ams;
    wxRadioButton* m_radio_other;

    wxRadioButton* m_radio_result1;
    wxRadioButton* m_radio_result2;
    wxRadioButton* m_radio_result3;
    wxRadioButton* m_radio_result4;

    /* problems */
    wxCheckBox* m_cb_fail_reason[FAIL_REASON_NUM];
    wxTextCtrl* txt_fail_reason[FAIL_REASON_NUM];

    wxCheckBox* m_ams_fail_reason[AMS_FAIL_REASON_NUM];
    wxTextCtrl* txt_ams_fail_reason[AMS_FAIL_REASON_NUM];

    wxCheckBox* m_cb_other_reason;
    
    wxTextCtrl* m_other_reason;
    wxButton* m_btn_submit;

    void submit();
public:
    PrintResultDialog(PrintSummary *s);
protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
};

} // namespace GUI
} // namespace Slic3r

#endif
