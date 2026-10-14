#include "AdvancedModeTip.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/utils.h>

namespace Slic3r { namespace GUI {

static wxColour card_bg() { return StateColor::darkModeColorFor(ThemeColor::White); }
static wxColour body_fg() { return StateColor::darkModeColorFor(ThemeColor::TextPrimary); }

// Mirrors the Setup Wizard completion page's OpenAcademyUrl(): keyed off the UI
// language only (not region), so switching the app to English always lands on
// the English site regardless of what region is configured.
static wxString academy_url()
{
    wxString lang = wxGetApp().current_language_code_safe().Lower();
    if (lang.Contains("zh"))
        return "https://bambulab.cn/zh-cn/support/academy/";

    wxString seg = "en";
    if (lang.Contains("fr")) seg = "fr-fr";
    else if (lang.Contains("de")) seg = "de-de";
    else if (lang.Contains("es")) seg = "es-mx";
    else if (lang.Contains("it")) seg = "it-it";
    else if (lang.Contains("ja")) seg = "ja-jp";
    else if (lang.Contains("ko")) seg = "ko-kr";
    else if (lang.Contains("pt")) seg = "pt-br";
    else if (lang.Contains("nl")) seg = "nl-nl";

    return "https://bambulab.com/" + seg + "/support/academy/";
}

AdvancedModeTip::AdvancedModeTip(wxWindow *parent)
    : RoundedPopupWindow(parent, 8, true)
{
    m_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(m_sizer);

    m_body = new wxStaticText(this, wxID_ANY, _L(
        "Enabling the advanced mode will unlock a vast number of parameters, that can increase or "
        "decrease your print quality. It's important to understand them to improve your prints."));
    m_body->SetFont(Label::Body_12);
    m_body->SetForegroundColour(body_fg());
    m_body->SetBackgroundColour(card_bg());
    m_body->Wrap(FromDIP(260));
    m_sizer->Add(m_body, 0, wxALL, FromDIP(12));

    m_link = new wxStaticText(this, wxID_ANY, _L("Take the Bambu Studio Academy course to learn how to master them."));
    m_link->SetFont(Label::Body_12.Underlined());
    m_link->SetForegroundColour(ThemeColor::BrandGreen);
    m_link->SetBackgroundColour(card_bg());
    m_link->SetCursor(wxCursor(wxCURSOR_HAND));
    m_link->Wrap(FromDIP(260));
    m_link->Bind(wxEVT_LEFT_DOWN, [](wxMouseEvent &) { wxLaunchDefaultBrowser(academy_url()); });
    m_sizer->Add(m_link, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    Finalize();
}

void AdvancedModeTip::ShowTip(const wxPoint &pos)
{
    // The disabled-switch fallback path in ParamsPanel re-checks on every mouse-move
    // while hovering the switch's rect; guard against re-popping an already-open card.
    if (IsShown()) return;
    SetPosition(pos);
    Popup();
}

}} // namespace Slic3r::GUI
