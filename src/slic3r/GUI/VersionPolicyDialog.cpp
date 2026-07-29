/**
 * @file VersionPolicyDialog.cpp
 * @brief Implementation of the version policy dialog.
 */

#include "VersionPolicyDialog.hpp"

#include <cctype>
#include <sstream>
#include <string>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include <wx/html/htmlwin.h>
#include <wx/sizer.h>

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Widgets/StateColor.hpp"
#include "libslic3r/Utils.hpp"
#include "wxExtensions.hpp"

namespace Slic3r { namespace GUI {

namespace {

const int DIALOG_WIDTH  = 560;
const int BODY_HEIGHT   = 300;
const int BODY_PADDING  = 16;
const int EDGE_PADDING  = 24;
const int BUTTON_WIDTH  = 76;
const int BUTTON_HEIGHT = 24;
const int BUTTON_GAP    = 12;

/** @brief Escapes what wxHTML would otherwise read as markup. */
std::string escape_html(const std::string &text)
{
    std::string escaped;
    escaped.reserve(text.size());

    for (const char c : text) {
        switch (c) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        default: escaped += c; break;
        }
    }
    return escaped;
}

/** @brief Drops the leading and trailing blanks of a line. */
std::string trim(const std::string &text)
{
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };

    size_t begin = 0;
    while (begin < text.size() && blank(text[begin])) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin && blank(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool starts_with(const std::string &text, const std::string &prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

/** @brief Whether the line is a thematic break, three or more of one marker. */
bool is_thematic_break(const std::string &line)
{
    if (line.empty() || (line[0] != '-' && line[0] != '*' && line[0] != '_')) {
        return false;
    }

    const char marker = line[0];
    int        count  = 0;
    for (const char c : line) {
        if (c == marker) {
            ++count;
        } else if (c != ' ' && c != '\t') {
            return false;
        }
    }
    return count >= 3;
}

/**
 * @brief Renders the inline markup of one line: code, links, bold and italic.
 *
 * The text must already be escaped, so that every tag below is one this
 * function put there. An unpaired marker is left alone and reads as text,
 * which is what a reader of the raw markdown would see too.
 */
std::string inline_to_html(const std::string &text)
{
    std::string  html;
    const size_t size = text.size();
    size_t       i    = 0;

    while (i < size) {
        const char c = text[i];

        if (c == '`') {
            const size_t end = text.find('`', i + 1);
            if (end != std::string::npos) {
                html += "<tt>" + text.substr(i + 1, end - i - 1) + "</tt>";
                i = end + 1;
                continue;
            }
        }

        if (c == '[') {
            const size_t label_end = text.find(']', i + 1);
            if (label_end != std::string::npos && label_end + 1 < size && text[label_end + 1] == '(') {
                const size_t url_end = text.find(')', label_end + 2);
                if (url_end != std::string::npos) {
                    const std::string label = text.substr(i + 1, label_end - i - 1);
                    const std::string url   = text.substr(label_end + 2, url_end - label_end - 2);
                    html += "<a href=\"" + url + "\">" + inline_to_html(label) + "</a>";
                    i = url_end + 1;
                    continue;
                }
            }
        }

        if ((c == '*' || c == '_') && i + 1 < size && text[i + 1] == c) {
            const std::string marker(2, c);
            const size_t      end = text.find(marker, i + 2);
            if (end != std::string::npos && end > i + 2) {
                html += "<b>" + inline_to_html(text.substr(i + 2, end - i - 2)) + "</b>";
                i = end + 2;
                continue;
            }
        }

        if (c == '*' || c == '_') {
            const size_t end = text.find(c, i + 1);
            if (end != std::string::npos && end > i + 1) {
                html += "<i>" + inline_to_html(text.substr(i + 1, end - i - 1)) + "</i>";
                i = end + 1;
                continue;
            }
        }

        html += c;
        ++i;
    }
    return html;
}

/**
 * @brief Renders the markdown subset a policy message realistically carries.
 *
 * Headings, thematic breaks, bullet and numbered lists, quotes, fenced code,
 * and whatever inline_to_html() covers. Anything else ends up as text, which
 * keeps a message readable instead of losing it to a parse error.
 */
std::string markdown_to_html(const std::string &markdown)
{
    enum ListKind { LIST_NONE, LIST_BULLET, LIST_NUMBER };

    std::string html;
    ListKind    list      = LIST_NONE;
    bool        paragraph = false;
    bool        quote     = false;
    bool        code      = false;

    const auto close_paragraph = [&]() {
        if (paragraph) {
            html += "</p>";
            paragraph = false;
        }
    };
    const auto close_list = [&]() {
        if (list == LIST_BULLET) {
            html += "</ul>";
        } else if (list == LIST_NUMBER) {
            html += "</ol>";
        }
        list = LIST_NONE;
    };
    const auto close_quote = [&]() {
        if (quote) {
            html += "</blockquote>";
            quote = false;
        }
    };
    const auto close_blocks = [&]() {
        close_paragraph();
        close_list();
        close_quote();
    };

    std::istringstream stream(markdown);
    std::string        raw;
    while (std::getline(stream, raw)) {
        // A fence is the one block whose indentation carries meaning, so it is
        // decided on the raw line and emitted before anything is trimmed.
        const std::string line = trim(raw);
        if (starts_with(line, "```")) {
            if (code) {
                html += "</pre>";
                code = false;
            } else {
                close_blocks();
                html += "<pre>";
                code = true;
            }
            continue;
        }
        if (code) {
            html += escape_html(raw) + "\n";
            continue;
        }

        if (line.empty()) {
            close_blocks();
            continue;
        }

        // Before the bullet list: "---" and "***" would pass for one otherwise.
        if (is_thematic_break(line)) {
            close_blocks();
            html += "<hr>";
            continue;
        }

        size_t hashes = 0;
        while (hashes < line.size() && line[hashes] == '#') {
            ++hashes;
        }
        if (hashes >= 1 && hashes <= 6 && hashes < line.size() && line[hashes] == ' ') {
            close_blocks();
            const std::string level = std::to_string(hashes);
            html += "<h" + level + ">" + inline_to_html(escape_html(trim(line.substr(hashes + 1)))) + "</h" + level + ">";
            continue;
        }

        if (line[0] == '>') {
            close_paragraph();
            close_list();
            if (quote) {
                html += "<br>";
            } else {
                html += "<blockquote>";
                quote = true;
            }
            html += inline_to_html(escape_html(trim(line.substr(1))));
            continue;
        }

        if ((line[0] == '-' || line[0] == '*' || line[0] == '+') && line.size() > 1 && (line[1] == ' ' || line[1] == '\t')) {
            close_paragraph();
            close_quote();
            if (list != LIST_BULLET) {
                close_list();
                html += "<ul>";
                list = LIST_BULLET;
            }
            html += "<li>" + inline_to_html(escape_html(trim(line.substr(2)))) + "</li>";
            continue;
        }

        size_t digits = 0;
        while (digits < line.size() && std::isdigit(static_cast<unsigned char>(line[digits]))) {
            ++digits;
        }
        if (digits > 0 && digits + 1 < line.size() && (line[digits] == '.' || line[digits] == ')') && line[digits + 1] == ' ') {
            close_paragraph();
            close_quote();
            if (list != LIST_NUMBER) {
                close_list();
                html += "<ol>";
                list = LIST_NUMBER;
            }
            html += "<li>" + inline_to_html(escape_html(trim(line.substr(digits + 2)))) + "</li>";
            continue;
        }

        close_list();
        close_quote();
        if (paragraph) {
            html += "<br>";
        } else {
            html += "<p>";
            paragraph = true;
        }
        html += inline_to_html(escape_html(line));
    }

    // An unterminated fence is a message the cloud got wrong; still close the
    // tag, or wxHTML swallows the rest of the body.
    if (code) {
        html += "</pre>";
    }
    close_blocks();
    return html;
}

/** @brief Renders a plain text message, keeping only its line breaks. */
std::string plain_text_to_html(const std::string &text)
{
    std::string html;
    for (const char c : escape_html(text)) {
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            html += "<br>";
            continue;
        }
        html += c;
    }
    return "<p>" + html + "</p>";
}

/**
 * @brief Merges the hits into one HTML body, each hit a section of its own.
 *
 * Only a markdown hit goes through the markdown reader; a plain text one is
 * escaped and left alone, so an asterisk in it stays an asterisk.
 */
std::string build_html(const std::vector<PolicyHit> &hits)
{
    std::string html;
    for (const auto &hit : hits) {
        if (!html.empty()) {
            html += "<hr>";
        }
        if (!hit.title.empty()) {
            html += "<h3>" + escape_html(hit.title) + "</h3>";
        }
        html += hit.message_type == PolicyMessageType::MarkDown ? markdown_to_html(hit.message) : plain_text_to_html(hit.message);
    }
    return html;
}

/** @brief Applies the shared button metrics of the design. */
void style_button(Button *button, const StateColor &background, const wxColour &border, const wxColour &text)
{
    button->SetBackgroundColor(background);
    button->SetBorderColor(border);
    button->SetTextColor(text);
    button->SetFont(Label::Body_12);

    const wxSize size(button->FromDIP(BUTTON_WIDTH), button->FromDIP(BUTTON_HEIGHT));
    button->SetSize(size);
    button->SetMinSize(size);
    button->SetCornerRadius(button->FromDIP(BUTTON_HEIGHT / 2));
}

} // namespace

VersionPolicyDialog::VersionPolicyDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("Warning"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    CreateGUI();
}

VersionPolicyDialog::~VersionPolicyDialog() = default;

void VersionPolicyDialog::CreateGUI()
{
    const std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));
    SetBackgroundColour(ThemeColor::White);

    auto *sizer_main = new wxBoxSizer(wxVERTICAL);

    auto *line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    line_top->SetBackgroundColour(ThemeColor::Grey450);
    sizer_main->Add(line_top, 0, wxEXPAND);

    CreateBody(sizer_main);

    auto *sizer_button = new wxBoxSizer(wxHORIZONTAL);
    CreateButtons(sizer_button);
    sizer_main->Add(sizer_button, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(12));

    SetSizer(sizer_main);
    Layout();
    Fit();

    Centre(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);
}

void VersionPolicyDialog::CreateBody(wxSizer *sizer)
{
    m_body = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(DIALOG_WIDTH), FromDIP(BODY_HEIGHT)), wxHW_SCROLLBAR_AUTO);
    m_body->SetBackgroundColour(ThemeColor::Grey200);
    m_body->SetMinSize(wxSize(FromDIP(DIALOG_WIDTH), FromDIP(BODY_HEIGHT)));
    m_body->SetBorders(FromDIP(BODY_PADDING));

    // wxHTML sizes text through the seven HTML font sizes rather than points,
    // so the body font decides the middle one and the headings grow from there.
    const wxFont body     = Label::Body_15;
    const int    base     = body.GetPointSize();
    int          sizes[7] = {base - 2, base - 1, base, base + 1, base + 2, base + 4, base + 6};
    m_body->SetFonts(body.GetFaceName(), wxGetApp().code_font().GetFaceName(), sizes);

    m_body->Bind(wxEVT_HTML_LINK_CLICKED, &VersionPolicyDialog::on_body_link, this);

    sizer->Add(m_body, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(EDGE_PADDING));
}

void VersionPolicyDialog::CreateButtons(wxSizer *sizer)
{
    sizer->AddStretchSpacer();

    // Every button carries the gap to the next one, so the row only owes the
    // rest of the edge padding after the last of them.
    m_sizer_buttons = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_sizer_buttons, 0, wxRIGHT, FromDIP(EDGE_PADDING - BUTTON_GAP));
}

Button *VersionPolicyDialog::add_button(ButtonId id, const wxString &label, ButtonHandler handler, ButtonStyle style)
{
    set_handler(id, std::move(handler));

    // Two check points may share a severity and still word the same button
    // differently, so a second call relabels instead of adding a second one.
    const auto known = m_buttons.find(id);
    if (known != m_buttons.end()) {
        known->second->SetLabel(label);
        Layout();
        Fit();
        return known->second;
    }

    auto *button = new Button(this, label);
    if (style == ButtonStyle::Primary) {
        StateColor background(std::pair<wxColour, int>(ThemeColor::BrandGreenPressed, StateColor::Pressed),
                              std::pair<wxColour, int>(ThemeColor::BrandGreenHovered, StateColor::Hovered),
                              std::pair<wxColour, int>(ThemeColor::BrandGreen, StateColor::Normal));
        style_button(button, background, ThemeColor::White, wxColour("#FFFFFE"));
    } else {
        StateColor background(std::pair<wxColour, int>(ThemeColor::Grey400, StateColor::Pressed),
                              std::pair<wxColour, int>(ThemeColor::Grey300, StateColor::Hovered),
                              std::pair<wxColour, int>(ThemeColor::White, StateColor::Normal));
        style_button(button, background, ThemeColor::Grey400, ThemeColor::TextPrimary);
    }

    button->Bind(wxEVT_LEFT_DOWN, [this, id](wxMouseEvent &) {
        fire(id);
        EndModal(modal_result_of(id));
    });

    m_buttons[id] = button;
    m_sizer_buttons->Add(button, 0, wxRIGHT, FromDIP(BUTTON_GAP));

    Layout();
    Fit();
    return button;
}

int VersionPolicyDialog::modal_result_of(ButtonId id)
{
    switch (id) {
    case ButtonId::Continue: return wxID_YES;
    case ButtonId::Back: return wxID_NO;
    case ButtonId::Acknowledge:
    default: return wxID_OK;
    }
}

void VersionPolicyDialog::UpdateByPolicyHits(const PolicyCheckResult &result)
{
    m_blocking = result.blocked();

    // wxHTML carries no stylesheet, so the surface and the text colour of the
    // design are handed to it on the body tag itself.
    const wxString background = ThemeColor::Grey200.GetAsString(wxC2S_HTML_SYNTAX);
    const wxString foreground = ThemeColor::TextMuted.GetAsString(wxC2S_HTML_SYNTAX);
    m_body->SetPage("<html><body bgcolor=\"" + background + "\" text=\"" + foreground + "\">" + from_u8(build_html(result.all_hits)) +
                    "</body></html>");

    Layout();
    Fit();
    SetMinSize(GetSize());
    Centre(wxBOTH);
}

void VersionPolicyDialog::on_body_link(wxHtmlLinkEvent &event)
{
    // Not skipped on purpose: wxHtmlWindow would otherwise navigate to the
    // target itself, replacing the message with a page it cannot render.
    const wxString url = event.GetLinkInfo().GetHref();
    if (url.StartsWith("http://") || url.StartsWith("https://")) {
        wxGetApp().open_browser_with_warning_dialog(url);
    } else {
        BOOST_LOG_TRIVIAL(warning) << "[VersionPolicy]: ignoring a message link that is not http, " << into_u8(url);
    }
}

void VersionPolicyDialog::set_handler(ButtonId id, ButtonHandler handler)
{
    m_handlers[id] = std::move(handler);
}

void VersionPolicyDialog::fire(ButtonId id)
{
    const auto it = m_handlers.find(id);
    if (it == m_handlers.end() || !it->second) {
        return;
    }

    try {
        it->second();
    } catch (const std::exception &e) {
        // A check point must not be able to take the dialog, or the operation
        // it guards, down with it.
        BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: a dialog button handler threw, " << e.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: a dialog button handler threw an unknown error";
    }
}

bool VersionPolicyDialog::run()
{
    const int result = ShowModal();

    // Closing the window through the title bar must not be a way past a block.
    if (m_blocking) {
        return false;
    }
    return result == wxID_YES;
}

void VersionPolicyDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    for (const auto &entry : m_buttons) {
        entry.second->Rescale();
    }
    Layout();
}

}} // namespace Slic3r::GUI
