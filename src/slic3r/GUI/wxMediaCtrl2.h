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
#include <chrono>
#ifdef __WXMAC__
#include "Printer/LiveViewTrackContext.h"
#define BAMBU_DYNAMIC
#include "Printer/BambuTunnel.h"
#endif

wxDECLARE_EVENT(EVT_MEDIA_CTRL_STAT, wxCommandEvent);
wxDECLARE_EVENT(EVT_MEDIA_CTRL_FIRST_FRAME, wxCommandEvent);
wxDECLARE_EVENT(EVT_MEDIA_CTRL_SESSION_END, wxCommandEvent);

#ifdef __WXMAC__
struct FirstFrameInfo {
    int     first_frame_cost_ms        = 0;
    int64_t first_packet_time_ms       = 0;
    int64_t decode_first_frame_time_ms = 0;
    int64_t render_first_frame_time_ms = 0;
    int     video_codec                = 0;
    int     resolution_width           = 0;
    int     resolution_height          = 0;
};
#endif


void wxMediaCtrl_OnSize(wxWindow * ctrl, wxSize const & videoSize, int width, int height);

#ifdef __WXMAC__

class wxMediaCtrl2 : public wxWindow
{
public:
    wxMediaCtrl2(wxWindow * parent);

    ~wxMediaCtrl2();

    void Load(wxURI url, std::chrono::system_clock::time_point play_start_time = {});

    void Play();

    void Stop();

    void SetIdleImage(wxString const & image, wxString const & watermark_text = {});
    void SetIdleImage(const wxImage &image, wxString const & watermark_text = {});

    wxMediaState GetState() const;

    wxSize GetVideoSize() const;

    int GetLastError() const { return m_error; }

    void SetConstrainByAspectRatio(bool constrain) { m_constrain_by_aspect_ratio = constrain; }
    bool GetConstrainByAspectRatio() const { return m_constrain_by_aspect_ratio; }

    void SetTrackChannel(const BambuLiveViewTrack::ChannelInfo& info);
    void UpdateSessionStat() {}  // data filled by session-end callback, nothing to do here

    static inline wxMediaState MEDIASTATE_BUFFERING = (wxMediaState) 6;

protected:
    void DoSetSize(int x, int y, int width, int height, int sizeFlags) override;

    static void bambu_log(void const *ctx, int level, char const *msg);

    void NotifyStopped();

private:
    void create_player();
    void updateIdleLayer();
    void updateWatermarkLayer();
    void removeIdleLayer();

    void * m_player = nullptr;
    wxMediaState m_state = wxMEDIASTATE_STOPPED;
    int          m_error  = 0;
    wxSize       m_video_size{16, 9};
    bool         m_constrain_by_aspect_ratio{true};

    wxString m_idle_image;
    wxString m_watermark_text;
    void *   m_idle_layer = nullptr;      // CALayer* for idle image
    void *   m_watermark_layer = nullptr;  // CATextLayer* for watermark

public:
    std::chrono::system_clock::time_point m_play_start_time;
    FirstFrameInfo                    m_first_frame_info;
    BambuLiveViewTrack::ChannelInfo   m_track_channel;
    Bambu_SessionStat m_session_stat          = {};
    int               m_video_decode_error_count = 0;
    int               m_render_error_count       = 0;
};

#else

class wxMediaCtrl2 : public wxMediaCtrl
{
public:
    wxMediaCtrl2(wxWindow *parent);

    void Load(wxURI url);

    void Play();

    void Stop();

    void SetIdleImage(wxString const & image, wxString const & watermark_text = {});
    void SetIdleImage(const wxImage &image, wxString const & watermark_text = {});

    int GetLastError() const;

    wxSize GetVideoSize() const;

    void SetConstrainByAspectRatio(bool constrain) { m_constrain_by_aspect_ratio = constrain; }
    bool GetConstrainByAspectRatio() const { return m_constrain_by_aspect_ratio; }

protected:
    wxSize DoGetBestSize() const override;

    void DoSetSize(int x, int y, int width, int height, int sizeFlags) override;

#ifdef __WIN32__
    WXLRESULT MSWWindowProc(WXUINT   nMsg,
                            WXWPARAM wParam,
                            WXLPARAM lParam) override;
#endif

private:
    wxString m_idle_image;
    int      m_error = 0;
    bool     m_loaded = false;
    wxSize   m_video_size{16, 9};
    bool     m_constrain_by_aspect_ratio{true};
};

#endif

#endif /* wxMediaCtrl2_h */
