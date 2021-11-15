#ifndef slic3r_MarkdownTip_hpp_
#define slic3r_MarkdownTip_hpp_

#include <wx/frame.h>
#include <wx/timer.h>

#ifdef __linux__
class wxWebView : public wxWindow {};
class wxWebViewEvent;
#else
#include <wx/webview.h>
#endif

namespace Slic3r { namespace GUI {

class MarkdownTip : public wxFrame
{
public:
    static bool ShowTip(std::string const & tip, wxPoint pos);

    static wxWindow* AttachTo(wxWindow * parent);

private:
    static MarkdownTip& markdownTip();

    MarkdownTip();

    bool ShowTip(wxPoint pos, std::string const& tip);

    std::string LoadTip(std::string const& tip);

    void RunScript(std::string const& script);

private:
    wxWebView* CreateTipView(wxWindow* parent);

    void OnLoaded(wxWebViewEvent& event);

    void OnTitleChanged(wxWebViewEvent& event);

    void OnError(wxWebViewEvent& event);

    void OnTimer(wxTimerEvent& event);
    
private:
    wxWebView* _tipView = NULL;
    std::string _lastTip;
    std::string _pendingScript = " ";
    wxPoint _requestPos;
    double _lastHeight = 0;
    wxTimer* _timer;
    bool _hide = false;
};

}
}

#endif
