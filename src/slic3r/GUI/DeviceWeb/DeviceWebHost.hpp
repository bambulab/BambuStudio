#ifndef DEVICEWEBHOST_H
#define DEVICEWEBHOST_H

#include <memory>
#include <string>

#include <wx/panel.h>

#include "DeviceHttpServer.hpp"
#include "DeviceWebBridge.hpp"
#include "DeviceWebManager.hpp"
#include "ViewModels/FilamentManager/FilamentManagerVM.hpp"
#include "slic3r/GUI/PrinterWebView.hpp"

namespace Slic3r {
namespace GUI {

enum class DeviceWebHostMode {
    FilamentManager,
    AllForDebug
};

class DeviceWebHost : public wxPanel {
public:
    // allow_lazy: if true, webview/bridge/manager construction is deferred until
    // the panel is first shown, avoiding startup cost for hidden tabs.
    explicit DeviceWebHost(wxWindow* parent, DeviceWebHostMode mode,
                           std::string initial_path = {}, bool allow_lazy = false);
    ~DeviceWebHost() override;

    void LoadUrl();
    void NavigateTo(const std::string& path);

    // Suspend tears down the live web document (loads about:blank) so the WKWebView
    // stops driving the run loop while this tab is hidden. On macOS a live React SPA
    // left mounted in a hidden WKWebView keeps the CFRunLoop busy and starves
    // wxEVT_IDLE app-wide, freezing the OpenGL canvas and tab switching on other
    // pages. NavigateTo() (called when the tab is shown again) reloads the page.
    void Suspend();

    void NotifyFilamentSessionState();
    void NotifyFilamentMachineChanged();

    void on_sys_color_changed();
    void msw_rescale();

    wxWebView* GetWebView() const {
        if (m_device_webview) return m_device_webview->GetWebView();
        return nullptr;
    }

private:
    wxString BuildUrl(const std::string& path) const;
    // Deferred construction: build webview + bridge + manager + LoadUrl on first use.
    void EnsureBuilt();
    bool CanReportToWeb() const;
    bool CanBuildDeviceState() const;

private:
    DeviceWebHostMode                 m_mode{ DeviceWebHostMode::AllForDebug };
    std::string                       m_initial_path;
    bool                              m_allow_lazy{false};   // deferred construction enabled
    bool                              m_built{false};        // has EnsureBuilt() run?
    bool                              m_just_built{false};   // skip next NavigateTo after lazy init
    bool                              m_suspended{false};    // web document torn down (about:blank) while hidden
    PrinterWebView*                   m_device_webview{ nullptr }; // owned by wx parent
    std::unique_ptr<DeviceHttpServer> m_device_http_server;
    std::unique_ptr<DeviceWebBridge>  m_device_web_bridge;
    std::unique_ptr<DeviceWebManager> m_device_web_mgr;
};

} // namespace GUI
} // namespace Slic3r

#endif // DEVICEWEBHOST_H
