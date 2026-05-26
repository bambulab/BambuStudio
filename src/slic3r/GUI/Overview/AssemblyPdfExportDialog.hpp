#ifndef slic3r_AssemblyPdfExportDialog_hpp_
#define slic3r_AssemblyPdfExportDialog_hpp_

#include "../GUI_Utils.hpp"
#include <wx/string.h>

class wxTextCtrl;
class wxWindow;
class wxBoxSizer;
class Button;

namespace Slic3r {
namespace GUI {

struct AssemblyPdfExportParams
{
    wxString title;
    wxString cover_image_path;
    wxString second_page_image_path;
};

class AssemblyPdfExportDialog : public DPIDialog
{
public:
    AssemblyPdfExportDialog(wxWindow *parent, const AssemblyPdfExportParams &params);
    AssemblyPdfExportParams get_params() const;

private:
    void on_dpi_changed(const wxRect &suggested_rect) override;

    wxTextCtrl *m_title_ctrl{nullptr};
    wxTextCtrl *m_cover_image_ctrl{nullptr};
    wxTextCtrl *m_second_page_image_ctrl{nullptr};
    Button     *m_ok_btn{nullptr};

    wxTextCtrl *create_path_row(wxWindow *parent, wxBoxSizer *sizer,
                                const wxString &label, const wxString &value,
                                const wxString &tooltip = wxEmptyString);
    void browse_image(wxTextCtrl *ctrl);
    // Re-evaluates whether the OK button should be enabled. We disable it
    void update_ok_button_state();
    // Returns true when at least one of the three input fields is non-empty
    bool has_any_input() const;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_AssemblyPdfExportDialog_hpp_
