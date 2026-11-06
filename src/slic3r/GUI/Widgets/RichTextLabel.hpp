#ifndef slic3r_GUI_RichTextLabel_hpp_
#define slic3r_GUI_RichTextLabel_hpp_

#include <wx/panel.h>
#include <wx/font.h>
#include <wx/colour.h>

#include <vector>

// A run of body text; bold and link segments may be freely mixed.
struct RichSegment {
    wxString text;
    bool     bold = false;
    wxString url;
};

class RichTextLabel : public wxPanel
{
public:
    RichTextLabel(wxWindow *parent, std::vector<RichSegment> segments, int width, bool wrap = false);

    void SetSegments(std::vector<RichSegment> segments);
    void SetFonts(const wxFont &regular, const wxFont &bold);
    void SetColours(const wxColour &text, const wxColour &link);

    static std::vector<RichSegment> segments_from_template(const wxString &tmpl, const std::vector<RichSegment> &args);

private:
    struct TextRun {
        wxString text;
        wxPoint  position;
        bool     bold;
        bool     is_link;
    };

    struct LinkRect {
        wxRect   rect;
        wxString url;
    };

    wxFont   font_for(bool bold, bool is_link) const;
    void     layout_text();
    void     on_paint(wxPaintEvent &);
    void     on_motion(wxMouseEvent &event);
    wxString hit_link(const wxPoint &point) const;

    std::vector<RichSegment> m_segments;
    int                      m_width;
    bool                     m_wrap;
    wxFont                   m_regular_font;
    wxFont                   m_bold_font;
    bool                     m_custom_colours{false};
    wxColour                 m_text_colour;
    wxColour                 m_link_colour;
    bool                     m_link_hovered{false};
    std::vector<TextRun>     m_runs;
    std::vector<LinkRect>    m_link_rects;
};

#endif // !slic3r_GUI_RichTextLabel_hpp_
