#include "BetaVersionDialog.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/AMSItem.hpp"

#include <wx/sizer.h>
#include <wx/dcgraph.h>
#include <wx/richtext/richtextctrl.h>

#include <boost/format.hpp>

namespace Slic3r { namespace GUI {

static constexpr int DIALOG_PADDING   = 30;
static constexpr int DETAIL_PANEL_W   = 500;
static constexpr int DETAIL_ITEM_GAP  = 12;
static constexpr int BUTTON_H         = 24;
static constexpr int BUTTON_RADIUS    = 12;

BetaVersionDialog::BetaVersionDialog(wxWindow *parent)
    : DPIDialog(parent ? parent : static_cast<wxWindow *>(wxGetApp().mainframe),
                wxID_ANY,
                _L("Software Update"),
                wxDefaultPosition,
                wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);

    auto *sizer_main = new wxBoxSizer(wxVERTICAL);

    // Top separator line
    auto *line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    line_top->SetBackgroundColour(wxColour(166, 169, 170));
    sizer_main->Add(line_top, 0, wxEXPAND, 0);

    // --- Content area ---
    auto *sizer_content = new wxBoxSizer(wxVERTICAL);

    m_heading_label = new Label(this, Label::Head_16, wxEmptyString, LB_AUTO_WRAP);
    m_heading_label->SetForegroundColour(wxColour(0x26, 0x2E, 0x30));
    sizer_content->Add(m_heading_label, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

    m_version_label = new Label(this, Label::Body_13, wxEmptyString, LB_AUTO_WRAP);
    m_version_label->SetForegroundColour(wxColour(0x6B, 0x6B, 0x6B));
    sizer_content->Add(m_version_label, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
    m_overview_label = new Label(this, Label::Body_14, wxEmptyString, LB_AUTO_WRAP);
    m_overview_label->SetForegroundColour(wxColour(0x6B, 0x6B, 0x6B));
    m_overview_label->SetMinSize(wxSize(FromDIP(DETAIL_PANEL_W), -1));
    m_overview_label->SetMaxSize(wxSize(FromDIP(DETAIL_PANEL_W), -1));
    sizer_content->Add(m_overview_label, 0, wxEXPAND | wxBOTTOM, FromDIP(15));

    // Row 3: Detail panel with gray background
    m_detail_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_detail_panel->SetBackgroundColour(wxColour(0xF5, 0xF5, 0xF5));

    m_detail_sizer = new wxBoxSizer(wxVERTICAL);
    m_detail_sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
    m_detail_panel->SetSizer(m_detail_sizer);
    m_detail_panel->SetMinSize(wxSize(FromDIP(DETAIL_PANEL_W), -1));

    sizer_content->Add(m_detail_panel, 0, wxEXPAND | wxBOTTOM, FromDIP(20));

    // Row 4: Buttons
    auto *sizer_button = new wxBoxSizer(wxHORIZONTAL);

    StateColor btn_bg_green(
        std::pair<wxColour, int>(wxColour(27, 136, 68),     StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115),    StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR,  StateColor::Normal));

    StateColor btn_bg_white(
        std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE,                StateColor::Normal));

    // "Try Now" — brand color
    m_button_try_now = new Button(this, _L("Try Now"));
    m_button_try_now->SetBackgroundColor(btn_bg_green);
    m_button_try_now->SetBorderColor(*wxWHITE);
    m_button_try_now->SetTextColor(wxColour("#FFFFFE"));
    m_button_try_now->SetFont(Label::Body_12);
    m_button_try_now->SetSize(wxSize(FromDIP(58), FromDIP(BUTTON_H)));
    m_button_try_now->SetMinSize(wxSize(FromDIP(58), FromDIP(BUTTON_H)));
    m_button_try_now->SetCornerRadius(FromDIP(BUTTON_RADIUS));
    m_button_try_now->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) {
        EndModal(wxID_YES);
    });

    // "Skip"
    m_button_skip = new Button(this, _L("Skip"));
    m_button_skip->SetBackgroundColor(btn_bg_white);
    m_button_skip->SetBorderColor(wxColour(38, 46, 48));
    m_button_skip->SetFont(Label::Body_12);
    m_button_skip->SetSize(wxSize(FromDIP(58), FromDIP(BUTTON_H)));
    m_button_skip->SetMinSize(wxSize(FromDIP(58), FromDIP(BUTTON_H)));
    m_button_skip->SetCornerRadius(FromDIP(BUTTON_RADIUS));
    m_button_skip->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) {
        EndModal(wxID_NO);
    });

    // "Don't show me Beta updates again"
    m_button_dont_show = new Button(this, _L("Don't show me Beta updates again"));
    m_button_dont_show->SetBackgroundColor(btn_bg_white);
    m_button_dont_show->SetBorderColor(wxColour(38, 46, 48));
    m_button_dont_show->SetFont(Label::Body_12);
    m_button_dont_show->SetSize(wxSize(-1, FromDIP(BUTTON_H)));
    m_button_dont_show->SetMinSize(wxSize(FromDIP(58), FromDIP(BUTTON_H)));
    m_button_dont_show->SetCornerRadius(FromDIP(BUTTON_RADIUS));
    m_button_dont_show->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) {
        EndModal(wxID_CANCEL);
    });

    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_try_now,   0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_skip,      0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_dont_show, 0, wxALL, FromDIP(5));

    sizer_content->Add(sizer_button, 0, wxEXPAND, 0);

    sizer_main->Add(sizer_content, 0, wxEXPAND | wxALL, FromDIP(DIALOG_PADDING));

    SetSizer(sizer_main);
    Layout();
    Fit();

    SetMinSize(GetSize());
    Centre(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);

    // The window close box (X) must not be conflated with the explicit
    // "Don't show me Beta updates again" button: that button intentionally
    // disables the beta-channel preference, while pressing X is a passive
    // dismiss. Map X to a dedicated id so the caller can leave preferences
    // untouched.
    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) {
        EndModal(wxID_CLOSE);
    });
}

BetaVersionDialog::~BetaVersionDialog() {}

void BetaVersionDialog::updateContent(const wxString &available_version,
                                      const wxString &current_version)
{
    m_heading_label->SetLabel(
        wxString::Format(_L("New Bambu Studio Beta Available") + wxString(": %s"), available_version));

    m_version_label->SetLabel(
        wxString::Format(_L("Current Version") + wxString(": %s"), current_version));

    m_overview_label->SetLabel(
        _L("Bambu Studio Beta gives you early access to upcoming features, improvements, optimizations, and bug fixes before they are released in the stable version of Bambu Studio. "
           "It is intended for advanced users who would like to explore new functionality early and help improve the software experience."));

    m_detail_sizer->Clear(true);
    m_detail_title_labels.clear();
    m_detail_body_labels.clear();

    m_detail_sizer->Add(0, 0, 0, wxTOP, FromDIP(15));

    createDetailItem(m_detail_sizer, m_detail_panel, 1,
        _L("Separate configuration folder."),
        _L("As a beta release, some features may still be under development, and you may occasionally encounter bugs, unexpected behavior, or incomplete functionality. "
            "To prevent conflicts with the stable release, Bambu Studio Beta uses a separate configuration folder "
            "(Help -> Show Configuration Folder) and operates independently from the standard version of Bambu Studio."),
        _L("(Help -> Show Configuration Folder)"));

    m_detail_sizer->Add(0, 0, 0, wxTOP, FromDIP(DETAIL_ITEM_GAP));

    createDetailItem(m_detail_sizer, m_detail_panel, 2,
        _L("Why use the Beta version?"),
        _L("By using the Beta version and sharing feedback, bug reports, and suggestions, you can help Bambu Lab improve stability, compatibility, and future features before the official release."));

    m_detail_sizer->Add(0, 0, 0, wxTOP, FromDIP(DETAIL_ITEM_GAP));

    createDetailItem(m_detail_sizer, m_detail_panel, 3,
        _L("How to change settings?"),
        _L("If you prefer not to participate in the Beta program, you can disable Beta Update prompts at any time in Settings."));

    m_detail_sizer->Add(0, 0, 0, wxBOTTOM, FromDIP(15));

    m_detail_panel->Layout();
    m_detail_panel->Fit();
    Layout();
    Fit();
    SetMinSize(GetSize());
    CentreOnParent(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);
}

void BetaVersionDialog::createDetailItem(wxSizer *parent_sizer, wxWindow *parent_win,
                                         int index,
                                         const wxString &title, const wxString &body,
                                         const wxString &bold_segment)
{
    static constexpr int BODY_INDENT = 40;
    static constexpr int BODY_WIDTH  = DETAIL_PANEL_W - 60;

    auto *item_sizer = new wxBoxSizer(wxVERTICAL);

    wxString numbered_title = wxString::Format("%d. %s", index, title);
    auto *title_label = new Label(parent_win, Label::Head_13, numbered_title, LB_AUTO_WRAP);
    title_label->SetForegroundColour(wxColour(0x26, 0x2E, 0x30));
    title_label->SetMinSize(wxSize(FromDIP(DETAIL_PANEL_W - 40), -1));
    title_label->SetMaxSize(wxSize(FromDIP(DETAIL_PANEL_W - 40), -1));
    item_sizer->Add(title_label, 0, wxLEFT | wxRIGHT, FromDIP(20));
    m_detail_title_labels.push_back(title_label);

    wxWindow *body_widget = nullptr;

    if (!bold_segment.IsEmpty()) {
        auto *rtc = new wxRichTextCtrl(parent_win, wxID_ANY, wxEmptyString,
            wxDefaultPosition, wxDefaultSize,
            wxRE_MULTILINE | wxRE_READONLY | wxBORDER_NONE | wxVSCROLL | wxWANTS_CHARS);
        rtc->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_NEVER);
        rtc->SetBackgroundColour(parent_win->GetBackgroundColour());
        rtc->SetMinSize(wxSize(FromDIP(BODY_WIDTH), -1));
        rtc->SetMaxSize(wxSize(FromDIP(BODY_WIDTH), -1));

        wxFont normal_font = Label::Body_12;
        wxFont bold_font   = Label::Body_12.Bold();
        wxColour body_colour(0x6B, 0x6B, 0x6B);
        wxColour bold_colour(0x26, 0x2E, 0x30);

        rtc->BeginSuppressUndo();
        int bold_pos = body.Find(bold_segment);
        if (bold_pos != wxNOT_FOUND) {
            wxString before = body.Left(bold_pos);
            wxString after  = body.Mid(bold_pos + bold_segment.length());

            if (!before.IsEmpty()) {
                rtc->BeginFont(normal_font);
                rtc->BeginTextColour(body_colour);
                rtc->WriteText(before);
                rtc->EndTextColour();
                rtc->EndFont();
            }

            rtc->BeginFont(bold_font);
            rtc->BeginTextColour(bold_colour);
            rtc->WriteText(bold_segment);
            rtc->EndTextColour();
            rtc->EndFont();

            if (!after.IsEmpty()) {
                rtc->BeginFont(normal_font);
                rtc->BeginTextColour(body_colour);
                rtc->WriteText(after);
                rtc->EndTextColour();
                rtc->EndFont();
            }
        } else {
            rtc->BeginFont(normal_font);
            rtc->BeginTextColour(body_colour);
            rtc->WriteText(body);
            rtc->EndTextColour();
            rtc->EndFont();
        }
        rtc->EndSuppressUndo();

        wxRichTextAttr para_attr;
        para_attr.SetLineSpacing(12);
        rtc->SetBasicStyle(para_attr);
        rtc->LayoutContent();

        wxSize best = rtc->GetVirtualSize();
        best.SetWidth(FromDIP(BODY_WIDTH));
        best.SetHeight(best.GetHeight() + FromDIP(10));
        rtc->SetMinSize(best);
        rtc->SetMaxSize(wxSize(FromDIP(BODY_WIDTH), -1));

        body_widget = rtc;
    } else {
        auto *body_label = new Label(parent_win, Label::Body_12, body, LB_AUTO_WRAP);
        body_label->SetForegroundColour(wxColour(0x6B, 0x6B, 0x6B));
        body_label->SetMinSize(wxSize(FromDIP(BODY_WIDTH), -1));
        body_label->SetMaxSize(wxSize(FromDIP(BODY_WIDTH), -1));
        m_detail_body_labels.push_back(body_label);
        body_widget = body_label;
    }

    auto *body_indent_sizer = new wxBoxSizer(wxHORIZONTAL);
    body_indent_sizer->Add(FromDIP(BODY_INDENT), 0, 0, 0);
    body_indent_sizer->Add(body_widget, 0, 0, 0);
    item_sizer->Add(body_indent_sizer, 0, wxTOP, FromDIP(4));

    parent_sizer->Add(item_sizer, 0, wxEXPAND, 0);
}

void BetaVersionDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_button_try_now->Rescale();
    m_button_skip->Rescale();
    m_button_dont_show->Rescale();
}

}} // namespace Slic3r::GUI
