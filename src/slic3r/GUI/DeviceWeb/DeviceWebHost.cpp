#include "DeviceWebHost.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Widgets/WebViewTraceLogger.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "libslic3r/Utils.hpp"

#if defined(__WXOSX__)
#include "slic3r/Utils/MacDarkMode.hpp"
#endif

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/clipbrd.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <boost/log/trivial.hpp>
#include <chrono>

namespace Slic3r { namespace GUI {

#if !BBL_RELEASE_TO_PUBLIC
#define DEVICE_USE_HTTP_SERVER
#endif

static std::uint64_t TimeNowMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

static std::int64_t SteadyNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static int RecoveryLimit(WebViewWatcher::Fault fault)
{
    return fault == WebViewWatcher::Fault::RenderProcessGone ? 3 :
           fault == WebViewWatcher::Fault::BrowserProcessGone ||
           fault == WebViewWatcher::Fault::ReadyTimeout ? 1 : 0;
}

static const char *ViewNameForMode(DeviceWebHostMode mode)
{
    switch (mode) {
    case DeviceWebHostMode::FilamentManager: return "FilamentManager";
    case DeviceWebHostMode::AllForDebug: return "DeviceWebDebug";
    default: return "DeviceWeb";
    }
}

static void LogRecovery(wxWebView *webview, WebViewWatcher::Fault fault, const char *event,
                        WebViewWatcher::Action action, int attempt, int max_attempts,
                        WebViewTraceLogger::Severity severity)
{
    WebViewTraceLogger::Fields fields;
    fields.Add("action", WebViewWatcher::ActionName(action));
    if (action == WebViewWatcher::Action::None)
        fields.Add("state", "failed");
    fields.Add("attempt", wxString::Format("%d/%d", attempt, max_attempts));
    WebViewTraceLogger::Emit(
        WebViewWatcher::StageOf(fault), webview ? webview->GetName() : wxString(), event,
        fields, severity);
}

DeviceWebHost::DeviceWebHost(wxWindow* parent, DeviceWebHostMode mode,
                             std::string initial_path, bool allow_lazy)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
    , m_mode(mode)
    , m_initial_path(std::move(initial_path))
    , m_allow_lazy(allow_lazy)
{
    auto *host_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(host_sizer);
    m_fallback_panel = new wxPanel(this);
    auto *fallback_sizer = new wxBoxSizer(wxVERTICAL);
    auto *message = new wxStaticText(m_fallback_panel, wxID_ANY,
                                     _L("The embedded webpage could not be loaded."));
    auto *reload = new wxButton(m_fallback_panel, wxID_ANY, _L("Reload"));
    auto *copy = new wxButton(m_fallback_panel, wxID_ANY, _L("Copy diagnostic information"));
    fallback_sizer->AddStretchSpacer();
    fallback_sizer->Add(message, 0, wxALIGN_CENTER | wxALL, FromDIP(8));
    fallback_sizer->Add(reload, 0, wxALIGN_CENTER | wxALL, FromDIP(4));
    fallback_sizer->Add(copy, 0, wxALIGN_CENTER | wxALL, FromDIP(4));
    fallback_sizer->AddStretchSpacer();
    m_fallback_panel->SetSizer(fallback_sizer);
    host_sizer->Add(m_fallback_panel, 1, wxEXPAND);
    m_fallback_panel->Hide();
    reload->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { RetryFromFallback(); });
    copy->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(m_diagnostic_summary));
            wxTheClipboard->Close();
        }
    });
#if defined(__WXOSX__)
    m_alive_flag = std::make_shared<bool>(true);
#endif
    Bind(wxEVT_SHOW, &DeviceWebHost::OnHostShown, this);
    wxGetApp().Bind(wxEVT_ACTIVATE_APP, &DeviceWebHost::OnAppActivated, this);
#ifdef wxHAS_POWER_EVENTS
    wxGetApp().Bind(wxEVT_POWER_SUSPENDING, &DeviceWebHost::OnPowerSuspended, this);
    wxGetApp().Bind(wxEVT_POWER_SUSPENDED, &DeviceWebHost::OnPowerSuspended, this);
    wxGetApp().Bind(wxEVT_POWER_RESUME, &DeviceWebHost::OnPowerResume, this);
#endif
    Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) {
        const wxSize size = GetClientSize();
        if (m_device_webview && size.GetWidth() > 0 && size.GetHeight() > 0) {
            if (m_device_webview->GetSize() != size) {
                m_device_webview->SetSize(size);
                if (auto* wv = m_device_webview->GetWebView())
                    wv->SetSize(m_device_webview->GetClientSize());
                m_device_webview->Layout();
            }
        }
        evt.Skip();
    });
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

    m_device_webview = new PrinterWebView(
        this, ViewNameForMode(m_mode), WebViewProtectionMode::DeviceHost);
    m_device_webview->SetMinSize(wxSize(FromDIP(320), FromDIP(260)));
    if (auto *webview = m_device_webview->GetWebView())
        webview->Bind(EVT_WEBVIEW_RECOVERY, &DeviceWebHost::OnWebViewRecovery, this);
    m_device_web_bridge = std::make_unique<DeviceWebBridge>(
        m_device_webview->GetWebView(), m_device_webview->IsHealthMonitoringAvailable());
    m_device_web_bridge->SetReportEnabledHandler([this]() {
        return CanReportToWeb();
    });

    if (!m_device_web_mgr) {
        m_device_web_mgr = std::make_unique<DeviceWebManager>();
        if (!wxGetApp().is_fila_manager_disabled() &&
            (m_mode == DeviceWebHostMode::FilamentManager || m_mode == DeviceWebHostMode::AllForDebug)) {
            m_device_web_mgr->Register(std::make_unique<FilamentManagerVM>());
        }
    }
    m_device_web_mgr->SetBridge(m_device_web_bridge.get());
    m_device_web_bridge->SetManager(m_device_web_mgr.get());
    m_host_hidden = !IsShownOnScreen();
    UpdateReadyWatchdogPaused();

    auto* web_sizer = GetSizer();
    web_sizer->Add(m_device_webview, 1, wxEXPAND);
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
    Unbind(wxEVT_SHOW, &DeviceWebHost::OnHostShown, this);

    wxGetApp().Unbind(wxEVT_ACTIVATE_APP, &DeviceWebHost::OnAppActivated, this);
#ifdef wxHAS_POWER_EVENTS
    wxGetApp().Unbind(wxEVT_POWER_SUSPENDING, &DeviceWebHost::OnPowerSuspended, this);
    wxGetApp().Unbind(wxEVT_POWER_SUSPENDED, &DeviceWebHost::OnPowerSuspended, this);
    wxGetApp().Unbind(wxEVT_POWER_RESUME, &DeviceWebHost::OnPowerResume, this);
#endif
    WebView::CancelRecovery(GetWebView());
    WebView::CancelReadyWatchdog(GetWebView());
    WebView::SetReadyWatchdogPaused(GetWebView(), true);
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
    if (auto *webview = GetWebView())
        webview->Unbind(EVT_WEBVIEW_RECOVERY, &DeviceWebHost::OnWebViewRecovery, this);
    if (m_device_web_bridge) m_device_web_bridge->SetManager(nullptr);
    if (m_device_web_mgr)    m_device_web_mgr->SetBridge(nullptr);
    m_device_webview = nullptr;
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
    if (const auto fault = WebView::CreationFault(GetWebView())) {
        ShowFallback(*fault);
        return;
    }

    const wxString url = BuildUrl(m_initial_path);
    if (!WebView::ValidateLocalTarget(GetWebView(), url)) {
        ShowFallback(WebViewWatcher::Fault::ResourceMissing);
        return;
    }
    m_device_webview->load_url(url);
}

void DeviceWebHost::NavigateTo(const std::string& path, bool re_init)
{
    m_initial_path = path;
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

    if (m_suspended) {
        // macOS: resume from about:blank; loading the real URL re-runs init().
        m_suspended = false;
        UpdateReadyWatchdogPaused();
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

void DeviceWebHost::OnWebViewRecovery(wxCommandEvent &event)
{
    if (m_failed || m_suspended || wxGetApp().is_closing())
        return;

    const auto fault = static_cast<WebViewWatcher::Fault>(event.GetExtraLong());
    const int attempt = RecordRecovery(fault);
    const int max_attempts = RecoveryLimit(fault);
    if (attempt <= 0) {
        if (attempt == 0) {
            LogRecovery(GetWebView(), fault, "recovery_exhausted", WebViewWatcher::Action::None,
                        max_attempts, max_attempts, WebViewTraceLogger::Severity::Error);
            ShowFallback(fault);
        }
        return;
    }

    WebViewWatcher::Action action = static_cast<WebViewWatcher::Action>(event.GetInt());
    if (fault == WebViewWatcher::Fault::RenderProcessGone)
        action = attempt == 1 ? WebViewWatcher::Action::LoadUrl : WebViewWatcher::Action::Recreate;
    else if (fault == WebViewWatcher::Fault::BrowserProcessGone)
        action = WebViewWatcher::Action::Recreate;
    else if (fault == WebViewWatcher::Fault::ReadyTimeout)
        action = WebViewWatcher::Action::Reload;

    LogRecovery(GetWebView(), fault, "recovery_scheduled", action, attempt, max_attempts,
                WebViewTraceLogger::Severity::Warning);
    Recover(action);
}

void DeviceWebHost::OnHostShown(wxShowEvent &event)
{
    m_host_hidden = !event.IsShown();
    UpdateReadyWatchdogPaused();
    event.Skip();
}

void DeviceWebHost::OnAppActivated(wxActivateEvent &event)
{
    m_app_inactive = !event.GetActive();
    UpdateReadyWatchdogPaused();
    event.Skip();
}

void DeviceWebHost::UpdateReadyWatchdogPaused()
{
    WebView::SetReadyWatchdogPaused(
        GetWebView(), m_host_hidden || m_app_inactive || m_power_suspended || m_suspended || m_failed);
}

#ifdef wxHAS_POWER_EVENTS
void DeviceWebHost::OnPowerSuspended(wxPowerEvent &event)
{
    m_power_suspended = true;
    UpdateReadyWatchdogPaused();
    event.Skip();
}

void DeviceWebHost::OnPowerResume(wxPowerEvent &event)
{
    m_power_suspended = false;
    UpdateReadyWatchdogPaused();
    event.Skip();
}
#endif

void DeviceWebHost::Recover(WebViewWatcher::Action action)
{
    if (action == WebViewWatcher::Action::Reload) {
        if (GetWebView())
            GetWebView()->Reload();
    } else if (action == WebViewWatcher::Action::LoadUrl) {
        LoadUrl();
    } else if (action == WebViewWatcher::Action::Recreate) {
        RecreateWebView();
    }
}

int DeviceWebHost::RecordRecovery(WebViewWatcher::Fault fault)
{
    const int max_attempts = RecoveryLimit(fault);
    std::deque<std::int64_t> *recoveries =
        fault == WebViewWatcher::Fault::RenderProcessGone ? &m_renderer_recoveries :
        fault == WebViewWatcher::Fault::BrowserProcessGone ? &m_browser_recoveries :
        fault == WebViewWatcher::Fault::ReadyTimeout ? &m_ready_recoveries : nullptr;
    if (!recoveries)
        return -1;

    const std::int64_t now_ms = SteadyNowMs();
    constexpr std::int64_t recovery_window_ms = 60000;
    while (!recoveries->empty() && now_ms - recoveries->front() >= recovery_window_ms)
        recoveries->pop_front();
    if (static_cast<int>(recoveries->size()) >= max_attempts)
        return 0;
    recoveries->push_back(now_ms);
    return static_cast<int>(recoveries->size());
}

void DeviceWebHost::ShowFallback(WebViewWatcher::Fault fault)
{
    m_failed = true;
    UpdateReadyWatchdogPaused();
    WebView::CancelRecovery(GetWebView());
    WebView::CancelReadyWatchdog(GetWebView());
    m_failed_fault = fault;
    m_diagnostic_summary = BuildDiagnosticSummary(fault);
    if (m_device_webview)
        m_device_webview->Hide();
    m_fallback_panel->Show();
    Layout();
}

void DeviceWebHost::RetryFromFallback()
{
    m_failed = false;
    UpdateReadyWatchdogPaused();
    m_renderer_recoveries.clear();
    m_browser_recoveries.clear();
    m_ready_recoveries.clear();
    m_fallback_panel->Hide();
    if (m_device_webview)
        m_device_webview->Show();

    WebViewWatcher::Action action = WebViewWatcher::Action::Reload;
    if (m_failed_fault == WebViewWatcher::Fault::BrowserProcessGone ||
        m_failed_fault == WebViewWatcher::Fault::BackendUnavailable ||
        m_failed_fault == WebViewWatcher::Fault::UserDataPathUnusable)
        action = WebViewWatcher::Action::Recreate;
    else if (m_failed_fault == WebViewWatcher::Fault::RenderProcessGone ||
             m_failed_fault == WebViewWatcher::Fault::ResourceMissing)
        action = WebViewWatcher::Action::LoadUrl;
    Recover(action);
    Layout();
}

wxString DeviceWebHost::BuildDiagnosticSummary(WebViewWatcher::Fault fault) const
{
    wxString fault_name = "unknown";
    switch (fault) {
    case WebViewWatcher::Fault::BackendUnavailable: fault_name = "backend_unavailable"; break;
    case WebViewWatcher::Fault::ResourceMissing: fault_name = "resource_missing"; break;
    case WebViewWatcher::Fault::UserDataPathUnusable: fault_name = "userdata_unusable"; break;
    case WebViewWatcher::Fault::RenderProcessGone: fault_name = "renderer_process_gone"; break;
    case WebViewWatcher::Fault::BrowserProcessGone: fault_name = "browser_process_gone"; break;
    case WebViewWatcher::Fault::ReadyTimeout: fault_name = "ready_timeout"; break;
    default: break;
    }

    const wxString view = GetWebView() ? GetWebView()->GetName() : wxString("unnamed");
    const wxString url = GetWebView()
        ? WebViewTraceLogger::SanitizeUrl(GetWebView()->GetCurrentURL())
        : WebViewTraceLogger::SanitizeUrl(BuildUrl(m_initial_path));
    const WebViewTraceLogger::Stage stage = WebViewWatcher::StageOf(fault);
    const char *stage_name =
        stage == WebViewTraceLogger::Stage::L0_BACKEND ? "L0_BACKEND" :
        stage == WebViewTraceLogger::Stage::L1_RESOURCE ? "L1_RESOURCE" :
        stage == WebViewTraceLogger::Stage::L4_READY ? "L4_READY" : "L3_PROCESS";
    return wxString::Format(
        "[WebView]\nstage=%s\nview=%s\nevent=recovery_exhausted\nfault=%s\nurl=%s\nbackend=%s",
        stage_name, view, fault_name, url, wxWebView::GetBackendVersionInfo().ToString());
}

void DeviceWebHost::RecreateWebView()
{
    if (!m_device_webview || wxGetApp().is_closing())
        return;

    if (auto *webview = GetWebView()) {
        WebView::CancelRecovery(webview);
        WebView::CancelReadyWatchdog(webview);
        webview->Unbind(EVT_WEBVIEW_RECOVERY, &DeviceWebHost::OnWebViewRecovery, this);
    }
    if (m_device_web_bridge)
        m_device_web_bridge->SetManager(nullptr);
    if (m_device_web_mgr)
        m_device_web_mgr->SetBridge(nullptr);

    m_device_web_bridge.reset();
    PrinterWebView *old_webview = m_device_webview;
    m_device_webview = nullptr;
    if (GetSizer())
        GetSizer()->Detach(old_webview);
    old_webview->Destroy();

    m_built      = false;
    m_just_built = false;
    m_suspended  = false;
    EnsureBuilt();
}

void DeviceWebHost::Suspend()
{
    // Nothing running yet if the webview was never built, or already suspended.
    if (!m_device_webview || m_suspended)
        return;

    WebView::CancelRecovery(GetWebView());
    WebView::CancelReadyWatchdog(GetWebView());
    m_suspended = true;
    UpdateReadyWatchdogPaused();
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
    if (!m_device_webview || m_failed || m_suspended || wxGetApp().is_closing())
        return;

    const int attempt = RecordRecovery(WebViewWatcher::Fault::RenderProcessGone);
    if (attempt <= 0) {
        LogRecovery(GetWebView(), WebViewWatcher::Fault::RenderProcessGone, "recovery_exhausted",
                    WebViewWatcher::Action::None, 3, 3, WebViewTraceLogger::Severity::Error);
        ShowFallback(WebViewWatcher::Fault::RenderProcessGone);
        return;
    }

    BOOST_LOG_TRIVIAL(warning) << "[WebView] WKWebView content process terminated, recovery attempt "
                               << attempt << "/3";
    m_suspended  = false;
    m_just_built = false;
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

}} // namespace Slic3r::GUI
