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

    void Load(wxURI url);

    void Play();

    void Stop();

    void SetIdleImage(wxString const & image, wxString const & watermark_text = {});
    void SetIdleImage(const wxImage &image, wxString const & watermark_text = {});

    wxMediaState GetState();

    int GetLastError();

    wxSize GetVideoSize();

protected:
    DECLARE_EVENT_TABLE()

    void paintEvent(wxPaintEvent &evt);

    wxSize DoGetBestSize() const override;

    void DoSetSize(int x, int y, int width, int height, int sizeFlags) override;

    static void bambu_log(void *ctx, int level, tchar const *msg);

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
    std::shared_ptr<wxURI> m_url;
    std::mutex m_mutex;
    std::mutex m_ui_mutex;
    std::condition_variable m_cond;
    std::thread m_thread;

    const int m_buffer_time = 300;
    Slic3r::Utils::FixedOverwriteBuffer<PlayFrame> m_frame_buffer;
    std::thread m_get_frame_thread;
    std::atomic<bool> m_get_frame_exit{false};

    wxTimer m_render_timer;
    std::atomic<bool> m_need_refresh{false};
};

#endif

#endif /* wxMediaCtrl3_h */
