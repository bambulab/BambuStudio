#include "WebView.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/Utils/MacDarkMode.hpp"

#include <boost/log/trivial.hpp>

#include <wx/webviewarchivehandler.h>
#include <wx/webviewfshandler.h>
#include <wx/dynlib.h>
#include <wx/utils.h>
#if wxUSE_WEBVIEW_EDGE
#include <wx/msw/webview_edge.h>
#elif defined(__WXMAC__)
#include <wx/osx/webview_webkit.h>
#endif
#include <wx/uri.h>
#if defined(__WIN32__) || defined(__WXMAC__)
#include "wx/private/jsscriptwrapper.h"
#endif

#ifdef __WIN32__
#include <WebView2.h>
#include <wrl/client.h>
#include <wrl/event.h>
#elif defined __linux__
#include <gtk/gtk.h>
#define WEBKIT_API
struct WebKitWebView;
#if defined(BBL_WEBKITGTK_4_1)
struct _JSCValue;
typedef struct _JSCValue JSCValue;
#else
struct WebKitJavascriptResult;
#endif
extern "C" {
#if defined(BBL_WEBKITGTK_4_1)
WEBKIT_API void
webkit_web_view_evaluate_javascript                  (WebKitWebView             *web_view,
                                                      const gchar               *script,
                                                      gssize                     length,
                                                      const gchar               *world_name,
                                                      const gchar               *source_uri,
                                                      GCancellable              *cancellable,
                                                      GAsyncReadyCallback       callback,
                                                      gpointer                  user_data);
WEBKIT_API JSCValue *
webkit_web_view_evaluate_javascript_finish           (WebKitWebView             *web_view,
                                                      GAsyncResult              *result,
                                                      GError                    **error);
#else
WEBKIT_API void
webkit_web_view_run_javascript                       (WebKitWebView             *web_view,
                                                      const gchar               *script,
                                                      GCancellable              *cancellable,
                                                      GAsyncReadyCallback       callback,
                                                      gpointer                  user_data);
WEBKIT_API WebKitJavascriptResult *
webkit_web_view_run_javascript_finish                (WebKitWebView             *web_view,
                                                      GAsyncResult              *result,
                                                      GError                    **error);
WEBKIT_API void
webkit_javascript_result_unref                       (WebKitJavascriptResult    *js_result);
#endif
}

static GOnce register_handler_once = G_ONCE_INIT;

gpointer
register_webview_handler(gpointer data)
{
    wxWebView *webView = (wxWebView *) data;

    // With WKWebView handlers need to be registered before creation
    webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
    // And the memory: file system
    webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
    return NULL;
}
#endif

#ifdef __WIN32__

namespace {

void enable_default_webview2_cdp_for_internal_builds()
{
#if !BBL_RELEASE_TO_PUBLIC
    wxString existing;
    if (wxGetEnv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", &existing) && !existing.empty())
        return;

    wxSetEnv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
             "--remote-debugging-port=9222 --remote-allow-origins=*");
#endif
}

// BBL_ENABLE_WEBVIEW_DEBUG gate: ProcessFailed log/dialog is enabled when
// BBL_RELEASE_TO_PUBLIC is false, or env BBL_ENABLE_WEBVIEW_DEBUG is true/1/on/yes.
bool is_bbl_webview_debug_enabled()
{
    static const bool enabled = []() -> bool {
#if !BBL_RELEASE_TO_PUBLIC
        return true;
#else
        wxString value;
        if (!wxGetEnv("BBL_ENABLE_WEBVIEW_DEBUG", &value) || value.empty())
            return false;
        value.MakeLower();
        return value == "1" || value == "true" || value == "on" || value == "yes";
#endif
    }();
    return enabled;
}

// Cookie name to clear and the domain substring it must belong to on logout.
constexpr wchar_t kLogoutCookieName[]   = L"token";
constexpr wchar_t kLogoutCookieDomain[] = L"bambulab";

bool domain_matches_bambulab(LPCWSTR domain)
{
    if (!domain)
        return false;
    std::wstring lower(domain);
    for (wchar_t &c : lower)
        c = towlower(c);
    return lower.find(kLogoutCookieDomain) != std::wstring::npos;
}

const char *process_failed_kind_str(COREWEBVIEW2_PROCESS_FAILED_KIND kind)
{
    switch (kind) {
    case COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED: return "BROWSER_PROCESS_EXITED";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED: return "RENDER_PROCESS_EXITED";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE: return "RENDER_PROCESS_UNRESPONSIVE";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED: return "FRAME_RENDER_PROCESS_EXITED";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_UTILITY_PROCESS_EXITED: return "UTILITY_PROCESS_EXITED";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_SANDBOX_HELPER_PROCESS_EXITED: return "SANDBOX_HELPER_PROCESS_EXITED";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_GPU_PROCESS_EXITED: return "GPU_PROCESS_EXITED";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_PPAPI_PLUGIN_PROCESS_EXITED: return "PPAPI_PLUGIN_PROCESS_EXITED";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_PPAPI_BROKER_PROCESS_EXITED: return "PPAPI_BROKER_PROCESS_EXITED";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_UNKNOWN_PROCESS_EXITED: return "UNKNOWN_PROCESS_EXITED";
    default: return "UNKNOWN_KIND";
    }
}

const char *process_failed_reason_str(COREWEBVIEW2_PROCESS_FAILED_REASON reason)
{
    switch (reason) {
    case COREWEBVIEW2_PROCESS_FAILED_REASON_UNEXPECTED: return "UNEXPECTED";
    case COREWEBVIEW2_PROCESS_FAILED_REASON_UNRESPONSIVE: return "UNRESPONSIVE";
    case COREWEBVIEW2_PROCESS_FAILED_REASON_TERMINATED: return "TERMINATED";
    case COREWEBVIEW2_PROCESS_FAILED_REASON_CRASHED: return "CRASHED";
    case COREWEBVIEW2_PROCESS_FAILED_REASON_LAUNCH_FAILED: return "LAUNCH_FAILED";
    case COREWEBVIEW2_PROCESS_FAILED_REASON_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    case COREWEBVIEW2_PROCESS_FAILED_REASON_PROFILE_DELETED: return "PROFILE_DELETED";
    default: return "UNKNOWN_REASON";
    }
}

} // namespace

class WebViewEdge : public wxWebViewEdge
{
public:
    ~WebViewEdge()
    {
        UnsubscribeProcessFailed();
    }

    bool SetUserAgent(const wxString &userAgent)
    {
        bool dark = userAgent.Contains("dark");
        SetColorScheme(dark ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT);

        ICoreWebView2 *webView2 = (ICoreWebView2 *) GetNativeBackend();
        if (webView2) {
            EnsureProcessFailedSubscribed();
            ICoreWebView2Settings *settings;
            HRESULT                hr = webView2->get_Settings(&settings);
            if (hr == S_OK) {
                ICoreWebView2Settings2 *settings2;
                hr = settings->QueryInterface(&settings2);
                if (hr == S_OK) {
                    settings2->put_UserAgent(userAgent.wc_str());
                    settings2->Release();
                    ICoreWebView2Settings4 *settings4;
                    hr = settings->QueryInterface(&settings4);
                    if (hr == S_OK) {
                        settings4->put_IsGeneralAutofillEnabled(Slic3r::GUI::wxGetApp().app_config->get_bool("webview_auto_fill"));
                        settings4->Release();
                    }
                    return true;
                }
            }
            settings->Release();
            return false;
        }
        pendingUserAgent = userAgent;
        return true;
    }

    bool SetColorScheme(COREWEBVIEW2_PREFERRED_COLOR_SCHEME colorScheme)
    {
        ICoreWebView2 *webView2 = (ICoreWebView2 *) GetNativeBackend();
        if (webView2) {
            EnsureProcessFailedSubscribed();
            ICoreWebView2_13 * webView2_13;
            HRESULT           hr = webView2->QueryInterface(&webView2_13);
            if (hr == S_OK) {
                ICoreWebView2Profile *profile;
                hr = webView2_13->get_Profile(&profile);
                if (hr == S_OK) {
                    profile->put_PreferredColorScheme(colorScheme);
                    profile->Release();
                    return true;
                }
                webView2_13->Release();
            }
            return false;
        }
        pendingColorScheme = colorScheme;
        return true;
    }

    void DoGetClientSize(int *x, int *y) const override
    {
        if (!pendingUserAgent.empty()) {
            auto thiz = const_cast<WebViewEdge *>(this);
            thiz->EnsureProcessFailedSubscribed();            
            auto userAgent = std::move(thiz->pendingUserAgent);
            thiz->pendingUserAgent.clear();
            thiz->SetUserAgent(userAgent);
        }
        if (pendingColorScheme) {
            auto thiz = const_cast<WebViewEdge *>(this);
            thiz->EnsureProcessFailedSubscribed();
            auto colorScheme = pendingColorScheme;
            thiz->pendingColorScheme = COREWEBVIEW2_PREFERRED_COLOR_SCHEME_AUTO;
            thiz->SetColorScheme(colorScheme);
        }
        wxWebViewEdge::DoGetClientSize(x, y);
    };

private:
    void EnsureProcessFailedSubscribed()
    {
        if (!is_bbl_webview_debug_enabled())
            return;
        if (m_processFailedSubscribed)
            return;

        ICoreWebView2 *webView2 = (ICoreWebView2 *) GetNativeBackend();
        if (!webView2)
            return;

        using Microsoft::WRL::Callback;
        HRESULT hr = webView2->add_ProcessFailed(
            Callback<ICoreWebView2ProcessFailedEventHandler>(
                [this](ICoreWebView2 *sender, ICoreWebView2ProcessFailedEventArgs *args) -> HRESULT {
                    if (!args)
                        return S_OK;

                    COREWEBVIEW2_PROCESS_FAILED_KIND kind =
                        COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
                    args->get_ProcessFailedKind(&kind);

                    COREWEBVIEW2_PROCESS_FAILED_REASON reason =
                        COREWEBVIEW2_PROCESS_FAILED_REASON_UNEXPECTED;
                    int exit_code = 0;
                    Microsoft::WRL::ComPtr<ICoreWebView2ProcessFailedEventArgs2> args2;
                    if (SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&args2))) && args2) {
                        args2->get_Reason(&reason);
                        args2->get_ExitCode(&exit_code);
                    }

                    wxString url;
                    if (sender) {
                        LPWSTR source = nullptr;
                        if (SUCCEEDED(sender->get_Source(&source)) && source) {
                            url = source;
                            CoTaskMemFree(source);
                        }
                    }
                    if (url.empty())
                        url = GetCurrentURL();

                    BOOST_LOG_TRIVIAL(error)
                        << GetName()
                        << " [WebView] ProcessFailed"
                        << " kind=" << process_failed_kind_str(kind)
                        << " (" << static_cast<int>(kind) << ")"
                        << " reason=" << process_failed_reason_str(reason)
                        << " (" << static_cast<int>(reason) << ")"
                        << " exitCode=" << exit_code
                        << " url=" << url.ToUTF8().data();

                    // Only surface fatal browser/renderer exits to the user.
                    // UNRESPONSIVE fires repeatedly while stuck; GPU/utility usually auto-recover.
                    const bool notify_user =
                        kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED ||
                        kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED;
                    if (notify_user) {
                        const wxString detail = wxString::Format(
                            "kind=%s (%d)\nreason=%s (%d)\nexitCode=%d\nurl=%s",
                            wxString(process_failed_kind_str(kind)), static_cast<int>(kind),
                            wxString(process_failed_reason_str(reason)), static_cast<int>(reason),
                            exit_code, url);
                        const wxString webview_name = GetName();
                        // Do not ShowModal inside ProcessFailed (reentrancy). Defer to UI loop.
                        Slic3r::GUI::wxGetApp().CallAfter([detail, webview_name]() {
                            auto &app = Slic3r::GUI::wxGetApp();
                            if (app.is_closing())
                                return;

                            static bool s_showing = false;
                            if (s_showing)
                                return;
                            s_showing = true;

                            wxString reason_block = detail;
                            if (!webview_name.empty())
                                reason_block = wxString::Format("name=%s\n%s", webview_name, detail);

                            const wxString message = wxString::Format(
                                _L("The embedded webpage has crashed. Please contact Bambu Studio.\n\nReason:\n%s"),
                                reason_block);

                            Slic3r::GUI::MessageDialog dlg(nullptr, message, _L("Embedded Webpage Crashed"),
                                                           wxOK | wxICON_ERROR);
                            dlg.ShowModal();
                            s_showing = false;
                        });
                    }

                    return S_OK;
                })
                .Get(),
            &m_processFailedToken);

        if (SUCCEEDED(hr)) {
            m_processFailedSubscribed = true;
            BOOST_LOG_TRIVIAL(info) << GetName() << " [WebView] ProcessFailed handler subscribed";
        } else {
            BOOST_LOG_TRIVIAL(warning) << GetName()
                                      << wxString::Format(" [WebView] add_ProcessFailed failed, hr=0x%08X",
                                                          static_cast<unsigned>(hr)).ToUTF8().data();
        }
    }

    void UnsubscribeProcessFailed()
    {
        if (!m_processFailedSubscribed)
            return;
        ICoreWebView2 *webView2 = (ICoreWebView2 *) GetNativeBackend();
        if (webView2)
            webView2->remove_ProcessFailed(m_processFailedToken);
        m_processFailedSubscribed = false;
    }

    wxString pendingUserAgent;
    COREWEBVIEW2_PREFERRED_COLOR_SCHEME pendingColorScheme = COREWEBVIEW2_PREFERRED_COLOR_SCHEME_AUTO;
    EventRegistrationToken m_processFailedToken{};
    bool m_processFailedSubscribed{false};
};

#elif defined __WXOSX__

class WebViewWebKit : public wxWebViewWebKit
{
    ~WebViewWebKit() override
    {
        RemoveScriptMessageHandler("wx");
    }
};

#endif

class FakeWebView : public wxWebView
{
    virtual bool Create(wxWindow* parent, wxWindowID id, const wxString& url, const wxPoint& pos, const wxSize& size, long style, const wxString& name) override { return false; }
    virtual wxString GetCurrentTitle() const override { return wxString(); }
    virtual wxString GetCurrentURL() const override { return wxString(); }
    virtual bool IsBusy() const override { return false; }
    virtual bool IsEditable() const override { return false; }
    virtual void LoadURL(const wxString& url) override { }
    virtual void Print() override { }
    virtual void RegisterHandler(wxSharedPtr<wxWebViewHandler> handler) override { }
    virtual void Reload(wxWebViewReloadFlags flags = wxWEBVIEW_RELOAD_DEFAULT) override { }
    virtual bool RunScript(const wxString& javascript, wxString* output = NULL) const override { return false; }
    virtual void SetEditable(bool enable = true) override { }
    virtual void Stop() override { }
    virtual bool CanGoBack() const override { return false; }
    virtual bool CanGoForward() const override { return false; }
    virtual void GoBack() override { }
    virtual void GoForward() override { }
    virtual void ClearHistory() override { }
    virtual void EnableHistory(bool enable = true) override { }
    virtual wxVector<wxSharedPtr<wxWebViewHistoryItem>> GetBackwardHistory() override { return {}; }
    virtual wxVector<wxSharedPtr<wxWebViewHistoryItem>> GetForwardHistory() override { return {}; }
    virtual void LoadHistoryItem(wxSharedPtr<wxWebViewHistoryItem> item) override { }
    virtual bool CanSetZoomType(wxWebViewZoomType type) const override { return false; }
    virtual float GetZoomFactor() const override { return 0.0f; }
    virtual wxWebViewZoomType GetZoomType() const override { return wxWebViewZoomType(); }
    virtual void SetZoomFactor(float zoom) override { }
    virtual void SetZoomType(wxWebViewZoomType zoomType) override { }
    virtual bool CanUndo() const override { return false; }
    virtual bool CanRedo() const override { return false; }
    virtual void Undo() override { }
    virtual void Redo() override { }
    virtual void* GetNativeBackend() const override { return nullptr; }
    virtual void DoSetPage(const wxString& html, const wxString& baseUrl) override { }
};

wxDEFINE_EVENT(EVT_WEBVIEW_RECREATED, wxCommandEvent);

static std::vector<wxWebView*> g_webviews;
static std::vector<wxWebView*> g_delay_webviews;

class WebViewRef : public wxObjectRefData
{
public:
    WebViewRef(wxWebView *webView) : m_webView(webView) {}
    ~WebViewRef() {
        auto iter = std::find(g_webviews.begin(), g_webviews.end(), m_webView);
        assert(iter != g_webviews.end());
        if (iter != g_webviews.end())
            g_webviews.erase(iter);
        // Also drop it from the delayed list so a pending flush of g_delay_webviews
        // never calls AddScriptMessageHandler() on an already-destroyed view.
        // See bambulab/BambuStudio #11004 and #10968.
        auto diter = std::find(g_delay_webviews.begin(), g_delay_webviews.end(), m_webView);
        if (diter != g_delay_webviews.end())
            g_delay_webviews.erase(diter);
    }
    wxWebView *m_webView;
};

#define BAMBU_LOCK_FILE_NAME "bambu_lockfile"
wxString WebView::BuildEdgeUserDataPath()
{
#ifdef __WIN32__
    static wxString data_dir;
    if (!data_dir.empty()) { return data_dir; }

    data_dir = wxStandardPaths::Get().GetUserLocalDataDir();
    data_dir.append("\\WebView2Cache\\");

    // find a path
    for (int bambu_id = 0; bambu_id < std::numeric_limits<int>::max(); bambu_id++) {
        wxString bambu_dir = data_dir + wxString::Format("%d", bambu_id);
        if (!wxDir::Exists(bambu_dir) && !wxDir::Make(bambu_dir, 511, wxPATH_MKDIR_FULL)) { break; } /*maybe don't have access rights to create dir, break*/

        wxString bambu_lock_file = bambu_dir + "\\" BAMBU_LOCK_FILE_NAME;

        static wxFile lockFile;
        if (lockFile.Exists(bambu_lock_file)) { DeleteFileW(bambu_lock_file.wc_str()); }/*try delete previous file so that we could lock it by wxFile::write_excl*/

        wxLogNull suppress_log;
        if (lockFile.Open(bambu_lock_file, wxFile::write_excl)) {
            data_dir = bambu_dir;
            break;
        }

        if (!lockFile.Exists(bambu_lock_file)) { break; } /*maybe don't have access rights to create file, break*/
    }

    return data_dir;

#else
    return wxEmptyString;
#endif
}

void on_webview_evt(wxWebView *webview)
{
    webview->Bind(wxEVT_WEBVIEW_NAVIGATING, [webview](wxWebViewEvent &e) {
        wxLogMessage("NAVIGATING: %s", e.GetURL());
        BOOST_LOG_TRIVIAL(info) << webview->GetName() << " [WebView] navigating " << e.GetURL();
        e.Skip();
    });

    webview->Bind(wxEVT_WEBVIEW_NAVIGATED, [webview](wxWebViewEvent &e) {
        BOOST_LOG_TRIVIAL(info) << webview->GetName() << " [WebView] navigated: " << e.GetURL();
        e.Skip();
    });

    webview->Bind(wxEVT_WEBVIEW_LOADED, [webview](wxWebViewEvent &e) {
        BOOST_LOG_TRIVIAL(info) << webview->GetName() << " [WebView] loaded: " << e.GetURL();
        e.Skip();
    });

    webview->Bind(wxEVT_WEBVIEW_ERROR, [webview](wxWebViewEvent &e) {
        BOOST_LOG_TRIVIAL(info) << webview->GetName()
                                << wxString::Format(" [WebView] error: url=%s, code=%d, description=%s", e.GetURL(), static_cast<int>(e.GetInt()), e.GetString().utf8_string());
        e.Skip();
    });

    webview->Bind(wxEVT_WEBVIEW_TITLE_CHANGED, [webview](wxWebViewEvent &e) {
        BOOST_LOG_TRIVIAL(info) << webview->GetName() << wxString::Format(" [WebView] title changed: %s", e.GetString().utf8_string());
        e.Skip();
    });

    webview->Bind(wxEVT_WEBVIEW_NEWWINDOW, [webview](wxWebViewEvent &e) {
        BOOST_LOG_TRIVIAL(info) << webview->GetName() << wxString::Format(" [WebView] new window: %s", e.GetString());
        e.Skip();
    });
}

wxWebView *WebView::CreateWebView(wxWindow *parent, wxString const &url, wxString const &name)
{
#if wxUSE_WEBVIEW_EDGE
    // Check if a fixed version of edge is present in
    // $executable_path/edge_fixed and use it
    wxFileName edgeFixedDir(wxStandardPaths::Get().GetExecutablePath());
    edgeFixedDir.SetFullName("");
    edgeFixedDir.AppendDir("edge_fixed");
    if (edgeFixedDir.DirExists()) {
        wxWebViewEdge::MSWSetBrowserExecutableDir(edgeFixedDir.GetFullPath());
        wxLogMessage("Using fixed edge version");
    }

    if(!wxWebView::IsBackendAvailable(wxWebViewBackendEdge)) {
        BOOST_LOG_TRIVIAL(warning) << "WebView2 runtime is not available. WebView based features may not work properly";
    }
#endif
    auto url2  = url;
#ifdef __WIN32__
    url2.Replace("\\", "/");
#endif
    if (!url2.empty()) { url2 = wxURI(url2).BuildURI(); }
    //BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << url2.ToUTF8();

#ifdef __WIN32__
    enable_default_webview2_cdp_for_internal_builds();

    wxWebView* webView = new WebViewEdge;
    webView->SetUserDataPathOption(BuildEdgeUserDataPath());
#elif defined(__WXOSX__)
    wxWebView *webView = new WebViewWebKit;
#else
    auto webView = wxWebView::New();
#endif
    if (webView) {
        on_webview_evt(webView);

        webView->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));

        wxString language_code = Slic3r::GUI::wxGetApp().current_language_code().BeforeFirst('_');
        language_code          = language_code.ToStdString();
#ifdef __WIN32__
        webView->SetUserAgent(wxString::Format("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                                               "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/107.0.0.0 Safari/537.36 Edg/107.0.1418.52 BBL-Slicer/v%s (%s) BBL-Language/%s",
                                               SLIC3R_VERSION, Slic3r::GUI::wxGetApp().dark_mode() ? "dark" : "light", language_code.mb_str()));
        webView->Create(parent, wxID_ANY, url2, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        if (!name.empty()) webView->SetName(name);
        // We register the wxfs:// protocol for testing purposes
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("bbl")));
        // And the memory: file system
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
#else
#if defined __linux__
        g_once(&register_handler_once, register_webview_handler, webView);
#else
        // With WKWebView handlers need to be registered before creation
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
        // And the memory: file system
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
#endif
        webView->Create(parent, wxID_ANY, url2, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        webView->SetUserAgent(wxString::Format("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) BBL-Slicer/v%s (%s) BBL-Language/%s",
                                               SLIC3R_VERSION, Slic3r::GUI::wxGetApp().dark_mode() ? "dark" : "light", language_code.mb_str()));
#endif
#ifdef __WXMAC__
        WKWebView * wkWebView = (WKWebView *) webView->GetNativeBackend();
        Slic3r::GUI::WKWebView_setTransparentBackground(wkWebView);
#endif
        auto addScriptMessageHandler = [] (wxWebView *webView) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": begin to add script message handler for wx.";
            Slic3r::GUI::wxGetApp().set_adding_script_handler(true);
            if (!webView->AddScriptMessageHandler("wx"))
                wxLogError("Could not add script message handler");
            Slic3r::GUI::wxGetApp().set_adding_script_handler(false);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": finished add script message handler for wx.";
        };
#ifndef __WIN32__
        webView->CallAfter([webView, addScriptMessageHandler] {
            // This async callback may fire after webView has already been destroyed,
            // which would call AddScriptMessageHandler() on a dangling pointer
            // (use-after-free -> pointer-authentication crash on Apple Silicon, or a
            // long hang during startup on macOS 26.5+). g_webviews lists every live
            // view, so bail out if this one is already gone.
            // See bambulab/BambuStudio #11004 and #10968.
            if (std::find(g_webviews.begin(), g_webviews.end(), webView) == g_webviews.end())
                return;
#endif
            if (Slic3r::GUI::wxGetApp().is_adding_script_handler()) {
                g_delay_webviews.push_back(webView);
            } else {
                addScriptMessageHandler(webView);
                // AddScriptMessageHandler pumps a nested event loop (RunScriptSync ->
                // wxYieldFor). While the adding-flag is set, other webviews' deferred
                // CallAfter lambdas dispatched by that nested loop take the guarded
                // branch above and queue themselves here, so drain them now. Any webview
                // torn down while queued (e.g. a language-switch GUI rebuild) is skipped:
                // ~WebViewRef removes it from g_webviews, so the find() below filters it
                // out instead of dereferencing freed memory.
                while (!g_delay_webviews.empty()) {
                    auto wv = g_delay_webviews.front();
                    g_delay_webviews.erase(g_delay_webviews.begin());
                    if (std::find(g_webviews.begin(), g_webviews.end(), wv) == g_webviews.end()) continue;
                    addScriptMessageHandler(wv);
                }
            }
#ifndef __WIN32__
        });
#endif
        const bool enable_devtools = Slic3r::GUI::wxGetApp().app_config && Slic3r::GUI::wxGetApp().app_config->get("enable_webview_devtools") == "true";
        webView->EnableContextMenu(enable_devtools);
        webView->EnableAccessToDevTools(enable_devtools);
#ifdef __WXMAC__
        // EnableAccessToDevTools only flips the legacy developerExtrasEnabled preference.
        // Since macOS 13.3 / Safari 16.4 WKWebView is not inspectable unless setInspectable:YES
        // is called, so without this the view never shows up in Safari's Develop menu.
        if (WKWebView *wkWebView = (WKWebView *) webView->GetNativeBackend())
            Slic3r::GUI::WKWebView_setInspectable(wkWebView, enable_devtools);
#endif
    } else {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": failed. Use fake web view.";
        webView = new FakeWebView;
    }
    webView->SetRefData(new WebViewRef(webView));
    g_webviews.push_back(webView);
    return webView;
}

void WebView::LoadUrl(wxWebView * webView, wxString const &url)
{
    auto url2  = url;
#ifdef __WIN32__
    url2.Replace("\\", "/");
#endif
    if (!url2.empty()) { url2 = wxURI(url2).BuildURI(); }
    //BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << url2.ToUTF8();
    webView->LoadURL(url2);
}

bool WebView::RunScript(wxWebView *webView, wxString const &javascript)
{
    if (Slic3r::GUI::wxGetApp().app_config->get("internal_developer_mode") == "true"
            && javascript.find("studio_userlogin") == wxString::npos)
        wxLogMessage("Running JavaScript:\n%s\n", javascript);

    if (webView == nullptr)
        return false;

#ifdef __WXMAC__
    // Skip JS evaluation on hidden webviews on macOS. Several panels keep a
    // pool of wxWebView instances that are Hide()'d and swapped in on demand
    // (see WebViewPanel::SetWebviewShow). Pushing JS into a hidden view
    // still wakes the WebContent process via WKWebView evaluateJavaScript
    // (runJavaScriptInFrameInScriptWorld cancels ProcessThrottler
    // suspension) while the events delivered to its NSView are silently
    // dropped because it is setHidden:YES. After sleep/wake this has been
    // observed to produce a minutes-long "frozen input" state for the user
    // -- the window has focus but the hit-test view is hidden.
    //
    // Return true (not false) because "view is hidden, skipped" is not an
    // error from the caller's perspective: the wxWebView's wxEVT_SHOW
    // handler in WebViewDialog re-pushes the relevant state when a hidden
    // panel becomes visible. Returning false would conflate this benign
    // skip with a real wkWebView/ICoreWebView2 failure and trigger the
    // error-log paths in callers like wgtFilaManagerPanel::SendMsg. Same
    // truthy contract as the macOS evaluateJavaScript path further down.
    //
    // Known follow-up: the timer-driven JS pushes that fire on a hidden
    // home tab (e.g. m_LoginUpdateTimer's user-print-task refresh in
    // WebViewPanel::OnFreshLoginStatus) drop on the floor without an
    // explicit replay-on-show hook. The home page already lazy-initializes
    // most state on tab activation, so this is mostly cosmetic, but a
    // future commit could add the same m_has_pending_* defer/replay
    // pattern used by SendDesignStaffpick at WebViewDialog.cpp:763.
    if (!webView->IsShownOnScreen())
        return true;
#endif // __WXMAC__

    try {
#ifdef __WIN32__
        ICoreWebView2 *   webView2 = (ICoreWebView2 *) webView->GetNativeBackend();
        if (webView2 == nullptr)
            return false;
        return webView2->ExecuteScript(javascript, NULL) == 0;
#elif defined __WXMAC__
        WKWebView * wkWebView = (WKWebView *) webView->GetNativeBackend();
        Slic3r::GUI::WKWebView_evaluateJavaScript(wkWebView, javascript, nullptr);
        return true;
#else
        WebKitWebView *wkWebView = (WebKitWebView *) webView->GetNativeBackend();
#if defined(BBL_WEBKITGTK_4_1)
        webkit_web_view_evaluate_javascript(
            wkWebView, javascript.utf8_str(), -1, NULL, NULL, NULL,
            [](GObject *wkWebView, GAsyncResult *res, void *) {
                GError *error = NULL;
                JSCValue *result = webkit_web_view_evaluate_javascript_finish((WebKitWebView*)wkWebView, res, &error);
                if (!result) {
                    if (error) g_error_free(error);
                } else {
                    g_object_unref(result);
                }
        }, NULL);
#else
        webkit_web_view_run_javascript(
            wkWebView, javascript.utf8_str(), NULL,
            [](GObject *wkWebView, GAsyncResult *res, void *) {
                GError * error = NULL;
                auto result = webkit_web_view_run_javascript_finish((WebKitWebView*)wkWebView, res, &error);
                if (!result)
                    g_error_free (error);
                else
                    webkit_javascript_result_unref (result);
        }, NULL);
#endif
        return true;
#endif
    } catch (std::exception &/*e*/) {
        return false;
    }
}

// Single source of truth for "the active webview backend is CEF/libcef".
// libcef is built and shipped cross-platform, so when it is enabled BOTH the
// Windows (WebView2) and macOS (WebKit) cookie paths must be bypassed in favour
// of the CEF path below. Map this to the real build flag once libcef lands
// (e.g. wxUSE_WEBVIEW_CHROMIUM or a dedicated define).
#ifndef BBL_WEBVIEW_USE_CEF
#  if defined(wxUSE_WEBVIEW_CHROMIUM) && wxUSE_WEBVIEW_CHROMIUM
#    define BBL_WEBVIEW_USE_CEF 1
#  else
#    define BBL_WEBVIEW_USE_CEF 0
#  endif
#endif

void WebView::ClearBambulabTokenCookies()
{
    // Dispatch on the active webview backend, not the platform: the cookie store
    // is owned by the backend (WebView2 / WebKit / CEF), so a future backend
    // switch must land in the matching branch instead of mis-casting the native
    // pointer returned by GetNativeBackend().
#if BBL_WEBVIEW_USE_CEF
    // CEF/Chromium backend (Windows + macOS): cookies live in Chromium's network
    // context, reachable via CefCookieManager::GetGlobalManager()->
    // VisitAllCookies(visitor); the visitor sets its deleteCookie out-param for
    // name=="token" on domains that contain "bambulab". Wire this up once the
    // libcef headers are part of the build.
    BOOST_LOG_TRIVIAL(warning)
        << "WebView: ClearBambulabTokenCookies not yet implemented for the Chromium/CEF backend";
#elif defined(__WIN32__) && wxUSE_WEBVIEW_EDGE
    using Microsoft::WRL::ComPtr;
    using Microsoft::WRL::Callback;

    // Every WebView created via CreateWebView shares one WebView2 profile/cookie
    // store, so clearing through any live backend covers all of them.
    ICoreWebView2 *backend = nullptr;
    for (wxWebView *webView : g_webviews) {
        if (webView && (backend = static_cast<ICoreWebView2 *>(webView->GetNativeBackend())))
            break;
    }
    if (!backend) {
        BOOST_LOG_TRIVIAL(warning) << "WebView: ClearBambulabTokenCookies skipped, no WebView2 backend ready";
        return;
    }

    ComPtr<ICoreWebView2_2> webView2_2;
    if (FAILED(backend->QueryInterface(IID_PPV_ARGS(&webView2_2))) || !webView2_2) {
        BOOST_LOG_TRIVIAL(warning) << "WebView: ClearBambulabTokenCookies failed to get ICoreWebView2_2";
        return;
    }

    ComPtr<ICoreWebView2CookieManager> cookieManager;
    if (FAILED(webView2_2->get_CookieManager(&cookieManager)) || !cookieManager)
        return;

    // nullptr uri => enumerate all cookies; capturing cookieManager keeps it alive
    // until the async handler runs. WRL handles the handler's lifetime/refcount.
    cookieManager->GetCookies(
        nullptr,
        Callback<ICoreWebView2GetCookiesCompletedHandler>(
            [cookieManager](HRESULT result, ICoreWebView2CookieList *list) -> HRESULT {
                if (FAILED(result) || !list)
                    return S_OK;
                UINT count = 0;
                list->get_Count(&count);
                for (UINT i = 0; i < count; ++i) {
                    ComPtr<ICoreWebView2Cookie> cookie;
                    if (FAILED(list->GetValueAtIndex(i, &cookie)) || !cookie)
                        continue;
                    LPWSTR name = nullptr, domain = nullptr;
                    cookie->get_Name(&name);
                    cookie->get_Domain(&domain);
                    if (name && wcscmp(name, kLogoutCookieName) == 0 && domain_matches_bambulab(domain)) {
                        cookieManager->DeleteCookie(cookie.Get());
                        BOOST_LOG_TRIVIAL(info) << "WebView: cleared bambulab token cookie";
                    }
                    CoTaskMemFree(name);
                    CoTaskMemFree(domain);
                }
                return S_OK;
            })
            .Get());
#elif defined(__WXOSX__)
    // Native WebKit backend only. Under a macOS CEF build this branch is skipped
    // because BBL_WEBVIEW_USE_CEF wins above; WKWebsiteDataStore does not own the
    // CEF cookie store, so it must not run there.
    // wxWebView WebKit uses the default WKWebsiteDataStore; cookies are process-wide.
    Slic3r::GUI::WKWebView_clearBambulabTokenCookies();
    BOOST_LOG_TRIVIAL(info) << "WebView: requested bambulab token cookie cleanup (WebKit)";
#else
    BOOST_LOG_TRIVIAL(warning)
        << "WebView: ClearBambulabTokenCookies has no implementation for the active webview backend";
#endif
}

void WebView::RecreateAll()
{
    auto dark = Slic3r::GUI::wxGetApp().dark_mode();
    wxString language_code = Slic3r::GUI::wxGetApp().current_language_code().BeforeFirst('_');
    language_code          = language_code.ToStdString();
    for (auto webView : g_webviews) {
        webView->SetUserAgent(wxString::Format("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) BBL-Slicer/v%s (%s) BBL-Language/%s",
                                               SLIC3R_VERSION, dark ? "dark" : "light", language_code.mb_str()));
        webView->Reload();
    }
}
