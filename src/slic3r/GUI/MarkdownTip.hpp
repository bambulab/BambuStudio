#ifndef slic3r_MarkdownTip_hpp_
#define slic3r_MarkdownTip_hpp_

#include <wx/webview.h>
#include <wx/frame.h>
#include <wx/timer.h>

#ifdef __linux__
class wxWebView;
class wxWebViewEvent;
#endif

namespace Slic3r { namespace GUI {

class MarkdownTip : wxFrame
{
public:
    static bool ShowTip(std::string const & tip, wxPoint pos);

private:
    MarkdownTip();

    bool ShowTip(wxPoint pos, std::string const& tip);

    std::string LoadTip(std::string const& tip);

private:
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
