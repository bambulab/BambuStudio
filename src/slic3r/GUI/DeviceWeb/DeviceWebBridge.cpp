#include "DeviceWebBridge.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include <wx/uri.h>

namespace Slic3r { namespace GUI {

DeviceWebBridge::DeviceWebBridge(wxWebView* webView)
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

void DeviceWebBridge::OnWebLoaded(wxWebViewEvent& /*e*/)
{
    InitBridge();
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

    WebView::RunScript(m_web, wxString(bridge));
}

bool DeviceWebBridge::ValidateJson(const nlohmann::json& j)
{
    if (j.is_discarded()) {
        BOOST_LOG_TRIVIAL(warning) << "json from web command parse error";
        return false;
    }
    if (!j.contains("head")) {
        BOOST_LOG_TRIVIAL(warning) << "json from web missing head field";
        return false;
    }
    if (!j.contains("body")) {
        BOOST_LOG_TRIVIAL(warning) << "json from web missing body field";
        return false;
    }
    return true;
}

void DeviceWebBridge::OnWebNav(wxWebViewEvent& e)
{
    auto url = e.GetURL();
    if (!url.StartsWith("app://")) return;

    wxURI       uri(url);
    std::string raw = wxURI::Unescape(uri.GetPath()).ToStdString();

    nlohmann::json j = nlohmann::json::parse(raw, nullptr, false);
    if (ValidateJson(j)) {
        DispatchWebCommand(j["head"], j["body"]);
    }

    e.Veto();
}

void DeviceWebBridge::OnWebMsg(wxWebViewEvent& e)
{
    const std::string raw = e.GetString().ToStdString();
    nlohmann::json    j   = nlohmann::json::parse(raw, nullptr, false);
    if (ValidateJson(j)) {
        DispatchWebCommand(j["head"], j["body"]);
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

#if !BBL_RELEASE_TO_PUBLIC
    if (body.value("module", std::string()) == "filament") {
        wxGetApp().emit_fila_debug_log(
            "bridge",
            "info",
            "Web request received by C++",
            "DeviceWebBridge accepted a filament request from the web page",
            {
                {"submod", body.value("submod", std::string())},
                {"action", body.value("action", std::string())},
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
            const std::string mod    = body.value("module",  std::string("(unknown)"));
            const std::string submod = body.value("submod",  std::string("(unknown)"));
            const std::string action = body.value("action",  std::string("(unknown)"));
            BOOST_LOG_TRIVIAL(error) << "DeviceWebBridge: exception while dispatching "
                                     << mod << "/" << submod << "/" << action
                                     << ": " << e.what();
            if (mod == "filament") {
                wxGetApp().emit_fila_debug_log(
                    "bridge", "error",
                    "C++ dispatch threw",
                    "A ViewModel threw while handling a web request; an error response was returned so the UI does not stall",
                    {
                        {"submod", submod}, {"action", action},
                        {"what", e.what()},
                        {"payload", body.contains("payload") ? body["payload"] : nlohmann::json::object()}
                    });
            }
            nlohmann::json err_resp = {
                {"module", mod}, {"submod", submod}, {"action", action},
                {"error_code", 2},
                {"message", std::string("C++ exception: ") + e.what()}
            };
            ResponseMsg(head, err_resp);
            return;
        }
    }

#if !BBL_RELEASE_TO_PUBLIC
    std::string mod      = body.value("module",   "(unknown)");
    std::string submod = body.value("func", "(unknown)");
    BOOST_LOG_TRIVIAL(warning) << "DeviceWebBridge: no handler for module='" << mod << "' submod='" << submod << "'";
#endif
    nlohmann::json err_resp = {
        {"error_code", 1},
        {"message", "unknown module or submod"}
    };
    ResponseMsg(head, err_resp);
}

}} // namespace Slic3r::GUI
