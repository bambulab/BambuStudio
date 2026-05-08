#ifndef DEVICEWEBPAGE_H
#define DEVICEWEBPAGE_H


#include <memory>

#include <wx/panel.h>

#include "DeviceHttpServer.hpp"
#include "DeviceWebBridge.hpp"
#include "DeviceWebManager.hpp"
#include "FilaManagerVM.hpp"
#include "slic3r/GUI/PrinterWebView.hpp"

namespace Slic3r {
namespace GUI {


class DeviceWebPage: public wxPanel {
public:
    DeviceWebPage(wxWindow *parent);
    ~DeviceWebPage();

    void LoadUrl();

    void NavigateTo(const std::string& path);

    // Re-broadcast auth-sensitive filament state to the current web page.
    void NotifyFilamentSessionState();

    // F4.7: broadcast the Studio-wide selected-machine change to the web
    // page so the "Read from AMS" dialog can mirror Studio's current
    // printer (driven by DeviceManager::OnSelectedMachineChanged).
    void NotifyFilamentMachineChanged();

    void on_sys_color_changed();

    void msw_rescale();

    wxWebView *GetWebView() const {
        if (m_device_webview) return m_device_webview->GetWebView();
        return nullptr;
    }

private:
    PrinterWebView*                    m_device_webview{nullptr};   // owned by wx parent
    std::unique_ptr<DeviceHttpServer>  m_device_http_server;
    std::unique_ptr<DeviceWebBridge>   m_device_web_bridge;
    std::unique_ptr<DeviceWebManager>  m_device_web_mgr;

};

} // GUI
} // Slic3r


#endif // DEVICEWEBPAGE_H