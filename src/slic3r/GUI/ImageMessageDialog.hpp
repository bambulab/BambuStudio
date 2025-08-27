#pragma once

#include <unordered_set>
#include <wx/statbmp.h>
#include <wx/webrequest.h>

#include "GUI_Utils.hpp"
#include "Widgets/StateColor.hpp"

class Label;
class Button;

namespace Slic3r {

class MachineObject;//Previous definitions

namespace GUI {

class ImageMessageDialog : public DPIDialog
{
public:
    ImageMessageDialog(wxWindow       *parent,
                      wxWindowID  id = wxID_ANY,
                      const wxString& title = wxEmptyString,
                       const wxString &message = wxEmptyString,
                      const wxPoint& pos = wxDefaultPosition,
                      const wxSize& size = wxDefaultSize,
                      long  style = wxCLOSE_BOX | wxCAPTION);
    ~ImageMessageDialog();

    void on_dpi_changed(const wxRect& suggested_rect);

private:
    std::unordered_set<Button*> m_used_button;

    wxStaticBitmap* m_error_picture;
    Label* m_error_msg_label{ nullptr };
    Label* m_error_code_label{ nullptr };
    wxBoxSizer* m_sizer_main;
    wxBoxSizer* m_sizer_button;
    wxPanel* m_scroll_area{ nullptr };

    std::map<int, Button*> m_button_list;
    StateColor btn_bg_white;
};
}} // namespace Slic3r::GUI
