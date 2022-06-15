//
//  wxMediaCtrl2.h
//  libslic3r_gui
//
//  Created by cmguo on 2021/12/7.
//

#ifndef wxMediaCtrl2_h
#define wxMediaCtrl2_h

#include "wx/uri.h"
#include "wx/mediactrl.h"

#ifdef __WXMAC__

class wxMediaCtrl2 : public wxWindow
{
public:
    wxMediaCtrl2(wxWindow * parent, wxSize const & size);
    
    void Load(wxURI url);
    
    void Play();
    
    void Stop();
    
    wxMediaState GetState() const;
    
    int GetLastError() const { return m_error; }

    static constexpr wxMediaState MEDIASTATE_BUFFERING = (wxMediaState) 6;

protected:
    wxSize DoGetBestSize() const override;
    
private:
    void * m_player;
    wxMediaState m_state;
    int m_error = 0;
};

#else

class wxMediaCtrl2 : public wxMediaCtrl
{
public:
    wxMediaCtrl2();

    void Load(wxURI url);

    int GetLastError() const { return m_error; }
    wxSize GetBestSize() { return DoGetBestSize(); };

protected:
#ifdef __WIN32__
    WXLRESULT MSWWindowProc(WXUINT   nMsg,
                            WXWPARAM wParam,
                            WXLPARAM lParam) override;
#endif

private:
    int m_error = 0;
};

#endif

#endif /* wxMediaCtrl2_h */
