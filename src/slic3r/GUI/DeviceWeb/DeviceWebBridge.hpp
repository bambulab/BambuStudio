#ifndef DEVICEWEBBRIDGE_HPP
#define DEVICEWEBBRIDGE_HPP

#include "wx/webview.h"

#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif

#include <wx/panel.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <optional>

#include "slic3r/GUI/Widgets/WebView.hpp"
#include "DeviceWebModel.hpp"
#include "DeviceWebManager.hpp"

namespace Slic3r { namespace GUI {

static constexpr const char* DEVICE_WEB_BRIDGE_VERSION = "1.0";

class DeviceWebBridge
{
private:
    std::atomic<std::uint64_t> m_seq{0};
    wxWebView*         m_web{nullptr};
    DeviceWebManager*  m_vm_mgr{nullptr};

    static inline std::uint64_t TimeNowMs() {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    template<typename T>
    void SendMsg(const Header& head, T&& params)
    {
        nlohmann::json j;
        j["event"] = "device";
        j["head"]  = head;
        j["body"]  = std::forward<T>(params);
        std::string script = "window.__cppPush(" + j.dump() + ");";
        WebView::RunScript(m_web, wxString::FromUTF8(script));
    }

    void InitBridge();
    void OnWebLoaded(wxWebViewEvent& e);
    void OnWebNav(wxWebViewEvent& e);
    void OnWebMsg(wxWebViewEvent& e);
    bool ValidateJson(const nlohmann::json& j);
    bool ValidateHeader(const Header& head);
    void DispatchWebCommand(const nlohmann::json& header, const nlohmann::json& body);

public:
    DeviceWebBridge(wxWebView* webView);
    ~DeviceWebBridge();

    void SetManager(DeviceWebManager* mgr) { m_vm_mgr = mgr; }

    /* C++ → Web: report state change */
    template<typename T>
    void ReportMsg(T&& params) {
        Header head{DEVICE_WEB_BRIDGE_VERSION, MsgType::Report, m_seq++, TimeNowMs()};
        SendMsg(head, std::forward<T>(params));
    }

    /* C++ → Web: respond to a frontend request (ack) */
    template<typename T>
    void ResponseMsg(Header head, T&& params) {
        head.type = MsgType::Response;
        head.ts   = TimeNowMs();
        SendMsg(head, std::forward<T>(params));
    }
};

}} // namespace Slic3r::GUI

#endif // DEVICEWEBBRIDGE_HPP
