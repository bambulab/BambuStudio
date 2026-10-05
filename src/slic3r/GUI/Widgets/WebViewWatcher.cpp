#include "WebViewWatcher.hpp"

#include <wx/webview.h>

#include <chrono>

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_WEBVIEW_RECOVERY, wxCommandEvent);

namespace {

constexpr int kProcessRecoveryDelayMs = 1000;

constexpr int64_t kFaultLogWindowMs = 10000;
constexpr int64_t kDefaultReadyTimeoutMs = 10000;
constexpr int64_t kReadyTimerOvershootGraceMs = 5000;
constexpr int kHiddenReadyPollMs = 1000;

int64_t steady_now_ms()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

} // namespace

WebViewWatcher::WebViewWatcher(wxWebView *webView, WebViewProtectionMode mode) : m_webView(webView), m_mode(mode) {}

WebViewWatcher::~WebViewWatcher()
{
    CancelRecovery();
    CancelReadyWatchdog();
}

WebViewTraceLogger::Stage WebViewWatcher::StageOf(Fault fault)
{
    switch (fault) {
    case Fault::BackendUnavailable: return WebViewTraceLogger::Stage::L0_BACKEND;
    case Fault::ResourceMissing:
    case Fault::UserDataPathUnusable: return WebViewTraceLogger::Stage::L1_RESOURCE;
    case Fault::NavConnection:
    case Fault::NavOther: return WebViewTraceLogger::Stage::L2_NAVIGATION;
    case Fault::RenderProcessGone:
    case Fault::FrameRenderProcessGone:
    case Fault::BrowserProcessGone:
    case Fault::ProcessUnresponsive: return WebViewTraceLogger::Stage::L3_PROCESS;
    case Fault::ReadyTimeout: return WebViewTraceLogger::Stage::L4_READY;
    case Fault::RuntimeScriptError: return WebViewTraceLogger::Stage::L5_RUNTIME;
    default: return WebViewTraceLogger::Stage::L2_NAVIGATION;
    }
}

const char *WebViewWatcher::ActionName(Action action)
{
    switch (action) {
    case Action::None: return "none";
    case Action::Reload: return "reload";
    case Action::LoadUrl: return "load_url";
    case Action::Recreate: return "recreate";
    case Action::ErrorPage: return "error_page";
    case Action::InstallGuide: return "install_guide";
    default: return "unknown";
    }
}

WebViewWatcher::Decision WebViewWatcher::Decide(Fault fault)
{
    if (m_mode == WebViewProtectionMode::DiagnosticsOnly)
        return {fault};

    switch (fault) {
    case Fault::BackendUnavailable: return {fault, Action::InstallGuide};
    case Fault::ResourceMissing:
    case Fault::UserDataPathUnusable:
    case Fault::NavConnection: return {fault, Action::ErrorPage};
    case Fault::FrameRenderProcessGone:
    case Fault::ProcessUnresponsive:
    case Fault::RuntimeScriptError: return {fault};
    case Fault::NavOther: return {fault, Action::Reload, kProcessRecoveryDelayMs};
    case Fault::RenderProcessGone: return {fault, Action::LoadUrl, kProcessRecoveryDelayMs};
    case Fault::BrowserProcessGone: return {fault, Action::Recreate, kProcessRecoveryDelayMs};
    case Fault::ReadyTimeout: return {fault, Action::Reload};
    default: return {fault};
    }
}

WebViewWatcher::FaultLogDecision WebViewWatcher::DecideFaultLog(Fault fault, int discriminator)
{
    if (StageOf(fault) != WebViewTraceLogger::Stage::L3_PROCESS)
        return {};

    // Throttling a fault we react to would cost the only record of it: once the retry
    // budget is used up there is no recovery line either, so the user would be left
    // with a blank page and nothing in the log behind it. Only the faults nobody acts
    // on are noisy enough to need a window: UNRESPONSIVE re-fires for as long as the
    // renderer stays stuck, and GPU/utility exits are restarted by WebView2 itself.
    if (fault == Fault::RenderProcessGone || fault == Fault::FrameRenderProcessGone ||
        fault == Fault::BrowserProcessGone)
        return {};

    const int64_t now_ms = steady_now_ms();
    FaultLogWindow &window = m_l3_log_windows[discriminator];
    if (!window.active) {
        window.active   = true;
        window.start_ms = now_ms;
        return {};
    }

    if (now_ms - window.start_ms < kFaultLogWindowMs) {
        ++window.suppressed;
        return {false, 0};
    }

    FaultLogDecision result{true, window.suppressed};
    window.start_ms   = now_ms;
    window.suppressed = 0;
    return result;
}

void WebViewWatcher::RecordNavigationStart()
{
    m_navigation_start_ms = steady_now_ms();
}

std::optional<int64_t> WebViewWatcher::ConsumeNavigationDurationMs()
{
    if (!m_navigation_start_ms)
        return std::nullopt;
    const int64_t start_ms = *m_navigation_start_ms;
    m_navigation_start_ms.reset();
    const int64_t now_ms = steady_now_ms();
    return now_ms >= start_ms ? now_ms - start_ms : 0;
}

void WebViewWatcher::StartReadyWatchdog()
{
    if (m_ready_timer)
        m_ready_timer->Stop();
    m_ready_start_ms = steady_now_ms();
    ArmReadyTimer();
}

void WebViewWatcher::CancelReadyWatchdog()
{
    if (m_ready_timer)
        m_ready_timer->Stop();
    m_ready_start_ms.reset();
}

std::optional<int64_t> WebViewWatcher::MarkReady()
{
    if (!m_ready_start_ms)
        return std::nullopt;

    if (m_ready_timer)
        m_ready_timer->Stop();
    const int64_t start_ms = *m_ready_start_ms;
    m_ready_start_ms.reset();
    const int64_t now_ms = steady_now_ms();
    return now_ms >= start_ms ? now_ms - start_ms : 0;
}

std::optional<int64_t> WebViewWatcher::ExpireReadyWatchdog()
{
    if (m_ready_paused || !m_ready_start_ms)
        return std::nullopt;

    const int64_t now_ms = steady_now_ms();
    const int64_t duration_ms = now_ms >= *m_ready_start_ms ? now_ms - *m_ready_start_ms : 0;
    if (duration_ms < kDefaultReadyTimeoutMs)
        return std::nullopt;

    if (m_ready_timer)
        m_ready_timer->Stop();
    m_ready_start_ms.reset();
    return duration_ms;
}

void WebViewWatcher::SetReadyWatchdogPaused(bool paused)
{
    if (m_ready_paused == paused)
        return;

    m_ready_paused = paused;
    if (paused) {
        if (m_ready_timer)
            m_ready_timer->Stop();
    } else if (m_ready_start_ms) {
        m_ready_start_ms = steady_now_ms();
        ArmReadyTimer();
    }
}

void WebViewWatcher::ArmReadyTimer()
{
    if (!m_webView || !m_ready_start_ms || m_ready_paused)
        return;
    if (!m_ready_timer)
        m_ready_timer = std::make_unique<Timer>(*this, &WebViewWatcher::OnReadyTimer);
    m_ready_timer->Start(static_cast<int>(kDefaultReadyTimeoutMs), wxTIMER_ONE_SHOT);
}

void WebViewWatcher::OnReadyTimer()
{
    if (!m_ready_start_ms || m_ready_paused)
        return;

    const int64_t now_ms = steady_now_ms();
    const int64_t elapsed_ms = now_ms >= *m_ready_start_ms ? now_ms - *m_ready_start_ms : 0;
    if (!m_webView->IsShownOnScreen()) {
        m_ready_start_ms = now_ms;
        m_ready_timer->Start(kHiddenReadyPollMs, wxTIMER_ONE_SHOT);
        return;
    }
    if (elapsed_ms > kDefaultReadyTimeoutMs + kReadyTimerOvershootGraceMs) {
        m_ready_start_ms = now_ms;
        ArmReadyTimer();
        return;
    }

    const auto duration_ms = ExpireReadyWatchdog();
    if (!duration_ms) {
        m_ready_start_ms = steady_now_ms();
        ArmReadyTimer();
        return;
    }

    WebViewTraceLogger::Emit(
        WebViewTraceLogger::Stage::L4_READY, m_webView ? m_webView->GetName() : wxString(), "ready_timeout",
        WebViewTraceLogger::Fields().Add("duration_ms", static_cast<long long>(*duration_ms)),
        WebViewTraceLogger::Severity::Error);

    const Decision decision = Decide(Fault::ReadyTimeout);
    if (decision.action != Action::None)
        ScheduleRecovery(decision);
}

void WebViewWatcher::ScheduleRecovery(const Decision &decision)
{
    if (decision.action == Action::None || m_webView == nullptr)
        return;

    if (!m_timer)
        m_timer = std::make_unique<Timer>(*this, &WebViewWatcher::OnRecoveryTimer);

    m_pending       = decision.action;
    m_pending_fault = decision.fault;
    if (decision.delay_ms <= 0) {
        OnRecoveryTimer();
        return;
    }
    m_timer->Start(decision.delay_ms, wxTIMER_ONE_SHOT);
}

void WebViewWatcher::CancelRecovery()
{
    if (m_timer)
        m_timer->Stop();
    m_pending = Action::None;
}

void WebViewWatcher::OnRecoveryTimer()
{
    if (m_timer)
        m_timer->Stop();

    const Action action = m_pending;
    const Fault fault   = m_pending_fault;
    m_pending           = Action::None;
    if (m_webView == nullptr)
        return;

    wxCommandEvent event(EVT_WEBVIEW_RECOVERY, m_webView->GetId());
    event.SetEventObject(m_webView);
    event.SetInt(static_cast<int>(action));
    event.SetExtraLong(static_cast<long>(fault));
    wxPostEvent(m_webView, event);
}

}} // namespace Slic3r::GUI
