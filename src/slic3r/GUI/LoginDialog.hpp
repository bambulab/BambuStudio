#ifndef slic3r_GUI_LoginDialog_hpp_
#define slic3r_GUI_LoginDialog_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/collpane.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

namespace Slic3r { 
namespace GUI {

class LoginDialog : public DPIDialog
{
private:
    enum {
        DIALOG_MARGIN = 15,
        MIN_HEIGHT = 35,
        MAX_HEIGHT = 35,
        MIN_HEIGHT_EXPANDED = 50,
        WIN_WIDTH = 35,
        SPACING = 10,
    };
    void bind_handlers();
    void fit_no_shrink();

    ScalableBitmap  m_logo_bitmap;
    wxStaticBitmap* m_logo;
    wxStaticText* m_label_user;
    wxTextCtrl* m_txt_user;
    wxStaticText* m_label_password;
    wxTextCtrl* m_txt_password;
    wxStaticText* m_label_tips;
    wxButton* m_btn_login;
    wxButton* m_btn_register;
    wxStaticText* m_label_info;
    wxCollapsiblePane* spoiler;
    wxTextCtrl* txt_stdout;
public:
    LoginDialog();
protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
};

} // namespace GUI
} // namespace Slic3r

#endif
