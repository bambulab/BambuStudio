#include "BreathingDot.hpp"
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/image.h>
#include <cmath>
#include <algorithm>

namespace {

wxColour sample_icon_colour(wxWindow *parent, const std::string &icon_name)
{
    ScalableBitmap bmp(parent, icon_name, 16);
    wxImage        img = bmp.bmp().ConvertToImage();
    if (!img.IsOk() || img.GetWidth() == 0 || img.GetHeight() == 0)
        return wxColour();
    const int cx = img.GetWidth() / 2;
    const int cy = img.GetHeight() / 2;
    return wxColour(img.GetRed(cx, cy), img.GetGreen(cx, cy), img.GetBlue(cx, cy));
}

unsigned char lerp_channel(unsigned char a, unsigned char b, double t)
{
    return static_cast<unsigned char>(std::lround(a + (b - a) * t));
}

} // namespace

BreathingDot::BreathingDot(wxWindow *parent, const std::string &dim_icon, const std::string &bright_icon, int diameter_dip, int period_ms)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_dim(sample_icon_colour(parent, dim_icon))
    , m_bright(sample_icon_colour(parent, bright_icon))
    , m_diameter_dip(diameter_dip)
    , m_period_ms(period_ms)
    , m_timer(this)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &BreathingDot::OnPaint, this);
    Bind(wxEVT_TIMER, &BreathingDot::OnTimer, this);
    Hide();
}

wxSize BreathingDot::DoGetBestSize() const
{
    const int d = FromDIP(m_diameter_dip);
    return wxSize(d, d);
}

void BreathingDot::Start()
{
    if (m_playing)
        return;
    m_playing    = true;
    m_start_time = std::chrono::steady_clock::now();
    m_timer.Start(33);
    Refresh();
}

void BreathingDot::Stop()
{
    if (!m_playing)
        return;
    m_playing = false;
    m_timer.Stop();
}

void BreathingDot::OnTimer(wxTimerEvent &)
{
    Refresh();
}

void BreathingDot::OnPaint(wxPaintEvent &)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();

    if (!m_playing)
        return;

    const auto   elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_start_time).count();
    const double phase      = std::fmod(static_cast<double>(elapsed_ms), static_cast<double>(m_period_ms)) / m_period_ms;
    const double t          = phase < 0.5 ? phase / 0.5 : 1.0 - (phase - 0.5) / 0.5;

    const wxColour fill(lerp_channel(m_dim.Red(), m_bright.Red(), t), lerp_channel(m_dim.Green(), m_bright.Green(), t),
                         lerp_channel(m_dim.Blue(), m_bright.Blue(), t));

    wxGCDC gdc(dc);
    gdc.SetPen(*wxTRANSPARENT_PEN);
    gdc.SetBrush(wxBrush(fill));
    const wxSize sz = GetClientSize();
    const int    d  = std::min(sz.x, sz.y);
    gdc.DrawEllipse((sz.x - d) / 2, (sz.y - d) / 2, d, d);
}
