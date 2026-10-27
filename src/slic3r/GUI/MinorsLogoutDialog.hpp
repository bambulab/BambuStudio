#ifndef slic3r_GUI_MinorsLogoutDialog_hpp_
#define slic3r_GUI_MinorsLogoutDialog_hpp_

#include "GUI_Utils.hpp"
#include "Widgets/Button.hpp"

namespace Slic3r { namespace GUI {

// Shown when a minor account is not linked to a guardian (family_link_decision == no_config)
// and the user has been force-logged-out. Offers links to the age-requirements and privacy
// policies plus a "Bind Now" action.
class MinorsLogoutDialog : public DPIDialog
{
public:
    explicit MinorsLogoutDialog(wxWindow* parent, wxString signin_url = wxString());
    ~MinorsLogoutDialog() override = default;

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    Button*  m_button_got_it{nullptr};
    wxString m_signin_url;   // family-hub sign-in url; empty => "Got it" just closes the dialog
};

}} // namespace Slic3r::GUI

#endif
