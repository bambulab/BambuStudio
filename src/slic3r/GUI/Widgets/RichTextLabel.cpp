#include "RichTextLabel.hpp"
#include "Label.hpp"
#include "StateColor.hpp"

#include <wx/dcbuffer.h>
#include <wx/utils.h>

#include <algorithm>

RichTextLabel::RichTextLabel(wxWindow *parent, std::vector<RichSegment> segments, int width, bool wrap)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(width, 1), wxBORDER_NONE)
    , m_segments(std::move(segments))
    , m_width(width)
    , m_wrap(wrap)
    , m_regular_font(Label::Body_14)
    , m_bold_font(Label::Head_14)
    , m_text_colour(ThemeColor::TextPrimary)
    , m_link_colour(ThemeColor::BrandGreen)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(parent->GetBackgroundColour());
    SetCursor(wxCURSOR_ARROW);
    layout_text();

    Bind(wxEVT_PAINT, &RichTextLabel::on_paint, this);
    Bind(wxEVT_MOTION, &RichTextLabel::on_motion, this);
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) {
        m_link_hovered = false;
        SetCursor(wxCURSOR_ARROW);
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &event) {
        const wxString url = hit_link(event.GetPosition());
        if (!url.empty())
            wxLaunchDefaultBrowser(url);
    });
}

void RichTextLabel::SetSegments(std::vector<RichSegment> segments)
{
    m_segments = std::move(segments);
    layout_text();
    Refresh();
}

void RichTextLabel::SetFonts(const wxFont &regular, const wxFont &bold)
{
    m_regular_font = regular;
    m_bold_font    = bold;
    layout_text();
    Refresh();
}

void RichTextLabel::SetColours(const wxColour &text, const wxColour &link)
{
    m_custom_colours = true;
    m_text_colour    = text;
    m_link_colour    = link;
    Refresh();
}

std::vector<RichSegment> RichTextLabel::segments_from_template(const wxString &tmpl, const std::vector<RichSegment> &args)
{
    std::vector<RichSegment> segments;
    size_t pos = 0;
    while (pos < tmpl.length()) {
        // Find the earliest remaining "{i}" placeholder.
        size_t next     = wxString::npos;
        size_t next_len = 0;
        size_t next_arg = 0;
        for (size_t i = 0; i < args.size(); ++i) {
            const wxString placeholder = wxString::Format("{%d}", int(i));
            const size_t   found       = tmpl.find(placeholder, pos);
            if (found != wxString::npos && (next == wxString::npos || found < next)) {
                next     = found;
                next_len = placeholder.length();
                next_arg = i;
            }
        }
        if (next == wxString::npos) {
            segments.push_back({tmpl.Mid(pos), false, wxString()});
            break;
        }
        if (next > pos)
            segments.push_back({tmpl.Mid(pos, next - pos), false, wxString()});
        segments.push_back(args[next_arg]);
        pos = next + next_len;
    }
    return segments;
}

wxFont RichTextLabel::font_for(bool bold, bool is_link) const
{
    const wxFont &f = bold ? m_bold_font : m_regular_font;
    return is_link ? f.Underlined() : f;
}

void RichTextLabel::layout_text()
{
    m_runs.clear();
    m_link_rects.clear();
    m_link_hovered = false;

    wxClientDC dc(this);
    dc.SetFont(m_regular_font);
    const int line_height = dc.GetCharHeight();
    int       x           = 0;
    int       y           = 0;
    int       max_x       = 0;

    auto place_token = [&](const wxString &token, bool bold, bool is_link, const wxString &url) {
        if (token.empty()) return;
        dc.SetFont(font_for(bold, is_link));
        const wxSize size     = dc.GetTextExtent(token);
        const bool   is_space = token == " " || token == "\t";
        if (m_wrap && !is_space && x > 0 && x + size.x > m_width) {
            x = 0;
            y += line_height;
        }
        if (is_space && x == 0) return;
        m_runs.push_back({token, wxPoint(x, y), bold, is_link});
        if (is_link)
            m_link_rects.push_back({wxRect(x, y, size.x, line_height), url});
        x += size.x;
        max_x = std::max(max_x, x);
    };

    auto append_segment = [&](const RichSegment &seg) {
        const bool is_link = !seg.url.empty();
        wxString   token;
        auto       flush_token = [&]() {
            if (!token.empty()) {
                place_token(token, seg.bold, is_link, seg.url);
                token.clear();
            }
        };

        for (const wxUniChar ch : seg.text) {
            if (ch == '\n') {
                flush_token();
                x = 0;
                y += line_height;
            } else if (ch == ' ' || ch == '\t') {
                flush_token();
                wxString whitespace;
                whitespace += ch;
                place_token(whitespace, seg.bold, is_link, seg.url);
            } else if (ch.GetValue() >= 0x2E80) {
                // CJK and wider: every character is its own token, i.e. a valid break point.
                flush_token();
                wxString character;
                character += ch;
                place_token(character, seg.bold, is_link, seg.url);
            } else {
                token += ch;
            }
        }
        flush_token();
    };

    for (const auto &seg : m_segments)
        append_segment(seg);

    const int width  = m_wrap ? m_width : std::max(m_width, max_x);
    const int height = std::max(line_height, y + line_height);
    SetMinSize(wxSize(width, height));
    SetMaxSize(wxSize(width, height));
}

void RichTextLabel::on_paint(wxPaintEvent &)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    for (const auto &run : m_runs) {
        const wxColour &colour = run.is_link ? m_link_colour : m_text_colour;
        dc.SetFont(font_for(run.bold, run.is_link));
        dc.SetTextForeground(m_custom_colours ? colour : StateColor::darkModeColorFor(colour));
        dc.DrawText(run.text, run.position);
    }
}

void RichTextLabel::on_motion(wxMouseEvent &event)
{
    const bool is_link = !hit_link(event.GetPosition()).empty();
    if (is_link != m_link_hovered) {
        m_link_hovered = is_link;
        SetCursor(is_link ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_ARROW));
    }
}

wxString RichTextLabel::hit_link(const wxPoint &point) const
{
    for (const auto &lr : m_link_rects) {
        if (lr.rect.Contains(point))
            return lr.url;
    }
    return wxString();
}
