#ifndef slic3r_GUI_WebView_hpp_
#define slic3r_GUI_WebView_hpp_

#include <wx/webview.h>

class WebView
{
public:
    static wxWebView *CreateWebView(wxWindow *parent, wxString const &url);
    
    static void LoadUrl(wxWebView * webView, wxString const &url);

    static bool RunScript(wxWebView * webView, wxString const & msg);

    static void RecreateAll();

    // Remove WebView cookies named "token" on domains containing "bambulab".
    // Windows: WebView2 CookieManager; macOS: WKHTTPCookieStore (default data store).
    static void ClearBambulabTokenCookies();

    /*Find a user data path*/
    static wxString BuildEdgeUserDataPath();
};

#endif // !slic3r_GUI_WebView_hpp_
