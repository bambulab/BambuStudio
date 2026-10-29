#ifndef DEVICEWEBBRIDGE_HPP
#define DEVICEWEBBRIDGE_HPP

#include "wx/webview.h"

#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif

#include <wx/panel.h>
#include <nlohmann/json.hpp>
#include <boost/log/trivial.hpp>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <utility>

#include "slic3r/GUI/Widgets/WebView.hpp"
#include "DeviceWebHealth.hpp"
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
    std::deque<nlohmann::json> m_recent_trace;
    std::function<void(int width, int height)> m_host_content_size_handler;
    std::function<bool()> m_report_enabled_handler;
    std::string m_health_page_instance_id;
    std::string m_health_document_url;
    bool m_health_monitoring_enabled{false};
    bool m_ready_watchdog_started{false};
    bool m_ready_before_load{false};

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
        // Device-sourced strings (filament/AMS names from firmware) may carry
        // non-UTF-8 bytes; strict dump() throws type_error.316. Guard the
        // serialization so a single bad byte cannot break the whole bridge.
        try {
            std::string script = "window.__cppPush(" + j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + ");";
            WebView::RunScript(m_web, wxString::FromUTF8(script));
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "DeviceWebBridge::SendMsg serialize failed: " << e.what();
        }
    }

    // Shared body for ReportMsg / ReportMsgForce. Does NOT consult the
    // visibility gate; callers decide whether the gate applies.
    template<typename T>
    void ReportMsgImpl(T&& params) {
        Header head{DEVICE_WEB_BRIDGE_VERSION, MsgType::Report, m_seq++, TimeNowMs()};
        nlohmann::json trace_body = params;
        AppendTrace("cpp_report", head, trace_body);
        SendMsg(head, std::forward<T>(params));
    }

    void InitBridge();
    void OnWebLoaded(wxWebViewEvent& e);
    void OnWebNav(wxWebViewEvent& e);
    void OnWebMsg(wxWebViewEvent& e);
    void DispatchRawJson(const std::string &raw, const char *source);
    bool ValidateJson(const nlohmann::json& j);
    bool ValidateHeader(const Header& head);
    void DispatchWebCommand(const nlohmann::json& header, const nlohmann::json& body);
    void AppendTrace(const std::string& direction, const Header& head, const nlohmann::json& body);

public:
    DeviceWebBridge(wxWebView* webView, bool health_monitoring_enabled);
    ~DeviceWebBridge();

    void SetManager(DeviceWebManager* mgr) { m_vm_mgr = mgr; }
    void SetHostContentSizeHandler(std::function<void(int width, int height)> handler) { m_host_content_size_handler = std::move(handler); }
    void SetReportEnabledHandler(std::function<bool()> handler) { m_report_enabled_handler = std::move(handler); }

    /* C++ → Web: report state change (suppressed when the page is hidden).
     * Returns true if the visibility gate passed and RunScript was invoked.
     * Does not guarantee the JS listener is already registered. */
    template<typename T>
    bool ReportMsg(T&& params) {
        if (m_report_enabled_handler && !m_report_enabled_handler()) return false;
        ReportMsgImpl(std::forward<T>(params));
        return true;
    }

    /* C++ → Web: report bypassing the visibility gate.
     * Only for on-demand, about-to-show popups that must pre-render their
     * content into a still-hidden (but built and alive) WebView so the window
     * shows the fresh state on its first frame. The periodic/background report
     * paths must keep using ReportMsg so the hidden-page suppression stays.
     * Always returns true after invoking RunScript. */
    template<typename T>
    bool ReportMsgForce(T&& params) {
        ReportMsgImpl(std::forward<T>(params));
        return true;
    }

    /* C++ → Web: respond to a frontend request (ack) */
    template<typename T>
    void ResponseMsg(Header head, T&& params) {
        head.type = MsgType::Response;
        head.ts   = TimeNowMs();
        nlohmann::json trace_body = params;
        AppendTrace("cpp_response", head, trace_body);
        SendMsg(head, std::forward<T>(params));
    }

    nlohmann::json RecentTrace() const;
};

}} // namespace Slic3r::GUI

#endif // DEVICEWEBBRIDGE_HPP
