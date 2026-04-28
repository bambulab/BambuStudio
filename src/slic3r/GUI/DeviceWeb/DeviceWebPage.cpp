#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r_version.h"
#include "DeviceWebPage.hpp"

#if defined(__WXOSX__)
#include "slic3r/Utils/MacDarkMode.hpp"
#endif

#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>

namespace Slic3r { namespace GUI {

// In release builds, always use file:// protocol.
// In debug builds, enable HTTP server toolbar for hot-reload development.
#if !BBL_RELEASE_TO_PUBLIC
#define DEVICE_USE_HTTP_SERVER
#endif

DeviceWebPage::DeviceWebPage(wxWindow *parent): wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
#ifdef DEVICE_USE_HTTP_SERVER
    m_device_http_server = std::make_unique<DeviceHttpServer>();
#endif

    m_device_webview    = new PrinterWebView(this);
    m_device_web_bridge = std::make_unique<DeviceWebBridge>(m_device_webview->GetWebView());

    // Create ViewModel infrastructure
    m_device_web_mgr = std::make_unique<DeviceWebManager>();
    m_device_web_mgr->Register(std::make_unique<FilaManagerVM>());
    m_device_web_mgr->SetBridge(m_device_web_bridge.get());

    m_device_web_bridge->SetManager(m_device_web_mgr.get());

    auto web_sizer = new wxBoxSizer(wxVERTICAL);
    web_sizer->Add(m_device_webview, 1, wxEXPAND);

    LoadUrl();

    SetSizer(web_sizer);
    Layout();
    Fit();
}

DeviceWebPage::~DeviceWebPage()
{
    // Disconnect cross-references before destruction
    if (m_device_web_bridge) m_device_web_bridge->SetManager(nullptr);
    if (m_device_web_mgr)    m_device_web_mgr->SetBridge(nullptr);

    // unique_ptr members are released automatically in reverse declaration order
}

void DeviceWebPage::LoadUrl()
{
    // Get current studio language for frontend i18n
    std::string lang = wxGetApp().app_config->get("language");
    if (lang.empty()) lang = "en";

    // Load the web app
#ifdef DEVICE_USE_HTTP_SERVER
    if(!m_device_http_server->is_started()){
        m_device_http_server->start();
    }
    wxString url = wxString::Format("http://localhost:13628/index.html?lang=%s", lang);
#else
    wxString url = wxString::Format("file://%s/web/device_page/dist/index.html?lang=%s", from_u8(resources_dir()), lang);
#endif

    m_device_webview->load_url(url);
}

void DeviceWebPage::on_sys_color_changed()
{
    if (m_device_web_mgr)
        m_device_web_mgr->NotifyColorChanged();
}

void DeviceWebPage::msw_rescale()
{
    // WebView content scales via browser zoom — no native wx controls to rescale
}

void DeviceWebPage::NavigateTo(const std::string& path)
{
#if defined(__WXOSX__)
    // macOS: wxWebView::RunScript can block the UI thread for a long time on WKWebView (STUDIO-18111).
    // Use async evaluateJavaScript; other platforms keep synchronous RunScript.
    wxWebView* wv = m_device_webview ? m_device_webview->GetWebView() : nullptr;
    if (!wv)
        return;
    const wxString p      = wxString::FromUTF8(path);
    const wxString script = wxString::Format(
        "try{var t='#%s';if(window.location.hash!==t)window.location.hash=t;}catch(e){}", p);
    void* native = wv->GetNativeBackend();
    if (native)
        WKWebView_evaluateJavaScript(native, script, nullptr);
    else
        wv->RunScript(script);
#else
    if (auto* wv = m_device_webview->GetWebView()) {
        const wxString p = wxString::FromUTF8(path);
        wv->RunScript(wxString::Format(
            "try{var t='#%s';if(window.location.hash!==t)window.location.hash=t;}catch(e){}", p));
    }
#endif
}

void DeviceWebPage::NotifyFilamentSessionState()
{
    if (!m_device_web_mgr)
        return;

    m_device_web_mgr->NotifyState("filament", "sync", "state");
    m_device_web_mgr->NotifyState("filament", "spool", "list");
}

void DeviceWebPage::NotifyFilamentMachineChanged()
{
    if (!m_device_web_mgr)
        return;

    m_device_web_mgr->NotifyState("filament", "machine", "selected_changed");
}

}}
