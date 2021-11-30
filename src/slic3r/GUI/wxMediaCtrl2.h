//
//  wxMediaCtrl2.h
//  libslic3r_gui
//
//  Created by cmguo on 2021/12/7.
//

#ifndef wxMediaCtrl2_h
#define wxMediaCtrl2_h

#include "wx/statbmp.h"
#include "wx/uri.h"

class wxMediaCtrl2: public wxWindow
{
public:
    wxMediaCtrl2(wxWindow * parent);
    
    void Load(wxURI url);
    
    void Play();
    
    void Stop();
    
protected:
    wxSize DoGetBestSize() const override;
    
private:
    void * m_player;
    NSView * view;
};

#endif /* wxMediaCtrl2_h */
