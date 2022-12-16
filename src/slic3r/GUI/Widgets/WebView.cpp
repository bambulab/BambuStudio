#include "WebView.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/MacDarkMode.hpp"

#include <wx/webviewarchivehandler.h>
#include <wx/webviewfshandler.h>
#if wxUSE_WEBVIEW_EDGE
#include <wx/msw/webview_edge.h>
#endif
#include <wx/uri.h>
#if defined(__WIN32__) || defined(__WXMAC__)
#include "wx/private/jsscriptwrapper.h"
#endif

#ifdef __WIN32__
#include "../WebView2.h"
#elif defined __linux__
#include <gtk/gtk.h>
#define WEBKIT_API
struct WebKitWebView;
struct WebKitJavascriptResult;
extern "C" {
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
webkit_javascript_result_unref              (WebKitJavascriptResult *js_result);
}
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

class WebViewRef : public wxObjectRefData
{
public:
    WebViewRef(wxWebView *webView) : m_webView(webView) {}
    ~WebViewRef() {
        auto iter = std::find(g_webviews.begin(), g_webviews.end(), m_webView);
        assert(iter != g_webviews.end());
        if (iter != g_webviews.end())
            g_webviews.erase(iter);
    }
    wxWebView *m_webView;
};

wxWebView* WebView::CreateWebView(wxWindow * parent, wxString const & url)
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
#endif
    auto url2  = url;
#ifdef __WIN32__
    url2.Replace("\\", "/");
#endif
    if (!url2.empty()) { url2 = wxURI(url2).BuildURI(); }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": " << url2.ToUTF8();

    auto webView = wxWebView::New();
    if (webView) {
        webView->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
#ifdef __WIN32__
        webView->SetUserAgent(wxString::Format("BBL-Slicer/v%s (%s) Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
            "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/107.0.0.0 Safari/537.36 Edg/107.0.1418.52", SLIC3R_VERSION, 
            Slic3r::GUI::wxGetApp().dark_mode() ? "dark" : "light"));
        webView->Create(parent, wxID_ANY, url2, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        // We register the wxfs:// protocol for testing purposes
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("bbl")));
        // And the memory: file system
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
#else
        // With WKWebView handlers need to be registered before creation
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
        // And the memory: file system
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
        webView->Create(parent, wxID_ANY, url2, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        webView->SetUserAgent(wxString::Format("BBL-Slicer/v%s (%s) Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko)", SLIC3R_VERSION,
                                               Slic3r::GUI::wxGetApp().dark_mode() ? "dark" : "light"));
#endif
#ifdef __WXMAC__
        WKWebView * wkWebView = (WKWebView *) webView->GetNativeBackend();
        Slic3r::GUI::WKWebView_setTransparentBackground(wkWebView);
#endif
#ifndef __WIN32__
        Slic3r::GUI::wxGetApp().CallAfter([webView] {
#endif
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": begin to add script message handler for wx.";
        Slic3r::GUI::wxGetApp().set_adding_script_handler(true);
        if (!webView->AddScriptMessageHandler("wx"))
            wxLogError("Could not add script message handler");
        Slic3r::GUI::wxGetApp().set_adding_script_handler(false);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": finished add script message handler for wx.";
#ifndef __WIN32__
        });
#endif
        webView->EnableContextMenu(false);
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
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << url2.ToUTF8();
    webView->LoadURL(url2);
}

bool WebView::RunScript(wxWebView *webView, wxString const &javascript)
{
    if (Slic3r::GUI::wxGetApp().get_mode() == Slic3r::comDevelop)
        wxLogMessage("Running JavaScript:\n%s\n", javascript);

    try {
#ifdef __WIN32__
        ICoreWebView2 *   webView2 = (ICoreWebView2 *) webView->GetNativeBackend();
        if (webView2 == nullptr)
            return false;
        int               count   = 0;
        wxJSScriptWrapper wrapJS(javascript, &count);
        return webView2->ExecuteScript(wrapJS.GetWrappedCode(), NULL) == 0;
#elif defined __WXMAC__
        WKWebView * wkWebView = (WKWebView *) webView->GetNativeBackend();
        int               count   = 0;
        wxJSScriptWrapper wrapJS(javascript, &count);
        Slic3r::GUI::WKWebView_evaluateJavaScript(wkWebView, wrapJS.GetWrappedCode(), nullptr);
        return true;
#else
        WebKitWebView *wkWebView = (WebKitWebView *) webView->GetNativeBackend();
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
        return true;
#endif
    } catch (std::exception &e) {
        return false;
    }
}

void WebView::RecreateAll()
{
#ifdef __WXMSW__
    auto webviews = g_webviews;
    std::vector<wxWindow*> parents;
    std::vector<wxString> urls;
    for (auto web : webviews) {
        parents.push_back(web->GetParent());
        urls.push_back(web->GetCurrentURL());
        delete web;
    }
    assert(g_webviews.empty());
    for (int i = 0; i < parents.size(); ++i) {
        auto webView = CreateWebView(parents[i], urls[i]);
        if (webView) {
            wxCommandEvent evt(EVT_WEBVIEW_RECREATED);
            evt.SetEventObject(webView);
            wxPostEvent(parents[i], evt);
        }
    }
#else
    for (auto webView : g_webviews) {
        webView->SetUserAgent(wxString::Format("BBL-Slicer/v%s (%s) Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko)", SLIC3R_VERSION,
                                               Slic3r::GUI::wxGetApp().dark_mode() ? "dark" : "light"));
        webView->Reload();
        wxCommandEvent evt(EVT_WEBVIEW_RECREATED);
        evt.SetEventObject(webView);
        wxPostEvent(webView->GetParent(), evt);
    }
#endif
}
