#include "DeviceWebBridge.hpp"
#include "DeviceWebHealth.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Widgets/WebViewTraceLogger.hpp"
#include <wx/uri.h>

namespace Slic3r { namespace GUI {

DeviceWebBridge::DeviceWebBridge(wxWebView* webView, bool health_monitoring_enabled)
    : m_health_monitoring_enabled(health_monitoring_enabled)
{
    #if !BBL_RELEASE_TO_PUBLIC
    webView->EnableAccessToDevTools(true);
    webView->EnableContextMenu(true);
    #endif

    m_web = webView;
    m_web->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &DeviceWebBridge::OnWebMsg, this);
    m_web->Bind(wxEVT_WEBVIEW_NAVIGATING, &DeviceWebBridge::OnWebNav, this);
    m_web->Bind(wxEVT_WEBVIEW_LOADED, &DeviceWebBridge::OnWebLoaded, this);
}

DeviceWebBridge::~DeviceWebBridge()
{
    if (m_web) {
        m_web->Unbind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &DeviceWebBridge::OnWebMsg, this);
        m_web->Unbind(wxEVT_WEBVIEW_NAVIGATING, &DeviceWebBridge::OnWebNav, this);
        m_web->Unbind(wxEVT_WEBVIEW_LOADED, &DeviceWebBridge::OnWebLoaded, this);
    }
}

void DeviceWebBridge::OnWebLoaded(wxWebViewEvent& e)
{
    if (e.GetURL() == "about:blank") {
        e.Skip();
        return;
    }
    InitBridge();
    if (m_health_monitoring_enabled) {
        WebView::StartReadyWatchdog(m_web);
        m_ready_watchdog_started = true;
        if (m_ready_before_load) {
            WebView::NotifyReady(m_web);
            m_ready_before_load = false;
        }
    }
    e.Skip();
}

void DeviceWebBridge::InitBridge()
{
    std::string bridge = R"JS(
        if (!window.__cppPush) {
            window.__cppPush = function(pkt){
                try {
                    document.dispatchEvent(
                        new CustomEvent('cpp:' + pkt.event, { detail: pkt })
                    );
                } catch(e){ console.error(e); }
            };
        }
    )JS";

#if !BBL_RELEASE_TO_PUBLIC
    // Internal build: signal to the frontend that debug panel should be shown
    // regardless of whether init() succeeds or the user is logged in.
    bridge += R"JS(
        window.__internalBuild = true;
    )JS";
#endif

#ifndef NDEBUG
    // Debug build only, a narrower gate than __internalBuild: guards developer
    // affordances that must never reach an internal Release package either.
    bridge += R"JS(
        window.__debugBuild = true;
    )JS";
#endif

    WebView::RunScript(m_web, wxString(bridge));
}

bool DeviceWebBridge::ValidateJson(const nlohmann::json& j)
{
    if (j.is_discarded()) {
        BOOST_LOG_TRIVIAL(warning) << "json from web command parse error";
        return false;
    }
    if (!IsValidWebMessageEnvelope(j)) {
        BOOST_LOG_TRIVIAL(warning) << "json from web requires object root, head and body";
        return false;
    }
    return true;
}

void DeviceWebBridge::OnWebNav(wxWebViewEvent& e)
{
    auto url = e.GetURL();
    if (!url.StartsWith("app://")) {
        const std::string target_url = url.ToUTF8().data();
        if (!m_health_document_url.empty() && target_url != m_health_document_url &&
            DeviceWebHealth::MatchesCurrentDocument(target_url, m_health_document_url)) {
            m_health_document_url = target_url;
            e.Skip();
            return;
        }
        m_health_page_instance_id.clear();
        m_health_document_url.clear();
        m_ready_watchdog_started = false;
        m_ready_before_load = false;
        WebView::CancelReadyWatchdog(m_web);
        e.Skip();
        return;
    }

    // app://<encoded> places the payload in the authority, not wxURI::GetPath().
    DispatchRawJson(wxURI::Unescape(url.Mid(6)).ToUTF8().data(), "navigation");
    e.Veto();
}

void DeviceWebBridge::OnWebMsg(wxWebViewEvent& e)
{
    // ToStdString() uses the current C locale, which on Windows CJK builds
    // mangles non-ASCII characters (Chinese notes, emoji, etc.) before the
    // JSON parser sees them. Force UTF-8 to match the C++->JS direction
    // (SendMsg uses wxString::FromUTF8).
    DispatchRawJson(e.GetString().ToUTF8().data(), "script");
}

void DeviceWebBridge::DispatchRawJson(const std::string &raw, const char *source)
{
    try {
        const nlohmann::json j = nlohmann::json::parse(raw, nullptr, false);
        if (ValidateJson(j))
            DispatchWebCommand(j["head"], j["body"]);
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(warning) << "DeviceWebBridge: malformed " << source << " message: " << ex.what();
    }
}

bool DeviceWebBridge::ValidateHeader(const Header& head)
{
    if (head.version != DEVICE_WEB_BRIDGE_VERSION) {
        BOOST_LOG_TRIVIAL(warning) << "web command version not support";
        return false;
    }
    return true;
}

void DeviceWebBridge::AppendTrace(const std::string& direction, const Header& head, const nlohmann::json& body)
{
#if !BBL_RELEASE_TO_PUBLIC
    nlohmann::json entry;
    entry["direction"] = direction;
    entry["head"] = head;
    entry["body"] = body;
    entry["recorded_ts"] = TimeNowMs();
    m_recent_trace.push_back(std::move(entry));
    constexpr std::size_t max_trace_size = 1;
    while (m_recent_trace.size() > max_trace_size) {
        m_recent_trace.pop_front();
    }
#else
    (void) direction;
    (void) head;
    (void) body;
#endif
}

nlohmann::json DeviceWebBridge::RecentTrace() const
{
    nlohmann::json result = nlohmann::json::array();
#if !BBL_RELEASE_TO_PUBLIC
    for (const auto& item : m_recent_trace) {
        result.push_back(item);
    }
#endif
    return result;
}

void DeviceWebBridge::DispatchWebCommand(const nlohmann::json& header, const nlohmann::json& body)
{
    Header head;
    try {
        head = header.get<Header>();
    } catch (const nlohmann::json::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "DeviceWebBridge: malformed header: " << e.what();
        return;
    }
    if (head.type != MsgType::Request) return;
    if (!ValidateHeader(head)) return;

    const std::string module = WebMessageStringField(body, "module");
    const std::string submod = WebMessageStringField(body, "submod");
    const std::string action = WebMessageStringField(body, "action");
    if (module == "device_host" && submod == "health") {
        const wxString view = m_web ? m_web->GetName() : wxString();
        const auto emit = [&view](WebViewTraceLogger::Stage stage, const char *event,
                                  const WebViewTraceLogger::Fields &fields = {},
                                  WebViewTraceLogger::Severity severity = WebViewTraceLogger::Severity::Auto) {
            WebViewTraceLogger::Emit(stage, view, event, fields, severity);
        };
        const auto health = DeviceWebHealth::Parse(body);
        if (!health) {
            emit(WebViewTraceLogger::Stage::L4_READY, "invalid_health_message", {},
                 WebViewTraceLogger::Severity::Warning);
            return;
        }

        if (health->action == DeviceWebHealth::Action::Boot ||
            health->action == DeviceWebHealth::Action::Ready) {
            const std::string current_url =
                m_web ? std::string(m_web->GetCurrentURL().ToUTF8().data()) : std::string();
            if (!DeviceWebHealth::MatchesCurrentDocument(health->document_url, current_url)) {
                emit(WebViewTraceLogger::Stage::L4_READY,
                     health->action == DeviceWebHealth::Action::Boot ? "stale_boot" : "stale_ready", {},
                     WebViewTraceLogger::Severity::Warning);
                return;
            }
        }

        switch (health->action) {
        case DeviceWebHealth::Action::Boot: {
            if (m_health_page_instance_id != health->page_instance_id) {
                m_health_page_instance_id = health->page_instance_id;
                m_health_document_url = health->document_url;
                emit(WebViewTraceLogger::Stage::L4_READY, "boot");
            }
            break;
        }
        case DeviceWebHealth::Action::Ready: {
            if (health->page_instance_id != m_health_page_instance_id) {
                emit(WebViewTraceLogger::Stage::L4_READY, "stale_ready", {},
                     WebViewTraceLogger::Severity::Warning);
            } else if (!m_ready_watchdog_started) {
                m_ready_before_load = true;
            } else {
                WebView::NotifyReady(m_web);
            }
            break;
        }
        case DeviceWebHealth::Action::JsError: {
            WebViewTraceLogger::Fields fields;
            fields.Add("message", WebViewTraceLogger::SanitizeText(wxString::FromUTF8(health->message)))
                .Add("source", WebViewTraceLogger::SanitizeUrl(wxString::FromUTF8(health->source)))
                .Add("line", health->line)
                .Add("column", health->column);
            emit(WebViewTraceLogger::Stage::L5_RUNTIME, "js_error", fields,
                 WebViewTraceLogger::Severity::Error);
            break;
        }
        case DeviceWebHealth::Action::UnhandledRejection:
            emit(WebViewTraceLogger::Stage::L5_RUNTIME, "unhandled_rejection",
                WebViewTraceLogger::Fields().Add(
                    "reason", WebViewTraceLogger::SanitizeText(wxString::FromUTF8(health->reason))),
                WebViewTraceLogger::Severity::Error);
            break;
        case DeviceWebHealth::Action::ResourceError:
            emit(WebViewTraceLogger::Stage::L5_RUNTIME, "resource_error",
                WebViewTraceLogger::Fields()
                    .Add("tag", WebViewTraceLogger::SanitizeText(wxString::FromUTF8(health->tag), 32))
                    .Add("url", WebViewTraceLogger::SanitizeUrl(wxString::FromUTF8(health->url))),
                WebViewTraceLogger::Severity::Error);
            break;
        }
        return;
    }

    AppendTrace("web_request", head, body);
    if (module == "device_host" && submod == "layout" && action == "content_size") {
        if (m_host_content_size_handler && body.contains("payload") && body["payload"].is_object()) {
            const auto& payload = body["payload"];
            const int width = payload.value("width", 0);
            const int height = payload.value("height", 0);
            if (width > 0 && height > 0) {
                m_host_content_size_handler(width, height);
            }
        }
        return;
    }

#if !BBL_RELEASE_TO_PUBLIC
    if (module == "filament") {
        wxGetApp().emit_fila_debug_log(
            "bridge",
            "info",
            "Web request received by C++",
            "DeviceWebBridge accepted a filament request from the web page",
            {
                {"submod", submod},
                {"action", action},
                {"payload", body.contains("payload") ? body["payload"] : nlohmann::json::object()}
            });
    }
#endif

    // Wrap the dispatch in a catch-all so an exception inside a ViewModel
    // (e.g. FilamentSpool::from_json on malformed payload) cannot silently
    // swallow the response: the web side would otherwise wait until its 5s
    // request timeout with no clue why the call failed.
    if (m_vm_mgr) {
        try {
            auto result = m_vm_mgr->Dispatch(body);
            if (result.has_value()) {
                ResponseMsg(head, result.value());
                return;
            }
        } catch (const std::exception& e) {
            const std::string mod = module.empty() ? "(unknown)" : module;
            const std::string sub = submod.empty() ? "(unknown)" : submod;
            const std::string act = action.empty() ? "(unknown)" : action;
            BOOST_LOG_TRIVIAL(error) << "DeviceWebBridge: exception while dispatching "
                                     << mod << "/" << sub << "/" << act
                                     << ": " << e.what();
            if (mod == "filament") {
                wxGetApp().emit_fila_debug_log(
                    "bridge", "error",
                    "C++ dispatch threw",
                    "A ViewModel threw while handling a web request; an error response was returned so the UI does not stall",
                    {
                        {"submod", sub}, {"action", act},
                        {"what", e.what()},
                        {"payload", body.contains("payload") ? body["payload"] : nlohmann::json::object()}
                    });
            }
            nlohmann::json err_resp = {
                {"module", mod}, {"submod", sub}, {"action", act},
                {"error_code", 2},
                {"message", std::string("C++ exception: ") + e.what()}
            };
            ResponseMsg(head, err_resp);
            return;
        }
    }

    nlohmann::json err_resp = {
        {"error_code", 1},
        {"message", "unknown module or submod"}
    };
    ResponseMsg(head, err_resp);
}

}} // namespace Slic3r::GUI
