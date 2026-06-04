#include "DeviceWebHost.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "libslic3r/Utils.hpp"

#if defined(__WXOSX__)
#include "slic3r/Utils/MacDarkMode.hpp"
#endif

#include <wx/sizer.h>

namespace Slic3r { namespace GUI {

#if !BBL_RELEASE_TO_PUBLIC
#define DEVICE_USE_HTTP_SERVER
#endif

DeviceWebHost::DeviceWebHost(wxWindow* parent, DeviceWebHostMode mode, std::string initial_path)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
    , m_mode(mode)
    , m_initial_path(std::move(initial_path))
{
#ifdef DEVICE_USE_HTTP_SERVER
    m_device_http_server = std::make_unique<DeviceHttpServer>();
#endif

    m_device_webview    = new PrinterWebView(this);
    SetMinSize(wxSize(FromDIP(320), FromDIP(260)));
    m_device_webview->SetMinSize(wxSize(FromDIP(320), FromDIP(260)));
    m_device_web_bridge = std::make_unique<DeviceWebBridge>(m_device_webview->GetWebView());

    m_device_web_mgr = std::make_unique<DeviceWebManager>();
    if (m_mode == DeviceWebHostMode::FilamentManager || m_mode == DeviceWebHostMode::AllForDebug) {
        m_device_web_mgr->Register(std::make_unique<FilamentManagerVM>());
    }
    m_device_web_mgr->SetBridge(m_device_web_bridge.get());
    m_device_web_bridge->SetManager(m_device_web_mgr.get());

    auto* web_sizer = new wxBoxSizer(wxVERTICAL);
    web_sizer->Add(m_device_webview, 1, wxEXPAND);
    SetSizer(web_sizer);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) {
        const wxSize size = GetClientSize();
        if (m_device_webview && size.GetWidth() > 0 && size.GetHeight() > 0) {
            m_device_webview->SetSize(size);
            if (auto* wv = m_device_webview->GetWebView()) {
                wv->SetSize(m_device_webview->GetClientSize());
            }
            m_device_webview->Layout();
        }
        evt.Skip();
    });

    LoadUrl();
    Layout();
    web_sizer->Layout();
    m_device_webview->Layout();
    Fit();
}

DeviceWebHost::~DeviceWebHost()
{
    if (m_device_web_bridge) m_device_web_bridge->SetManager(nullptr);
    if (m_device_web_mgr)    m_device_web_mgr->SetBridge(nullptr);
}

wxString DeviceWebHost::BuildUrl(const std::string& path) const
{
    std::string lang = wxGetApp().app_config->get("language");
    if (lang.empty()) lang = "en";

#ifdef DEVICE_USE_HTTP_SERVER
    if (!m_device_http_server->is_started()) {
        m_device_http_server->start();
    }
    wxString url = wxString::Format("http://localhost:13628/index.html?lang=%s", lang);
#else
    wxString url = wxString::Format("file://%s/web/device_page/dist/index.html?lang=%s", from_u8(resources_dir()), lang);
#endif

    if (!path.empty()) {
        url += "#" + wxString::FromUTF8(path);
    }
    return url;
}

void DeviceWebHost::LoadUrl()
{
    const wxString url = BuildUrl(m_initial_path);
    m_device_webview->load_url(url);
}

void DeviceWebHost::NavigateTo(const std::string& path)
{
    if (!m_device_webview) {
        return;
    }
    m_device_webview->load_url(BuildUrl(path));
}

void DeviceWebHost::NotifyFilamentSessionState()
{
    if (!m_device_web_mgr)
        return;

    m_device_web_mgr->NotifyState("filament", "sync", "state");
    m_device_web_mgr->NotifyState("filament", "spool", "list");
}

void DeviceWebHost::NotifyFilamentMachineChanged()
{
    if (!m_device_web_mgr)
        return;

    m_device_web_mgr->NotifyState("filament", "machine", "selected_changed");
}

void DeviceWebHost::on_sys_color_changed()
{
    if (m_device_web_mgr)
        m_device_web_mgr->NotifyColorChanged();
}

void DeviceWebHost::msw_rescale()
{
}

}} // namespace Slic3r::GUI