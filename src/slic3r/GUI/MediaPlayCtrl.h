//
//  MediaPlayCtrl.h
//  libslic3r_gui
//
//  Created by cmguo on 2021/12/7.
//

#ifndef MediaPlayCtrl_h
#define MediaPlayCtrl_h

#define USE_WX_MEDIA_CTRL_2 0

#if USE_WX_MEDIA_CTRL_2
#include "wxMediaCtrl2.h"
#define wxMediaCtrl3 wxMediaCtrl2
#else
#include "wxMediaCtrl3.h"
#endif

#include <wx/panel.h>
#include <ostream>

#include <boost/thread.hpp>
#include <boost/thread/condition_variable.hpp>

#include <chrono>
#include <deque>
#include <set>

class Button;
class Label;

namespace Slic3r {

class MachineObject;

namespace GUI {

// Extended media state enum that includes wxMediaState values plus streaming-specific states
enum class MediaStreamState {
    STOPPED = 0,        // Maps to wxMEDIASTATE_STOPPED
    PAUSED = 1,         // Maps to wxMEDIASTATE_PAUSED
    PLAYING = 2,        // Maps to wxMEDIASTATE_PLAYING
    IDLE = 3,           // Ready to play, not streaming
    INITIALIZING = 4,   // Starting up the stream
    LOADING = 5,        // Loading video data
    BUFFERING = 6       // Waiting for buffered data
};

// Helper function to convert wxMediaState to MediaStreamState
inline MediaStreamState from_wxMediaState(wxMediaState state) {
    return static_cast<MediaStreamState>(static_cast<int>(state));
}

// Output stream operator for logging
inline std::ostream& operator<<(std::ostream& os, MediaStreamState state) {
    return os << static_cast<int>(state);
}

class MediaPlayCtrl : public wxPanel
{
public:
    MediaPlayCtrl(wxWindow *parent, wxMediaCtrl3 *media_ctrl, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);

    ~MediaPlayCtrl();

    void SetMachineObject(MachineObject * obj);

    bool IsStreaming() const;

    void ToggleStream();

    void msw_rescale();

    void jump_to_play();

protected:
    void onStateChanged(wxMediaEvent & event);

    void Play();

    void Stop(wxString const &msg = {}, wxString const &msg2 = {});

    void TogglePlay();

    void SetStatus(wxString const &msg, bool hyperlink = true);

private:
    void load();

    void on_show_hide(wxShowEvent & evt);

    void media_proc();

    static bool start_stream_service(bool *need_install = nullptr);

    static bool get_stream_url(std::string *url = nullptr);

private:
    // token
    std::shared_ptr<int> m_token = std::make_shared<int>(0);

    wxMediaCtrl3 * m_media_ctrl;
    MediaStreamState m_last_state = MediaStreamState::IDLE;
    std::string m_machine;
    int m_lan_proto = 0;
    std::string m_lan_ip;
    std::string m_lan_user;
    std::string m_lan_passwd;
    std::string m_dev_ver;
    std::string m_tutk_state;
    bool m_camera_exists = false;
    bool m_lan_mode = false;
    int m_remote_proto = 0;
    bool m_device_busy = false;
    bool m_disable_lan = false;
    wxString m_url;

    std::deque<wxString> m_tasks;
    boost::mutex m_mutex;
    boost::condition_variable m_cond;
    boost::thread m_thread;

    bool m_streaming = false;
    bool m_user_triggered = false;
    int m_failed_retry = 0;
    int m_failed_code = 0;
    std::vector<double> m_stat;
    std::set<int> m_last_failed_codes;
    wxDateTime    m_last_user_play;
    wxDateTime    m_next_retry;
    std::chrono::system_clock::time_point m_play_timer;
    int           m_print_idle = 0;
    int           m_load_duration = 0;

    ::Button *m_button_play;
    ::Label * m_label_stat;
    ::Label * m_label_status;
};

}}

#endif /* MediaPlayCtrl_h */
