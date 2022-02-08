//
//  MediaPlayCtrl.h
//  libslic3r_gui
//
//  Created by cmguo on 2021/12/7.
//

#ifndef MediaPlayCtrl_h
#define MediaPlayCtrl_h

#include <wx/panel.h>
#include "wxMediaCtrl2.h"

class Button;
class Label;

namespace Slic3r {

class MachineObject;

namespace GUI {

class MediaPlayCtrl : public wxPanel
{
public:
    MediaPlayCtrl(wxWindow * parent, wxMediaCtrl2 * media_ctrl);

    void SetMachineObject(MachineObject * obj);

protected:
    void onStateChanged(wxMediaEvent & event);

    void Play();

    void Stop();

    void TogglePlay();

    void SetStatus(wxString const & msg);

private:
    static constexpr wxMediaState MEDIASTATE_IDLE = (wxMediaState) 3;
    static constexpr wxMediaState MEDIASTATE_INITIALIZING = (wxMediaState) 4;
    static constexpr wxMediaState MEDIASTATE_LOADING = (wxMediaState) 5;
    static constexpr wxMediaState MEDIASTATE_BUFFERING = (wxMediaState) 6;

    wxMediaCtrl2 * m_media_ctrl;
    wxMediaState m_last_state = MEDIASTATE_IDLE;
    MachineObject* m_machine = nullptr;
    wxString m_url;

    ::Button * m_button_play;
    ::Label * m_label_status;
};

}}

#endif /* MediaPlayCtrl_h */
