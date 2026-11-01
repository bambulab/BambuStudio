#include "PrinterWebView.hpp"

#include "I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r_version.h"

#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>

#include <slic3r/GUI/Widgets/WebView.hpp>
#include "DeviceWeb/DeviceWebHealth.hpp"
#include "Widgets/WebViewTraceLogger.hpp"

namespace pt = boost::property_tree;

namespace Slic3r {
namespace GUI {

PrinterWebView::PrinterWebView(wxWindow *parent, const wxString &view_name, WebViewProtectionMode mode)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
 {

    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);

      // Create the webview
    m_browser = WebView::CreateWebView(this, "", view_name, mode);
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }
    if (mode == WebViewProtectionMode::DeviceHost) {
        m_health_monitoring_available =
            m_browser->AddUserScript(wxString::FromUTF8(DeviceWebHealth::DiagnosticShim()),
                                     wxWEBVIEW_INJECT_AT_DOCUMENT_START);
        if (!m_health_monitoring_available) {
            WebViewTraceLogger::Emit(WebViewTraceLogger::Stage::L4_READY, m_browser->GetName(),
                                     "shim_injection_failed", WebViewTraceLogger::Fields(),
                                     WebViewTraceLogger::Severity::Warning);
        }
    }

    SetSizer(topsizer);

    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    // Log backend information
    /* m_browser->GetUserAgent() may lead crash
    if (wxGetApp().get_mode() == comDevelop) {
        wxLogMessage(wxWebView::GetBackendVersionInfo().ToString());
        wxLogMessage("Backend: %s Version: %s", m_browser->GetClassInfo()->GetClassName(),
            wxWebView::GetBackendVersionInfo().ToString());
        wxLogMessage("User Agent: %s", m_browser->GetUserAgent());
    }
    */

    //Zoom
    m_zoomFactor = 100;

    //Connect the idle events
    Bind(wxEVT_CLOSE_WINDOW, &PrinterWebView::OnClose, this);

 }

PrinterWebView::~PrinterWebView()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Start";
    SetEvtHandlerEnabled(false);

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " End";
}


void PrinterWebView::load_url(const wxString& url)
{
//    this->Show();
//    this->Raise();
    if (m_browser == nullptr)
        return;
    m_browser->LoadURL(url);
    //m_browser->SetFocus();
    UpdateState();
}
/**
 * Method that retrieves the current state from the web control and updates the
 * GUI the reflect this current state.
 */
void PrinterWebView::UpdateState() {
  // SetTitle(m_browser->GetCurrentTitle());

}

void PrinterWebView::OnClose(wxCloseEvent& evt)
{
    this->Hide();
}

} // GUI
} // Slic3r
