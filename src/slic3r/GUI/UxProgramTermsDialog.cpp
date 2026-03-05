#include "UxProgramTermsDialog.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <wx/sizer.h>
#include <wx/utils.h>
#include <wx/webview.h>

#include <algorithm>

namespace Slic3r { namespace GUI {

UxProgramTermsDialog::UxProgramTermsDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE)
{
    SetTitle(_L("Terms of Bambu Lab User Experience Improvement Program"));

    // Prevent user resizing (no resize border / no maximize).
    SetWindowStyleFlag(GetWindowStyleFlag() & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX));

    // Keep the base background consistent with Preferences.
    SetBackgroundColour(*wxWHITE);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    m_webview = WebView::CreateWebView(this, "");
    if (m_webview == nullptr) {
        wxLogError("Could not init ux program terms webview");
        return;
    }
    m_webview->SetBackgroundColour(*wxWHITE);

    fs::path ph(resources_dir());
    ph /= "web/guide/32/32.html";
    m_host_url = ph.string();
    std::replace(m_host_url.begin(), m_host_url.end(), '\\', '/');
    m_host_url = "file:///" + m_host_url;

    // Provide region hint for the embedded page (used to select CN/global policy URL).
    const std::string country_code = Slic3r::GUI::wxGetApp().app_config->get_country_code();
    m_host_url += (country_code == "CN") ? "?region=china" : "?region=US";

    // Provide language from app config (same as Preferences' "language" key), so the page
    // can decide language-related behavior (e.g. which website to open) reliably.
    const std::string language = Slic3r::GUI::wxGetApp().app_config->get("language");
    if (!language.empty())
        m_host_url += "&lang=" + language;

    m_webview->LoadURL(from_u8(m_host_url));

    m_webview->Bind(wxEVT_WEBVIEW_NAVIGATING, [this](wxWebViewEvent& evt) {
        const wxString url = evt.GetURL();
        if (url.StartsWith("bambu://close")) {
            evt.Veto();
            EndModal(wxID_OK);
            return;
        }

        if (url.StartsWith("http://") || url.StartsWith("https://")) {
            evt.Veto();
            wxLaunchDefaultBrowser(url);
            return;
        }
        evt.Skip();
    });

    // The embedded page renders its own action area using the same CSS as guide pages.
    sizer->Add(m_webview, wxSizerFlags(1).Expand());

    SetSizerAndFit(sizer);
    SetMinSize(FromDIP(wxSize(720, 640)));
    SetSize(FromDIP(wxSize(720, 640)));

    CenterOnParent();

    wxGetApp().UpdateDlgDarkUI(this);
}

void UxProgramTermsDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    if (suggested_rect.IsEmpty()) {
        Layout();
        CenterOnParent();
        return;
    }

    SetSize(suggested_rect);
    Layout();

    // Keep it centered after DPI-driven resize.
    CenterOnParent();
}

}} // namespace Slic3r::GUI
