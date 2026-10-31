#include "MinorsLogoutDialog.hpp"
#include "GUI_App.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"
#include <wx/dcbuffer.h>
#include <slic3r/GUI/I18N.hpp>

namespace Slic3r { namespace GUI {

static const wxColour TEXT_COLOUR = ThemeColor::TextPrimary; // #262E30, dark-mode mapped to #EFEFF0
static const wxColour LINK_COLOUR = ThemeColor::BrandGreen;  // #00AE42, dark-mode mapped to #21A452

// A run of mixed body text where some segments are bold and some are clickable links.
struct RichSegment {
    wxString text;
    bool     bold = false;
    wxString url; // non-empty => rendered as an underlined link that opens the URL
};

// Self-drawn multi-segment rich-text label: wraps word/CJK-aware, mixes bold/regular fonts,
// and lets each link segment carry its own URL. Modeled on ReleaseNote.cpp's
// DialogWikiWarningLabel, generalized to multiple fonts and multiple links.
class RichTextLabel : public wxPanel
{
public:
    RichTextLabel(wxWindow* parent, std::vector<RichSegment> segments, int width)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(width, 1), wxBORDER_NONE)
        , m_segments(std::move(segments))
        , m_width(width)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(parent->GetBackgroundColour());
        SetCursor(wxCURSOR_ARROW);
        layout_text();

        Bind(wxEVT_PAINT, &RichTextLabel::on_paint, this);
        Bind(wxEVT_MOTION, &RichTextLabel::on_motion, this);
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
            m_link_hovered = false;
            SetCursor(wxCURSOR_ARROW);
        });
        Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
            const wxString url = hit_link(event.GetPosition());
            if (!url.empty())
                wxLaunchDefaultBrowser(url);
        });
    }

private:
    struct TextRun {
        wxString text;
        wxPoint  position;
        wxSize   size;
        bool     bold;
        bool     is_link;
    };

    static wxFont font_for(bool bold, bool is_link)
    {
        wxFont f = bold ? Label::Head_14 : Label::Body_14;
        return is_link ? f.Underlined() : f;
    }

    void layout_text()
    {
        wxClientDC dc(this);
        dc.SetFont(Label::Body_14);
        const int line_height = dc.GetCharHeight();
        int x = 0;
        int y = 0;
        int max_x = 0;

        // Line breaks come only from explicit '\n' in the segments; no width-based wrapping,
        // so the layout matches the copy exactly. The panel sizes itself to the widest line.
        auto place_token = [&](const wxString& token, bool bold, bool is_link, const wxString& url) {
            if (token.empty()) return;
            dc.SetFont(font_for(bold, is_link));
            const wxSize size = dc.GetTextExtent(token);
            const bool is_space = token == " " || token == "\t";
            if (is_space && x == 0) return;
            m_runs.push_back({token, wxPoint(x, y), wxSize(size.x, line_height), bold, is_link});
            if (is_link)
                m_link_rects.push_back({wxRect(x, y, size.x, line_height), url});
            x += size.x;
            max_x = std::max(max_x, x);
        };

        auto append_segment = [&](const RichSegment& seg) {
            const bool is_link = !seg.url.empty();
            wxString token;
            auto flush_token = [&]() {
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

        for (const auto& seg : m_segments)
            append_segment(seg);

        const int width = std::max(m_width, max_x);
        const int height = std::max(line_height, y + line_height);
        SetMinSize(wxSize(width, height));
        SetMaxSize(wxSize(width, height));
    }

    void on_paint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();
        for (const auto& run : m_runs) {
            dc.SetFont(font_for(run.bold, run.is_link));
            dc.SetTextForeground(StateColor::darkModeColorFor(run.is_link ? LINK_COLOUR : TEXT_COLOUR));
            dc.DrawText(run.text, run.position);
        }
    }

    void on_motion(wxMouseEvent& event)
    {
        const bool is_link = !hit_link(event.GetPosition()).empty();
        if (is_link != m_link_hovered) {
            m_link_hovered = is_link;
            SetCursor(is_link ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_ARROW));
        }
    }

    wxString hit_link(const wxPoint& point) const
    {
        for (const auto& lr : m_link_rects) {
            if (lr.rect.Contains(point))
                return lr.url;
        }
        return wxString();
    }

    struct LinkRect {
        wxRect   rect;
        wxString url;
    };

    std::vector<RichSegment> m_segments;
    int                      m_width;
    bool                     m_link_hovered{false};
    std::vector<TextRun>     m_runs;
    std::vector<LinkRect>    m_link_rects;
};

MinorsLogoutDialog::MinorsLogoutDialog(wxWindow* parent, wxString signin_url)
    : DPIDialog(parent, wxID_ANY, _L("Automatically logged out"), wxDefaultPosition, wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
    , m_signin_url(std::move(signin_url))
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);

    const int body_width = FromDIP(440);

    wxBoxSizer* sizer_main = new wxBoxSizer(wxVERTICAL);

    auto line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(body_width, 1));
    line_top->SetBackgroundColour(wxColour(166, 169, 170));
    sizer_main->Add(line_top, 0, wxEXPAND, 0);

    // TODO: portal-qa is the QA-environment host; swap to the production host before release.
    const wxString URL_AGE_REQ = "https://portal-qa.bambulab.net/zh/policies/page/age-requirements-for-bambulab-account";
    const wxString URL_PRIVACY = "https://bambulab.com/en/policies/privacy";

    std::vector<RichSegment> segs = {
        { _L("For users under the age of majority, some services will be restricted ."), false, "" },
        { "\n", false, "" },
        { _L("Automatically logged out. Please log in again after binding a parent/\nguardian account. "), true, "" },
        { _L("See our "), false, "" },
        { _L("Age Requirements for Bambu Lab Account"), false, URL_AGE_REQ },
        { ".", false, "" },
        { "\n", false, "" },
        { _L("To understand our privacy practices, see "), false, "" },
        { _L("Privacy Notice"), false, URL_PRIVACY },
        { ".", false, "" },
    };
    auto body = new RichTextLabel(this, std::move(segs), body_width);
    sizer_main->Add(body, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    m_button_got_it = new Button(this, _L("Got it"));
    m_button_got_it->SetBackgroundColor(btn_bg_green);
    m_button_got_it->SetBorderColor(*wxWHITE);
    m_button_got_it->SetTextColor(wxColour("#FFFFFE"));
    m_button_got_it->SetFont(Label::Body_12);
    m_button_got_it->SetMinSize(wxSize(FromDIP(68), FromDIP(24)));
    m_button_got_it->SetCornerRadius(FromDIP(12));
    m_button_got_it->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        if (!m_signin_url.empty())
            wxLaunchDefaultBrowser(m_signin_url);
        EndModal(wxID_OK);
    });

    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_got_it, 0, wxALL, FromDIP(5));
    sizer_main->Add(sizer_button, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(15));

    SetSizer(sizer_main);
    Layout();
    sizer_main->Fit(this);

    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void MinorsLogoutDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    Layout();
    Fit();
    Refresh();
}

}} // namespace Slic3r::GUI
