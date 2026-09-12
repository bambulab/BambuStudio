#ifndef slic3r_GUI_WebViewWatcher_hpp_
#define slic3r_GUI_WebViewWatcher_hpp_

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

#include <wx/string.h>
#include <wx/timer.h>

#include "WebViewTraceLogger.hpp"

class wxWebView;

namespace Slic3r { namespace GUI {

enum class WebViewProtectionMode {
    DiagnosticsOnly,
    DeviceHost,
};

wxDECLARE_EVENT(EVT_WEBVIEW_RECOVERY, wxCommandEvent);

/// Observes one wxWebView and applies its diagnostic/recovery policy.
class WebViewWatcher
{
public:
    /// Faults are finer-grained than log stages because recovery differs.
    enum class Fault {
        BackendUnavailable,   // L0: runtime missing, fell back to FakeWebView
        ResourceMissing,      // L1: local file:// target does not exist
        UserDataPathUnusable, // L1: no writable WebView2 user data directory
        NavConnection,        // L2: connection error, offline
        NavOther,             // L2: any other navigation error
        RenderProcessGone,    // L3: render process exited/crashed
        FrameRenderProcessGone,
        BrowserProcessGone,   // L3: browser process exited
        ProcessUnresponsive,  // L3: hang; fires repeatedly while stuck
        ReadyTimeout,         // L4: front-end never reported ready
        RuntimeScriptError,   // L5: JS error or sub-resource failure
    };

    enum class Action {
        None,         // log only
        Reload,       // Reload() is enough
        LoadUrl,      // must re-navigate; Reload() is a no-op after a crash
        Recreate,     // the browser process exited; replace the control
        ErrorPage,    // give up, surface a placeholder to the user
        InstallGuide, // runtime is missing, point the user at the installer
    };

    struct Decision
    {
        Fault  fault        = Fault::NavOther;
        Action action       = Action::None;
        int    delay_ms     = 0;
    };

    struct FaultLogDecision
    {
        bool emit       = true;
        int  suppressed = 0;
    };

    explicit WebViewWatcher(wxWebView *webView,
                            WebViewProtectionMode mode = WebViewProtectionMode::DiagnosticsOnly);
    ~WebViewWatcher();

    WebViewWatcher(const WebViewWatcher &) = delete;
    WebViewWatcher &operator=(const WebViewWatcher &) = delete;

    /// Returns the suggested action; DeviceWebHost owns retry budgets.
    Decision Decide(Fault fault);

    FaultLogDecision DecideFaultLog(Fault fault, int discriminator);

    void RecordNavigationStart();
    std::optional<int64_t> ConsumeNavigationDurationMs();

    void StartReadyWatchdog();
    void CancelReadyWatchdog();
    std::optional<int64_t> MarkReady();
    void SetReadyWatchdogPaused(bool paused);

    /// Posts the recovery action after its delay.
    void ScheduleRecovery(const Decision &decision);
    void CancelRecovery();

    static WebViewTraceLogger::Stage StageOf(Fault fault);
    static const char               *ActionName(Action action);

private:
    class Timer : public wxTimer
    {
    public:
        using Callback = void (WebViewWatcher::*)();
        Timer(WebViewWatcher &owner, Callback callback) : m_owner(owner), m_callback(callback) {}
        void Notify() override { (m_owner.*m_callback)(); }

    private:
        WebViewWatcher &m_owner;
        Callback        m_callback;
    };

    void OnRecoveryTimer();
    void OnReadyTimer();
    void ArmReadyTimer();
    std::optional<int64_t> ExpireReadyWatchdog();

    struct FaultLogWindow
    {
        int64_t start_ms   = 0;
        int     suppressed = 0;
        bool    active     = false;
    };

    wxWebView    *m_webView = nullptr;
    std::unique_ptr<Timer> m_timer;
    std::unique_ptr<Timer> m_ready_timer;
    Action        m_pending = Action::None;
    Fault         m_pending_fault = Fault::NavOther;
    WebViewProtectionMode m_mode = WebViewProtectionMode::DiagnosticsOnly;

    std::unordered_map<int, FaultLogWindow> m_l3_log_windows;
    std::optional<int64_t> m_navigation_start_ms;
    std::optional<int64_t> m_ready_start_ms;
    bool m_ready_paused  = false;
};

}} // namespace Slic3r::GUI

#endif // !slic3r_GUI_WebViewWatcher_hpp_
