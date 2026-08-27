#ifndef slic3r_GUI_WebView_hpp_
#define slic3r_GUI_WebView_hpp_

#include <optional>

#include <wx/webview.h>

#include "WebViewWatcher.hpp"

class WebView
{
public:
    // Creation.
    static wxWebView *CreateWebView(
        wxWindow *parent, wxString const &url, wxString const &name = wxEmptyString,
        Slic3r::GUI::WebViewProtectionMode mode = Slic3r::GUI::WebViewProtectionMode::DiagnosticsOnly);
    static std::optional<Slic3r::GUI::WebViewWatcher::Fault> CreationFault(wxWebView *webView);
    static bool ValidateLocalTarget(wxWebView *webView, const wxString &url);

    // Page operations.
    static void LoadUrl(wxWebView * webView, wxString const &url);
    static bool RunScript(wxWebView * webView, wxString const & msg);

    // Ready watchdog.
    static void StartReadyWatchdog(wxWebView *webView);
    static void CancelReadyWatchdog(wxWebView *webView);
    static bool NotifyReady(wxWebView *webView);
    static void SetReadyWatchdogPaused(wxWebView *webView, bool paused);

    // Recovery.
    static void CancelRecovery(wxWebView *webView);
    static void RecreateAll();

    // Cookies.
    // Remove WebView cookies named "token" on domains containing "bambulab".
    // Windows: WebView2 CookieManager; macOS: WKHTTPCookieStore (default data store).
    static void ClearBambulabTokenCookies();

    // Backend setup.
    static wxString BuildEdgeUserDataPath();
};

#endif // !slic3r_GUI_WebView_hpp_
