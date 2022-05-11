#include "WebView.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include <wx/webviewarchivehandler.h>
#include <wx/webviewfshandler.h>
#include <wx/msw/webview_edge.h>

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

    auto webView = wxWebView::New();
    if (webView) {
#ifdef __WIN32__
        webView->SetUserAgent(wxString::Format("BBL-Slicer/v%s", SLIC3R_VERSION));
        webView->Create(parent, wxID_ANY, url, wxDefaultPosition, wxDefaultSize);
        //We register the wxfs:// protocol for testing purposes
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("bbl")));
        //And the memory: file system
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
#else
        // With WKWebView handlers need to be registered before creation
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
        // And the memory: file system
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
        webView->Create(parent, wxID_ANY, url, wxDefaultPosition, wxDefaultSize);
        webView->SetUserAgent(wxString::Format("BBL-Slicer/v%s", SLIC3R_VERSION));
#endif
#ifdef __WXMAC__
        Slic3r::GUI::wxGetApp().CallAfter([webView] {
#endif
        if (!webView->AddScriptMessageHandler("wx"))
            wxLogError("Could not add script message handler");
#ifdef __WXMAC__
                             });
#endif
        webView->EnableContextMenu(false);
    }
    return webView;
}
