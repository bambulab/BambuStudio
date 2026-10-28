//
//  wxMediaCtrl3.h
//  libslic3r_gui
//
//  Created by cmguo on 2024/6/22.
//

#ifndef wxMediaCtrl3_h
#define wxMediaCtrl3_h

#include "wx/bitmap.h"
#include "wx/uri.h"
#include "wx/mediactrl.h"
#include "wx/timer.h"
#include "../Utils/FrameBuffer.hpp"
#include <atomic>
#include <limits>

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
#include <wx/image.h> // full type needed for the m_idle_image_raw member
#include "Printer/BambuTunnel.h"
#include "Printer/LiveViewTrackContext.h"

#ifdef _WIN32
typedef wxBitmap PlayFrame;
#else
typedef wxImage PlayFrame;
#endif

class AVVideoDecoder;
class wxGraphicsContext;

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

    wxSize GetIdleImageSize() const { return m_idle_image_size; }

    void SetConstrainByAspectRatio(bool constrain) { m_constrain_by_aspect_ratio = constrain; }
    bool GetConstrainByAspectRatio() const { return m_constrain_by_aspect_ratio; }
    void SuppressPauseIcon(bool suppress);
    void SetTrackChannel(const BambuLiveViewTrack::ChannelInfo& info);

    void UpdateSessionStat();

protected:
    DECLARE_EVENT_TABLE()

    void paintEvent(wxPaintEvent &evt);

    void DrawLiveviewPause(wxGraphicsContext *gc, double bg_x, double bg_y, double bg_w, double bg_h);

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
    wxImage  m_idle_image_raw; // last idle image applied via the wxImage overload (UI thread only)
    wxSize   m_idle_image_size = wxDefaultSize;
    wxString m_watermark_text;
    wxString m_live_badge_text;
    wxBitmap m_pause_bitmap;     // cached rasterized pause icon (UI thread only)
    wxSize   m_pause_bitmap_size;
    bool     m_suppress_pause_icon = false; // hide the pause placeholder from INITIALIZING onward
    wxMediaState m_state  = wxMEDIASTATE_STOPPED;
    int m_error  = 0;
    wxSize m_video_size = wxDefaultSize;
    wxSize m_frame_size = wxDefaultSize;
    PlayFrame m_frame;
    bool m_frame_is_default_bg = false; // whether m_frame currently holds the default background (guarded by m_ui_mutex)
    double m_zoom = 1.0;   // digital zoom factor for the live view (mouse wheel)
    double m_view_cx = std::numeric_limits<double>::quiet_NaN();
    double m_view_cy = std::numeric_limits<double>::quiet_NaN();
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
