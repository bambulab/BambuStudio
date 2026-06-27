//
//  wxMediaCtrl3.h
//  libslic3r_gui
//
//  Created by cmguo on 2024/6/22.
//

#ifndef wxMediaCtrl3_h
#define wxMediaCtrl3_h

#include "wx/uri.h"
#include "wx/mediactrl.h"
#include "wx/timer.h"
#include "../Utils/FrameBuffer.hpp"
#include <atomic>

wxDECLARE_EVENT(EVT_MEDIA_CTRL_STAT, wxCommandEvent);
wxDECLARE_EVENT(EVT_MEDIA_CTRL_FIRST_FRAME, wxCommandEvent);
wxDECLARE_EVENT(EVT_MEDIA_CTRL_SESSION_END, wxCommandEvent);

#ifndef __WXMAC__
struct FirstFrameInfo {
    int     first_frame_cost_ms        = 0;   // play_start -> render
    int64_t first_packet_time_ms       = 0;   // epoch ms of first ReadSample success
    int64_t decode_first_frame_time_ms = 0;   // epoch ms of first got_frame
    int64_t render_first_frame_time_ms = 0;   // epoch ms of first frame pushed to UI
    int     video_codec                = 0;   // sub_type: AVC1=0 / MJPG=1
    int     resolution_width           = 0;
    int     resolution_height          = 0;
};
#endif

void wxMediaCtrl_OnSize(wxWindow * ctrl, wxSize const & videoSize, int width, int height);

#ifdef __WXMAC__

#include "wxMediaCtrl2.h"
#define wxMediaCtrl3 wxMediaCtrl2

#else

#define BAMBU_DYNAMIC
#include <condition_variable>
#include <thread>
#ifndef _WIN32
#include <wx/image.h>
#endif
#include "Printer/BambuTunnel.h"
#include "Printer/LiveViewTrackContext.h"

#ifdef _WIN32
typedef wxBitmap PlayFrame;
#else
typedef wxImage PlayFrame;
#endif

class AVVideoDecoder;

class wxMediaCtrl3 : public wxWindow, BambuLib
{
public:
    wxMediaCtrl3(wxWindow *parent);

    ~wxMediaCtrl3();

    void Load(wxURI url, std::chrono::system_clock::time_point play_start_time = {});

    std::chrono::system_clock::time_point m_play_start_time;

    void Play();

    void Stop();

    void SetIdleImage(wxString const & image, wxString const & watermark_text = {});
    void SetIdleImage(const wxImage &image, wxString const & watermark_text = {});

    wxMediaState GetState();

    int GetLastError();

    wxSize GetVideoSize();

    void SetConstrainByAspectRatio(bool constrain) { m_constrain_by_aspect_ratio = constrain; }
    bool GetConstrainByAspectRatio() const { return m_constrain_by_aspect_ratio; }
    void SetTrackChannel(const BambuLiveViewTrack::ChannelInfo& info);

    void UpdateSessionStat();

protected:
    DECLARE_EVENT_TABLE()

    void paintEvent(wxPaintEvent &evt);

    void mouseWheelEvent(wxMouseEvent &evt);

    wxSize DoGetBestSize() const override;

    void DoSetSize(int x, int y, int width, int height, int sizeFlags) override;

    static void bambu_log(void *ctx, int level, tchar const *msg);

    static void bambu_streaminfo(void *ctx, Bambu_StreamInfo *info);

    static void on_player_track_event(void *ctx, const PlayerEventC *event);

    void PlayThread();

    void NotifyStopped();

    void GetFrameThread(int frame_rate);

    void OnRenderTimer(wxTimerEvent &evt);

private:
    wxString m_idle_image;
    wxString m_watermark_text;
    wxMediaState m_state  = wxMEDIASTATE_STOPPED;
    int m_error  = 0;
    wxSize m_video_size = wxDefaultSize;
    wxSize m_frame_size = wxDefaultSize;
    PlayFrame m_frame;
    double m_zoom = 1.0;   // digital zoom factor for the live view (mouse wheel)
    std::shared_ptr<wxURI> m_url;
    std::atomic<Bambu_Tunnel> m_tunnel{nullptr};
    std::mutex m_mutex;
    std::mutex m_ui_mutex;
    std::condition_variable m_cond;
    std::thread m_thread;

    bool m_constrain_by_aspect_ratio{true};
    const int m_buffer_time = 300;
    Slic3r::Utils::FixedOverwriteBuffer<PlayFrame> m_frame_buffer;
    std::thread m_get_frame_thread;
    std::atomic<bool> m_get_frame_exit{false};

    wxTimer m_render_timer;
    std::atomic<bool> m_need_refresh{false};

    struct PendingStreamInfo {
        std::mutex        mutex;
        Bambu_StreamInfo  info{};
        std::atomic<bool> changed{false};
    } m_pending_stream;

    FirstFrameInfo    m_first_frame_info;

public:
    BambuLiveViewTrack::ChannelInfo m_track_channel;

    Bambu_SessionStat m_session_stat = {};
    int               m_video_decode_error_count = 0;
    int               m_render_error_count       = 0;
};

#endif

#endif /* wxMediaCtrl3_h */
