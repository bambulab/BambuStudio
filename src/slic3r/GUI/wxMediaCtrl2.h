//
//  wxMediaCtrl2.h
//  libslic3r_gui
//
//  Created by cmguo on 2021/12/7.
//

#ifndef wxMediaCtrl2_h
#define wxMediaCtrl2_h

#include "wx/uri.h"

#ifdef __WXMAC__

#include "wx/mediactrl.h"

class wxMediaCtrl2 : public wxWindow
{
public:
    wxMediaCtrl2(wxWindow * parent);
    
    void Load(wxURI url);
    
    void Play();
    
    void Stop();
    
    wxMediaState GetState() const;
    
protected:
    wxSize DoGetBestSize() const override;
    
private:
    void * m_player;
    wxMediaState m_state;
};

#else

class wxMediaCtrl2 : public wxMediaCtrl
{
public:
    wxMediaCtrl2();

    void Load(wxURI url);

protected:
    WXLRESULT MSWWindowProc(WXUINT   nMsg,
                            WXWPARAM wParam,
                            WXLPARAM lParam) override;
};

#endif

#endif /* wxMediaCtrl2_h */
