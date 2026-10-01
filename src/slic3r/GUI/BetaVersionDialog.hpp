#ifndef slic3r_GUI_BetaVersionDialog_hpp_
#define slic3r_GUI_BetaVersionDialog_hpp_

#include "GUI_Utils.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"

namespace Slic3r { namespace GUI {

// Dialog shown before the UpdateVersionDialog when a Beta release is detected.
// Explains what a Beta version is with three detail points in a descriptive panel.
//
// Layout:
//   Title bar:  "Software Update"
//   Row 1:      Bold heading — current version line
//   Row 2:      Overview text
//   Row 3:      Gray panel with 3 numbered detail items (title + body each)
//   Row 4:      [Try Now (brand)]  [Skip]  [Don't show me Beta updates again]
//
// Return codes:
//   wxID_YES  — user clicked "Try Now"
//   wxID_NO   — user clicked "Skip"
//   wxID_CANCEL — user clicked "Don't show me Beta updates again"
class BetaVersionDialog : public DPIDialog
{
public:
    BetaVersionDialog(wxWindow *parent = nullptr);
    ~BetaVersionDialog();

    void updateContent(const wxString &available_version,
                       const wxString &current_version);

    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void createDetailItem(wxSizer *parent_sizer, wxWindow *parent_win,
                          int index,
                          const wxString &title, const wxString &body,
                          const wxString &bold_segment = wxEmptyString);

    Label  *m_heading_label{nullptr};
    Label  *m_version_label{nullptr};
    Label  *m_overview_label{nullptr};
    wxPanel *m_detail_panel{nullptr};
    wxBoxSizer *m_detail_sizer{nullptr};

    Button *m_button_try_now{nullptr};
    Button *m_button_skip{nullptr};
    Button *m_button_dont_show{nullptr};

    // Keeps track of detail item labels for potential updates
    std::vector<Label *> m_detail_title_labels;
    std::vector<Label *> m_detail_body_labels;
};

}} // namespace Slic3r::GUI

#endif
