#include "MinorsLogoutDialog.hpp"
#include "GUI_App.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/RichTextLabel.hpp"
#include "Widgets/StateColor.hpp"
#include <wx/dcbuffer.h>
#include <slic3r/GUI/I18N.hpp>

namespace Slic3r { namespace GUI {

MinorsLogoutDialog::MinorsLogoutDialog(wxWindow* parent, wxString signin_url)
    : DPIDialog(parent, wxID_ANY, _L("Automatically logged out"), wxDefaultPosition, wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
    , m_signin_url(std::move(signin_url))
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);

    const int body_width = FromDIP(440);

    wxBoxSizer* sizer_main = new wxBoxSizer(wxVERTICAL);

    auto line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(body_width, 1));
    line_top->SetBackgroundColour(wxColour(166, 169, 170));
    sizer_main->Add(line_top, 0, wxEXPAND, 0);

    // TODO: portal-qa is the QA-environment host; swap to the production host before release.
    const wxString URL_AGE_REQ = "https://portal-qa.bambulab.net/zh/policies/page/age-requirements-for-bambulab-account";
    const wxString URL_PRIVACY = "https://bambulab.com/en/policies/privacy";

    std::vector<RichSegment> segs = {
        { _L("For users under the age of majority, some services will be restricted ."), false, "" },
        { "\n", false, "" },
        { _L("Automatically logged out. Please log in again after binding a parent/\nguardian account. "), true, "" },
        { _L("See our "), false, "" },
        { _L("Age Requirements for Bambu Lab Account"), false, URL_AGE_REQ },
        { ".", false, "" },
        { "\n", false, "" },
        { _L("To understand our privacy practices, see "), false, "" },
        { _L("Privacy Notice"), false, URL_PRIVACY },
        { ".", false, "" },
    };
    auto body = new RichTextLabel(this, std::move(segs), body_width);
    sizer_main->Add(body, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    m_button_got_it = new Button(this, _L("Got it"));
    m_button_got_it->SetBackgroundColor(btn_bg_green);
    m_button_got_it->SetBorderColor(*wxWHITE);
    m_button_got_it->SetTextColor(wxColour("#FFFFFE"));
    m_button_got_it->SetFont(Label::Body_12);
    m_button_got_it->SetMinSize(wxSize(FromDIP(68), FromDIP(24)));
    m_button_got_it->SetCornerRadius(FromDIP(12));
    m_button_got_it->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        if (!m_signin_url.empty())
            wxLaunchDefaultBrowser(m_signin_url);
        EndModal(wxID_OK);
    });

    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_got_it, 0, wxALL, FromDIP(5));
    sizer_main->Add(sizer_button, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(15));

    SetSizer(sizer_main);
    Layout();
    sizer_main->Fit(this);

    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void MinorsLogoutDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    Layout();
    Fit();
    Refresh();
}

}} // namespace Slic3r::GUI
