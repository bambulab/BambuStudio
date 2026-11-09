#pragma once

#include "../wxExtensions.hpp"
#include <wx/window.h>
#include <wx/timer.h>
#include <chrono>
#include <string>

class BreathingDot : public wxWindow
{
public:
    BreathingDot(wxWindow *parent, const std::string &dim_icon, const std::string &bright_icon, int diameter_dip = 6, int period_ms = 2000);

    void Start();
    void Stop();
    bool IsPlaying() const { return m_playing; }

protected:
    wxSize DoGetBestSize() const override;

private:
    void OnPaint(wxPaintEvent &evt);
    void OnTimer(wxTimerEvent &evt);

    wxColour m_dim;
    wxColour m_bright;
    int      m_diameter_dip;
    int      m_period_ms;
    bool     m_playing{false};
    wxTimer  m_timer;
    std::chrono::steady_clock::time_point m_start_time;
};
