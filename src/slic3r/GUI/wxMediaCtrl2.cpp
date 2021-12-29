#include "wxMediaCtrl2.h"

wxMediaCtrl2::wxMediaCtrl2()
{
}

void wxMediaCtrl2::Load(wxURI url)
{
    url = wxURI(url.BuildURI().append("&hwnd=").append(
        boost::lexical_cast<std::string>(GetHandle())));
    wxMediaCtrl::Load(url);
}

WXLRESULT wxMediaCtrl2::MSWWindowProc(WXUINT   nMsg,
                                   WXWPARAM wParam,
                                   WXLPARAM lParam)
{
    if (nMsg == WM_USER + 1000) {
        BOOST_LOG_TRIVIAL(info) << wxString((wchar_t const *) lParam).ToUTF8().data();
        return 0;
    }
    return wxMediaCtrl::MSWWindowProc(nMsg, wParam, lParam);
}
