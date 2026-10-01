#ifndef slic3r_GUI_TextureImportUi_hpp_
#define slic3r_GUI_TextureImportUi_hpp_

#include "Widgets/Label.hpp"

#include <wx/dc.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#ifdef __WXMSW__
#include <wx/dcbuffer.h>
#endif
#include <wx/panel.h>

#include <array>
#include <cmath>

class Button;

namespace Slic3r {
namespace GUI {

bool texture_import_is_dark();
wxColour texture_import_dark_or(const wxColour& light, const wxColour& dark);
std::array<std::size_t, 3> texture_import_rgb_from_rgba(const std::array<float, 4>& rgba);

// Match StaticBox::render: wxPaintDC everywhere; MSW uses the shared
// AutoBuffered buffer + wxGCDC so rounded rects stay AA without allocating
// a new wxBitmap on every paint.
template<typename PaintFn>
void texture_import_paint(wxWindow* win, PaintFn&& paint)
{
#ifdef __WXMSW__
    wxAutoBufferedPaintDC dc(win);
    const wxSize size = win->GetClientSize();
    if (size.x <= 0 || size.y <= 0)
        return;
    wxGCDC gcdc(dc);
    paint(gcdc);
#else
    wxPaintDC dc(win);
    paint(dc);
#endif
}

void texture_import_style_primary_button(Button* btn);
void texture_import_style_secondary_button(Button* btn);

class PreviewTagPanel : public wxPanel
{
public:
    // "behind" is what shows through the cut-away top corners: the tag floats
    // on the GL canvas, whose clear color matches the preview background.
    PreviewTagPanel(wxWindow* parent, const wxString& label, const wxColour& bg, const wxColour& fg,
                    const wxColour& behind)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE)
        , m_label(label)
        , m_bg(bg)
        , m_fg(fg)
        , m_behind(behind)
    {
        SetFont(Label::Body_14);
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &PreviewTagPanel::OnPaint, this);
    }

    void set_label(const wxString& label)
    {
        if (m_label == label)
            return;
        m_label = label;
        InvalidateBestSize();
        Refresh();
    }

    void set_theme(const wxColour& bg, const wxColour& fg, const wxColour& behind)
    {
        m_bg = bg;
        m_fg = fg;
        m_behind = behind;
        Refresh();
    }

    wxSize DoGetBestSize() const override
    {
        wxClientDC dc(const_cast<PreviewTagPanel*>(this));
        dc.SetFont(GetFont());
        const wxSize text = dc.GetTextExtent(m_label.IsEmpty() ? " " : m_label);
        return wxSize(text.x + FromDIP(16), FromDIP(23));
    }

private:
    int bottom_radius() const { return FromDIP(4); }

    void OnPaint(wxPaintEvent&)
    {
        texture_import_paint(this, [this](wxDC& dc) {
            const wxSize sz = GetClientSize();
            dc.SetBackground(wxBrush(m_behind));
            dc.Clear();

            dc.SetBrush(wxBrush(m_bg));
            dc.SetPen(*wxTRANSPARENT_PEN);
            const int radius = std::max(1, bottom_radius());
            dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, radius);
            dc.DrawRectangle(0, 0, sz.x, sz.y / 2 + 1);
            dc.SetTextForeground(m_fg);
            dc.SetFont(GetFont());
            const wxSize text = dc.GetTextExtent(m_label);
            dc.DrawText(m_label, std::max(0, (sz.x - text.x) / 2), std::max(0, (sz.y - text.y) / 2));
        });
    }

    wxString m_label;
    wxColour m_bg;
    wxColour m_fg;
    wxColour m_behind;
};

}} // namespace Slic3r::GUI

#endif
