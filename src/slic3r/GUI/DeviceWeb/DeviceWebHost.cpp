#include "DeviceWebHost.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "libslic3r/Utils.hpp"

#if defined(__WXOSX__)
#include "slic3r/Utils/MacDarkMode.hpp"
#endif

#include <wx/sizer.h>
#include <boost/log/trivial.hpp>
#include <chrono>
#include <stdexcept>

namespace Slic3r { namespace GUI {

#if !BBL_RELEASE_TO_PUBLIC
#define DEVICE_USE_HTTP_SERVER
#endif

static std::uint64_t TimeNowMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

DeviceWebHost::DeviceWebHost(wxWindow* parent, DeviceWebHostMode mode,
                             std::string initial_path, bool allow_lazy)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
    , m_mode(mode)
    , m_initial_path(std::move(initial_path))
    , m_allow_lazy(allow_lazy)
{
#if defined(__WXOSX__)
    m_alive_flag = std::make_shared<bool>(true);
#endif
    if (!m_allow_lazy) {
        EnsureBuilt();
    }
    // Ensure the panel has a valid minimum size even before the webview is built,
    // so the tab placeholder renders correctly.
    SetMinSize(wxSize(FromDIP(320), FromDIP(260)));
}

void DeviceWebHost::EnsureBuilt()
{
    if (m_built) return;
    m_built = true;

    auto t0 = TimeNowMs();

#ifdef DEVICE_USE_HTTP_SERVER
    m_device_http_server = std::make_unique<DeviceHttpServer>();
#endif

    m_device_webview = new PrinterWebView(this);
    m_device_webview->SetMinSize(wxSize(FromDIP(320), FromDIP(260)));

    // Restore saved zoom once the page has finished loading.
    if (wxWebView* wv = GetWebView())
        wv->Bind(wxEVT_WEBVIEW_LOADED, &DeviceWebHost::OnWebLoaded, this);

    m_device_web_bridge = std::make_unique<DeviceWebBridge>(m_device_webview->GetWebView());
    m_device_web_bridge->SetReportEnabledHandler([this]() {
        return CanReportToWeb();
    });

    m_device_web_mgr = std::make_unique<DeviceWebManager>();
    if (!wxGetApp().is_fila_manager_disabled() &&
        (m_mode == DeviceWebHostMode::FilamentManager || m_mode == DeviceWebHostMode::AllForDebug)) {
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

    // When built lazily the panel already has its final size; force the webview
    // to fill it immediately rather than waiting for the next SIZE event.
    if (m_allow_lazy) {
        const wxSize cur = GetClientSize();
        if (cur.GetWidth() > 0 && cur.GetHeight() > 0) {
            m_device_webview->SetSize(cur);
            if (auto* wv = m_device_webview->GetWebView()) {
                wv->SetSize(m_device_webview->GetClientSize());
            }
        }
        Layout();
        web_sizer->Layout();
        m_device_webview->Layout();
        if (GetParent()) GetParent()->Layout();
    } else {
        Layout();
        web_sizer->Layout();
        m_device_webview->Layout();
        Fit();
    }

    BOOST_LOG_TRIVIAL(info) << "[DevWebPerf] EnsureBuilt: done in " << (TimeNowMs() - t0) << "ms";
    // Mark so NavigateTo skips the next call: LoadUrl already loaded the correct URL,
    // and running JS hash navigation before the page is ready causes a white screen.
    m_just_built = true;

#if defined(__WXOSX__)
    if (auto* wv = m_device_webview->GetWebView()) {
        if (void* native = wv->GetNativeBackend())
            Slic3r::GUI::WKWebView_setCrashHandler(native, OnWKContentProcessCrash, this);
    }
#endif
}

DeviceWebHost::~DeviceWebHost()
{
#if defined(__WXOSX__)
    // 先置 false，使已入队的 CallAfter lambda 安全空转，再注销 ObjC 代理
    if (m_alive_flag) *m_alive_flag = false;
    if (m_device_webview) {
        if (auto* wv = m_device_webview->GetWebView()) {
            if (void* native = wv->GetNativeBackend())
                Slic3r::GUI::WKWebView_setCrashHandler(native, nullptr, nullptr);
        }
    }
#endif
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
    if (!m_device_webview) return;
    m_device_webview->load_url(BuildUrl(m_initial_path));
}

void DeviceWebHost::NavigateTo(const std::string& path, bool re_init)
{
    if (!m_device_webview) {
        // Lazy first build, already loaded by EnsureBuilt(). Clear the guard so
        // a later return to this tab can reload and re-run init().
        EnsureBuilt();
        m_just_built = false;
        return;
    }

    // Non-lazy hosts loaded in the constructor: skip the first NavigateTo to
    // avoid a redundant reload / white screen (unless suspended to about:blank).
    if (m_just_built && !m_suspended) {
        m_just_built = false;
        return;
    }

#if defined(__WXOSX__)
    // 走到这里说明即将真正加载一个新页面（非构造后的冗余跳过、非 lazy 首建），
    // 意味着上一次（若发生过崩溃恢复）页面是活着的——把崩溃恢复计数清零，
    // 让下一次偶发崩溃重新获得完整的重试预算。
    m_crash_recovery_count = 0;
#endif

    if (m_suspended) {
        // macOS: resume from about:blank; loading the real URL re-runs init().
        m_suspended = false;
        m_device_webview->load_url(BuildUrl(path));
        return;
    }

    if (re_init) {
        // Same URL is a no-op in WebView2; force a real reload to re-run init().
        if (auto* wv = m_device_webview->GetWebView()) {
            wv->Reload();
            return;
        }
    }

    m_device_webview->load_url(BuildUrl(path));
}

void DeviceWebHost::Suspend()
{
    // Nothing running yet if the webview was never built, or already suspended.
    if (!m_device_webview || m_suspended)
        return;

    m_suspended = true;
    // Replace the live React SPA with an empty document so the WKWebView stops
    // animating/compositing/running timers and lets the macOS run loop go idle.
    m_device_webview->load_url("about:blank");
}

#if defined(__WXOSX__)
void DeviceWebHost::OnWKContentProcessCrash(void* context)
{
    auto* host = static_cast<DeviceWebHost*>(context);
    std::weak_ptr<bool> weak_flag = host->m_alive_flag;
    wxTheApp->CallAfter([host, weak_flag]() {
        auto flag = weak_flag.lock();
        if (flag && *flag)
            host->RecoverFromCrash();
    });
}

void DeviceWebHost::RecoverFromCrash()
{
    if (!m_device_webview) return;

    // 重试上限：连续崩溃超过 kMaxCrashRecoveryAttempts 次后停止自动恢复，
    // 避免在页面持续崩溃（多为环境/页面自身问题）时无限重载。
    // 失败降级（错误占位页等）暂未实现，到顶后仅停止重试、保留当前（白屏）状态。
    if (m_crash_recovery_count >= kMaxCrashRecoveryAttempts) {
        BOOST_LOG_TRIVIAL(error) << "[DeviceWebHost] WKWebView 内容进程崩溃恢复已达上限（"
                                 << kMaxCrashRecoveryAttempts << " 次），停止自动重试";
        return;
    }
    ++m_crash_recovery_count;

    BOOST_LOG_TRIVIAL(warning) << "[DeviceWebHost] WKWebView 内容进程已终止，正在重载页面（第 "
                               << m_crash_recovery_count << "/" << kMaxCrashRecoveryAttempts << " 次尝试）";
    // 重置所有可能阻止 LoadUrl() 的状态标记
    m_suspended  = false;
    m_just_built = false;
    // LoadUrl() 始终调用 LoadURL(url) → WKWebView loadRequest:，
    // 这会重启已终止的 WK2 进程并重新导航。
    // 不能用 wv->Reload()：在已崩溃的 WKWebView 上是空操作。
    LoadUrl();
}
#endif

bool DeviceWebHost::CanReportToWeb() const
{
    return m_built && m_device_webview && IsShownOnScreen();
}

bool DeviceWebHost::CanBuildDeviceState() const
{
    return CanReportToWeb();
}

void DeviceWebHost::NotifyFilamentSessionState()
{
    if (!m_device_web_mgr || !CanReportToWeb())
        return;

    m_device_web_mgr->NotifyState("filament", "sync", "state");
    m_device_web_mgr->NotifyState("filament", "spool", "list");
}

void DeviceWebHost::DispatchCommand(const nlohmann::json& body)
{
    EnsureBuilt();
    if (m_device_web_mgr)
        m_device_web_mgr->Dispatch(body);
}

void DeviceWebHost::NotifyFilamentMachineChanged()
{
    if (!m_device_web_mgr || !CanReportToWeb())
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

void DeviceWebHost::OnWebLoaded(wxWebViewEvent& evt)
{
    evt.Skip();

    auto* config = wxGetApp().app_config;
    if (!config || !config->has("filament_manager_zoom_factor"))
        return;

    wxWebView* wv = GetWebView();
    if (!wv)
        return;

    try {
        float zoom = std::stof(config->get("filament_manager_zoom_factor"));
        if (zoom >= 0.25f && zoom <= 5.0f)
            wv->SetZoomFactor(zoom);
    } catch (const std::exception&) {
        // Ignore malformed config values; browser default (1.0) will be used.
    }
}

void DeviceWebHost::SaveZoom()
{
    wxWebView* wv = GetWebView();
    if (!wv)
        return;

    float zoom = wv->GetZoomFactor();
    if (zoom <= 0.0f)
        return;

    auto* config = wxGetApp().app_config;
    if (!config)
        return;

    config->set("filament_manager_zoom_factor", std::to_string(zoom));
    config->save();
}

}} // namespace Slic3r::GUI
