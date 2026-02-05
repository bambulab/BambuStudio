#include "HelioReleaseNote.hpp"
#include "HelioHistoryDialog.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Preview.hpp"
#include "MainFrame.hpp"
#include "format.hpp"

// Forward declaration - function is defined in Plater.cpp
namespace Slic3r {
namespace GUI {
    bool& get_helio_pre_select_optimization_flag();
}
}
#include "GLToolbar.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/StaticBox.hpp"
#include "Widgets/StaticLine.hpp"
#include "Widgets/WebView.hpp"
#include "Widgets/SwitchButton.hpp"
#include "slic3r/GUI/GCodeRenderer/BaseRenderer.hpp"
#include <wx/regex.h>
#include <wx/progdlg.h>
#include <wx/clipbrd.h>
#include <wx/dcgraph.h>
#include <wx/tooltip.h>
#include <miniz.h>
#include <wx/valnum.h>
#include <algorithm>
#include <climits>
#include <cstdlib>
#include "Plater.hpp"
#include "BitmapCache.hpp"

#include "DeviceCore/DevManager.h"
#include "DeviceCore/DevStorage.h"

namespace Slic3r { namespace GUI {

// Helio dark palette theme helper - defined early so it can be used throughout
namespace {
    // Base background: #07090C
    const wxColour HELIO_BG_BASE(7, 9, 12);
    // Panel/card background: #0E1320
    const wxColour HELIO_CARD_BG(14, 19, 32);
    // Card highlight (subtle): #121A2B
    const wxColour HELIO_CARD_HIGHLIGHT(18, 26, 43);
    // Border: rgba(255,255,255,0.10)
    const wxColour HELIO_BORDER(255, 255, 255, 25);
    // Text: #EEF2FF
    const wxColour HELIO_TEXT(238, 242, 255);
    // Muted text: #A8B0C0
    const wxColour HELIO_MUTED(168, 176, 192);
    // Purple accent: #AF7CFF
    const wxColour HELIO_PURPLE(175, 124, 255);
    // Blue accent: #4F86FF
    const wxColour HELIO_BLUE(79, 134, 255);
}

 HelioStatementDialog::HelioStatementDialog(wxWindow *parent /*= nullptr*/)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Legal & Activation Terms"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
     shared_ptr = std::make_shared<int>(0);

     // Make tooltips appear faster while this dialog is open
     // Note: GetDelay() may not be available in all wxWidgets versions, so we use a default
     m_original_tooltip_delay = 500; // Default tooltip delay
     wxToolTip::SetDelay(200);

     // Set Helio icon (not BambuStudio icon)
     wxBitmap bmp = create_scaled_bitmap("helio_icon", this, 32);
     wxIcon icon;
     icon.CopyFromBitmap(bmp);
     SetIcon(icon);

     // Use Helio dark palette (not neutral charcoal)
     SetBackgroundColour(HELIO_BG_BASE); // #07090C

     wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);

     // Create Legal Terms Page and PAT Page
     create_legal_page();
     create_pat_page();

     // Button colors for cancel button
     StateColor btn_bg_green = StateColor(
         std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
         std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

     m_button_cancel = new Button(this, _L("Got it"));
     m_button_cancel->SetBackgroundColor(btn_bg_green);
     m_button_cancel->SetBorderColor(wxColour(0, 174, 66));
     // White text for all states
     StateColor white_text(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
                           std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Hovered),
                           std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Pressed),
                           std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Enabled),
                           std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));
     m_button_cancel->SetTextColor(white_text);
     m_button_cancel->SetTextColorNormal(wxColour(255, 255, 254));
     m_button_cancel->SetFont(Label::Body_14);
     m_button_cancel->SetSize(wxSize(FromDIP(100), FromDIP(36)));
     m_button_cancel->SetMinSize(wxSize(FromDIP(100), FromDIP(36)));
     m_button_cancel->SetCornerRadius(FromDIP(4));
     m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { EndModal(wxID_NO); });

     // Button sizer for PAT page (centered)
     wxBoxSizer* pat_button_sizer = new wxBoxSizer(wxHORIZONTAL);
     pat_button_sizer->Add(0, 0, 1, wxEXPAND, 0);
     pat_button_sizer->Add(m_button_cancel, 0, wxALIGN_CENTER, 0);
     pat_button_sizer->Add(0, 0, 1, wxEXPAND, 0);

     // Bind checkbox event (checkbox is created in create_legal_page)
     // Note: The CheckBox class already has an internal handler that calls update()
     // We bind AFTER the checkbox is created so our handler runs after the internal one
     // The internal handler is bound in the CheckBox constructor, so it will run first
     m_agree_checkbox->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& e) {
         // The internal handler already called update(), but we need to ensure repaint
         refresh_checkbox_visual();
         update_confirm_button_state();
         e.Skip(); // Ensure event propagates
     });

     main_sizer->Add(page_legal_panel, 1, wxEXPAND, 0);
     main_sizer->Add(page_pat_panel, 0, wxEXPAND, 0);
     main_sizer->Add(pat_button_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(30));

    //
    // Page show/hide based on helio_enable state
    //
    if (GUI::wxGetApp().app_config->get("helio_enable") == "true") {
        show_pat_page(); // This will set the title and hide the cancel button
        request_pat();
    }
    else {
        show_legal_page();
        m_button_cancel->Hide();
    }


     SetSizer(main_sizer);
     Layout();
     Fit();

     CentreOnParent();
 }

 void HelioStatementDialog::OnLoaded(wxWebViewEvent& event)
 {
     event.Skip();
 }

 void HelioStatementDialog::OnTitleChanged(wxWebViewEvent& event)
 {
     event.Skip();
 }
 void HelioStatementDialog::OnError(wxWebViewEvent& event)
 {
     event.Skip();
 }

void HelioStatementDialog::on_confirm(wxMouseEvent& e)
{
    // User agreed to terms - enable Helio and show PAT page
    wxGetApp().app_config->set_bool("helio_enable", true);
    if (wxGetApp().getAgent()) {
        json j;
        j["operate"] = "switch";
        j["content"] = "enable";
        wxGetApp().getAgent()->track_event("helio_state", j.dump());
    }

    report_consent_install();
    show_pat_page();
    request_pat();

    /*show helio on main windows*/
    if (wxGetApp().mainframe->expand_program_holder) {
        wxGetApp().mainframe->expand_program_holder->ShowExpandButton(wxGetApp().mainframe->expand_helio_id, true);
        wxGetApp().mainframe->Layout();
    }

    Layout();
    Fit();
    CentreOnParent();
}

void HelioStatementDialog::report_consent_install()
{
    json consentBody;
    json formItemArray = json::array();
    json formItem;

    formItem["formID"] = "StudioHelioTOU";
    formItem["op"] = "Opt-in";
    formItemArray.push_back(formItem);

    formItem.clear();
    formItem["formID"] = "StudioHelioNotice";
    formItem["op"] = "Opt-in";
    formItemArray.push_back(formItem);

    consentBody["version"] = 1;
    consentBody["scene"] = "helio_enable";
    consentBody["formList"] = formItemArray;

    json consent;
    consent["consentBody"] = consentBody.dump();
    std::string post_body_str = consent.dump();

    NetworkAgent* agent = GUI::wxGetApp().getAgent();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "  client_id:" << wxGetApp().app_config->get("slicer_uuid") << "\nreport_consent:" << post_body_str;

    if (agent && agent->is_user_login()) {
        agent->report_consent(post_body_str);
    }
    else {
        wxGetApp().report_consent(post_body_str);
    }
}

void HelioStatementDialog::open_url(std::string type)
{
    std::string helio_home_link =  "https://www.helioadditive.com/";
    std::string helio_privacy_link;
    std::string helio_tou_link;

    if (GUI::wxGetApp().app_config->get("region") == "China") {
        helio_privacy_link = "https://www.helioadditive.com/zh-cn/policies/privacy";
        helio_tou_link =  "https://www.helioadditive.com/zh-cn/policies/terms";
    }
    else {
        helio_privacy_link = "https://www.helioadditive.com/en-us/policies/privacy";
        helio_tou_link = "https://www.helioadditive.com/en-us/policies/terms";
    }

    if (type == "helio_link_pp") {
        wxLaunchDefaultBrowser(helio_privacy_link);
    }
    else if (type == "helio_link_tou") {
        wxLaunchDefaultBrowser(helio_tou_link);
    }
    else if (type == "helio_link_home") {
        wxLaunchDefaultBrowser(helio_home_link);
    }
    else {
        wxLaunchDefaultBrowser(helio_home_link);
    }
}

HelioStatementDialog::~HelioStatementDialog()
{
    // Restore original tooltip delay
    wxToolTip::SetDelay(m_original_tooltip_delay);
}

void HelioStatementDialog::on_dpi_changed(const wxRect &suggested_rect)
{
}

void HelioStatementDialog::show_err_info(std::string type)
{
    if (type.empty()) {
        pat_err_label->Hide();
    }
    else {
        pat_err_label->Show();

        if (type == "error") {
            pat_err_label->SetLabel(_L("Failed to get Helio PAT, Click Refresh to obtain it again."));
        }
        else if (type == "not_enough") {
            pat_err_label->SetLabel(_L("Failed to obtain PAT. The quantity limit has been reached, so it cannot be obtained. Click the refresh button to re-obtain PAT."));
        }
    }
    Layout();
    Fit();
}

void HelioStatementDialog::show_pat_option(std::string opt)
{
    if (opt == "refresh") {
        helio_pat_refresh->Hide(); // Keep hidden in new design
        helio_pat_eview->Hide();
        helio_pat_dview->Hide();
        helio_pat_copy->Hide();
        if (copy_pat_button) copy_pat_button->Hide();
    }
    else if (opt == "eview") {
        helio_pat_refresh->Hide();
        helio_pat_eview->Hide();
        helio_pat_dview->Hide();
        helio_pat_copy->Hide();
        if (copy_pat_button) copy_pat_button->Show(); // Show copy button
    }
    else if (opt == "dview") {
        helio_pat_refresh->Hide();
        helio_pat_eview->Hide();
        helio_pat_dview->Hide();
        helio_pat_copy->Hide();
        if (copy_pat_button) copy_pat_button->Show(); // Show copy button when PAT is available
    }
    Layout();
    Fit();
}

void HelioStatementDialog::create_legal_page()
{
    page_legal_panel = new wxPanel(this);
    // Use Helio dark palette (not neutral charcoal)
    page_legal_panel->SetBackgroundColour(HELIO_BG_BASE);
    
    wxBoxSizer* legal_sizer = new wxBoxSizer(wxVERTICAL);
    
    // Title
    wxBoxSizer* title_row = new wxBoxSizer(wxHORIZONTAL);
    auto main_title = new Label(page_legal_panel, Label::Head_18, _L("Legal & Activation Terms"));
    main_title->SetForegroundColour(wxColour("#FFFFFF"));
    wxFont title_font = main_title->GetFont();
    title_font.SetWeight(wxFONTWEIGHT_BOLD);
    main_title->SetFont(title_font);
    
    title_row->Add(0, 0, 1, wxEXPAND, 0);
    title_row->Add(main_title, 0, wxALIGN_CENTER, 0);
    title_row->Add(0, 0, 1, wxEXPAND, 0);
    
    // Subtitle
    auto subtitle = new Label(page_legal_panel, Label::Body_13, _L("You are activating Helio Additive's advanced simulation services."));
    subtitle->SetForegroundColour(wxColour(180, 180, 180));
    
    // Scrollable content area
    m_scroll_panel = new wxScrolledWindow(page_legal_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_scroll_panel->SetScrollRate(0, 10);
    m_scroll_panel->SetBackgroundColour(HELIO_BG_BASE);
    wxBoxSizer* scroll_sizer = new wxBoxSizer(wxVERTICAL);

    // Accordion Section 1: Software Service Terms & Conditions
    terms_section_panel = new wxPanel(m_scroll_panel);
    terms_section_panel->SetBackgroundColour(wxColour(55, 55, 59));
    wxBoxSizer* terms_section_sizer = new wxBoxSizer(wxVERTICAL);
    
    // Create header panel FIRST so labels have correct parent
    wxPanel* terms_header_panel = new wxPanel(terms_section_panel);
    terms_header_panel->SetBackgroundColour(wxColour(55, 55, 59));
    wxBoxSizer* terms_header_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    auto terms_title = new Label(terms_header_panel, Label::Body_14, _L("Software Service Terms && Conditions"));
    terms_title->SetForegroundColour(wxColour("#FFFFFF"));
    wxFont section_font = terms_title->GetFont();
    section_font.SetWeight(wxFONTWEIGHT_BOLD);
    terms_title->SetFont(section_font);
    
    auto terms_arrow = new Label(terms_header_panel, Label::Body_14, L("^"));
    terms_arrow->SetForegroundColour(wxColour("#FFFFFF"));
    
    terms_header_sizer->Add(terms_title, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(15));
    terms_header_sizer->Add(terms_arrow, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(15));
    terms_header_panel->SetSizer(terms_header_sizer);
    terms_header_panel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { toggle_terms_section(); });
    terms_title->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { toggle_terms_section(); });
    terms_arrow->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { toggle_terms_section(); });
    terms_header_panel->SetCursor(wxCURSOR_HAND);
    terms_title->SetCursor(wxCURSOR_HAND);
    terms_arrow->SetCursor(wxCURSOR_HAND);
    
    // Terms content - use HTML for proper text flow with embedded links
    terms_content_panel = new wxPanel(terms_section_panel);
    terms_content_panel->SetBackgroundColour(wxColour(55, 55, 59));
    wxBoxSizer* terms_content_sizer = new wxBoxSizer(wxVERTICAL);
    
    // Get URLs based on region
    std::string helio_home_url = "https://www.helioadditive.com/";
    std::string helio_privacy_url;
    std::string helio_tou_url;
    
    if (GUI::wxGetApp().app_config->get("region") == "China") {
        helio_privacy_url = "https://www.helioadditive.com/zh-cn/policies/privacy";
        helio_tou_url = "https://www.helioadditive.com/zh-cn/policies/terms";
    } else {
        helio_privacy_url = "https://www.helioadditive.com/en-us/policies/privacy";
        helio_tou_url = "https://www.helioadditive.com/en-us/policies/terms";
    }
    
    // Build HTML content with embedded links
    // Note: URLs are hardcoded and validated (not user input), so they are safe from XSS.
    // Translated strings from _L() are trusted content (legal terms are carefully controlled).
    // wxHtmlWindow provides basic HTML sanitization for rendered content.
    // wxString terms_html = _L("Unless otherwise specified, Bambu Lab only provides support for the software features officially provided. The slicing evaluation and slicing optimization features based on <a href=\"") + 
    //     helio_home_url + _L("\" style=\"color:#00AE42; text-decoration:underline;\">Helio Additive</a>'s cloud service in this software will be developed, operated, provided, and maintained by <a href=\"") +
    //     helio_home_url + _L("\" style=\"color:#00AE42; text-decoration:underline;\">Helio Additive</a>. Helio Additive is responsible for the effectiveness and availability of this service. The optimization feature of this service may modify the default print commands, posing a risk of printer damage. These features will collect necessary user information and data to achieve relevant service functions. Subscriptions and payments may be involved. Please visit <a href=\"") +
    //     helio_home_url + _L("\" style=\"color:#00AE42; text-decoration:underline;\">Helio Additive</a> and refer to the <a href=\"") +
    //     helio_privacy_url + _L("\" style=\"color:#00AE42; text-decoration:underline;\">Helio Additive Privacy Agreement</a> and <a href=\"") +
    //     helio_tou_url + _L("\" style=\"color:#00AE42; text-decoration:underline;\">Helio Additive User Agreement</a> for detailed information.<br><br>") +
    //     _L("Meanwhile, you understand that this product is provided to you \"as is\" based on <a href=\"") +
    //     helio_home_url + _L("\" style=\"color:#00AE42; text-decoration:underline;\">Helio Additive</a>'s services, and Bambu Lab makes no express or implied warranties of any kind, nor can it control the service effects. To the fullest extent permitted by applicable law, Bambu Lab or its licensors/affiliates do not provide any express or implied representations or warranties, including but not limited to warranties regarding merchantability, satisfactory quality, fitness for a particular purpose, accuracy, confidentiality, and non-infringement of third-party rights. Due to the nature of network services, Bambu Lab cannot guarantee that the service will be available at all times, and Bambu Lab reserves the right to terminate the service based on relevant circumstances. You agree not to use this product and its related updates to engage in the following activities:<br><br>") +
    //     _L("1. Copy or use any part of this product outside the authorized scope of Helio Additive and Bambu Lab;<br>") +
    //     _L("2. Attempt to disrupt, bypass, alter, invalidate, or evade any Digital Rights Management system related to and/or an integral part of this product;<br>") +
    //     _L("3. Using this software and services for any improper or illegal activities.<br><br>") +
    //     _L("When you confirm to enable this feature, it means that you have confirmed and agreed to the above statements.");

    const wxString STYLE_LINK = "color:#00AE42; text-decoration:underline;";
    const wxString STYLE_END = "\">";

    const wxString TAG_A_START = "<a href=\"";
    const wxString TAG_A_MID = "\" style=\"";
    const wxString TAG_A_END = "</a>";
    const wxString TAG_BR = "<br>";

    const wxString URL_HELIO = helio_home_url;
    const wxString URL_PRIVACY = helio_privacy_url;
    const wxString URL_TOU = helio_tou_url;

    const wxString TXT_HELIO = _L("Helio Additive");
    const wxString TXT_PRIVACY = _L("Helio Additive Privacy Agreement");
    const wxString TXT_TOU = _L("Helio Additive User Agreement");

    const wxString TXT_P1_S1 = _L("Unless otherwise specified, Bambu Lab only provides support for the software features officially provided. The slicing evaluation and slicing optimization features based on ");
    const wxString TXT_P1_S2 = _L("'s cloud service in this software will be developed, operated, provided, and maintained by ");
    const wxString TXT_P1_S3 = _L(". Helio Additive is responsible for the effectiveness and availability of this service. The optimization feature of this service may modify the default print commands, posing a risk of printer damage. These features will collect necessary user information and data to achieve relevant service functions. Subscriptions and payments may be involved. Please visit ");
    const wxString TXT_P1_S4 = _L(" and refer to the ");
    const wxString TXT_P1_S5 = _L(" and ");
    const wxString TXT_P1_S6 = _L(" for detailed information.");

    const wxString TXT_P2_S1 = _L("Meanwhile, you understand that this product is provided to you \"as is\" based on ");
    const wxString TXT_P2_S2 = _L("'s services, and Bambu Lab makes no express or implied warranties of any kind, nor can it control the service effects. To the fullest extent permitted by applicable law, Bambu Lab or its licensors/affiliates do not provide any express or implied representations or warranties, including but not limited to warranties regarding merchantability, satisfactory quality, fitness for a particular purpose, accuracy, confidentiality, and non-infringement of third-party rights. Due to the nature of network services, Bambu Lab cannot guarantee that the service will be available at all times, and Bambu Lab reserves the right to terminate the service based on relevant circumstances. You agree not to use this product and its related updates to engage in the following activities:");

    const wxString TXT_ITEM_1 = _L("1. Copy or use any part of this product outside the authorized scope of Helio Additive and Bambu Lab;");
    const wxString TXT_ITEM_2 = _L("2. Attempt to disrupt, bypass, alter, invalidate, or evade any Digital Rights Management system related to and/or an integral part of this product;");
    const wxString TXT_ITEM_3 = _L("3. Using this software and services for any improper or illegal activities.");

    const wxString TXT_FINAL = _L("When you confirm to enable this feature, it means that you have confirmed and agreed to the above statements.");

    #define LINK(url, text) TAG_A_START + url + TAG_A_MID + STYLE_LINK + STYLE_END + text + TAG_A_END

    wxString terms_html = 
    TXT_P1_S1 + LINK(URL_HELIO, TXT_HELIO) + 
    TXT_P1_S2 + LINK(URL_HELIO, TXT_HELIO) + 
    TXT_P1_S3 + LINK(URL_HELIO, TXT_HELIO) + 
    TXT_P1_S4 + LINK(URL_PRIVACY, TXT_PRIVACY) + 
    TXT_P1_S5 + LINK(URL_TOU, TXT_TOU) + 
    TXT_P1_S6 + TAG_BR + TAG_BR +

    TXT_P2_S1 + LINK(URL_HELIO, TXT_HELIO) + 
    TXT_P2_S2 + TAG_BR + TAG_BR +

    TXT_ITEM_1 + TAG_BR +
    TXT_ITEM_2 + TAG_BR +
    TXT_ITEM_3 + TAG_BR + TAG_BR +

    TXT_FINAL;

    #undef LINK    


    // Use wxHtmlWindow for proper text rendering with embedded links
    wxHtmlWindow* terms_html_window = new wxHtmlWindow(terms_content_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO | wxHW_NO_SELECTION);
    terms_html_window->SetBackgroundColour(wxColour(55, 55, 59));
    terms_html_window->SetMinSize(wxSize(FromDIP(560), FromDIP(300)));
    terms_html_window->SetMaxSize(wxSize(FromDIP(560), -1));
    
    // Set HTML page with proper styling
    wxString html_content = wxString::Format(
        "<html><body bgcolor=\"#37373B\" text=\"#C8C8C8\" style=\"font-family:system-ui,-apple-system,sans-serif; font-size:12px; line-height:1.5;\">%s</body></html>",
        terms_html
    );
    terms_html_window->SetPage(html_content);
    
    // Handle link clicks
    terms_html_window->Bind(wxEVT_HTML_LINK_CLICKED, [this](wxHtmlLinkEvent& event) {
        wxString url = event.GetLinkInfo().GetHref();
        if (url.StartsWith("http://") || url.StartsWith("https://")) {
            wxLaunchDefaultBrowser(url);
        }
    });
    
    terms_content_sizer->Add(terms_html_window, 1, wxEXPAND | wxALL, FromDIP(15));
    terms_content_panel->SetSizer(terms_content_sizer);
    
    terms_section_sizer->Add(terms_header_panel, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(12));
    terms_section_sizer->Add(terms_content_panel, 0, wxEXPAND, 0);
    terms_section_panel->SetSizer(terms_section_sizer);
    
    // Accordion Section 2: Special Note & Privacy Policy
    privacy_section_panel = new wxPanel(m_scroll_panel);
    privacy_section_panel->SetBackgroundColour(wxColour(55, 55, 59));
    wxBoxSizer* privacy_section_sizer = new wxBoxSizer(wxVERTICAL);
    
    // Create header panel FIRST so labels have correct parent
    wxPanel* privacy_header_panel = new wxPanel(privacy_section_panel);
    privacy_header_panel->SetBackgroundColour(wxColour(55, 55, 59));
    wxBoxSizer* privacy_header_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    auto privacy_title = new Label(privacy_header_panel, Label::Body_14, _L("Special Note && Privacy Policy"));
    privacy_title->SetForegroundColour(wxColour("#FFFFFF"));
    privacy_title->SetFont(section_font);
    
    auto privacy_arrow = new Label(privacy_header_panel, Label::Body_14, L("^"));
    privacy_arrow->SetForegroundColour(wxColour("#FFFFFF"));
    
    privacy_header_sizer->Add(privacy_title, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(15));
    privacy_header_sizer->Add(privacy_arrow, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(15));
    privacy_header_panel->SetSizer(privacy_header_sizer);
    privacy_header_panel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { toggle_privacy_section(); });
    privacy_title->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { toggle_privacy_section(); });
    privacy_arrow->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { toggle_privacy_section(); });
    privacy_header_panel->SetCursor(wxCURSOR_HAND);
    privacy_title->SetCursor(wxCURSOR_HAND);
    privacy_arrow->SetCursor(wxCURSOR_HAND);
    
    // Privacy content - FULL original text
    privacy_content_panel = new wxPanel(privacy_section_panel);
    privacy_content_panel->SetBackgroundColour(wxColour(55, 55, 59));
    wxBoxSizer* privacy_content_sizer = new wxBoxSizer(wxVERTICAL);
    
    auto privacy_text = new Label(privacy_content_panel, Label::Body_12, 
        _L("This service is provided and hosted by a third party, Helio Additive. All data collection and processing activities are solely managed by Helio Additive, and Bambu Lab assumes no responsibility in this regard. By clicking \"Agree and Proceed\", you agree to Helio Additive's privacy policy."));
    privacy_text->SetForegroundColour(wxColour(200, 200, 200));
    privacy_text->SetMinSize(wxSize(FromDIP(560), -1));
    privacy_text->Wrap(FromDIP(560));
    
    privacy_content_sizer->Add(privacy_text, 0, wxALL, FromDIP(15));
    privacy_content_panel->SetSizer(privacy_content_sizer);
    
    privacy_section_sizer->Add(privacy_header_panel, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(12));
    privacy_section_sizer->Add(privacy_content_panel, 0, wxEXPAND, 0);
    privacy_section_panel->SetSizer(privacy_section_sizer);
    
    // Add sections to scroll panel
    scroll_sizer->Add(terms_section_panel, 0, wxEXPAND, 0);
    scroll_sizer->Add(0, 0, 0, wxTOP, FromDIP(10));
    scroll_sizer->Add(privacy_section_panel, 0, wxEXPAND, 0);
    m_scroll_panel->SetSizer(scroll_sizer);
    
    // Bottom row: Checkbox on left, button on right (same row)
    wxBoxSizer* bottom_row_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_agree_checkbox = new ::CheckBox(page_legal_panel);
    m_agree_checkbox->SetValue(false); // Initialize to unchecked state
    m_agree_checkbox->Enable(true); // Ensure checkbox is enabled
    // Ensure checkbox is properly shown and laid out
    m_agree_checkbox->Show();
    
    auto checkbox_label = new Label(page_legal_panel, Label::Body_13, _L("I have read and agree to the terms\nand policies."));
    checkbox_label->SetForegroundColour(wxColour("#FFFFFF"));
    
    // Make checkbox label clickable too
    checkbox_label->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        // Toggle the checkbox value - SetValue() will call update() internally
        bool new_value = !m_agree_checkbox->GetValue();
        m_agree_checkbox->SetValue(new_value);
        refresh_checkbox_visual();
        update_confirm_button_state();
    });
    
    // Confirm button (disabled until checkbox is checked)
    StateColor btn_bg_green = StateColor(
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor btn_bg_disabled = StateColor(
        std::pair<wxColour, int>(wxColour(100, 100, 100), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    m_button_confirm = new Button(page_legal_panel, _L("Agree and Proceed"));
    m_button_confirm->SetBackgroundColor(btn_bg_disabled);
    m_button_confirm->SetBorderColor(wxColour(0, 174, 66));
    // White text for all states including Disabled
    StateColor white_text(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
                          std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Hovered),
                          std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Pressed),
                          std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Enabled),
                          std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));
    m_button_confirm->SetTextColor(white_text);
    m_button_confirm->SetTextColorNormal(wxColour(255, 255, 254));
    m_button_confirm->SetFont(Label::Body_14);
    m_button_confirm->SetSize(wxSize(FromDIP(160), FromDIP(36)));
    m_button_confirm->SetMinSize(wxSize(FromDIP(160), FromDIP(36)));
    m_button_confirm->SetCornerRadius(FromDIP(4));
    m_button_confirm->Bind(wxEVT_LEFT_DOWN, &HelioStatementDialog::on_confirm, this);
    m_button_confirm->Disable(); // Disabled until checkbox is checked
    
    bottom_row_sizer->Add(m_agree_checkbox, 0, wxALIGN_CENTER_VERTICAL, 0);
    bottom_row_sizer->Add(checkbox_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    bottom_row_sizer->Add(0, 0, 1, wxEXPAND, 0);  // Spacer to push button right
    bottom_row_sizer->Add(m_button_confirm, 0, wxALIGN_CENTER_VERTICAL, 0);
    
    // Layout
    legal_sizer->Add(0, 0, 0, wxTOP, FromDIP(20));
    legal_sizer->Add(title_row, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));
    legal_sizer->Add(subtitle, 0, wxALIGN_CENTER | wxTOP, FromDIP(8));
    legal_sizer->Add(0, 0, 0, wxTOP, FromDIP(20));
    legal_sizer->Add(m_scroll_panel, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));
    legal_sizer->Add(0, 0, 0, wxTOP, FromDIP(20));
    legal_sizer->Add(bottom_row_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(30));
    
    page_legal_panel->SetSizer(legal_sizer);
    page_legal_panel->SetMinSize(wxSize(FromDIP(680), FromDIP(600)));
    page_legal_panel->Layout();
}

void HelioStatementDialog::create_pat_page()
{
    page_pat_panel = new wxPanel(this);
    // Dark background like the success screen
    page_pat_panel->SetBackgroundColour(HELIO_BG_BASE);
    
    wxBoxSizer* pat_sizer = new wxBoxSizer(wxVERTICAL);
    
    // Success icon - using create_success or helio_feature_check
    wxStaticBitmap* success_icon = new wxStaticBitmap(page_pat_panel, wxID_ANY, create_scaled_bitmap("create_success", page_pat_panel, 80), wxDefaultPosition, wxSize(FromDIP(80), FromDIP(80)), 0);
    
    // "Activation Successful!" heading
    auto success_title = new Label(page_pat_panel, Label::Head_18, _L("Activation Successful!"));
    success_title->SetForegroundColour(wxColour("#FFFFFF"));
    wxFont title_font = success_title->GetFont();
    title_font.SetWeight(wxFONTWEIGHT_BOLD);
    success_title->SetFont(title_font);
    
    // Description text
    auto success_description = new Label(page_pat_panel, Label::Body_14, _L("Helio Additive is now active. You have unlocked free optimizations!"));
    success_description->SetForegroundColour(wxColour("#FFFFFF"));
    success_description->SetToolTip(_L("Applies only when first activated"));
    
    // "Run Your First Optimization" button - use green primary style for visibility
    StateColor btn_bg_green = StateColor(
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    
    Button* run_optimization_button = new Button(page_pat_panel, _L("Run Your First Optimization"));
    run_optimization_button->SetBackgroundColor(btn_bg_green);
    run_optimization_button->SetBorderColor(wxColour(0, 174, 66));
    // Bright white text for maximum contrast on green background
    StateColor run_btn_text(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
                            std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Enabled),
                            std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));
    run_optimization_button->SetTextColor(run_btn_text);
    run_optimization_button->SetTextColorNormal(wxColour(255, 255, 254));  // Bright white text
    run_optimization_button->SetFont(Label::Body_14);
    run_optimization_button->SetSize(wxSize(FromDIP(220), FromDIP(36)));
    run_optimization_button->SetMinSize(wxSize(FromDIP(220), FromDIP(36)));
    run_optimization_button->SetCornerRadius(FromDIP(4));
    run_optimization_button->SetToolTip(_L("You're nearly there! Now that Helio is activated, your first optimization run for faster, more reliable printing takes only minutes! (Now referred to as 'Enhance' or 'Enhancement')"));
    run_optimization_button->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        // Set tutorial flag for first-time users
        wxGetApp().app_config->set("helio_first_time_tutorial", "active");
        wxGetApp().app_config->save();
        
        // Close this dialog
        EndModal(wxID_OK);
        
        // Show first tutorial popup (don't open Helio dialog immediately - let user follow tutorial steps)
        if (wxGetApp().plater() && wxGetApp().plater()->get_notification_manager()) {
            wxString tutorial_msg = _L("Add an object to the build plate, select a material and printer that Helio supports, then slice.\n\n");
            wxString hypertext = _L("Supported printers and materials");
            std::string url = "https://wiki.helioadditive.com/en/supportedprinters";
            wxGetApp().plater()->get_notification_manager()->push_notification(
                NotificationType::CustomNotification,
                NotificationManager::NotificationLevel::HintNotificationLevel,
                into_u8(tutorial_msg),
                into_u8(hypertext),
                [url](wxEvtHandler*) {
                    wxLaunchDefaultBrowser(url);
                    return false;
                }
            );
        }
    });
    run_optimization_button->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    run_optimization_button->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
    
    // Copy PAT button - styled with high contrast for legibility
    // Use lighter background for better contrast with white text
    StateColor btn_bg_copy(std::pair<wxColour, int>(wxColour(120, 125, 135), StateColor::Hovered),
                           std::pair<wxColour, int>(wxColour(100, 105, 115), StateColor::Normal));
    
    copy_pat_button = new Button(page_pat_panel, _L("Copy PAT"));
    copy_pat_button->SetBackgroundColor(btn_bg_copy);
    copy_pat_button->SetBorderColor(wxColour(150, 155, 165));  // Lighter border for visibility
    // Bright white text for maximum contrast - ensure all states use white
    StateColor copy_btn_text(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
                             std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Hovered),
                             std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Pressed),
                             std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Enabled),
                             std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));
    copy_pat_button->SetTextColor(copy_btn_text);
    // Also set normal text color directly as fallback
    copy_pat_button->SetTextColorNormal(wxColour(255, 255, 254));
    copy_pat_button->SetFont(Label::Body_13);
    copy_pat_button->SetSize(wxSize(FromDIP(120), FromDIP(32)));
    copy_pat_button->SetMinSize(wxSize(FromDIP(120), FromDIP(32)));
    copy_pat_button->SetCornerRadius(FromDIP(4));
    copy_pat_button->SetToolTip(_L("Personal Access Token (PAT)\n\nA secure credential that verifies your identity with Helio services. "
        "It's automatically used by BambuStudio for optimizations and simulations.\n\n"
        "You may need to copy this to:\n"
        "• Integrate Helio with other slicers or tools\n"
        "• Use the Helio API directly\n"
        "• Provide to support when troubleshooting"));
    copy_pat_button->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        std::string pat = Slic3r::HelioQuery::get_helio_pat();
        if (!pat.empty()) {
            if (wxTheClipboard->Open()) {
                wxTheClipboard->Clear();
                wxTextDataObject* dataObj = new wxTextDataObject(pat);
                wxTheClipboard->SetData(dataObj);
                wxTheClipboard->Close();
            }
            MessageDialog msg(this, _L("Copy successful!"), _L("Copy"), wxOK | wxYES_DEFAULT);
            msg.ShowModal();
        }
    });
    copy_pat_button->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& e) { SetCursor(wxCURSOR_HAND); e.Skip(); });
    copy_pat_button->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) { SetCursor(wxCURSOR_ARROW); e.Skip(); });
    copy_pat_button->Hide(); // Hidden by default, will be shown when PAT is available

    // History button - opens recent runs dialog
    StateColor btn_bg_history(std::pair<wxColour, int>(wxColour(120, 125, 135), StateColor::Hovered),
                              std::pair<wxColour, int>(wxColour(100, 105, 115), StateColor::Normal));

    Button* history_button = new Button(page_pat_panel, _L("History"));
    history_button->SetBackgroundColor(btn_bg_history);
    history_button->SetBorderColor(wxColour(150, 155, 165));
    StateColor history_btn_text(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
                                std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Hovered),
                                std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Pressed),
                                std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Enabled),
                                std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));
    history_button->SetTextColor(history_btn_text);
    history_button->SetTextColorNormal(wxColour(255, 255, 254));
    history_button->SetFont(Label::Body_13);
    history_button->SetSize(wxSize(FromDIP(120), FromDIP(32)));
    history_button->SetMinSize(wxSize(FromDIP(120), FromDIP(32)));
    history_button->SetCornerRadius(FromDIP(4));
    history_button->SetToolTip(_L("View and download recent optimizations and simulations"));
    history_button->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        // Use CallAfter to defer modal dialog creation until after mouse event processing
        // This prevents crashes on Windows where opening a modal during mouse-down corrupts mouse capture state
        // Note: Do NOT call e.Skip() here - it causes multiple triggers on Mac
        CallAfter([this]() {
            try {
                BOOST_LOG_TRIVIAL(info) << "History button clicked, creating dialog...";
                HelioHistoryDialog dlg(nullptr);  // Use mainframe as parent for cross-platform compatibility
                BOOST_LOG_TRIVIAL(info) << "Dialog created, showing modal...";
                dlg.ShowModal();
                BOOST_LOG_TRIVIAL(info) << "Dialog closed";
            } catch (const std::exception& ex) {
                BOOST_LOG_TRIVIAL(error) << "Error opening History dialog: " << ex.what();
                wxMessageBox(wxString::Format("Error opening History dialog: %s", ex.what()), "Error", wxOK | wxICON_ERROR);
            } catch (...) {
                BOOST_LOG_TRIVIAL(error) << "Unknown error opening History dialog";
                wxMessageBox("Unknown error opening History dialog", "Error", wxOK | wxICON_ERROR);
            }
        });
    });
    history_button->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& e) { SetCursor(wxCURSOR_HAND); e.Skip(); });
    history_button->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) { SetCursor(wxCURSOR_ARROW); e.Skip(); });

    // Legacy controls kept for backward compatibility with show_pat_option() method
    // These are always hidden in the new design but still referenced in show_pat_option()
    // Create them with zero size and hidden to prevent any visual artifacts
    helio_pat_copy = new wxStaticBitmap(page_pat_panel, wxID_ANY, create_scaled_bitmap("helio_copy", page_pat_panel, 20), wxDefaultPosition, wxSize(0, 0), 0);
    helio_pat_copy->Hide();
    helio_pat_copy->SetSize(0, 0);
    
    helio_input_pat = new ::TextInput(page_pat_panel, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(0, 0), wxTE_PROCESS_ENTER | wxTE_RIGHT);
    helio_input_pat->Hide();
    helio_input_pat->SetSize(0, 0);
    helio_input_pat->SetMinSize(wxSize(0, 0));
    helio_input_pat->SetMaxSize(wxSize(0, 0));
    
    helio_pat_refresh = new wxStaticBitmap(page_pat_panel, wxID_ANY, create_scaled_bitmap("helio_refesh", page_pat_panel, 24), wxDefaultPosition, wxSize(0, 0), 0);
    helio_pat_refresh->Hide();
    helio_pat_refresh->SetSize(0, 0);
    
    helio_pat_eview = new wxStaticBitmap(page_pat_panel, wxID_ANY, create_scaled_bitmap("helio_eview", page_pat_panel, 24), wxDefaultPosition, wxSize(0, 0), 0);
    helio_pat_eview->Hide();
    helio_pat_eview->SetSize(0, 0);
    
    helio_pat_dview = new wxStaticBitmap(page_pat_panel, wxID_ANY, create_scaled_bitmap("helio_dview", page_pat_panel, 24), wxDefaultPosition, wxSize(0, 0), 0);
    helio_pat_dview->Hide();
    helio_pat_dview->SetSize(0, 0);
    
    pat_err_label = new Label(page_pat_panel, Label::Body_14, wxEmptyString);
    pat_err_label->SetMinSize(wxSize(FromDIP(500), -1));
    pat_err_label->SetMaxSize(wxSize(FromDIP(500), -1));
    pat_err_label->Wrap(FromDIP(500));
    pat_err_label->SetForegroundColour(wxColour("#FC8800"));
    pat_err_label->Hide(); // Hide error label by default
    
    // Links at the bottom
    wxBoxSizer* helio_links_sizer = new wxBoxSizer(wxHORIZONTAL);
    LinkLabel* helio_home_link = new LinkLabel(page_pat_panel, _L("Helio Additive"), "https://www.helioadditive.com/");
    LinkLabel* helio_privacy_link = nullptr;
    LinkLabel* helio_tou_link = nullptr;
    
    if (GUI::wxGetApp().app_config->get("region") == "China") {
        helio_privacy_link = new LinkLabel(page_pat_panel, _L("Privacy Policy"), "https://www.helioadditive.com/zh-cn/policies/privacy");
        helio_tou_link = new LinkLabel(page_pat_panel, _L("Terms of Use"), "https://www.helioadditive.com/zh-cn/policies/terms");
    } else {
        helio_privacy_link = new LinkLabel(page_pat_panel, _L("Privacy Policy"), "https://www.helioadditive.com/en-us/policies/privacy");
        helio_tou_link = new LinkLabel(page_pat_panel, _L("Terms of Use"), "https://www.helioadditive.com/en-us/policies/terms");
    }
    
    helio_home_link->SetFont(Label::Body_13);
    helio_tou_link->SetFont(Label::Body_13);
    helio_privacy_link->SetFont(Label::Body_13);
    helio_home_link->SeLinkLabelFColour(wxColour(0, 119, 250));
    helio_tou_link->SeLinkLabelFColour(wxColour(0, 119, 250));
    helio_privacy_link->SeLinkLabelFColour(wxColour(0, 119, 250));
    // Set transparent/dark background to match the dark theme
    helio_home_link->SeLinkLabelBColour(HELIO_BG_BASE);
    helio_tou_link->SeLinkLabelBColour(HELIO_BG_BASE);
    helio_privacy_link->SeLinkLabelBColour(HELIO_BG_BASE);
    
    // Add hover cursor for links
    helio_home_link->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    helio_home_link->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
    helio_tou_link->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    helio_tou_link->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
    helio_privacy_link->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    helio_privacy_link->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
    
    helio_links_sizer->Add(helio_home_link, 0, wxLEFT, 0);
    helio_links_sizer->Add(helio_privacy_link, 0, wxLEFT, FromDIP(40));
    helio_links_sizer->Add(helio_tou_link, 0, wxLEFT, FromDIP(40));
    
    // Layout: centered content
    wxBoxSizer* center_sizer = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* content_sizer = new wxBoxSizer(wxVERTICAL);
    
    content_sizer->Add(0, 0, 0, wxTOP, FromDIP(60));
    content_sizer->Add(success_icon, 0, wxALIGN_CENTER, 0);
    content_sizer->Add(0, 0, 0, wxTOP, FromDIP(24));
    content_sizer->Add(success_title, 0, wxALIGN_CENTER, 0);
    content_sizer->Add(0, 0, 0, wxTOP, FromDIP(12));
    content_sizer->Add(success_description, 0, wxALIGN_CENTER, 0);
    content_sizer->Add(0, 0, 0, wxTOP, FromDIP(32));
    
    // Button row with copy PAT and history buttons
    wxBoxSizer* button_row = new wxBoxSizer(wxHORIZONTAL);
    button_row->Add(run_optimization_button, 0, wxALIGN_CENTER, 0);
    button_row->Add(0, 0, 0, wxLEFT, FromDIP(12));
    button_row->Add(copy_pat_button, 0, wxALIGN_CENTER_VERTICAL, 0);
    button_row->Add(0, 0, 0, wxLEFT, FromDIP(12));
    button_row->Add(history_button, 0, wxALIGN_CENTER_VERTICAL, 0);

    content_sizer->Add(button_row, 0, wxALIGN_CENTER, 0);
    content_sizer->Add(0, 0, 0, wxTOP, FromDIP(40));
    content_sizer->Add(helio_links_sizer, 0, wxALIGN_CENTER, 0);
    content_sizer->Add(0, 0, 0, wxTOP, FromDIP(20));
    
    center_sizer->Add(0, 0, 1, wxEXPAND, 0);
    center_sizer->Add(content_sizer, 0, wxALIGN_CENTER, 0);
    center_sizer->Add(0, 0, 1, wxEXPAND, 0);
    
    pat_sizer->Add(center_sizer, 1, wxEXPAND, 0);
    
    page_pat_panel->SetSizer(pat_sizer);
    page_pat_panel->SetMinSize(wxSize(FromDIP(600), FromDIP(500)));
    page_pat_panel->Layout();
}

void HelioStatementDialog::toggle_terms_section()
{
    terms_expanded = !terms_expanded;
    Freeze();
    if (terms_expanded) {
        terms_content_panel->Show();
    } else {
        terms_content_panel->Hide();
    }
    if (m_scroll_panel) {
        m_scroll_panel->FitInside();
        // Clamp scroll position to valid range after content height change
        int scroll_x, scroll_y;
        m_scroll_panel->GetViewStart(&scroll_x, &scroll_y);
        int virt_h = m_scroll_panel->GetVirtualSize().GetHeight();
        int client_h = m_scroll_panel->GetClientSize().GetHeight();
        int rate_x, rate_y;
        m_scroll_panel->GetScrollPixelsPerUnit(&rate_x, &rate_y);
        if (rate_y > 0) {
            int max_scroll_units = std::max(0, (virt_h - client_h + rate_y - 1) / rate_y);
            if (scroll_y > max_scroll_units)
                m_scroll_panel->Scroll(scroll_x, max_scroll_units);
        }
    }
    page_legal_panel->Layout();
    Layout();
    Fit();
    Thaw();
}

void HelioStatementDialog::toggle_privacy_section()
{
    privacy_expanded = !privacy_expanded;
    Freeze();
    if (privacy_expanded) {
        privacy_content_panel->Show();
    } else {
        privacy_content_panel->Hide();
    }
    if (m_scroll_panel) {
        m_scroll_panel->FitInside();
        // Clamp scroll position to valid range after content height change
        int scroll_x, scroll_y;
        m_scroll_panel->GetViewStart(&scroll_x, &scroll_y);
        int virt_h = m_scroll_panel->GetVirtualSize().GetHeight();
        int client_h = m_scroll_panel->GetClientSize().GetHeight();
        int rate_x, rate_y;
        m_scroll_panel->GetScrollPixelsPerUnit(&rate_x, &rate_y);
        if (rate_y > 0) {
            int max_scroll_units = std::max(0, (virt_h - client_h + rate_y - 1) / rate_y);
            if (scroll_y > max_scroll_units)
                m_scroll_panel->Scroll(scroll_x, max_scroll_units);
        }
    }
    page_legal_panel->Layout();
    Layout();
    Fit();
    Thaw();
}

void HelioStatementDialog::refresh_checkbox_visual()
{
    // Force refresh to ensure visual update (especially important on macOS)
    if (m_agree_checkbox) {
        m_agree_checkbox->Refresh(false);
        m_agree_checkbox->Update();
        // Also refresh parent panel to ensure checkbox is visible
        if (page_legal_panel) {
            page_legal_panel->Refresh(false);
        }
    }
}

void HelioStatementDialog::update_confirm_button_state()
{
    if (m_agree_checkbox && m_agree_checkbox->GetValue()) {
        m_button_confirm->Enable();
    } else {
        m_button_confirm->Disable();
    }
}

void HelioStatementDialog::show_legal_page()
{
    current_page = 0;
    SetBackgroundColour(HELIO_BG_BASE);
    SetTitle(_L("Legal & Activation Terms"));
    page_legal_panel->Show();
    page_pat_panel->Hide();
    // m_button_confirm is now part of page_legal_panel, so it's shown/hidden with the panel
    m_button_cancel->Hide();
    Layout();
    Fit();
}

void HelioStatementDialog::show_pat_page()
{
    current_page = 1;
    SetBackgroundColour(HELIO_BG_BASE);
    SetTitle(_L("Activation Successful"));
    page_legal_panel->Hide();
    page_pat_panel->Show();
    // m_button_confirm is part of page_legal_panel, hidden with the panel
    m_button_cancel->Hide(); // Hide "Got it" button as it's redundant
    Layout();
    Fit();
}

void HelioStatementDialog::request_pat()
{
    show_err_info("");
    /*request helio pat*/
    std::string helio_api_key = Slic3r::HelioQuery::get_helio_pat();
    if (helio_api_key.empty()) {
        std::weak_ptr<int> weak_ptr = shared_ptr;
        wxGetApp().request_helio_pat([this, weak_ptr](std::string pat) {
            if (auto temp_ptr = weak_ptr.lock()) {
                if (pat == "not_enough") {
                    show_err_info("not_enough");
                    show_pat_option("refresh");
                }
                else if (pat == "error") {
                    show_err_info("error");
                    show_pat_option("refresh");
                }
                else {
                    Slic3r::HelioQuery::set_helio_pat(pat);
                    wxString wpat = wxString(pat.length(), '*');
                    helio_input_pat->SetLabel(wpat);
                    show_pat_option("dview");
                    // Show copy button when PAT is successfully obtained
                    if (copy_pat_button) {
                        copy_pat_button->Show();
                    }

                    /*request helio data*/
                    wxGetApp().request_helio_supported_data();
                }
            }
        });
    }
    else {
        show_err_info("");
        show_pat_option("dview");
        // Show copy button when PAT is available
        if (copy_pat_button) {
            copy_pat_button->Show();
        }
    }
}

HelioRemainUsageTime::HelioRemainUsageTime(wxWindow* parent,  wxString label) : wxPanel(parent)
{
    Create(label);
}

void HelioRemainUsageTime::Create(wxString label)
{
    // Use parent's background color (for card-based dark mode support)
    wxColour bg = GetParent() ? GetParent()->GetBackgroundColour() : *wxWHITE;
    SetBackgroundColour(bg);
    
    // Determine text color based on theme (not hardcoded dark mode)
    wxColour text_color;
    if (wxGetApp().dark_mode()) {
        text_color = HELIO_TEXT;  // Use Helio light text in dark mode
    } else {
        text_color = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);  // System text in light mode
    }

    Label* label_prefix = new Label(this, label);
    label_prefix->SetMinSize(wxSize(FromDIP(120), -1));  // Fixed width for alignment
    label_prefix->SetMaxSize(wxSize(FromDIP(120), -1));  // Fixed width for alignment
    label_prefix->SetToolTip(label);
    label_prefix->SetForegroundColour(text_color);
    label_prefix->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);

    m_label_remain_usage_time = new Label(this, "0");
    wxFont bold_font = m_label_remain_usage_time->GetFont();
    bold_font.SetWeight(wxFONTWEIGHT_BOLD);
    m_label_remain_usage_time->SetMinSize(wxSize(FromDIP(80), -1));  // Increased from 40 to 80 for larger numbers
    m_label_remain_usage_time->SetMaxSize(wxSize(FromDIP(120), -1));  // Allow up to 120 for very large numbers
    m_label_remain_usage_time->SetFont(bold_font);
    m_label_remain_usage_time->SetForegroundColour(text_color);
    m_label_remain_usage_time->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);

    wxBoxSizer* remain_sizer = new wxBoxSizer(wxHORIZONTAL);
    remain_sizer->Add(label_prefix, 0, wxALIGN_CENTER_VERTICAL);
    remain_sizer->Add(m_label_remain_usage_time, 0, wxALIGN_CENTER_VERTICAL);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(remain_sizer);
    SetSizer(sizer);
}

void HelioRemainUsageTime::UpdateHelpTips(int type)
{
    Layout();
    Fit();
}

void HelioRemainUsageTime::UpdateRemainTime(int remain_time)
{
    if (m_remain_usage_time != remain_time) {
        m_remain_usage_time = remain_time;
        m_label_remain_usage_time->SetLabelText(wxString::Format("%d", m_remain_usage_time));
    }
}

static int s_get_min_chamber_temp()// TODO, use the chamber of used filaments
{
    auto preset_full_config = wxGetApp().preset_bundle->full_config();
    auto chamber_temperatures = preset_full_config.option<ConfigOptionInts>("chamber_temperatures");

    if (chamber_temperatures)
    {
        int min_temp = std::numeric_limits<int>::max();
        for (auto val : chamber_temperatures->values)
        {
            if (val < min_temp)
            {
                min_temp = val;
            }
        }

        if (min_temp != std::numeric_limits<int>::max())
        {
            return min_temp;
        }
    }

    return 0;
}

static double s_round(double value, int n)
{
    double factor = std::pow(10.0, n);
    return std::round(value * factor) / factor;
}

// Named constants for Limits dropdown selection indices
static constexpr int LIMITS_HELIO_DEFAULT = 0;
static constexpr int LIMITS_SLICER_DEFAULT = 1;
// Width chosen to accommodate "Helio default (recommended)" text in dropdown
static constexpr int LIMITS_DROPDOWN_WIDTH = 350;
// Width chosen to accommodate "Preserve Surface Finish" text in dropdown
static constexpr int PRINT_PRIORITY_DROPDOWN_WIDTH = 200;

HelioInputDialogTheme HelioInputDialog::get_theme() const
{
    HelioInputDialogTheme theme;
    bool is_dark = wxGetApp().dark_mode();
    
    if (is_dark) {
        // Helio dark palette
        theme.bg = HELIO_BG_BASE;              // #07090C
        theme.card = HELIO_CARD_BG;           // #0E1320
        theme.card2 = HELIO_CARD_BG;           // #0E1320
        theme.border = HELIO_BORDER;           // rgba(255,255,255,0.10)
        theme.text = HELIO_TEXT;               // #EEF2FF
        theme.muted = HELIO_MUTED;              // #A8B0C0
        theme.purple = HELIO_PURPLE;           // #AF7CFF
        theme.blue = HELIO_BLUE;                // #4F86FF
    } else {
        // Light mode palette
        theme.bg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
        theme.card = wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT);
        theme.card2 = *wxWHITE;  // White for input fields to ensure clear contrast
        theme.border = wxColour(0, 0, 0, 25);   // Subtle dark border
        theme.text = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
        theme.muted = wxColour(100, 100, 100);  // Gray for muted text
        theme.purple = HELIO_PURPLE;           // Keep accent colors
        theme.blue = HELIO_BLUE;
    }
    return theme;
}

// Custom panel for mode cards with selection styling
class HelioModeCardPanel : public wxPanel
{
public:
    HelioModeCardPanel(wxWindow* parent) 
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
        , m_selected(false)
        , m_accent(HELIO_PURPLE)
        , m_border_colour(wxColour(255, 255, 255, 25))
    {
        // Background will be set by parent dialog using theme
        Bind(wxEVT_PAINT, &HelioModeCardPanel::OnPaint, this);
    }
    
    void SetSelected(bool selected) { 
        m_selected = selected; 
        Refresh(); 
    }
    
    void SetAccent(const wxColour& accent) { 
        m_accent = accent; 
        Refresh(); 
    }
    
    void SetBorderColour(const wxColour& border) {
        m_border_colour = border;
        Refresh();
    }
    
private:
    void OnPaint(wxPaintEvent& event)
    {
        wxPaintDC dc(this);
        wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
        
        if (gc) {
            wxRect rect = GetClientRect();
            
            auto clamp_channel = [](int value) {
                return std::max(0, std::min(255, value));
            };

            dc.SetBackground(wxBrush(GetBackgroundColour()));
            dc.Clear();
            
            wxColour base_bg = GetBackgroundColour();
            wxColour bg_color = base_bg;
            if (m_selected) {
                if (m_accent == HELIO_PURPLE) {
                    bg_color = wxColour(
                        clamp_channel(base_bg.Red() + 18),
                        clamp_channel(base_bg.Green() + 8),
                        clamp_channel(base_bg.Blue() + 18));
                } else {
                    bg_color = wxColour(
                        clamp_channel(base_bg.Red() + 8),
                        clamp_channel(base_bg.Green() + 12),
                        clamp_channel(base_bg.Blue() + 22));
                }
            }
            
            const wxDouble radius = FromDIP(10);
            gc->SetBrush(wxBrush(bg_color));
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->DrawRoundedRectangle(0, 0, rect.width, rect.height, radius);
            
            if (m_selected) {
                wxColour glow_colour(m_accent.Red(), m_accent.Green(), m_accent.Blue(), 60);
                gc->SetBrush(*wxTRANSPARENT_BRUSH);
                gc->SetPen(wxPen(glow_colour, FromDIP(4)));
                gc->DrawRoundedRectangle(FromDIP(1), FromDIP(1), rect.width - FromDIP(2), rect.height - FromDIP(2), radius + FromDIP(2));

                gc->SetPen(wxPen(m_accent, FromDIP(2)));
                gc->DrawRoundedRectangle(FromDIP(2), FromDIP(2), rect.width - FromDIP(4), rect.height - FromDIP(4), radius);
            } else {
                gc->SetBrush(*wxTRANSPARENT_BRUSH);
                gc->SetPen(wxPen(m_border_colour, 1));
                gc->DrawRoundedRectangle(0.5, 0.5, rect.width - 1, rect.height - 1, radius);
            }
            
            delete gc;
        }
    }
    
    bool m_selected;
    wxColour m_accent;
    wxColour m_border_colour;
};

class HelioCheckBadgePanel : public wxPanel
{
public:
    explicit HelioCheckBadgePanel(wxWindow* parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(20), FromDIP(20)))
        , m_accent(HELIO_PURPLE)
    {
        SetBackgroundColour(parent ? parent->GetBackgroundColour() : HELIO_CARD_BG);
        SetMinSize(wxSize(FromDIP(20), FromDIP(20)));
        SetMaxSize(wxSize(FromDIP(20), FromDIP(20)));
        Bind(wxEVT_PAINT, &HelioCheckBadgePanel::OnPaint, this);
    }

    void SetAccent(const wxColour& accent) {
        m_accent = accent;
        Refresh();
    }

    void SetVisible(bool visible) {
        Show(visible);
        if (visible) {
            Refresh();
        }
    }

private:
    void OnPaint(wxPaintEvent& event)
    {
        wxPaintDC dc(this);
        wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
        if (!gc) return;

        wxRect rect = GetClientRect();
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();

        const double cx = rect.GetWidth() / 2.0;
        const double cy = rect.GetHeight() / 2.0;
        const double radius = std::min(rect.GetWidth(), rect.GetHeight()) / 2.0 - FromDIP(1);

        gc->SetBrush(wxBrush(m_accent));
        gc->SetPen(wxPen(m_accent, 1));
        gc->DrawEllipse(cx - radius, cy - radius, radius * 2, radius * 2);

        gc->SetPen(wxPen(*wxWHITE, FromDIP(2), wxPENSTYLE_SOLID));
        wxPoint2DDouble p1(cx - radius * 0.45, cy);
        wxPoint2DDouble p2(cx - radius * 0.15, cy + radius * 0.45);
        wxPoint2DDouble p3(cx + radius * 0.5, cy - radius * 0.35);
        gc->StrokeLine(p1.m_x, p1.m_y, p2.m_x, p2.m_y);
        gc->StrokeLine(p2.m_x, p2.m_y, p3.m_x, p3.m_y);

        delete gc;
    }

    wxColour m_accent;
};

wxPanel* HelioInputDialog::create_card_panel(wxWindow* parent, const wxString& title)
{
    auto theme = get_theme();
    wxPanel* card = new wxPanel(parent);
    card->SetBackgroundColour(theme.card);
    
    // We'll set up a rounded border effect via paint event if needed
    // For now, just set a flat background
    return card;
}

void HelioInputDialog::update_mode_card_styling(int selected_action)
{
    auto theme = get_theme();

    // Helper lambda for platform-specific transparency handling
    auto make_transparent = [](wxWindow* widget, wxWindow* card_panel, bool is_selected, bool is_purple) {
        if (!widget) return;
#ifdef __WXMSW__
        // Windows: wxStaticText/wxStaticBitmap ignore transparency, match parent's painted color exactly
        wxColour bg = card_panel ? card_panel->GetBackgroundColour() : *wxWHITE;
        if (is_selected) {
            auto clamp = [](int v) { return std::max(0, std::min(255, v)); };
            if (is_purple) {
                // Purple (simulation) brightening: RGB + (18, 8, 18)
                bg = wxColour(clamp(bg.Red() + 18), clamp(bg.Green() + 8), clamp(bg.Blue() + 18));
            } else {
                // Blue (optimization) brightening: RGB + (8, 12, 22)
                bg = wxColour(clamp(bg.Red() + 8), clamp(bg.Green() + 12), clamp(bg.Blue() + 22));
            }
        }
        widget->SetBackgroundColour(bg);
#else
        // macOS: true transparency works
        widget->SetBackgroundColour(wxNullColour);
        widget->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
#endif
    };

    // Update simulation card
    if (simulation_card_panel) {
        HelioModeCardPanel* sim_card = dynamic_cast<HelioModeCardPanel*>(simulation_card_panel);
        if (sim_card) {
            sim_card->SetSelected(selected_action == 0);
            sim_card->SetAccent(theme.purple);
            sim_card->SetBorderColour(theme.border);
            sim_card->SetBackgroundColour(theme.card);
        }
        // Apply platform-specific transparency to labels
        bool sim_selected = (selected_action == 0);
        make_transparent(simulation_card_title, simulation_card_panel, sim_selected, true);
        make_transparent(simulation_card_subtitle, simulation_card_panel, sim_selected, true);
    }
    
    if (simulation_mode_icon) {
        simulation_mode_icon->SetBitmap(selected_action == 0 ? simulation_icon_color : simulation_icon_gray);
        simulation_mode_icon->SetSize(wxSize(FromDIP(28), FromDIP(28)));  // Force size to prevent resizing
        make_transparent(simulation_mode_icon, simulation_card_panel, selected_action == 0, true);
        simulation_mode_icon->Refresh();
    }

    // Update optimization card
    if (optimization_card_panel) {
        HelioModeCardPanel* opt_card = dynamic_cast<HelioModeCardPanel*>(optimization_card_panel);
        if (opt_card) {
            opt_card->SetSelected(selected_action == 1);
            opt_card->SetAccent(theme.blue);
            opt_card->SetBorderColour(theme.border);
            opt_card->SetBackgroundColour(theme.card);
        }
        // Apply platform-specific transparency to labels
        bool opt_selected = (selected_action == 1);
        make_transparent(optimization_card_title, optimization_card_panel, opt_selected, false);
        make_transparent(optimization_card_subtitle, optimization_card_panel, opt_selected, false);
    }
    
    if (optimization_mode_icon) {
        optimization_mode_icon->SetBitmap(selected_action == 1 ? optimization_icon_color : optimization_icon_gray);
        optimization_mode_icon->SetSize(wxSize(FromDIP(28), FromDIP(28)));  // Force size to prevent resizing
        make_transparent(optimization_mode_icon, optimization_card_panel, selected_action == 1, false);
        optimization_mode_icon->Refresh();
    }
    
    // Update title colors - selected = accent color, unselected = theme text
    if (simulation_card_title) {
        simulation_card_title->SetForegroundColour(selected_action == 0 ? theme.purple : theme.text);
        simulation_card_title->Refresh();
    }
    if (optimization_card_title) {
        optimization_card_title->SetForegroundColour(selected_action == 1 ? theme.blue : theme.text);
        optimization_card_title->Refresh();
    }
    
    // Update subtitle colors - selected = brighter (theme.text), unselected = muted
    if (simulation_card_subtitle) {
        simulation_card_subtitle->SetForegroundColour(selected_action == 0 ? theme.text : theme.muted);
        simulation_card_subtitle->Refresh();
    }
    if (optimization_card_subtitle) {
        optimization_card_subtitle->SetForegroundColour(selected_action == 1 ? theme.text : theme.muted);
        optimization_card_subtitle->Refresh();
    }
    
    // Refresh panels to ensure label backgrounds update
    if (simulation_card_panel) simulation_card_panel->Refresh();
    if (optimization_card_panel) optimization_card_panel->Refresh();
    
    // Check badges removed - using colored/greyscale icons instead
}

 HelioInputDialog::HelioInputDialog(wxWindow *parent /*= nullptr*/, const std::string& material_id /*= ""*/)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, wxString("Helio Additive"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX),
      m_material_id(material_id)
{
    shared_ptr = std::make_shared<int>(0);
    auto theme = get_theme();

    // Make tooltips appear faster while this dialog is open
    // Note: GetDelay() may not be available in all wxWidgets versions, so we use a default
    m_original_tooltip_delay = 500; // Default tooltip delay
    wxToolTip::SetDelay(200);

    // Set Helio icon (not BambuStudio icon)
    wxBitmap bmp = create_scaled_bitmap("helio_icon", this, 32);
    wxIcon icon;
    icon.CopyFromBitmap(bmp);
    SetIcon(icon);

    SetBackgroundColour(theme.bg);
    SetMinSize(wxSize(FromDIP(520), -1));
    SetMaxSize(wxSize(FromDIP(520), -1));

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
    
    // Custom in-content header with Helio logo and white title text
    // (macOS title bar text color cannot be changed programmatically)
    wxPanel* header_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(44)));
    header_panel->SetBackgroundColour(theme.bg);
    wxBoxSizer* header_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    // Create header icon - invert colors for light mode so it's visible
    wxBitmap header_icon_bmp = create_scaled_bitmap("helio_icon", header_panel, 24);
    if (!wxGetApp().dark_mode()) {
        // In light mode, convert to greyscale and invert to make it dark
        wxImage icon_img = header_icon_bmp.ConvertToImage();
        if (icon_img.IsOk()) {
            int target_size = FromDIP(24);
            // Ensure image stays at the correct size
            if (icon_img.GetWidth() != target_size || icon_img.GetHeight() != target_size) {
                icon_img.Rescale(target_size, target_size, wxIMAGE_QUALITY_HIGH);
            }
            unsigned char* data = icon_img.GetData();
            if (data) {
                const size_t len = static_cast<size_t>(icon_img.GetWidth()) * static_cast<size_t>(icon_img.GetHeight()) * 3;
                for (size_t i = 0; i < len; i += 3) {
                    // Convert to greyscale, then invert
                    unsigned char grey = static_cast<unsigned char>((data[i] + data[i+1] + data[i+2]) / 3);
                    unsigned char inverted = 255 - grey;
                    data[i] = inverted;     // R
                    data[i+1] = inverted;   // G
                    data[i+2] = inverted;    // B
                }
                header_icon_bmp = wxBitmap(icon_img);
            }
        }
    }
    wxStaticBitmap* header_icon = new wxStaticBitmap(header_panel, wxID_ANY, 
        header_icon_bmp, 
        wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)), 0);
    
    wxStaticText* header_title = new wxStaticText(header_panel, wxID_ANY, "HELIO ADDITIVE");
    // Ensure text is visible in both light and dark modes
    wxColour header_text_color = wxGetApp().dark_mode() ? theme.text : wxColour(0, 0, 0);  // Black in light mode, theme text in dark mode
    header_title->SetForegroundColour(header_text_color);
    wxFont header_font = header_title->GetFont();
    header_font.SetPointSize(12);
    header_font.SetWeight(wxFONTWEIGHT_SEMIBOLD);
    header_title->SetFont(header_font);
    
    header_sizer->Add(header_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(20));
    header_sizer->Add(header_title, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    header_sizer->AddStretchSpacer();
    header_panel->SetSizer(header_sizer);
    
    main_sizer->Add(header_panel, 0, wxEXPAND, 0);
    
    wxPanel *line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    line->SetBackgroundColour(theme.border);

    wxBoxSizer* control_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    // Create container for mode cards
    wxPanel* toggle_container = new wxPanel(this);
    toggle_container->SetBackgroundColour(theme.bg);
    wxBoxSizer* toggle_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    // ========== MODE CARD: SIMULATION ==========
    simulation_card_panel = new HelioModeCardPanel(toggle_container);
    simulation_card_panel->SetBackgroundColour(theme.card);
    
    // Use vertical sizer as outer wrapper to add top/bottom padding for inset from border
    wxBoxSizer* sim_outer_sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* sim_card_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    // Shield/check icon for simulation
    simulation_icon_color = create_scaled_bitmap("helio_feature_shield_check", simulation_card_panel, 28);
    wxImage sim_grey_image = simulation_icon_color.ConvertToImage().ConvertToGreyscale();
    if (sim_grey_image.IsOk()) {
        // Ensure greyscale image is exactly the same size as the color version
        int target_size = FromDIP(28);
        if (sim_grey_image.GetWidth() != target_size || sim_grey_image.GetHeight() != target_size) {
            sim_grey_image.Rescale(target_size, target_size, wxIMAGE_QUALITY_HIGH);
        }
        unsigned char* data = sim_grey_image.GetData();
        if (data) {
            const size_t len = static_cast<size_t>(sim_grey_image.GetWidth()) * static_cast<size_t>(sim_grey_image.GetHeight()) * 3;
            for (size_t i = 0; i < len; ++i) {
                int scaled = static_cast<int>(data[i] * 0.9);
                data[i] = static_cast<unsigned char>(std::max(0, std::min(255, scaled)));
            }
        }
    }
    simulation_icon_gray = wxBitmap(sim_grey_image);
    simulation_mode_icon = new wxStaticBitmap(simulation_card_panel, wxID_ANY, 
        simulation_icon_color, 
        wxDefaultPosition, wxSize(FromDIP(28), FromDIP(28)), 0);
    simulation_mode_icon->SetMinSize(wxSize(FromDIP(28), FromDIP(28)));
    simulation_mode_icon->SetMaxSize(wxSize(FromDIP(28), FromDIP(28)));
    simulation_mode_icon->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_selected_simulation, this);
    
    wxBoxSizer* sim_text_sizer = new wxBoxSizer(wxVERTICAL);
    simulation_card_title = new Label(simulation_card_panel, Label::Head_14, _L("Assess"));
    simulation_card_title->SetForegroundColour(theme.text);
    simulation_card_title->SetToolTip(_L("Formerly referred to as 'Simulate' or 'Simulation'"));
    wxFont sim_title_font = simulation_card_title->GetFont();
    sim_title_font.SetWeight(wxFONTWEIGHT_BOLD);
    simulation_card_title->SetFont(sim_title_font);
    
    simulation_card_subtitle = new Label(simulation_card_panel, Label::Body_12, _L("See where it will fail"));
    simulation_card_subtitle->SetForegroundColour(theme.muted);
    simulation_card_subtitle->SetMinSize(wxSize(FromDIP(130), -1));
    simulation_card_subtitle->Wrap(FromDIP(130));
    
    // Make labels transparent to avoid black rectangles (selection highlight artifact)
    // Only use transparent style - do NOT also set background colour which conflicts
    simulation_card_title->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
    simulation_card_subtitle->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
    
    sim_text_sizer->Add(simulation_card_title, 0, wxALIGN_LEFT, 0);
    sim_text_sizer->Add(simulation_card_subtitle, 0, wxEXPAND | wxTOP, FromDIP(2));
    
    sim_card_sizer->Add(simulation_mode_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(16));
    sim_card_sizer->Add(sim_text_sizer, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(12));
    sim_card_sizer->Add(0, 0, 0, wxRIGHT, FromDIP(16));  // Spacer instead of check badge
    
    // Add the horizontal content sizer to outer vertical sizer with top/bottom inset padding
    sim_outer_sizer->Add(sim_card_sizer, 1, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(10));
    simulation_card_panel->SetSizer(sim_outer_sizer);
    // Mode cards with comfortable vertical padding
    simulation_card_panel->SetMinSize(wxSize(FromDIP(240), FromDIP(70)));
    simulation_card_panel->SetMaxSize(wxSize(FromDIP(260), FromDIP(80)));
    
    // Bind click events on the whole card
    simulation_card_panel->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_selected_simulation, this);
    simulation_card_title->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_selected_simulation, this);
    simulation_card_subtitle->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_selected_simulation, this);
    simulation_mode_icon->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_selected_simulation, this);
    
    // Show hand cursor on the entire card and all children
    auto sim_set_cursor = [](wxSetCursorEvent& e) { e.SetCursor(wxCURSOR_HAND); };
    simulation_card_panel->Bind(wxEVT_SET_CURSOR, sim_set_cursor);
    simulation_card_title->Bind(wxEVT_SET_CURSOR, sim_set_cursor);
    simulation_card_subtitle->Bind(wxEVT_SET_CURSOR, sim_set_cursor);
    simulation_mode_icon->Bind(wxEVT_SET_CURSOR, sim_set_cursor);
    
    // ========== MODE CARD: OPTIMIZATION ==========
    optimization_card_panel = new HelioModeCardPanel(toggle_container);
    optimization_card_panel->SetBackgroundColour(theme.card);
    
    // Use vertical sizer as outer wrapper to add top/bottom padding for inset from border
    wxBoxSizer* opt_outer_sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* opt_card_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    // Speed icon for optimization
    optimization_icon_color = create_scaled_bitmap("helio_feature_speed", optimization_card_panel, 28);
    wxImage opt_grey_image = optimization_icon_color.ConvertToImage().ConvertToGreyscale();
    if (opt_grey_image.IsOk()) {
        // Ensure greyscale image is exactly the same size as the color version
        int target_size = FromDIP(28);
        if (opt_grey_image.GetWidth() != target_size || opt_grey_image.GetHeight() != target_size) {
            opt_grey_image.Rescale(target_size, target_size, wxIMAGE_QUALITY_HIGH);
        }
        unsigned char* data = opt_grey_image.GetData();
        if (data) {
            const size_t len = static_cast<size_t>(opt_grey_image.GetWidth()) * static_cast<size_t>(opt_grey_image.GetHeight()) * 3;
            for (size_t i = 0; i < len; ++i) {
                int scaled = static_cast<int>(data[i] * 0.9);
                data[i] = static_cast<unsigned char>(std::max(0, std::min(255, scaled)));
            }
        }
    }
    optimization_icon_gray = wxBitmap(opt_grey_image);
    optimization_mode_icon = new wxStaticBitmap(optimization_card_panel, wxID_ANY, 
        optimization_icon_color, 
        wxDefaultPosition, wxSize(FromDIP(28), FromDIP(28)), 0);
    optimization_mode_icon->SetMinSize(wxSize(FromDIP(28), FromDIP(28)));
    optimization_mode_icon->SetMaxSize(wxSize(FromDIP(28), FromDIP(28)));
    optimization_mode_icon->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_selected_optimaztion, this);
    
    wxBoxSizer* opt_text_sizer = new wxBoxSizer(wxVERTICAL);
    optimization_card_title = new Label(optimization_card_panel, Label::Head_14, _L("Enhance"));
    optimization_card_title->SetForegroundColour(theme.text);
    optimization_card_title->SetToolTip(_L("Formerly referred to as 'Optimize' or 'Optimization'"));
    wxFont opt_title_font = optimization_card_title->GetFont();
    opt_title_font.SetWeight(wxFONTWEIGHT_BOLD);
    optimization_card_title->SetFont(opt_title_font);
    
    optimization_card_subtitle = new Label(optimization_card_panel, Label::Body_12, _L("Auto-fix with new speeds"));
    optimization_card_subtitle->SetForegroundColour(theme.muted);
    optimization_card_subtitle->SetMinSize(wxSize(FromDIP(130), -1));
    optimization_card_subtitle->Wrap(FromDIP(130));
    
    // Make labels transparent to avoid black rectangles (selection highlight artifact)
    // Only use transparent style - do NOT also set background colour which conflicts
    optimization_card_title->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
    optimization_card_subtitle->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
    
    opt_text_sizer->Add(optimization_card_title, 0, wxALIGN_LEFT, 0);
    opt_text_sizer->Add(optimization_card_subtitle, 0, wxEXPAND | wxTOP, FromDIP(2));
    
    opt_card_sizer->Add(optimization_mode_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(16));
    opt_card_sizer->Add(opt_text_sizer, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(12));
    opt_card_sizer->Add(0, 0, 0, wxRIGHT, FromDIP(16));  // Spacer instead of check badge
    
    // Add the horizontal content sizer to outer vertical sizer with top/bottom inset padding
    opt_outer_sizer->Add(opt_card_sizer, 1, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(10));
    optimization_card_panel->SetSizer(opt_outer_sizer);
    // Mode cards with comfortable vertical padding
    optimization_card_panel->SetMinSize(wxSize(FromDIP(240), FromDIP(70)));
    optimization_card_panel->SetMaxSize(wxSize(FromDIP(260), FromDIP(80)));
    
    // Bind click events on the whole card
    optimization_card_panel->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_selected_optimaztion, this);
    optimization_card_title->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_selected_optimaztion, this);
    optimization_card_subtitle->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_selected_optimaztion, this);
    optimization_mode_icon->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_selected_optimaztion, this);
    
    // Show hand cursor on the entire card and all children
    auto opt_set_cursor = [](wxSetCursorEvent& e) { e.SetCursor(wxCURSOR_HAND); };
    optimization_card_panel->Bind(wxEVT_SET_CURSOR, opt_set_cursor);
    optimization_card_title->Bind(wxEVT_SET_CURSOR, opt_set_cursor);
    optimization_card_subtitle->Bind(wxEVT_SET_CURSOR, opt_set_cursor);
    optimization_mode_icon->Bind(wxEVT_SET_CURSOR, opt_set_cursor);

    // Create hidden toggle buttons for backward compatibility with update_action
    togglebutton_simulate = new CustomToggleButton(this, _L("Assess"));
    togglebutton_optimize = new CustomToggleButton(this, _L("Enhance"));
    togglebutton_simulate->Hide();
    togglebutton_optimize->Hide();

    // Add mode cards to horizontal sizer
    toggle_sizer->Add(simulation_card_panel, 1, wxEXPAND | wxRIGHT, FromDIP(8));
    toggle_sizer->Add(optimization_card_panel, 1, wxEXPAND, 0);
    
    toggle_container->SetSizer(toggle_sizer);
    toggle_container->Layout();
    
    control_sizer->Add(toggle_container, 1, wxEXPAND, 0);

    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    auto source_model = preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle);
    is_no_chamber = !DevPrinterConfigUtil::get_value_from_config<bool>(source_model, "print", "support_chamber");

    /*Simulation panel - wrapped in a card*/
    panel_simulation = new wxPanel(this);
    panel_simulation->SetBackgroundColour(theme.bg);
    wxBoxSizer *sizer_simulation = new wxBoxSizer(wxVERTICAL);
    
    // Create card wrapper for simulation content
    card_simulation = new wxPanel(panel_simulation);
    card_simulation->SetBackgroundColour(theme.card);
    wxBoxSizer* card_sim_sizer = new wxBoxSizer(wxVERTICAL);

    /*chamber temp*/
    auto chamber_temp_checker = TextInputValChecker::CreateDoubleRangeChecker(5.0, 70.0, true);
    wxBoxSizer* chamber_temp_item_for_simulation = create_input_item(card_simulation, "chamber_temp_for_simulation", is_no_chamber?_L("Environment Temperature"):_L("Chamber Temperature"), wxT("\u00B0C"), {chamber_temp_checker});

    m_input_items["chamber_temp_for_simulation"]->GetTextCtrl()->SetHint(wxT("5-70"));
    wxString sim_temp_tooltip = _L("Refers to the environment temperature of the print (i.e. A-series - room temperature). Changing chamber temperature when running an assessment/simulation allows you to play around with different temperature scenarios.");
    m_input_items["chamber_temp_for_simulation"]->GetTextCtrl()->SetToolTip(sim_temp_tooltip);
    m_input_items["chamber_temp_for_simulation"]->SetToolTip(sim_temp_tooltip);

    Label* sub_simulation = new Label(card_simulation, _L("Optional. More accurate temperatures ensures better results."));
    sub_simulation->SetForegroundColour(theme.muted);
    sub_simulation->SetSize(wxSize(FromDIP(420), -1));
    sub_simulation->Wrap(FromDIP(420));

    card_sim_sizer->Add(0, 0, 0, wxTOP, FromDIP(8));
    card_sim_sizer->Add(chamber_temp_item_for_simulation, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    card_sim_sizer->Add(0, 0, 0, wxTOP, FromDIP(4));
    card_sim_sizer->Add(sub_simulation, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    card_sim_sizer->Add(0, 0, 0, wxTOP, FromDIP(6));
    
    // Wiki link will be added inside the simulation card
    // (moved from bottom of dialog to inside card for simulation mode)
    
    card_simulation->SetSizer(card_sim_sizer);
    card_simulation->Layout();
    
    sizer_simulation->Add(card_simulation, 0, wxEXPAND, 0);
    panel_simulation->SetSizer(sizer_simulation);
    panel_simulation->Layout();
    panel_simulation->Fit();

    /*Optimization Pay panel*/
    panel_pay_optimization = new wxPanel(this);
    panel_pay_optimization->SetBackgroundColour(theme.bg);
    wxBoxSizer* sizer_pay_optimization = new wxBoxSizer(wxVERTICAL);
    wxStaticBitmap* panel_pay_loading_icon = new wxStaticBitmap(panel_pay_optimization, wxID_ANY, create_scaled_bitmap("helio_loading", panel_pay_optimization, 109), wxDefaultPosition,
        wxSize(FromDIP(136), FromDIP(109)));

    Label* pay_sub = new Label(panel_pay_optimization, _L("Helio's advanced feature for power users: available post purchase on its official website."));
    pay_sub->SetForegroundColour(theme.muted);
    pay_sub->SetSize(wxSize(FromDIP(440), -1));
    pay_sub->Wrap(FromDIP(440));

    sizer_pay_optimization->Add(0, 0, 0, wxTOP, FromDIP(26));
    sizer_pay_optimization->Add(panel_pay_loading_icon, 0, wxALIGN_CENTER, 0);
    sizer_pay_optimization->Add(0, 0, 0, wxTOP, FromDIP(36));
    sizer_pay_optimization->Add(pay_sub, 0, wxLEFT, 0);

    panel_pay_optimization->SetSizer(sizer_pay_optimization);
    panel_pay_optimization->Layout();
    panel_pay_optimization->Fit();
    panel_pay_optimization->Hide();

    /*Optimization panel*/
    panel_optimization = new wxPanel(this);
    panel_optimization->SetBackgroundColour(theme.bg);
    wxBoxSizer *sizer_optimization = new wxBoxSizer(wxVERTICAL);

    // ========== CARD 1: ACCOUNT STATUS ==========
    card_account_status = new wxPanel(panel_optimization);
    card_account_status->SetBackgroundColour(theme.card);
    wxBoxSizer* card_account_sizer = new wxBoxSizer(wxVERTICAL);
    
    Label* account_header = new Label(card_account_status, Label::Head_14, _L("Account Status"));
    account_header->SetForegroundColour(theme.text);
    wxFont acct_font = account_header->GetFont();
    acct_font.SetWeight(wxFONTWEIGHT_BOLD);
    account_header->SetFont(acct_font);
    
    // Use a 2-column grid sizer for proper column alignment
    wxFlexGridSizer* quota_grid = new wxFlexGridSizer(2, FromDIP(4), FromDIP(12)); // 2 cols, vgap=4, hgap=12
    quota_grid->AddGrowableCol(0, 1); // label column can expand
    
    wxFont bold_font = Label::Body_13;
    bold_font.SetWeight(wxFONTWEIGHT_BOLD);
    
    // Subscription row
    Label* label_subscription = new Label(card_account_status, _L("Subscription:"));
    label_subscription->SetForegroundColour(theme.text);
    m_label_subscription = new Label(card_account_status, "-");
    m_label_subscription->SetFont(bold_font);
    m_label_subscription->SetForegroundColour(theme.text);
    m_label_subscription->SetToolTip(_L("Loading subscription status..."));
    
    quota_grid->Add(label_subscription, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
    quota_grid->Add(m_label_subscription, 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL);
    
    // Monthly quota row
    Label* label_monthly_quota = new Label(card_account_status, _L("Monthly quota:"));
    label_monthly_quota->SetForegroundColour(theme.text);
    m_label_monthly_quota = new Label(card_account_status, "0");
    m_label_monthly_quota->SetFont(bold_font);
    m_label_monthly_quota->SetForegroundColour(theme.text);
    
    quota_grid->Add(label_monthly_quota, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
    quota_grid->Add(m_label_monthly_quota, 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL);
    
    // Add-ons row
    Label* label_addons = new Label(card_account_status, _L("Add-ons:"));
    label_addons->SetForegroundColour(theme.text);
    m_label_addons = new Label(card_account_status, "0");
    m_label_addons->SetFont(bold_font);
    m_label_addons->SetForegroundColour(theme.text);
    
    quota_grid->Add(label_addons, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
    quota_grid->Add(m_label_addons, 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL);

    // Create Plans / Upgrades button
    StateColor btn_buy_bg_outlined;
    StateColor btn_buy_border;
    StateColor btn_buy_text;
    
    if (wxGetApp().dark_mode()) {
        // Dark mode: purple fill on hover, white icon always visible
        btn_buy_bg_outlined = StateColor(std::pair<wxColour, int>(wxColour(175, 124, 255), StateColor::Hovered), 
                                         std::pair<wxColour, int>(theme.card2, StateColor::Normal),
                                         std::pair<wxColour, int>(wxColour(175, 124, 255), StateColor::Pressed));
        btn_buy_text = StateColor(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Hovered),
                                  std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Pressed),
                                  std::pair<wxColour, int>(wxColour(175, 124, 255), StateColor::Normal));
    } else {
        // Light mode: light purple tint on hover so purple icon stays visible
        btn_buy_bg_outlined = StateColor(std::pair<wxColour, int>(wxColour(235, 220, 255), StateColor::Hovered), 
                                         std::pair<wxColour, int>(theme.card2, StateColor::Normal),
                                         std::pair<wxColour, int>(wxColour(220, 200, 255), StateColor::Pressed));
        btn_buy_text = StateColor(std::pair<wxColour, int>(wxColour(130, 80, 200), StateColor::Hovered),
                                  std::pair<wxColour, int>(wxColour(130, 80, 200), StateColor::Pressed),
                                  std::pair<wxColour, int>(wxColour(175, 124, 255), StateColor::Normal));
    }
    btn_buy_border = StateColor(std::pair<wxColour, int>(wxColour(175, 124, 255), StateColor::Normal));
    
    buy_now_button = new Button(card_account_status, _L("Plans / Upgrades"), wxGetApp().dark_mode() ? "topbar_store" : "topbar_store_dark", 0, 16);
    buy_now_button->SetBackgroundColor(btn_buy_bg_outlined);
    buy_now_button->SetBorderColor(btn_buy_border);
    buy_now_button->SetTextColor(btn_buy_text);
    buy_now_button->SetFont(Label::Body_13);
    buy_now_button->SetSize(wxSize(-1, FromDIP(28)));
    buy_now_button->SetMinSize(wxSize(-1, FromDIP(28)));
    buy_now_button->SetCornerRadius(FromDIP(12));
    buy_now_button->SetToolTip(_L("Loading..."));
    buy_now_button->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        std::string helio_api_key = Slic3r::HelioQuery::get_helio_pat();
        if (!helio_api_key.empty()) {
            wxString link_url;
            if (wxGetApp().app_config->get("region") == "China") {
                link_url = "https://store.helioam.cn?patToken=" + helio_api_key;
            } else {
                link_url = "https://store.helioadditive.com?patToken=" + helio_api_key;
            }
            wxLaunchDefaultBrowser(link_url);
        }
    });
    buy_now_button->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    buy_now_button->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
    buy_now_button->Hide();

    buy_now_link = new LinkLabel(card_account_status, _L("Manage Account"), "https://wiki.helioadditive.com/");
    buy_now_link->SeLinkLabelFColour(wxColour(175, 124, 255));
#ifdef __WXMSW__
    buy_now_link->SeLinkLabelBColour(card_account_status->GetBackgroundColour());
#else
    buy_now_link->SeLinkLabelBColour(wxNullColour);
    buy_now_link->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
#endif
    buy_now_link->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    buy_now_link->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
    buy_now_link->Hide();

    // Layout for account status card
    wxBoxSizer* secondary_actions_sizer = new wxBoxSizer(wxHORIZONTAL);
    secondary_actions_sizer->Add(0, 0, 1, wxEXPAND, 0);
    secondary_actions_sizer->Add(buy_now_link, 0, wxALIGN_CENTER_VERTICAL, 0);
    secondary_actions_sizer->Add(0, 0, 0, wxLEFT, FromDIP(12));
    secondary_actions_sizer->Add(buy_now_button, 0, 0, 0);
    
    card_account_sizer->Add(account_header, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    card_account_sizer->Add(quota_grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
    card_account_sizer->Add(secondary_actions_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(12));
    card_account_status->SetSizer(card_account_sizer);

    // ========== CARD 2: ENVIRONMENT SETTINGS ==========
    card_environment = new wxPanel(panel_optimization);
    card_environment->SetBackgroundColour(theme.card);
    wxBoxSizer* card_env_sizer = new wxBoxSizer(wxVERTICAL);
    
    Label* env_header = new Label(card_environment, Label::Head_14, _L("Environment Settings"));
    env_header->SetForegroundColour(theme.text);
    wxFont env_font = env_header->GetFont();
    env_font.SetWeight(wxFONTWEIGHT_BOLD);
    env_header->SetFont(env_font);
    
    wxBoxSizer* chamber_temp_item_for_optimization = nullptr;
    Label* sub_optimization = nullptr;
    if (is_no_chamber) {
        chamber_temp_item_for_optimization = create_input_item(card_environment, "chamber_temp_for_optimization", is_no_chamber?_L("Environment Temperature"):_L("Chamber Temperature"), wxT("\u00B0C"), {chamber_temp_checker});
        m_input_items["chamber_temp_for_optimization"]->GetTextCtrl()->SetHint(wxT("5-70"));
        wxString temp_tooltip = _L("Refers to the environment temperature of the print (i.e. A-series - room temperature). Changing chamber temperature when running an assessment/simulation allows you to play around with different temperature scenarios.");
        m_input_items["chamber_temp_for_optimization"]->GetTextCtrl()->SetToolTip(temp_tooltip);
        m_input_items["chamber_temp_for_optimization"]->SetToolTip(temp_tooltip);

        sub_optimization = new Label(card_environment, _L("Optional. More accurate temperatures ensures better results."));
        sub_optimization->SetForegroundColour(theme.muted);
        sub_optimization->SetSize(wxSize(FromDIP(400), -1));
        sub_optimization->Wrap(FromDIP(400));
    } else {
        sub_optimization = new Label(card_environment, _L("Uses the chamber temperature from Filament settings when available; otherwise estimates it from the build plate temperature."));
        sub_optimization->SetForegroundColour(theme.muted);
        sub_optimization->SetFont(Label::Body_12);
        sub_optimization->SetSize(wxSize(FromDIP(400), -1));
        sub_optimization->Wrap(FromDIP(400));
    }
    
    // Layout environment card
    card_env_sizer->Add(env_header, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
    if (is_no_chamber && chamber_temp_item_for_optimization) {
        card_env_sizer->Add(chamber_temp_item_for_optimization, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    }
    card_env_sizer->Add(sub_optimization, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(10));
    card_environment->SetSizer(card_env_sizer);

    // ========== CARD 3: OPTIMIZATION SETTINGS ==========
    card_optimization_settings = new wxPanel(panel_optimization);
    card_optimization_settings->SetBackgroundColour(theme.card);
    wxBoxSizer* card_opt_sizer = new wxBoxSizer(wxVERTICAL);
    
    Label* opt_header = new Label(card_optimization_settings, Label::Head_14, _L("Optimization Settings"));
    opt_header->SetForegroundColour(theme.text);
    opt_header->SetToolTip(_L("Now referred to as 'Enhance' or 'Enhancement'"));
    wxFont opt_font = opt_header->GetFont();
    opt_font.SetWeight(wxFONTWEIGHT_BOLD);
    opt_header->SetFont(opt_font);

    auto outerwall = create_print_priority_combo(card_optimization_settings);

    auto plater = Slic3r::GUI::wxGetApp().plater();
    int layer_count = plater ? plater->get_gcode_layers_count() : 0;
    wxBoxSizer* layers_to_optimize_item = create_input_optimize_layers(card_optimization_settings, layer_count);

    std::map<int, wxString> config_limits;
    config_limits[LIMITS_HELIO_DEFAULT] = _L("Helio default (recommended)");
    config_limits[LIMITS_SLICER_DEFAULT] = _L("Slicer default");
    auto limits = create_combo_item(card_optimization_settings, "limits", _L("Limits"), config_limits, LIMITS_HELIO_DEFAULT, LIMITS_DROPDOWN_WIDTH);
    
    wxString limits_tooltip = _L("Set your own speed and flow rate limits - or rely on Helio's custom limit settings. With unsupported materials you need to supply your own data or rely on the slicer's built in data.");
    if (m_combo_items.find("limits") != m_combo_items.end()) {
        m_combo_items["limits"]->SetToolTip(limits_tooltip);
    }

    // Create panel for velocity and volumetric speed fields
    panel_velocity_volumetric = new wxPanel(card_optimization_settings);
    panel_velocity_volumetric->SetBackgroundColour(theme.card);
    wxBoxSizer* sizer_velocity_volumetric = new wxBoxSizer(wxVERTICAL);

    // velocity and volumetric speed fields
    auto double_min_checker = TextInputValChecker::CreateDoubleMinChecker(0);

    // velocity
    float min_speed = 0.0;
    float max_speed = 0.0;
    if (plater) { plater->get_preview_min_max_value_of_option((int) gcode::EViewType::Feedrate, min_speed, max_speed); }
    wxBoxSizer* min_velocity_item = create_input_item(panel_velocity_volumetric, "min_velocity", _L("Min Velocity"), wxT("mm/s"), { double_min_checker } );
    m_input_items["min_velocity"]->GetTextCtrl()->SetLabel(wxString::Format("%.0f", s_round(min_speed, 0)));

    wxBoxSizer* max_velocity_item = create_input_item(panel_velocity_volumetric, "max_velocity", _L("Max Velocity"), wxT("mm/s"), { double_min_checker });
    m_input_items["max_velocity"]->GetTextCtrl()->SetLabel(wxString::Format("%.0f", s_round(max_speed, 0)));

    // volumetric speed
    float min_volumetric_speed = 0.0;
    float max_volumetric_speed = 0.0;
    if (plater) { plater->get_preview_min_max_value_of_option((int) gcode::EViewType::VolumetricRate, min_volumetric_speed, max_volumetric_speed); }
    wxBoxSizer* min_volumetric_speed_item = create_input_item(panel_velocity_volumetric, "min_volumetric_speed", _L("Min Volumetric Speed"), wxT("mm\u00B3/s"), { double_min_checker });
    m_input_items["min_volumetric_speed"]->GetTextCtrl()->SetLabel(wxString::Format("%.2f", s_round(min_volumetric_speed, 2)));

    wxBoxSizer* max_volumetric_speed_item = create_input_item(panel_velocity_volumetric, "max_volumetric_speed", _L("Max Volumetric Speed"), wxT("mm\u00B3/s"), { double_min_checker });
    m_input_items["max_volumetric_speed"]->GetTextCtrl()->SetLabel(wxString::Format("%.2f", s_round(max_volumetric_speed, 2)));

    sizer_velocity_volumetric->Add(min_velocity_item, 0, wxEXPAND, 0);
    sizer_velocity_volumetric->Add(max_velocity_item, 0, wxEXPAND|wxTOP, FromDIP(6));
    sizer_velocity_volumetric->Add(min_volumetric_speed_item, 0, wxEXPAND|wxTOP, FromDIP(6));
    sizer_velocity_volumetric->Add(max_volumetric_speed_item, 0, wxEXPAND|wxTOP, FromDIP(6));
    panel_velocity_volumetric->SetSizer(sizer_velocity_volumetric);
    panel_velocity_volumetric->Layout();
    panel_velocity_volumetric->Fit();

    // Hide velocity/volumetric speed fields by default (Helio default selected)
    panel_velocity_volumetric->Hide();

    // Add event handler for Limits dropdown to show/hide velocity/volumetric speed fields
    m_combo_items["limits"]->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& e) {
        // Prevent changing if only_advanced_settings is true (forced mode)
        if (only_advanced_settings) {
            m_combo_items["limits"]->SetSelection(LIMITS_SLICER_DEFAULT);
            return;
        }
        
        int selection = m_combo_items["limits"]->GetSelection();
        bool show_fields = (selection == LIMITS_SLICER_DEFAULT);
        use_advanced_settings = show_fields;
        
        if (show_fields) {
            panel_velocity_volumetric->Show();
        } else {
            panel_velocity_volumetric->Hide();
        }
        card_optimization_settings->Layout();
        panel_optimization->Layout();
        panel_optimization->Fit();
        Layout();
        Fit();
    });

    // Create empty panel_advanced_option for backward compatibility (kept hidden)
    panel_advanced_option = new wxPanel(card_optimization_settings);
    panel_advanced_option->SetBackgroundColour(theme.card);
    panel_advanced_option->Hide();

    // Layout optimization settings card
    card_opt_sizer->Add(opt_header, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
    card_opt_sizer->Add(outerwall, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    card_opt_sizer->Add(layers_to_optimize_item, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    card_opt_sizer->Add(limits, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    card_opt_sizer->Add(panel_velocity_volumetric, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));
    card_opt_sizer->Add(0, 0, 0, wxBOTTOM, FromDIP(10));
    card_optimization_settings->SetSizer(card_opt_sizer);

    // Stack the 3 cards in the optimization panel
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(10));
    sizer_optimization->Add(card_account_status, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(8));
    sizer_optimization->Add(card_environment, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(8));
    sizer_optimization->Add(card_optimization_settings, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(6));

    panel_optimization->SetSizer(sizer_optimization);
    panel_optimization->Layout();
    panel_optimization->Fit();

    panel_optimization->Hide();

    /*last trace id*/
    last_tid_panel = new wxPanel(this);
    last_tid_panel->SetBackgroundColour(theme.bg);
    wxBoxSizer* last_tid_sizer = new wxBoxSizer(wxHORIZONTAL);

    Label* last_tid_title = new Label(last_tid_panel, _L("Last Trace ID: "));
    last_tid_title->SetBackgroundColour(theme.bg);
    last_tid_title->SetForegroundColour(theme.muted);
    last_tid_label = new Label(last_tid_panel, wxEmptyString);
    last_tid_label->SetBackgroundColour(theme.bg);
    last_tid_label->SetForegroundColour(theme.muted);
    wxStaticBitmap* helio_tid_copy = new wxStaticBitmap(last_tid_panel, wxID_ANY, create_scaled_bitmap("helio_copy", last_tid_panel, 24), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)), 0);
    helio_tid_copy->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    helio_tid_copy->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });

    helio_tid_copy->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->Clear();
            wxTextDataObject* dataObj = new wxTextDataObject(last_tid_label->GetLabel());
            wxTheClipboard->SetData(dataObj);
            wxTheClipboard->Close();
        }
        MessageDialog msg(this, _L("Copy successful!"), _L("Copy"), wxOK | wxYES_DEFAULT);
        msg.ShowModal();
        });

    last_tid_sizer->Add(last_tid_title, 0, wxALIGN_CENTER, 0);
    last_tid_sizer->Add(last_tid_label, 0, wxALIGN_CENTER, 0);
    last_tid_sizer->Add(0, 0, 0, wxLEFT, FromDIP(6));
    last_tid_sizer->Add(helio_tid_copy, 0, wxALIGN_CENTER, 0);
    last_tid_panel->SetSizer(last_tid_sizer);
    last_tid_panel->Layout();
    last_tid_panel->Fit();

    /*helio wiki - placed inside simulation card for simulation mode*/
    helio_wiki_link = new LinkLabel(card_simulation, _L("Click for more details"), wxGetApp().app_config->get("language") =="zh_CN"? "https://wiki.helioadditive.com/zh/home" : "https://wiki.helioadditive.com/en/home");
    helio_wiki_link->SeLinkLabelFColour(theme.purple);
#ifdef __WXMSW__
    helio_wiki_link->SeLinkLabelBColour(card_simulation->GetBackgroundColour());
#else
    helio_wiki_link->SeLinkLabelBColour(wxNullColour);
    helio_wiki_link->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
#endif
    helio_wiki_link->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    helio_wiki_link->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
    // Add wiki link to simulation card
    card_sim_sizer->Add(helio_wiki_link, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(20));

    /*Primary action button - Start Optimization/Simulation - LARGE CTA*/
    wxBoxSizer* button_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    // Purple color for simulation mode (default will be set in update_action)
    StateColor btn_primary_bg(std::pair<wxColour, int>(wxColour(195, 144, 255), StateColor::Hovered), 
                              std::pair<wxColour, int>(wxColour(100, 100, 100), StateColor::Disabled), 
                              std::pair<wxColour, int>(wxColour(155, 104, 225), StateColor::Pressed), 
                              std::pair<wxColour, int>(theme.purple, StateColor::Normal));

    m_button_confirm = new Button(this, _L("Enhance"));
    m_button_confirm->SetBackgroundColor(btn_primary_bg);
    m_button_confirm->SetBorderColor(theme.purple);
    // White text for all states - use 255,255,254 to avoid dark mode color mapping
    StateColor white_text(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
                          std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Pressed),
                          std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Hovered),
                          std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Focused),
                          std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Enabled),
                          std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));
    m_button_confirm->SetTextColor(white_text);
    m_button_confirm->SetFont(Label::Head_14);
    m_button_confirm->SetSize(wxSize(-1, FromDIP(56))); // Large CTA button
    m_button_confirm->SetMinSize(wxSize(-1, FromDIP(56)));
    m_button_confirm->SetCornerRadius(FromDIP(16));
    m_button_confirm->SetToolTip(_L("Enhancement will only take a few minutes to complete, depending on the size of your object. (A lot of compute is happening in the background) (Formerly referred to as 'Optimize' or 'Optimization')"));
    m_button_confirm->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_confirm, this);
    m_button_confirm->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    m_button_confirm->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });

    // Full width CTA button
    button_sizer->Add(m_button_confirm, 1, wxEXPAND, 0);

    main_sizer->Add(line, 0, wxEXPAND, 0);
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(6));
    main_sizer->Add(control_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(6));
    main_sizer->Add(panel_simulation, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    main_sizer->Add(panel_pay_optimization, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    main_sizer->Add(panel_optimization, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(4));
    main_sizer->Add(last_tid_panel, 0, wxLEFT | wxRIGHT, FromDIP(20));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(6));
    // Primary action button - full width
    main_sizer->Add(button_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(8));

    update_action(1); // Default to Optimizations tab

    SetSizer(main_sizer);
    Layout();
    Fit();

    {
        wxWindow* parent = GetParent();
        if (parent) {
            wxPoint parentPos = parent->GetScreenPosition();
            wxSize parentSize = parent->GetSize();
            wxSize dlgSize = GetSize();
            int x = parentPos.x + (parentSize.GetWidth() - dlgSize.GetWidth()) / 2;
            int y = parentPos.y + (parentSize.GetHeight() - dlgSize.GetHeight()) / 3;
            SetPosition(wxPoint(x, y));
        }
    }
    wxGetApp().UpdateDlgDarkUI(this);

    /*set buy url - this is safe after main setup since it just updates an existing link*/
    std::string helio_api_key = Slic3r::HelioQuery::get_helio_pat();
    if (helio_api_key.empty()) return;

    wxString url;
    if (wxGetApp().app_config->get("region") == "China") {
        url = "store.helioam.cn?patToken=" + helio_api_key;
    }
    else {
        url = "store.helioadditive.com?patToken=" + helio_api_key;
    }
    buy_now_link->setLinkUrl(url);

    // Fetch print priority options for the material
    fetch_print_priority_options();
}

void HelioInputDialog::update_action(int action)
{
    if (current_action == action) {
        return;
    }

    auto theme = get_theme();
    
    // Update mode card styling
    update_mode_card_styling(action);
    
    // Update CTA button color based on mode
    if (action == 0) {
        // Simulation mode - purple CTA
        StateColor btn_sim_bg(std::pair<wxColour, int>(wxColour(195, 144, 255), StateColor::Hovered), 
                              std::pair<wxColour, int>(wxColour(100, 100, 100), StateColor::Disabled), 
                              std::pair<wxColour, int>(wxColour(155, 104, 225), StateColor::Pressed), 
                              std::pair<wxColour, int>(theme.purple, StateColor::Normal));
        m_button_confirm->SetBackgroundColor(btn_sim_bg);
        m_button_confirm->SetBorderColor(theme.purple);
        // White text for all states - use 255,255,254 to avoid dark mode color mapping
        StateColor white_text(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
                              std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Pressed),
                              std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Hovered),
                              std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Focused),
                              std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Enabled),
                              std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));
        m_button_confirm->SetTextColor(white_text);
        m_button_confirm->SetLabel(_L("Assess"));
        m_button_confirm->SetToolTip(_L("Assessment will only take a few minutes to complete, depending on the size of your object. (A lot of compute is happening in the background) (Formerly referred to as 'Simulate' or 'Simulation')"));
        m_button_confirm->Layout();
        m_button_confirm->Fit();
        m_button_confirm->Refresh();
        
        togglebutton_simulate->SetIsSelected(true);
        togglebutton_optimize->SetIsSelected(false);
        panel_simulation->Show();
        buy_now_link->Hide();
        buy_now_button->Hide();
        panel_pay_optimization->Hide();
        panel_optimization->Hide();
        m_button_confirm->Enable();
        helio_wiki_link->setLinkUrl(wxGetApp().app_config->get("language") == "zh_CN" ? "https://wiki.helioadditive.com/zh/home" : "https://wiki.helioadditive.com/en/home");

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "  last simulation trace id:" << Slic3r::HelioQuery::last_simulation_trace_id;

        if (Slic3r::HelioQuery::last_simulation_trace_id.empty()) {
            last_tid_panel->Hide();
        }
        else {
            last_tid_label->SetLabel(Slic3r::HelioQuery::last_simulation_trace_id);
            last_tid_panel->Show();
        }
    }
    else {
        // Optimization mode - blue CTA
        StateColor btn_opt_bg(std::pair<wxColour, int>(wxColour(79, 150, 255), StateColor::Hovered), 
                              std::pair<wxColour, int>(wxColour(100, 100, 100), StateColor::Disabled), 
                              std::pair<wxColour, int>(wxColour(39, 110, 220), StateColor::Pressed), 
                              std::pair<wxColour, int>(theme.blue, StateColor::Normal));
        m_button_confirm->SetBackgroundColor(btn_opt_bg);
        m_button_confirm->SetBorderColor(theme.blue);
        // White text for all states - use 255,255,254 to avoid dark mode color mapping
        StateColor white_text(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
                              std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Pressed),
                              std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Hovered),
                              std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Focused),
                              std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Enabled),
                              std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));
        m_button_confirm->SetTextColor(white_text);
        m_button_confirm->SetLabel(_L("Enhance"));
        m_button_confirm->SetToolTip(_L("Enhancement will only take a few minutes to complete, depending on the size of your object. (A lot of compute is happening in the background) (Formerly referred to as 'Optimize' or 'Optimization')"));
        m_button_confirm->Layout();
        m_button_confirm->Fit();
        m_button_confirm->Refresh();
        m_button_confirm->Disable();
        
        std::string helio_api_url = Slic3r::HelioQuery::get_helio_api_url();
        std::string helio_api_key = Slic3r::HelioQuery::get_helio_pat();

        std::weak_ptr<int> weak_ptr = shared_ptr;
        HelioQuery::request_remaining_optimizations(helio_api_url, helio_api_key, [this, weak_ptr](int times, int addons, const std::string& subscription_name, bool free_trial_eligible, bool is_free_trial_active, bool is_free_trial_claimed) {
            if (auto temp_ptr = weak_ptr.lock()) {
                CallAfter([=]() {
                    if (times > 0 || addons > 0) {
                        m_button_confirm->Enable();
                    }
                    
                    // Store free trial states for later use
                    m_free_trial_eligible = free_trial_eligible;
                    m_is_free_trial_active = is_free_trial_active;
                    m_is_free_trial_claimed = is_free_trial_claimed;
                    
                    // Update button text and tooltip based on free trial eligibility
                    // Show "Start Free Trial" only if eligible AND not yet claimed
                    if (buy_now_button) {
                        if (free_trial_eligible && !is_free_trial_claimed) {
                            buy_now_button->SetLabel(_L("Start Free Trial"));
                            buy_now_button->SetToolTip(_L("Activate your free trial to unlock premium enhancements"));
                        } else {
                            buy_now_button->SetLabel(_L("Plans / Upgrades"));
                            buy_now_button->SetToolTip(_L("Go to our storefront to purchase more enhancements"));
                        }
                    }
                    
                    // Update subscription label based on free trial state and subscription name
                    if (m_label_subscription) {
                        // Check if user has a paid subscription (not "Anonymous Slicer Subscription")
                        bool has_paid_subscription = !subscription_name.empty() && 
                                                     subscription_name != "Anonymous Slicer Subscription";
                        
                        if (has_paid_subscription) {
                            // User has a paid subscription - show the actual subscription name
                            m_label_subscription->SetLabelText(wxString::FromUTF8(subscription_name));
                            m_label_subscription->SetToolTip("");
                            m_label_subscription->SetCursor(wxCURSOR_ARROW);
                        } else if (is_free_trial_active) {
                            // Free trial is currently active
                            m_label_subscription->SetLabelText(_L("Free Trial"));
                            m_label_subscription->SetToolTip("");
                            m_label_subscription->SetCursor(wxCURSOR_ARROW);
                        } else if (is_free_trial_claimed && !is_free_trial_active) {
                            // Free trial was claimed but is no longer active (expired)
                            m_label_subscription->SetLabelText(_L("Free Trial Expired"));
                            m_label_subscription->SetToolTip(_L("Your free trial has ended. Explore our plans to continue using premium features."));
                            m_label_subscription->SetForegroundColour(wxColour(175, 124, 255));  // Purple to indicate actionable
                            m_label_subscription->SetCursor(wxCURSOR_HAND);
                        } else if (free_trial_eligible && !is_free_trial_claimed) {
                            // User is eligible for free trial but hasn't started it yet
                            wxString display_name = subscription_name.empty() ? "-" : wxString::FromUTF8(subscription_name);
                            m_label_subscription->SetLabelText(display_name);
                            m_label_subscription->SetToolTip(_L("Start your free trial to unlock premium enhancements and faster, warp-free prints!"));
                            m_label_subscription->SetForegroundColour(wxColour(175, 124, 255));  // Purple to indicate actionable
                            m_label_subscription->SetCursor(wxCURSOR_HAND);
                        } else if (subscription_name.empty()) {
                            m_label_subscription->SetLabelText("-");
                            m_label_subscription->SetToolTip("");
                            m_label_subscription->SetCursor(wxCURSOR_ARROW);
                        } else {
                            // Default: show the subscription name as-is
                            m_label_subscription->SetLabelText(wxString::FromUTF8(subscription_name));
                            m_label_subscription->SetToolTip("");
                            m_label_subscription->SetCursor(wxCURSOR_ARROW);
                        }
                    }
                    
                    if (m_label_monthly_quota) {
                        if (times > 999) {
                            m_label_monthly_quota->SetLabelText(_L("Unlimited*"));
                            m_label_monthly_quota->SetToolTip(_L("*Fair usage terms apply - click to learn more"));
                            m_label_monthly_quota->SetForegroundColour(wxColour(175, 124, 255));  // Purple to indicate clickable
                            m_label_monthly_quota->SetCursor(wxCURSOR_HAND);
                            // Bind click to open fair use policy (unbind first to avoid duplicate bindings)
                            m_label_monthly_quota->Unbind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_unlimited_click, this);
                            m_label_monthly_quota->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_unlimited_click, this);
                        } else {
                            m_label_monthly_quota->SetLabelText(wxString::Format("%d", times));
                            m_label_monthly_quota->SetToolTip("");  // Clear tooltip for numbered quota
                            m_label_monthly_quota->SetForegroundColour(get_theme().text);
                            m_label_monthly_quota->SetCursor(wxCURSOR_ARROW);
                            m_label_monthly_quota->Unbind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_unlimited_click, this);
                        }
                    }
                    if (m_label_addons) { m_label_addons->SetLabelText(wxString::Format("%d", addons)); }
                    
                    Layout();
                    Fit();
                });
            }
            else {
                //already closed dialog
            }
        });

        togglebutton_simulate->SetIsSelected(false);
        togglebutton_optimize->SetIsSelected(true);
        panel_simulation->Hide();
        buy_now_link->Show();
        buy_now_button->Show();
        helio_wiki_link->setLinkUrl(wxGetApp().app_config->get("language") == "zh_CN" ? "https://wiki.helioadditive.com/zh/optimization/quick-start" : "https://wiki.helioadditive.com/en/optimization/quick-start");

        if (support_optimization == 0) {
            panel_pay_optimization->Hide();
            panel_optimization->Show();
        }
        else {
            panel_pay_optimization->Show();
            panel_optimization->Hide();
        }

        try
        {
            /*force use Slicer default (show velocity/volumetric speed fields) when nozzle diameter = 0.2*/
            const auto& full_config = wxGetApp().preset_bundle->full_config();
            const auto& project_config = wxGetApp().preset_bundle->project_config;
            double nozzle_diameter = full_config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values[0];

            if (boost::str(boost::format("%.1f") % nozzle_diameter) == "0.2") {
                only_advanced_settings = true;
                use_advanced_settings = true;
                m_combo_items["limits"]->SetSelection(LIMITS_SLICER_DEFAULT);
                m_combo_items["limits"]->Disable();
                panel_velocity_volumetric->Show();
                card_optimization_settings->Layout();
                panel_optimization->Layout();
                panel_optimization->Fit();
                Layout();
                Fit();
            }

            /*force use Slicer default when preset has layer height < 0.2*/
            auto edited_preset = wxGetApp().preset_bundle->prints.get_edited_preset().config;
            auto layer_height = edited_preset.option<ConfigOptionFloat>("layer_height")->value;
            if (layer_height < 0.2) {
                only_advanced_settings = true;
                use_advanced_settings = true;
                m_combo_items["limits"]->SetSelection(LIMITS_SLICER_DEFAULT);
                m_combo_items["limits"]->Disable();
                panel_velocity_volumetric->Show();
                card_optimization_settings->Layout();
                panel_optimization->Layout();
                panel_optimization->Fit();
                Layout();
                Fit();
            }
        }
        catch (...){}

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "  last optimization trace id:" << Slic3r::HelioQuery::last_optimization_trace_id;

        if (Slic3r::HelioQuery::last_optimization_trace_id.empty()) {
            last_tid_panel->Hide();
        }
        else {
            last_tid_label->SetLabel(Slic3r::HelioQuery::last_optimization_trace_id);
            last_tid_panel->Show();
        }
    }
    Layout();
    Fit();

    current_action = action;
}

void HelioInputDialog::show_advanced_mode()
{
    // Deprecated: Advanced settings visibility is now controlled by the Limits dropdown event handler. This function does nothing.
    Layout();
    Fit();
}

void HelioInputDialog::on_unlimited_click(wxMouseEvent& e)
{
    wxString url = "https://wiki.helioadditive.com/en/policies/fair";
    wxLaunchDefaultBrowser(url);
    e.Skip();
}

void HelioInputDialog::set_force_slicer_default(bool force)
{
    if (force) {
        only_advanced_settings = true;
        use_advanced_settings = true;
        if (m_combo_items.find("limits") != m_combo_items.end()) {
            m_combo_items["limits"]->SetSelection(LIMITS_SLICER_DEFAULT);
            m_combo_items["limits"]->Disable();
        }
        if (panel_velocity_volumetric) {
            panel_velocity_volumetric->Show();
            if (card_optimization_settings) {
                card_optimization_settings->Layout();
            }
            if (panel_optimization) {
                panel_optimization->Layout();
                panel_optimization->Fit();
            }
        }
        Layout();
        Fit();
    }
}

wxBoxSizer* HelioInputDialog::create_input_item(wxWindow* parent, std::string key, wxString name, wxString unit,
                                                const std::vector<std::shared_ptr<TextInputValChecker>>& checkers)
{
    auto theme = get_theme();
    wxBoxSizer* item_sizer = new wxBoxSizer(wxHORIZONTAL);
    Label* inout_title = new Label(parent, Label::Body_14, name);
    inout_title->SetFont(::Label::Body_14);
    inout_title->SetForegroundColour(theme.text);

    // Pass unit as the 3rd 'label' parameter instead of the 8th 'unit' parameter
    // The 'unit' parameter has an upstream sizing bug where unit_space is calculated but not subtracted from text width
    TextInput *m_input_item = new TextInput(parent, wxEmptyString, unit, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(120), -1), wxTE_PROCESS_ENTER);

    // Use theme colors for input background (dark mode only for now)
    wxColour input_bg_color = theme.card2;
    wxColour disabled_bg = wxColour(30, 35, 45);
    StateColor input_bg(std::pair<wxColour, int>(disabled_bg, StateColor::Disabled), std::pair<wxColour, int>(input_bg_color, StateColor::Enabled));
    m_input_item->SetBackgroundColor(input_bg);
    // Set unit label color to match theme
    m_input_item->SetLabelColor(StateColor(std::pair<wxColour, int>(theme.muted, StateColor::Normal)));
    wxTextValidator validator(wxFILTER_NUMERIC);

    m_input_item->GetTextCtrl()->SetBackgroundColour(input_bg_color);
    m_input_item->GetTextCtrl()->SetForegroundColour(theme.text);
    m_input_item->GetTextCtrl()->SetWindowStyleFlag(wxBORDER_NONE | wxTE_PROCESS_ENTER);
    m_input_item->GetTextCtrl()->Bind(wxEVT_TEXT, [=](wxCommandEvent &) {
        wxTextCtrl *ctrl = m_input_item->GetTextCtrl();
        wxColour error_bg = wxColour(80, 40, 50);
        wxColour normal_bg = theme.card2;
        wxColour new_bg = ctrl->GetValue().IsEmpty() && (key != "chamber_temp_for_simulation" && key != "chamber_temp_for_optimization") ? error_bg : normal_bg;
        if (ctrl->GetBackgroundColour() != new_bg) {
            ctrl->SetBackgroundColour(new_bg);
            ctrl->Refresh();
        }
        });
    m_input_item->GetTextCtrl()->SetValidator(validator);
    m_input_item->GetTextCtrl()->SetMaxLength(10);
    m_input_item->SetValCheckers(checkers);

    item_sizer->Add(inout_title, 0, wxALIGN_CENTER, 0);
    item_sizer->Add(0, 0, 1, wxEXPAND, 0);
    item_sizer->Add(m_input_item, 0);

    m_input_items.insert(std::pair<std::string, TextInput*>(key, m_input_item));
    return item_sizer;
}

wxBoxSizer* HelioInputDialog::create_combo_item(wxWindow* parent, std::string key,  wxString name, std::map<int, wxString> combolist, int def, int width)
{
    auto theme = get_theme();
    wxBoxSizer* item_sizer = new wxBoxSizer(wxHORIZONTAL);
    Label* inout_title = new Label(parent, Label::Body_14, name);
    inout_title->SetFont(::Label::Body_14);
    inout_title->SetForegroundColour(theme.text);

    auto combobox = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(width), -1), 0, nullptr, wxCB_READONLY);
    combobox->SetFont(::Label::Body_13);
    combobox->GetDropDown().SetFont(::Label::Body_13);
    
    // Apply theme colors (respects OS dark/light mode)
    combobox->SetBackgroundColour(theme.card2);
    combobox->SetForegroundColour(theme.text);
    
    // Style dropdown list
    auto& dd = combobox->GetDropDown();
    dd.SetBackgroundColour(theme.card2);
    dd.SetForegroundColour(theme.text);
    
    // Style text control if available
    if (auto* tc = combobox->GetTextCtrl()) {
        tc->SetBackgroundColour(theme.card2);
        tc->SetForegroundColour(theme.text);
    }
    
    combobox->Refresh();

    std::vector<wxString>::iterator iter;
    for (auto label : combolist)
        combobox->Append(label.second);

    if (def < combolist.size()) {
        combobox->SetSelection(def);
    }

    item_sizer->Add(inout_title, 0, wxALIGN_CENTER, 0);
    item_sizer->Add(0, 0, 1, wxEXPAND, 0);
    item_sizer->Add(combobox, 0);

    m_combo_items.insert(std::pair<std::string, ComboBox*>(key, combobox));
    return item_sizer;
}


wxBoxSizer* HelioInputDialog::create_input_optimize_layers(wxWindow* parent, int layer_count)
{
    auto theme = get_theme();
    wxBoxSizer* item_sizer = new wxBoxSizer(wxHORIZONTAL);
    Label* inout_title = new Label(parent, Label::Body_14, _L("Layers"));
    inout_title->SetFont(::Label::Body_14);
    inout_title->SetForegroundColour(theme.text);
    wxString layers_tooltip = _L("Restrict Helio's fixes to only certain layers, or keep them for all layers - the choice is yours! (Layer 1 attached to the bed is typically skipped.)");
    inout_title->SetToolTip(layers_tooltip);

    // Dark mode only for now
    wxColour input_bg_color = theme.card2;
    wxColour disabled_bg = wxColour(30, 35, 45);
    StateColor input_bg(std::pair<wxColour, int>(disabled_bg, StateColor::Disabled), std::pair<wxColour, int>(input_bg_color, StateColor::Enabled));
    auto layer_range_checker = TextInputValChecker::CreateIntRangeChecker(0, layer_count);
    wxColour error_bg = wxColour(80, 40, 50);

    // Equal-sized layer inputs
    TextInput* m_layer_min_item = new TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(80), -1), wxTE_PROCESS_ENTER);
    m_layer_min_item->SetBackgroundColor(input_bg);
    m_layer_min_item->GetTextCtrl()->SetWindowStyleFlag(wxBORDER_NONE | wxTE_PROCESS_ENTER);
    m_layer_min_item->GetTextCtrl()->SetBackgroundColour(input_bg_color);
    m_layer_min_item->GetTextCtrl()->SetForegroundColour(theme.text);
    m_layer_min_item->GetTextCtrl()->Bind(wxEVT_TEXT, [=](wxCommandEvent &) {
        wxTextCtrl *ctrl = m_layer_min_item->GetTextCtrl();
        wxColour new_bg = ctrl->GetValue().IsEmpty() ? error_bg : input_bg_color;
        if (ctrl->GetBackgroundColour() != new_bg) {
            ctrl->SetBackgroundColour(new_bg);
            ctrl->Refresh();
        }
    });
    m_layer_min_item->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_layer_min_item->GetTextCtrl()->SetMaxLength(10);
    m_layer_min_item->GetTextCtrl()->SetLabel("2");
    m_layer_min_item->SetValCheckers({layer_range_checker});
    m_layer_min_item->SetToolTip(layers_tooltip);
    m_layer_min_item->GetTextCtrl()->SetToolTip(layers_tooltip);

    TextInput* m_layer_max_item = new TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(80), -1), wxTE_PROCESS_ENTER);
    m_layer_max_item->SetBackgroundColor(input_bg);
    m_layer_max_item->GetTextCtrl()->SetWindowStyleFlag(wxBORDER_NONE | wxTE_PROCESS_ENTER);
    m_layer_max_item->GetTextCtrl()->SetBackgroundColour(input_bg_color);
    m_layer_max_item->GetTextCtrl()->SetForegroundColour(theme.text);
    m_layer_max_item->GetTextCtrl()->Bind(wxEVT_TEXT, [=](wxCommandEvent &) {
        wxTextCtrl *ctrl = m_layer_max_item->GetTextCtrl();
        wxColour new_bg = ctrl->GetValue().IsEmpty() ? error_bg : input_bg_color;
        if (ctrl->GetBackgroundColour() != new_bg) {
            ctrl->SetBackgroundColour(new_bg);
            ctrl->Refresh();
        }
    });
    m_layer_max_item->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_layer_max_item->GetTextCtrl()->SetMaxLength(10);
    m_layer_max_item->GetTextCtrl()->SetLabel(wxString::Format("%d", layer_count));
    m_layer_max_item->SetValCheckers({ layer_range_checker });
    m_layer_max_item->SetToolTip(layers_tooltip);
    m_layer_max_item->GetTextCtrl()->SetToolTip(layers_tooltip);

    // "to" label with theme color
    Label* to_label = new Label(parent, Label::Body_13, _L("to"));
    to_label->SetForegroundColour(theme.text);

    item_sizer->Add(inout_title, 0, wxALIGN_CENTER, 0);
    item_sizer->Add(0, 0, 1, wxEXPAND, 0);
    m_layer_min_item->SetMinSize(wxSize(FromDIP(80), -1));
    m_layer_min_item->SetMaxSize(wxSize(FromDIP(80), -1));
    m_layer_max_item->SetMinSize(wxSize(FromDIP(80), -1));
    m_layer_max_item->SetMaxSize(wxSize(FromDIP(80), -1));
    item_sizer->Add(m_layer_min_item);
    item_sizer->AddSpacer(FromDIP(8));
    item_sizer->Add(to_label, 0, wxALIGN_CENTER_VERTICAL, 0);
    item_sizer->AddSpacer(FromDIP(8));
    item_sizer->Add(m_layer_max_item);

    m_input_items.insert(std::pair<std::string, TextInput*>("layers_to_optimize_min", m_layer_min_item));
    m_input_items.insert(std::pair<std::string, TextInput*>("layers_to_optimize_max", m_layer_max_item));

    return item_sizer;
}

wxBoxSizer* HelioInputDialog::create_print_priority_combo(wxWindow* parent)
{
    auto theme = get_theme();
    wxBoxSizer* item_sizer = new wxBoxSizer(wxHORIZONTAL);

    Label* label = new Label(parent, Label::Body_14, _L("Print Priority"));
    label->SetFont(::Label::Body_14);
    label->SetForegroundColour(theme.text);

    ComboBox* combobox = new ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                      wxSize(FromDIP(PRINT_PRIORITY_DROPDOWN_WIDTH), -1), 0, nullptr, wxCB_READONLY);
    combobox->SetBackgroundColor(StateColor(std::make_pair(theme.card2, (int)StateColor::Normal)));
    combobox->SetTextColor(StateColor(std::make_pair(theme.text, (int)StateColor::Normal)));
    combobox->SetBorderColor(StateColor(std::make_pair(theme.border, (int)StateColor::Normal)));

    item_sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL, 0);
    item_sizer->Add(0, 0, 1, wxEXPAND, 0);
    item_sizer->Add(combobox, 0, wxALIGN_CENTER_VERTICAL, 0);

    m_combo_items["optimiza_outerwall"] = combobox;
    populate_print_priority_dropdown(combobox);

    return item_sizer;
}

void HelioInputDialog::populate_print_priority_dropdown(ComboBox* combobox)
{
    if (!combobox) return;

    // Wrap operations in try-catch for Windows safety
    try {
        combobox->Clear();

        if (m_print_priority_loading) {
            // Loading state - show disabled with loading message
            combobox->Append(_L("Loading print priority options..."));
            combobox->SetSelection(0);
            combobox->Disable();
            combobox->SetToolTip(_L("Fetching material-specific options from Helio API..."));
            return;
        }

    if (m_print_priority_options.empty()) {
        // API failed - show hard-coded fallback options (OLD METHOD using optimizeOuterwall)
        // These options map to the old optimization method that uses optimizeOuterwall boolean
        m_using_fallback_print_priority = true;

        combobox->Append(_L("Preserve Surface Finish"));  // selection 0 → optimizeOuterwall: false
        combobox->Append(_L("Speed & Strength"));         // selection 1 → optimizeOuterwall: true
        combobox->SetSelection(1);  // Default to "Speed & Strength"
        combobox->Enable();  // ENABLED - user can select

        combobox->SetToolTip(
            _L("Speed & Strength: Optimizes outer walls for improved performance.\n"
               "Preserve Surface Finish: Maintains original wall speeds to preserve visual finish.\n\n"
               "Note: Using fallback options - couldn't fetch material-specific settings from API."));
        return;
    }

    // Using API options (NEW METHOD)
    m_using_fallback_print_priority = false;

    // Filter to only include available options
    m_available_print_priority_options.clear();
    for (const auto& option : m_print_priority_options) {
        if (option.isAvailable) {
            m_available_print_priority_options.push_back(option);
        }
    }

    // Check if any options are available for this material
    if (m_available_print_priority_options.empty()) {
        // No available options for this material
        combobox->Append(_L("No print priority options available for this material"));
        combobox->SetSelection(0);
        combobox->Disable();
        combobox->SetToolTip(_L("This material does not support print priority optimization."));
        return;
    }

    // Populate dropdown with only available options
    int default_selection = -1;
    for (size_t i = 0; i < m_available_print_priority_options.size(); i++) {
        const auto& option = m_available_print_priority_options[i];

        // Map known API values to local translated strings.
        // Fall back to API label/description for unknown options (future-safe).
        wxString display_label;
        wxString display_description;
        if (option.value == "SPEED_AND_STRENGTH") {
            display_label = _L("Speed & Strength");           // 速度与强度
            display_description = _L("Optimizes outer walls for improved performance.");
        } else if (option.value == "PRESERVE_SURFACE_FINISH") {
            display_label = _L("Preserve Surface Finish");    // 保持表面质量
            display_description = _L("Maintains original wall speeds to preserve visual finish.");
        } else if (option.value == "ENHANCE_SURFACE_GLOSS") {
            display_label = _L("Enhance Surface Gloss");      // 增强表面光泽
            display_description = _L("Optimizes for enhanced surface gloss.");
        } else {
            // Unknown option — use API strings directly (won't be translated, but won't break)
            display_label = wxString::FromUTF8(option.label.c_str());
            display_description = wxString::FromUTF8(option.description.c_str());
        }

        combobox->Append(display_label);
        combobox->SetItemTooltip(i, display_description);

        // Set default selection to first SPEED_AND_STRENGTH option
        if (default_selection == -1 && option.value == "SPEED_AND_STRENGTH") {
            default_selection = i;
        }
    }

    // If no SPEED_AND_STRENGTH, default to first available option
    if (default_selection == -1 && !m_available_print_priority_options.empty()) {
        default_selection = 0;
    }

    if (default_selection != -1) {
        combobox->SetSelection(default_selection);
    } else {
        combobox->SetSelection(0); // Fallback to first item
    }

    combobox->Enable();

    // Clear global tooltip to allow per-item tooltips to display
    combobox->UnsetToolTip();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "populate_print_priority_dropdown error: " << e.what();
        return;
    }
}

void HelioInputDialog::fetch_print_priority_options()
{
    if (m_material_id.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "fetch_print_priority_options: material_id is empty, using fallback options";
        return;
    }

    // Check cache first
    auto cached_options = HelioQuery::get_cached_print_priority_options(m_material_id);
    if (!cached_options.empty()) {
        m_print_priority_options = cached_options;
        update_print_priority_dropdown();
        return;
    }

    // Not cached, fetch from API
    m_print_priority_loading = true;
    update_print_priority_dropdown(); // Show loading state

    std::string helio_api_url = HelioQuery::get_helio_api_url();
    std::string helio_api_key = HelioQuery::get_helio_pat();

    if (helio_api_key.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "fetch_print_priority_options: No API key, using fallback options";
        m_print_priority_loading = false;
        update_print_priority_dropdown();
        return;
    }

    // Create a shared_ptr to this dialog to keep it alive during async callback
    auto self_ptr = shared_ptr; // Keep the dialog alive

    HelioQuery::request_print_priority_options(
        helio_api_url,
        helio_api_key,
        m_material_id,
        [this, self_ptr](HelioQuery::GetPrintPriorityOptionsResult result) {
            // Use CallAfter to update UI from main thread
            // Keep self_ptr capture to ensure dialog stays alive during callback
            CallAfter([this, self_ptr, result]() {
                // Validate self_ptr to ensure dialog is still alive
                // self_ptr is a shared_ptr<int> member - if it's valid, the dialog is valid
                if (!self_ptr || self_ptr.use_count() == 0) return;

                m_print_priority_loading = false;

                if (result.success && !result.options.empty()) {
                    m_print_priority_options = result.options;
                } else {
                    // Log error and use fallback
                    BOOST_LOG_TRIVIAL(error) << "fetch_print_priority_options failed: " << result.error
                                            << ", trace-id: " << result.trace_id;

                    // Show notification to user about using standard optimization method
                    auto notification_manager = wxGetApp().plater()->get_notification_manager();
                    if (notification_manager) {
                        notification_manager->push_notification(
                            NotificationType::CustomNotification,
                            NotificationManager::NotificationLevel::WarningNotificationLevel,
                            _u8L("Failed to load print priority options from Helio API.\n"
                                 "Using standard optimization method instead (same as before print priority feature).")
                        );
                    }
                }

                update_print_priority_dropdown();
            });
        }
    );
}

void HelioInputDialog::update_print_priority_dropdown()
{
    auto it = m_combo_items.find("optimiza_outerwall");
    if (it != m_combo_items.end() && it->second != nullptr) {
        populate_print_priority_dropdown(it->second);
    }
}


static bool s_get_double_regex(const wxString& str, double& value)
{
    value = -1;

    std::string s = str.ToStdString();
    std::regex  pattern("^[-+]?[0-9]*\\.?[0-9]+$");

    if (std::regex_match(s, pattern))
    {
        return str.ToDouble(&value);
    }

    return false;
}

static bool s_get_int_regex(const wxString& str, int& value)
{
    value = -1;

    std::string s = str.ToStdString();
    std::regex  pattern("^[-+]?[0-9]+$");

    if (std::regex_match(s, pattern))
    {
        long val;
        if (str.ToLong(&val))
        {
            if (val >= INT_MIN && val <= INT_MAX)
            {
                value = static_cast<int>(val);
                return true;
            }
        }
    }
    return false;
}


static bool s_get_double_val(TextInput* item, float& val)
{
    double double_val = -1;
    if (item->CheckValid() && s_get_double_regex(item->GetTextCtrl()->GetValue(), double_val))
    {
        val = double_val;
        return true;
    }

    val = double_val;
    item->SetFocus();
    item->GetTextCtrl()->SetInsertionPointEnd();
    return false;
};

static bool s_get_int_val(TextInput* item, int& val)
{
    if (item->CheckValid() && s_get_int_regex(item->GetTextCtrl()->GetValue(), val))
    {
        return true;
    }

    item->SetFocus();
    item->GetTextCtrl()->SetInsertionPointEnd();
    return false;
}

Slic3r::HelioQuery::SimulationInput HelioInputDialog::get_simulation_input(bool& ok)
{
    HelioQuery::SimulationInput data;
    if (m_input_items["chamber_temp_for_simulation"]->GetTextCtrl()->GetValue().IsEmpty()) {
        ok = true;
    }
    else {
        ok = s_get_double_val(m_input_items["chamber_temp_for_simulation"], data.chamber_temp);
    }
    return data;
}

Slic3r::HelioQuery::OptimizationInput HelioInputDialog::get_optimization_input(bool& ok)
{
    ok = false;
    HelioQuery::OptimizationInput data;

    auto it = m_input_items.find("chamber_temp_for_optimization");
    if (it != m_input_items.end()) {
        if (!m_input_items["chamber_temp_for_optimization"]->GetTextCtrl()->GetValue().IsEmpty() && !s_get_double_val(m_input_items["chamber_temp_for_optimization"], data.chamber_temp)) {
            return data;
        }
    }
    
    // Handle print priority dropdown - either new API method or old fallback method
    auto combo_it = m_combo_items.find("optimiza_outerwall");
    if (combo_it != m_combo_items.end() && combo_it->second && combo_it->second->IsEnabled()) {
        int selection = combo_it->second->GetSelection();

        if (m_using_fallback_print_priority) {
            // Using hard-coded fallback options → OLD METHOD (optimizeOuterwall boolean)
            // selection 0 = "Preserve Surface Finish" → optimizeOuterwall: false
            // selection 1 = "Speed & Strength" → optimizeOuterwall: true
            data.optimize_outerwall = (selection == 1);
            data.use_old_method = true;
            // Leave data.print_priority EMPTY (will use old mutation format)

        } else if (!m_available_print_priority_options.empty()) {
            // Using API options → NEW METHOD (printPriority string)
            if (selection >= 0 && static_cast<size_t>(selection) < m_available_print_priority_options.size()) {
                data.print_priority = m_available_print_priority_options[selection].value;
            }
            data.use_old_method = false;
            // Leave data.optimize_outerwall at default (will use new mutation format)
        }
    }

    if (!s_get_int_val(m_input_items["layers_to_optimize_min"], data.layers_to_optimize[0])) { return data; }
    if (!s_get_int_val(m_input_items["layers_to_optimize_max"], data.layers_to_optimize[1])) { return data; }

    if (data.layers_to_optimize[0] > data.layers_to_optimize[1]) {
        int temp = data.layers_to_optimize[0];
        data.layers_to_optimize[0] = data.layers_to_optimize[1];
        data.layers_to_optimize[1] = temp;
    }

    // Check Limits selection - only read velocity/volumetric speed when "Slicer default" is selected
    int limits_selection = m_combo_items["limits"]->GetSelection();
    if (limits_selection == LIMITS_SLICER_DEFAULT) {
        // Read velocity and volumetric speed fields
        if (!s_get_double_val(m_input_items["min_velocity"], data.min_velocity)) { return data; }
        if (!s_get_double_val(m_input_items["max_velocity"], data.max_velocity)) { return data; }
        if (!s_get_double_val(m_input_items["min_volumetric_speed"], data.min_volumetric_speed)) { return data; }
        if (!s_get_double_val(m_input_items["max_volumetric_speed"], data.max_volumetric_speed)) { return data; }
    }
    // When "Helio default" (selection == 0) is selected, skip reading these fields - backend will use Helio defaults
    ok = true;
    return data;
}


void HelioInputDialog::on_confirm(wxMouseEvent& e)
{
    bool ok = false;
    if (current_action == 0) // Simulation
    {
        get_simulation_input(ok);
    }
    else if (current_action == 1) // Optimization
    {
        get_optimization_input(ok);
    }

    if (ok)
    {
        // Mark tutorial as complete when user clicks optimize/enhance button
        if (current_action == 1 && wxGetApp().app_config->get("helio_first_time_tutorial") == "active") {
            wxGetApp().app_config->set("helio_first_time_tutorial", "completed");
            wxGetApp().app_config->save();
        }
        EndModal(wxID_OK);
    }
}

HelioInputDialog::~HelioInputDialog()
{
    // Restore original tooltip delay
    wxToolTip::SetDelay(m_original_tooltip_delay);
}

void HelioInputDialog::on_dpi_changed(const wxRect &suggested_rect)
{
}

HelioPatNotEnoughDialog::HelioPatNotEnoughDialog(wxWindow* parent /*= nullptr*/)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, wxString("Helio Additive"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    // Set Helio icon (not BambuStudio icon)
    wxBitmap bmp = create_scaled_bitmap("helio_icon", this, 32);
    wxIcon icon;
    icon.CopyFromBitmap(bmp);
    SetIcon(icon);
    
    // Use Helio dark palette (not neutral charcoal)
    SetBackgroundColour(HELIO_BG_BASE);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    wxPanel* line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    line->SetBackgroundColour(wxColour(166, 169, 170));

    Label* text = new Label(this, Label::Body_14, _L("Failed to obtain Helio PAT. The number of issued PATs has reached the upper limit. Please pay attention to the information on the Helio official website. Click Refresh to get it again once it is available."), LB_AUTO_WRAP);
    text->SetForegroundColour(wxColour("#6C6C6C"));
    text->SetMinSize(wxSize(FromDIP(450), -1));
    text->SetMaxSize(wxSize(FromDIP(450), -1));
    text->Wrap(FromDIP(450));

    auto helio_wiki_link = new LinkLabel(this, _L("Click for more details"), wxGetApp().app_config->get("language") =="zh_CN"? "https://wiki.helioadditive.com/zh/home" : "https://wiki.helioadditive.com/en/home");
    helio_wiki_link->SeLinkLabelFColour(wxColour(0, 174, 66));
#ifdef __WXMSW__
    helio_wiki_link->SeLinkLabelBColour(GetBackgroundColour());
#else
    helio_wiki_link->SeLinkLabelBColour(wxNullColour);
    helio_wiki_link->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
#endif
    helio_wiki_link->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    helio_wiki_link->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));


    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    auto m_button_ok = new Button(this, _L("Confirm"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    // White text for all states
    StateColor white_text(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
                          std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));
    m_button_ok->SetTextColor(white_text);
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));

    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));

    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        EndModal(wxID_OK);
        });

    main_sizer->Add(line, 0, wxEXPAND, 0);
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(26));
    main_sizer->Add(text, 0, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(30));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
    main_sizer->Add(helio_wiki_link, 0, wxLEFT|wxRIGHT, FromDIP(30));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
    main_sizer->Add(sizer_button, 0, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(30));

    SetSizer(main_sizer);
    Layout();
    Fit();
    {
        wxWindow* parent = GetParent();
        if (parent) {
            wxPoint parentPos = parent->GetScreenPosition();
            wxSize parentSize = parent->GetSize();
            wxSize dlgSize = GetSize();
            int x = parentPos.x + (parentSize.GetWidth() - dlgSize.GetWidth()) / 2;
            int y = parentPos.y + (parentSize.GetHeight() - dlgSize.GetHeight()) / 3;
            SetPosition(wxPoint(x, y));
        }
    }
}

HelioPatNotEnoughDialog::~HelioPatNotEnoughDialog() {}

void HelioPatNotEnoughDialog::on_dpi_changed(const wxRect& suggested_rect)
{

}

HelioRatingDialog::HelioRatingDialog(wxWindow *parent, int original, int optimized, std::string mean_impro, std::string std_impro)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, wxString("Helio Additive"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    original_time = original;       
    optimized_time = optimized; 
    quality_mean_improvement = wxString(mean_impro);
    quality_std_improvement = wxString(std_impro);

    shared_ptr = std::make_shared<int>(0);

    // Set Helio icon (not BambuStudio icon)
    wxBitmap bmp = create_scaled_bitmap("helio_icon", this, 32);
    wxIcon icon;
    icon.CopyFromBitmap(bmp);
    SetIcon(icon);

    // Theme colors based on light/dark mode
    bool is_dark = wxGetApp().dark_mode();
    wxColour bg_color = is_dark ? HELIO_BG_BASE : wxColour(255, 255, 254);
    // Header banner always stays dark for Helio branding
    wxColour header_bg = wxColour(16, 16, 16);
    wxColour header_text = wxColour("#FEFEFF");
    wxColour label_color = is_dark ? wxColour(144, 144, 144) : wxColour(107, 107, 107);
    wxColour value_color = wxColour(106, 174, 89);  // Green accent works in both modes
    wxColour line_color = is_dark ? wxColour(166, 169, 170) : wxColour(220, 220, 220);
    
    SetBackgroundColour(bg_color);

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);


    wxBoxSizer *helio_top_hsizer        = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *helio_top_vsizer        = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *helio_top_content_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto helio_top_background = new wxPanel(this);
    helio_top_background->SetBackgroundColour(header_bg);
    helio_top_background->SetMinSize(wxSize(-1, FromDIP(70)));
    helio_top_background->SetMaxSize(wxSize(-1, FromDIP(70)));
    auto   helio_top_icon  = new wxStaticBitmap(helio_top_background, wxID_ANY, create_scaled_bitmap("helio_icon", helio_top_background, 32), wxDefaultPosition,
                                             wxSize(FromDIP(32), FromDIP(32)), 0);
    auto   helio_top_label = new Label(helio_top_background, Label::Body_16, L("HELIO ADDITIVE"));
    wxFont bold_font       = helio_top_label->GetFont();
    bold_font.SetWeight(wxFONTWEIGHT_BOLD);
    helio_top_label->SetFont(bold_font);
    helio_top_label->SetForegroundColour(header_text);
    // helio_top_hsizer->Add(0, 0, wxLEFT, FromDIP(40));
    helio_top_content_sizer->Add(helio_top_icon, 0, wxLEFT | wxALIGN_CENTER, FromDIP(45));
    helio_top_content_sizer->Add(helio_top_label, 0, wxLEFT | wxALIGN_CENTER, FromDIP(8));
    helio_top_vsizer->Add(helio_top_content_sizer, 0, wxALIGN_CENTER, 0);
    helio_top_hsizer->Add(helio_top_vsizer, 0, wxALIGN_CENTER, 0);
    helio_top_background->SetSizer(helio_top_hsizer);
    helio_top_background->Layout();

    
    wxBoxSizer *time_impro = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *average_impro = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *consistency_impro = new wxBoxSizer(wxHORIZONTAL);

    auto title_time_impro = new Label(this, Label::Body_14, _L("Print Time Improvement"));
    auto title_average_impro     = new Label(this, Label::Body_14, _L("Average Improvement"));
    auto title_consistency_impro = new Label(this, Label::Body_14, _L("Consistency Improvement"));


    // Use theme-aware text colors
    title_time_impro->SetForegroundColour(label_color);
    title_average_impro->SetForegroundColour(label_color);
    title_consistency_impro->SetForegroundColour(label_color);
    
    // Add tooltips for improvement metrics
    title_time_impro->SetToolTip(_L("Estimate of improvement in print time before and after enhancement"));
    title_average_impro->SetToolTip(_L("Level of Strength & Warping improvement for this part after enhancement - average for the part"));
    title_consistency_impro->SetToolTip(_L("Level of Strength & Warping improvement for this part after enhancement - change in overall consistency"));

    title_time_impro->SetMinSize(wxSize(FromDIP(225), -1));
    title_average_impro->SetMinSize(wxSize(FromDIP(225), -1));
    title_consistency_impro->SetMinSize(wxSize(FromDIP(225), -1));

    wxString txt_average_impro;
    wxString txt_consistency_impro;

    quality_mean_improvement.MakeLower();
    quality_std_improvement.MakeLower();   

    auto label_original_time = wxString::Format("%s", short_time(get_time_dhms(original_time)));
    auto label_optimized_time = wxString::Format("%s", short_time(get_time_dhms(optimized_time)));

    int time_diff = original_time - optimized_time;
    auto label_abs_diff = wxString::Format("%s", short_time(get_time_dhms(std::abs(time_diff))));

    wxString time_impro_text;
    wxColour time_impro_color;
    if (time_diff > 0) {
        time_impro_text = label_abs_diff + " (" + label_original_time + " -> " + label_optimized_time + ")";
        time_impro_color = value_color;
    } else if (time_diff < 0) {
        time_impro_text = "+" + label_abs_diff + " (" + label_original_time + " -> " + label_optimized_time + ")";
        time_impro_color = wxColour(221, 160, 62);
    } else {
        time_impro_text = "0s (" + label_original_time + " -> " + label_optimized_time + ")";
        time_impro_color = value_color;
    }

    auto label_time_impro        = new Label(this, Label::Body_14, time_impro_text);
    auto label_average_impro     = new Label(this, Label::Body_14, format_improvement(quality_mean_improvement));
    auto label_consistency_impro = new Label(this, Label::Body_14, format_improvement(quality_std_improvement));

    label_time_impro->SetForegroundColour(time_impro_color);
    label_average_impro->SetForegroundColour(value_color);
    label_consistency_impro->SetForegroundColour(value_color);

    auto lab_bold_font = label_time_impro->GetFont();
    lab_bold_font.SetWeight(wxFONTWEIGHT_BOLD);
    label_time_impro->SetFont(lab_bold_font);
    label_average_impro->SetFont(lab_bold_font);
    label_consistency_impro->SetFont(lab_bold_font);

    time_impro->Add(title_time_impro, 0, wxLEFT, 0);
    time_impro->Add(label_time_impro, 0, wxLEFT, 0);

    average_impro->Add(title_average_impro, 0, wxLEFT, 0);
    average_impro->Add(label_average_impro, 0, wxLEFT, 0);

    consistency_impro->Add(title_consistency_impro, 0, wxLEFT, 0);
    consistency_impro->Add(label_consistency_impro, 0, wxLEFT, 0);


    wxPanel *line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    line->SetBackgroundColour(line_color);

    auto tips = new Label(this, Label::Body_14, _L("Your gcode has been improved for the best possible print. To further improve your print please check out our wiki for tips & tricks on what to do next."));
    tips->SetForegroundColour(label_color);
    tips->SetSize(wxSize(FromDIP(410), -1));
    tips->SetMinSize(wxSize(FromDIP(410), -1));
    tips->SetMaxSize(wxSize(FromDIP(410), -1));
    tips->Wrap(FromDIP(410));

    /*helio wiki*/
    auto helio_wiki_link = new LinkLabel(this, _L("Click for more details"),
                                    wxGetApp().app_config->get("language") == "zh_CN" ? "https://wiki.helioadditive.com/zh/home" : "https://wiki.helioadditive.com/en/home");
    helio_wiki_link->SeLinkLabelFColour(wxColour(175, 124, 255));
#ifdef __WXMSW__
    helio_wiki_link->SeLinkLabelBColour(GetBackgroundColour());
#else
    helio_wiki_link->SeLinkLabelBColour(wxNullColour);
    helio_wiki_link->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
#endif
    helio_wiki_link->Bind(wxEVT_ENTER_WINDOW, [this](auto &e) { SetCursor(wxCURSOR_HAND); });
    helio_wiki_link->Bind(wxEVT_LEAVE_WINDOW, [this](auto &e) { SetCursor(wxCURSOR_ARROW); });

    wxBoxSizer *sizer_bottom = new wxBoxSizer(wxHORIZONTAL);

    /*rating*/
    wxBoxSizer *sizer_rating = new wxBoxSizer(wxHORIZONTAL);
    std::vector<wxStaticBitmap *> stars;
    // score_star_dark = grey/unselected, score_star_light = yellow/selected
    auto rating_star1 = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("score_star_dark", this, 24), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)), 0);
    auto rating_star2 = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("score_star_dark", this, 24), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)), 0);
    auto rating_star3 = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("score_star_dark", this, 24), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)), 0);
    auto rating_star4 = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("score_star_dark", this, 24), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)), 0);
    auto rating_star5 = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("score_star_dark", this, 24), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)), 0);

    stars.push_back(rating_star1);
    stars.push_back(rating_star2);
    stars.push_back(rating_star3);
    stars.push_back(rating_star4);
    stars.push_back(rating_star5);

    for (auto i = 0; i < stars.size(); i++) { 
        stars[i]->SetToolTip(_L("Rate the enhancement based on your expectations. 5 stars means awesome! 1 star means it didn't add much value for you."));
        stars[i]->Bind(wxEVT_ENTER_WINDOW, [this](auto &e) { SetCursor(wxCURSOR_HAND); });
        stars[i]->Bind(wxEVT_LEAVE_WINDOW, [this](auto &e) { SetCursor(wxCURSOR_ARROW); });
        stars[i]->Bind(wxEVT_LEFT_DOWN, [this, stars, i](wxMouseEvent &e) {
            if (!finish_rating) {
                finish_rating = true;
                std::vector<float> rating_num = { 0.2f, 0.4f, 0.6f, 0.8f, 1.0f };
                if (i < rating_num.size()) {
                    show_rating(stars, i);  
                    wxGetApp().plater()->feedback_helio_process(rating_num[i], "");
                }
            }
        });
    }

    sizer_rating->Add(rating_star1, 0, wxLEFT, 0);
    sizer_rating->Add(rating_star2, 0, wxLEFT, FromDIP(10));
    sizer_rating->Add(rating_star3, 0, wxLEFT, FromDIP(10));
    sizer_rating->Add(rating_star4, 0, wxLEFT, FromDIP(10));
    sizer_rating->Add(rating_star5, 0, wxLEFT, FromDIP(10));

    wxStaticBitmap* save_icon = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("save", this, 24), wxDefaultPosition,
        wxSize(FromDIP(24), FromDIP(24)));
    save_icon->SetToolTip(_L("Save the enhanced gcode locally"));

    save_icon->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    save_icon->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
    save_icon->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxPostEvent(wxGetApp().plater(), SimpleEvent(EVT_GLTOOLBAR_EXPORT_SLICED_FILE));
    });

    // Print Plate button - primary action with green styling
    StateColor btn_bg_green = StateColor(
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    auto m_button_print_plate = new Button(this, _L("Print Plate"));
    m_button_print_plate->SetBackgroundColor(btn_bg_green);
    m_button_print_plate->SetBorderColor(wxColour(0, 174, 66));
    StateColor white_text(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
                          std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Hovered),
                          std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Pressed),
                          std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Enabled),
                          std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));
    m_button_print_plate->SetTextColor(white_text);
    m_button_print_plate->SetTextColorNormal(wxColour(255, 255, 254));
    m_button_print_plate->SetFont(Label::Body_12);
    m_button_print_plate->SetSize(wxSize(FromDIP(100), FromDIP(24)));
    m_button_print_plate->SetMinSize(wxSize(FromDIP(100), FromDIP(24)));
    m_button_print_plate->SetCornerRadius(FromDIP(12));
    m_button_print_plate->SetToolTip(_L("Print the enhanced part immediately"));
    m_button_print_plate->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        EndModal(wxID_OK);
        wxGetApp().plater()->CallAfter([]() {
            wxPostEvent(wxGetApp().plater(), SimpleEvent(EVT_GLTOOLBAR_PRINT_PLATE));
        });
    });
    m_button_print_plate->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    m_button_print_plate->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });

    auto m_button_view_details = new Button(this, _L("View Details"));
    // Use theme-aware colors for secondary button (matches simulation dialog)
    if (is_dark) {
        m_button_view_details->SetBackgroundColor(StateColor(std::pair<wxColour, int>(HELIO_CARD_BG, StateColor::Normal)));
        m_button_view_details->SetBorderColor(HELIO_TEXT);
        m_button_view_details->SetTextColor(HELIO_TEXT);
    } else {
        m_button_view_details->SetBackgroundColor(StateColor(std::pair<wxColour, int>(*wxWHITE, StateColor::Normal)));
        m_button_view_details->SetBorderColor(wxColour(208, 208, 208));
        m_button_view_details->SetTextColor(wxColour(50, 50, 50));
    }
    m_button_view_details->SetFont(Label::Body_12);
    m_button_view_details->SetSize(wxSize(FromDIP(100), FromDIP(24)));
    m_button_view_details->SetMinSize(wxSize(FromDIP(100), FromDIP(24)));
    m_button_view_details->SetCornerRadius(FromDIP(12));
    m_button_view_details->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        EndModal(wxID_CLOSE);
        // Switch to the Preview panel to show details
        wxGetApp().plater()->CallAfter([]() {
            wxGetApp().mainframe->select_tab(size_t(MainFrame::tpPreview));
        });
    });
    m_button_view_details->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    m_button_view_details->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });

    sizer_bottom->Add(sizer_rating, 0, wxLEFT|wxALIGN_CENTER, 0);
    sizer_bottom->Add( 0, 0, 1, wxEXPAND, 0 );
    sizer_bottom->Add(save_icon, 0, wxLEFT|wxALIGN_CENTER, 0);
    sizer_bottom->Add(0, 0, 0, wxLEFT, FromDIP(14));
    sizer_bottom->Add(m_button_print_plate, 0, wxLEFT|wxALIGN_CENTER, 0);
    sizer_bottom->Add(0, 0, 0, wxLEFT, FromDIP(10));
    sizer_bottom->Add(m_button_view_details, 0, wxLEFT|wxALIGN_CENTER, 0);
    
    main_sizer->Add(helio_top_background, 0, wxEXPAND, 0);
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(26));
    main_sizer->Add(time_impro, 0, wxLEFT | wxRIGHT, FromDIP(40));
    main_sizer->Add(0, 0, 0,wxTOP, FromDIP(14));
    main_sizer->Add(average_impro, 0, wxLEFT | wxRIGHT, FromDIP(40));
    main_sizer->Add(0, 0, 0,wxTOP, FromDIP(14));
    main_sizer->Add(consistency_impro, 0, wxLEFT | wxRIGHT, FromDIP(40));
    main_sizer->Add(0, 0, 0,wxTOP, FromDIP(24));
    main_sizer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(40));
    main_sizer->Add(0, 0, 0,wxTOP, FromDIP(14));
    main_sizer->Add(tips, 0, wxLEFT|wxRIGHT, FromDIP(40));
    main_sizer->Add(0, 0, 0,wxTOP, FromDIP(10));
    main_sizer->Add(helio_wiki_link, 0, wxLEFT | wxRIGHT, FromDIP(40));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(18));
    main_sizer->Add(sizer_bottom, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(40));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(23));

    SetSizer(main_sizer);
    Layout();
    Fit();
    {
        wxWindow* parent = GetParent();
        if (parent) {
            wxPoint parentPos = parent->GetScreenPosition();
            wxSize parentSize = parent->GetSize();
            wxSize dlgSize = GetSize();
            int x = parentPos.x + (parentSize.GetWidth() - dlgSize.GetWidth()) / 2;
            int y = parentPos.y + (parentSize.GetHeight() - dlgSize.GetHeight()) / 3;
            SetPosition(wxPoint(x, y));
        }
    }
}

void HelioRatingDialog::show_rating(std::vector<wxStaticBitmap *> stars, int rating)
{
    for (auto i = 0; i <= rating; i++) {
        stars[i]->SetBitmap(create_scaled_bitmap("score_star_light", this, 24));
        stars[i]->Refresh(); 
    }
}

wxString HelioRatingDialog::format_improvement(wxString imp) 
{
    if (imp == "low") {
        return _L("Low");
    }
    else if (imp == "medium") {
        return _L("Medium");
    }
    else if (imp == "high") {
        return _L("High");  
    }
    return _L("Medium");
} 

void HelioRatingDialog::on_dpi_changed(const wxRect &suggested_rect) 
{
}

HelioInputDialogTheme HelioSimulationResultsDialog::get_theme() const
{
    HelioInputDialogTheme theme;
    bool is_dark = wxGetApp().dark_mode();
    
    if (is_dark) {
        // Helio dark palette
        theme.bg = HELIO_BG_BASE;
        theme.card = HELIO_CARD_BG;
        theme.card2 = HELIO_CARD_BG;
        theme.border = HELIO_BORDER;
        theme.text = HELIO_TEXT;
        theme.muted = HELIO_MUTED;
        theme.purple = HELIO_PURPLE;
        theme.blue = HELIO_BLUE;
    } else {
        // Light mode palette
        theme.bg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
        theme.card = wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT);
        theme.card2 = *wxWHITE;  // White for input fields to ensure clear contrast
        theme.border = wxColour(0, 0, 0, 25);
        theme.text = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
        theme.muted = wxColour(100, 100, 100);
        theme.purple = HELIO_PURPLE;
        theme.blue = HELIO_BLUE;
    }
    return theme;
}

HelioSimulationResultsDialog::HelioSimulationResultsDialog(wxWindow *parent, 
                                                             HelioQuery::SimulationResult simulation,
                                                             int original_print_time_seconds,
                                                             const std::vector<std::pair<ExtrusionRole, float>>& roles_times)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, wxString("Helio Additive"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    m_simulation = simulation;
    m_original_print_time_seconds = original_print_time_seconds;
    m_roles_times = roles_times;

    // Get theme colors for current mode
    auto theme = get_theme();

    // Set Helio icon (not BambuStudio icon)
    wxBitmap bmp = create_scaled_bitmap("helio_icon", this, 32);
    wxIcon icon;
    icon.CopyFromBitmap(bmp);
    SetIcon(icon);

    // Use theme background
    SetBackgroundColour(theme.bg);

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);

    // Header with Helio logo (always dark for branding)
    wxBoxSizer *helio_top_hsizer = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *helio_top_vsizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *helio_top_content_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto helio_top_background = new wxPanel(this);
    helio_top_background->SetBackgroundColour(wxColour(16, 16, 16));
    helio_top_background->SetMinSize(wxSize(-1, FromDIP(70)));
    helio_top_background->SetMaxSize(wxSize(-1, FromDIP(70)));
    auto helio_top_icon = new wxStaticBitmap(helio_top_background, wxID_ANY, create_scaled_bitmap("helio_icon", helio_top_background, 32), wxDefaultPosition,
                                             wxSize(FromDIP(32), FromDIP(32)), 0);
    auto helio_top_label = new Label(helio_top_background, Label::Body_16, L("HELIO ADDITIVE"));
    wxFont bold_font = helio_top_label->GetFont();
    bold_font.SetWeight(wxFONTWEIGHT_BOLD);
    helio_top_label->SetFont(bold_font);
    helio_top_label->SetForegroundColour(wxColour("#FEFEFF"));
    helio_top_content_sizer->Add(helio_top_icon, 0, wxLEFT | wxALIGN_CENTER, FromDIP(45));
    helio_top_content_sizer->Add(helio_top_label, 0, wxLEFT | wxALIGN_CENTER, FromDIP(8));
    helio_top_vsizer->Add(helio_top_content_sizer, 0, wxALIGN_CENTER, 0);
    helio_top_hsizer->Add(helio_top_vsizer, 0, wxALIGN_CENTER, 0);
    helio_top_background->SetSizer(helio_top_hsizer);
    helio_top_background->Layout();

    // Title section
    auto title = new Label(this, Label::Head_20, _L("Verify Print Success Results"));
    title->SetForegroundColour(theme.purple);
    wxFont title_font = title->GetFont();
    title_font.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(title_font);

    auto subtitle = new Label(this, Label::Body_14, _L("Simulates the thermal process to confirm your part will print without failure."));
    subtitle->SetForegroundColour(theme.muted);
    subtitle->SetToolTip(_L("Now referred to as 'Assess' or 'Assessment'"));
    subtitle->SetSize(wxSize(FromDIP(410), -1));
    subtitle->SetMinSize(wxSize(FromDIP(410), -1));
    subtitle->SetMaxSize(wxSize(FromDIP(410), -1));
    subtitle->Wrap(FromDIP(410));

    // Divider
    wxPanel *line1 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    line1->SetBackgroundColour(theme.muted);

    // Calculate speed improvement (for use in optimization block later)
    wxString speed_impro_text;
    bool has_speed_improvement = false;
    if (m_simulation.speedFactor && *m_simulation.speedFactor < 1.0 && m_original_print_time_seconds > 0) {
        // Calculate time for potentially optimizable sections (A)
        // Sum times for: inner wall (erPerimeter), outer wall (erExternalPerimeter), 
        // sparse infill (erInternalInfill), and internal solid infill (erSolidInfill)
        float optimizable_time = 0.0f;
        for (const auto& role_time : m_roles_times) {
            ExtrusionRole role = role_time.first;
            if (role == erPerimeter ||           // Inner wall
                role == erExternalPerimeter ||    // Outer wall
                role == erInternalInfill ||       // Sparse infill
                role == erSolidInfill) {          // Internal solid infill
                optimizable_time += role_time.second;
            }
        }
        
        // Calculate time for optimizable sections after optimization (B)
        // speedFactor < 1 means faster, so multiply to get reduced time
        float optimized_optimizable_time = optimizable_time * static_cast<float>(*m_simulation.speedFactor);
        
        // Calculate final potential speed improved time (C) = original_time - A + B
        float final_optimized_time = m_original_print_time_seconds - optimizable_time + optimized_optimizable_time;
        
        // Calculate improvement = original_time - C = A - B
        float improvement_seconds = optimizable_time - optimized_optimizable_time;
        
        if (improvement_seconds > 0) {
            has_speed_improvement = true;
            int improvement_sec = static_cast<int>(improvement_seconds);
            int final_opt_sec = static_cast<int>(final_optimized_time);
            auto label_improvement = wxString::Format("%s", short_time(get_time_dhms(improvement_sec)));
            auto label_original = wxString::Format("%s", short_time(get_time_dhms(m_original_print_time_seconds)));
            auto label_optimized = wxString::Format("%s", short_time(get_time_dhms(final_opt_sec)));
            speed_impro_text = _L("Time saved: ") + label_improvement + " (" + _L("From ") + label_original + wxString(L" \u2192 ") + label_optimized + ")";
        }
    }

    // Bold font for labels
    wxFont lab_bold_font = Label::Body_14;
    lab_bold_font.SetWeight(wxFONTWEIGHT_BOLD);

    // Expected Outcome (vertical layout like Analysis)
    wxBoxSizer *outcome_sizer = new wxBoxSizer(wxVERTICAL);
    auto title_outcome = new Label(this, Label::Body_14, _L("Expected Outcome:"));
    title_outcome->SetForegroundColour(theme.muted);

    wxString outcome_text;
    if (m_simulation.printInfo) {
        outcome_text = get_outcome_text(*m_simulation.printInfo);
    } else {
        outcome_text = _L("Unknown");
    }

    auto label_outcome = new Label(this, Label::Body_14, outcome_text);
    label_outcome->SetForegroundColour(theme.text);
    label_outcome->SetFont(lab_bold_font);

    outcome_sizer->Add(title_outcome, 0, 0, 0);
    outcome_sizer->Add(label_outcome, 0, wxTOP, FromDIP(8));

    // Analysis
    wxBoxSizer *analysis_sizer = new wxBoxSizer(wxVERTICAL);
    auto title_analysis = new Label(this, Label::Body_14, _L("Analysis:"));
    title_analysis->SetForegroundColour(theme.muted);
    title_analysis->SetMinSize(wxSize(FromDIP(225), -1));

    wxString analysis_text;
    wxString fix_suggestions_preview;
    if (m_simulation.printInfo) {
        analysis_text = get_analysis_text(*m_simulation.printInfo);
        fix_suggestions_preview = get_fix_suggestions_preview(*m_simulation.printInfo);
    }

    // Add bullet point to analysis text
    wxString bulleted_analysis = wxString(L"\u2022 ") + analysis_text;
    auto label_analysis = new Label(this, Label::Body_14, bulleted_analysis);
    label_analysis->SetForegroundColour(theme.text);
    label_analysis->SetSize(wxSize(FromDIP(500), -1));
    label_analysis->SetMinSize(wxSize(FromDIP(500), -1));
    label_analysis->SetMaxSize(wxSize(FromDIP(500), -1));
    label_analysis->Wrap(FromDIP(500));

    analysis_sizer->Add(title_analysis, 0, wxLEFT, 0);
    analysis_sizer->Add(label_analysis, 0, wxTOP, FromDIP(8));

    // Additional observations (caveats) - same level as Analysis
    wxBoxSizer *caveats_sizer = nullptr;
    if (m_simulation.printInfo && !m_simulation.printInfo->caveats.empty()) {
        caveats_sizer = new wxBoxSizer(wxVERTICAL);
        auto title_caveats = new Label(this, Label::Body_14, _L("Additional observations:"));
        title_caveats->SetForegroundColour(theme.purple);
        wxFont caveat_title_font = title_caveats->GetFont();
        caveat_title_font.SetWeight(wxFONTWEIGHT_BOLD);
        title_caveats->SetFont(caveat_title_font);

        caveats_sizer->Add(title_caveats, 0, 0, 0);
        
        // Limit to 2 caveats max
        size_t caveat_count = std::min(m_simulation.printInfo->caveats.size(), size_t(2));
        for (size_t i = 0; i < caveat_count; ++i) {
            auto caveat_label = new Label(this, Label::Body_14, wxString(L"\u2022 ") + wxString::FromUTF8(m_simulation.printInfo->caveats[i].description.c_str()));
            caveat_label->SetForegroundColour(theme.text);
            caveat_label->SetSize(wxSize(FromDIP(500), -1));
            caveat_label->SetMinSize(wxSize(FromDIP(500), -1));
            caveat_label->SetMaxSize(wxSize(FromDIP(500), -1));
            caveat_label->Wrap(FromDIP(500));
            caveats_sizer->Add(caveat_label, 0, wxTOP, FromDIP(4));
        }
    }

    // Fix suggestions expandable section
    wxBoxSizer *fix_suggestions_sizer = nullptr;
    if (!fix_suggestions_preview.IsEmpty() && m_simulation.printInfo) {
        fix_suggestions_sizer = new wxBoxSizer(wxVERTICAL);
        
        // Header row with arrow, title, and preview
        wxBoxSizer *fix_header_sizer = new wxBoxSizer(wxHORIZONTAL);
        
        m_fix_suggestions_arrow = new Label(this, Label::Body_14, L"▶");
        m_fix_suggestions_arrow->SetForegroundColour(theme.purple);
        
        auto fix_title = new Label(this, Label::Body_14, _L("Fix suggestions"));
        fix_title->SetForegroundColour(theme.purple);
        wxFont fix_title_font = fix_title->GetFont();
        fix_title_font.SetWeight(wxFONTWEIGHT_BOLD);
        fix_title->SetFont(fix_title_font);
        
        // Preview text shown when collapsed
        m_fix_suggestions_preview = new Label(this, Label::Body_13, wxString(L" \u2014 ") + fix_suggestions_preview);
        m_fix_suggestions_preview->SetForegroundColour(theme.muted);
        
        fix_header_sizer->Add(m_fix_suggestions_arrow, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        fix_header_sizer->Add(fix_title, 0, wxALIGN_CENTER_VERTICAL, 0);
        fix_header_sizer->Add(m_fix_suggestions_preview, 0, wxALIGN_CENTER_VERTICAL, 0);
        
        // Make header clickable
        m_fix_suggestions_arrow->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { toggle_fix_suggestions(); });
        fix_title->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { toggle_fix_suggestions(); });
        m_fix_suggestions_preview->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { toggle_fix_suggestions(); });
        m_fix_suggestions_arrow->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
        m_fix_suggestions_arrow->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
        fix_title->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
        fix_title->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
        m_fix_suggestions_preview->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
        m_fix_suggestions_preview->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
        
        // Scrolled window to contain the fix suggestions content
        m_fix_suggestions_scroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
        m_fix_suggestions_scroll->SetBackgroundColour(theme.bg);
        m_fix_suggestions_scroll->SetScrollRate(0, FromDIP(10));
        m_fix_suggestions_scroll->SetMinSize(wxSize(-1, FromDIP(200)));
        m_fix_suggestions_scroll->SetMaxSize(wxSize(-1, FromDIP(200)));
        
        // Content panel inside the scrolled window
        m_fix_suggestions_content = new wxPanel(m_fix_suggestions_scroll);
        m_fix_suggestions_content->SetBackgroundColour(theme.bg);
        
        create_fix_suggestions_section(fix_suggestions_sizer, theme);
        
        // Add content to scrolled window
        wxBoxSizer* scroll_sizer = new wxBoxSizer(wxVERTICAL);
        scroll_sizer->Add(m_fix_suggestions_content, 0, wxEXPAND, 0);
        m_fix_suggestions_scroll->SetSizer(scroll_sizer);
        m_fix_suggestions_scroll->FitInside();
        m_fix_suggestions_scroll->Hide(); // Start collapsed
        
        fix_suggestions_sizer->Add(fix_header_sizer, 0, 0, 0);
        fix_suggestions_sizer->Add(m_fix_suggestions_scroll, 0, wxEXPAND, 0);
    }

    // View Details button (in summary section)
    m_button_view_details = new Button(this, _L("View Details"));
    // Use theme-aware colors for secondary button
    if (wxGetApp().dark_mode()) {
        m_button_view_details->SetBackgroundColor(StateColor(std::pair<wxColour, int>(theme.card, StateColor::Normal)));
        m_button_view_details->SetBorderColor(theme.text);
        m_button_view_details->SetTextColor(theme.text);
    } else {
        m_button_view_details->SetBackgroundColor(StateColor(std::pair<wxColour, int>(*wxWHITE, StateColor::Normal)));
        m_button_view_details->SetBorderColor(wxColour(208, 208, 208));
        m_button_view_details->SetTextColor(theme.text);
    }
    m_button_view_details->SetFont(Label::Body_12);
    m_button_view_details->SetSize(wxSize(FromDIP(100), FromDIP(28)));
    m_button_view_details->SetMinSize(wxSize(FromDIP(100), FromDIP(28)));
    m_button_view_details->SetCornerRadius(FromDIP(6));
    m_button_view_details->Bind(wxEVT_LEFT_DOWN, &HelioSimulationResultsDialog::on_view_details, this);
    m_button_view_details->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    m_button_view_details->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });

    // Divider
    wxPanel *line2 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    line2->SetBackgroundColour(theme.muted);

    // Optimization block (right-aligned, grouped with Enhance Speed & Quality button)
    wxBoxSizer *optimization_block_sizer = nullptr;
    if (has_speed_improvement) {
        optimization_block_sizer = new wxBoxSizer(wxHORIZONTAL);
        
        // Title: "With Enhance Speed & Quality:"
        auto optimization_title = new Label(this, Label::Body_14, wxEmptyString);
        optimization_title->SetLabelText(_L("With Enhance Speed & Quality:"));
        optimization_title->SetForegroundColour(theme.muted);
        
        // Speed improvement value
        auto optimization_value = new Label(this, Label::Body_14, speed_impro_text);
        optimization_value->SetForegroundColour(theme.purple);
        wxFont opt_bold_font = optimization_value->GetFont();
        opt_bold_font.SetWeight(wxFONTWEIGHT_BOLD);
        optimization_value->SetFont(opt_bold_font);
        optimization_value->SetToolTip(_L("This is the time that can be saved if you run the Helio Additive Enhance feature. Click the Enhance Speed and Quality button to proceed."));
        
        // Right-align the optimization text
        optimization_block_sizer->Add(0, 0, 1, wxEXPAND, 0);
        optimization_block_sizer->Add(optimization_title, 0, wxALIGN_CENTER_VERTICAL, 0);
        optimization_block_sizer->Add(optimization_value, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(8));
    }

    // Enhance Speed & Quality button (right-aligned)
    wxBoxSizer *sizer_actions = new wxBoxSizer(wxHORIZONTAL);

    StateColor btn_bg_purple(std::pair<wxColour, int>(wxColour(175, 124, 255), StateColor::Enabled), 
                             std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Disabled), 
                             std::pair<wxColour, int>(wxColour(175, 124, 255), StateColor::Pressed), 
                             std::pair<wxColour, int>(wxColour(175, 124, 255), StateColor::Hovered),
                             std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    m_button_enhance = new Button(this, wxEmptyString);
    m_button_enhance->SetLabel(_L("Enhance Speed & Quality"));
    m_button_enhance->SetBackgroundColor(btn_bg_purple);
    m_button_enhance->SetBorderColor(*wxWHITE);
    // White text for all states (use 254 to avoid dark mode remapping)
    StateColor enhance_btn_text(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Hovered),
                                std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
                                std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Pressed),
                                std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Enabled),
                                std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));
    m_button_enhance->SetTextColor(enhance_btn_text);
    m_button_enhance->SetFont(Label::Body_12);
    m_button_enhance->SetSize(wxSize(FromDIP(200), FromDIP(40)));
    m_button_enhance->SetMinSize(wxSize(FromDIP(200), FromDIP(40)));
    m_button_enhance->SetCornerRadius(FromDIP(12));
    m_button_enhance->SetToolTip(_L("Applies Helio's optimized speed/flow/fan strategy."));
    m_button_enhance->Bind(wxEVT_LEFT_DOWN, &HelioSimulationResultsDialog::on_enhance_speed_quality, this);
    m_button_enhance->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    m_button_enhance->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });

    // Right-align the enhance button
    sizer_actions->Add(0, 0, 1, wxEXPAND, 0);
    sizer_actions->Add(m_button_enhance, 0, wxALIGN_CENTER, 0);

    // Layout
    main_sizer->Add(helio_top_background, 0, wxEXPAND, 0);
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(26));
    main_sizer->Add(title, 0, wxLEFT | wxRIGHT, FromDIP(40));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(8));
    main_sizer->Add(subtitle, 0, wxLEFT | wxRIGHT, FromDIP(40));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(24));
    main_sizer->Add(line1, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(40));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(20));
    main_sizer->Add(outcome_sizer, 0, wxLEFT | wxRIGHT, FromDIP(40));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(14));
    main_sizer->Add(analysis_sizer, 0, wxLEFT | wxRIGHT, FromDIP(40));
    if (caveats_sizer) {
        main_sizer->Add(0, 0, 0, wxTOP, FromDIP(14));
        main_sizer->Add(caveats_sizer, 0, wxLEFT | wxRIGHT, FromDIP(40));
    }
    if (fix_suggestions_sizer) {
        main_sizer->Add(0, 0, 0, wxTOP, FromDIP(14));
        main_sizer->Add(fix_suggestions_sizer, 0, wxLEFT | wxRIGHT, FromDIP(40));
    }
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(18));
    main_sizer->Add(m_button_view_details, 0, wxALIGN_RIGHT | wxRIGHT, FromDIP(40));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(24));
    main_sizer->Add(line2, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(40));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(18));
    if (optimization_block_sizer) {
        main_sizer->Add(optimization_block_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(40));
        main_sizer->Add(0, 0, 0, wxTOP, FromDIP(12));
    }
    main_sizer->Add(sizer_actions, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(40));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(23));

    SetSizer(main_sizer);
    Layout();
    Fit();
    {
        wxWindow* parent = GetParent();
        if (parent) {
            wxPoint parentPos = parent->GetScreenPosition();
            wxSize parentSize = parent->GetSize();
            wxSize dlgSize = GetSize();
            int x = parentPos.x + (parentSize.GetWidth() - dlgSize.GetWidth()) / 2;
            int y = parentPos.y + (parentSize.GetHeight() - dlgSize.GetHeight()) / 3;
            SetPosition(wxPoint(x, y));
        }
    }
}

wxString HelioSimulationResultsDialog::get_outcome_text(const HelioQuery::PrintInfo& print_info)
{
    // Get emoji prefix based on outcome
    wxString emoji_prefix;
    if (print_info.printOutcome == "WILL_PRINT") {
        emoji_prefix = L"✅ ";
    } else if (print_info.printOutcome == "MAY_PRINT") {
        emoji_prefix = L"⚠️ ";
    } else if (print_info.printOutcome == "LIKELY_FAIL") {
        emoji_prefix = L"❌ ";
    }
    
    // Use API description if available, otherwise fall back to hardcoded text
    if (!print_info.printOutcomeDescription.empty()) {
        return emoji_prefix + wxString::FromUTF8(print_info.printOutcomeDescription);
    }
    
    // Fallback for older API versions
    if (print_info.printOutcome == "WILL_PRINT") {
        return _L("✅ Will Print");
    } else if (print_info.printOutcome == "MAY_PRINT") {
        return _L("⚠️ May Print with Issues");
    } else if (print_info.printOutcome == "LIKELY_FAIL") {
        return _L("❌ Likely to Fail");
    }
    return _L("Unknown");
}

wxString HelioSimulationResultsDialog::get_analysis_text(const HelioQuery::PrintInfo& print_info)
{
    // Use API description if available
    if (!print_info.temperatureDirectionDescription.empty()) {
        return wxString::FromUTF8(print_info.temperatureDirectionDescription);
    }
    
    // Fallback for older API versions
    if (print_info.temperatureDirection == "NONE") {
        return _L("Thermal conditions are balanced. The part is printing within safe temperature limits.");
    } else if (print_info.temperatureDirection == "OVERCOOLING") {
        return _L("Material is cooling too rapidly before the next layer is applied. This can cause weak layer bonding, warping, or cracking.");
    } else if (print_info.temperatureDirection == "OVERHEATING") {
        return _L("Heat is building up in small layers faster than it can dissipate. This may cause sagging or loss of detail in fine features.");
    }
    return wxString();
}

wxString HelioSimulationResultsDialog::get_fix_suggestions_preview(const HelioQuery::PrintInfo& print_info)
{
    const std::string& direction = print_info.temperatureDirection;
    
    if (direction == "NONE" && print_info.caveats.empty()) {
        return _L("Ready to print");
    }
    
    if (direction == "OVERCOOLING") {
        return _L("Too cold overall — see suggestions");
    }
    
    if (direction == "OVERHEATING") {
        return _L("Too hot overall — see suggestions");
    }
    
    return wxString();
}

void HelioSimulationResultsDialog::toggle_fix_suggestions()
{
    m_fix_suggestions_expanded = !m_fix_suggestions_expanded;
    if (m_fix_suggestions_expanded) {
        if (m_fix_suggestions_scroll) {
            m_fix_suggestions_scroll->Show();
            m_fix_suggestions_scroll->FitInside();
        }
        m_fix_suggestions_arrow->SetLabel(L"▼");
        if (m_fix_suggestions_preview) m_fix_suggestions_preview->Hide();
    } else {
        if (m_fix_suggestions_scroll) m_fix_suggestions_scroll->Hide();
        m_fix_suggestions_arrow->SetLabel(L"▶");
        if (m_fix_suggestions_preview) m_fix_suggestions_preview->Show();
    }
    Layout();
    Fit();
}

void HelioSimulationResultsDialog::toggle_advanced()
{
    m_advanced_expanded = !m_advanced_expanded;
    if (m_advanced_expanded) {
        if (m_advanced_content) m_advanced_content->Show();
        if (m_advanced_arrow) m_advanced_arrow->SetLabel(L"▼");
    } else {
        if (m_advanced_content) m_advanced_content->Hide();
        if (m_advanced_arrow) m_advanced_arrow->SetLabel(L"▶");
    }
    if (m_fix_suggestions_scroll) {
        m_fix_suggestions_content->Layout();
        m_fix_suggestions_scroll->FitInside();
    }
}

void HelioSimulationResultsDialog::toggle_expert()
{
    m_expert_expanded = !m_expert_expanded;
    if (m_expert_expanded) {
        if (m_expert_content) m_expert_content->Show();
        if (m_expert_arrow) m_expert_arrow->SetLabel(L"▼");
    } else {
        if (m_expert_content) m_expert_content->Hide();
        if (m_expert_arrow) m_expert_arrow->SetLabel(L"▶");
    }
    if (m_fix_suggestions_scroll) {
        m_fix_suggestions_content->Layout();
        m_fix_suggestions_scroll->FitInside();
    }
}

void HelioSimulationResultsDialog::toggle_learn_more()
{
    m_learn_more_expanded = !m_learn_more_expanded;
    if (m_learn_more_expanded) {
        if (m_learn_more_content) m_learn_more_content->Show();
        if (m_learn_more_arrow) m_learn_more_arrow->SetLabel(L"▼");
    } else {
        if (m_learn_more_content) m_learn_more_content->Hide();
        if (m_learn_more_arrow) m_learn_more_arrow->SetLabel(L"▶");
    }
    if (m_fix_suggestions_scroll) {
        m_fix_suggestions_content->Layout();
        m_fix_suggestions_scroll->FitInside();
    }
}

void HelioSimulationResultsDialog::create_fix_suggestions_section(wxBoxSizer* parent_sizer, const HelioInputDialogTheme& theme)
{
    if (!m_simulation.printInfo) return;
    
    const std::string& direction = m_simulation.printInfo->temperatureDirection;
    wxBoxSizer* content_sizer = new wxBoxSizer(wxVERTICAL);
    
    // Helper lambda to create a nested expander
    auto create_nested_expander = [this, &theme](wxPanel* parent, const wxString& title, 
                                                  Label*& arrow_ref, wxPanel*& content_ref,
                                                  std::function<void()> toggle_func) -> wxBoxSizer* {
        wxBoxSizer* expander_sizer = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* header_sizer = new wxBoxSizer(wxHORIZONTAL);
        
        arrow_ref = new Label(parent, Label::Body_13, L"▶");
        arrow_ref->SetForegroundColour(theme.muted);
        
        auto title_label = new Label(parent, Label::Body_13, title);
        title_label->SetForegroundColour(theme.muted);
        
        header_sizer->Add(arrow_ref, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
        header_sizer->Add(title_label, 0, wxALIGN_CENTER_VERTICAL, 0);
        
        arrow_ref->Bind(wxEVT_LEFT_DOWN, [toggle_func](wxMouseEvent& e) { toggle_func(); });
        title_label->Bind(wxEVT_LEFT_DOWN, [toggle_func](wxMouseEvent& e) { toggle_func(); });
        arrow_ref->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
        arrow_ref->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
        title_label->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
        title_label->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
        
        content_ref = new wxPanel(parent);
        content_ref->SetBackgroundColour(theme.bg);
        content_ref->Hide();
        
        expander_sizer->Add(header_sizer, 0, wxTOP, FromDIP(8));
        expander_sizer->Add(content_ref, 0, wxLEFT, FromDIP(16));
        
        return expander_sizer;
    };
    
    // Helper lambda to add wrapped text label
    auto add_wrapped_label = [&theme, this](wxPanel* parent, wxBoxSizer* sizer, const wxString& text) {
        auto label = new Label(parent, Label::Body_13, text);
        label->SetForegroundColour(theme.text);
        label->SetSize(wxSize(FromDIP(500), -1));
        label->SetMinSize(wxSize(FromDIP(500), -1));
        label->SetMaxSize(wxSize(FromDIP(500), -1));
        label->Wrap(FromDIP(500));
        sizer->Add(label, 0, wxTOP, FromDIP(4));
    };
    
    if (direction == "NONE" && m_simulation.printInfo->caveats.empty()) {
        // Ready to print - simple message
        auto ready_label = new Label(m_fix_suggestions_content, Label::Body_13, 
            _L("Your part is ready to print! Thermal conditions are within safe limits."));
        ready_label->SetForegroundColour(theme.text);
        content_sizer->Add(ready_label, 0, wxTOP, FromDIP(8));
    }
    else if (!m_simulation.suggestedFixes.empty()) {
        // Use dynamic suggested fixes from API
        
        // Title based on temperature direction
        wxString title_text;
        wxString body_text;
        if (direction == "OVERCOOLING") {
            title_text = _L("Too cold (overall)");
            body_text = _L("The part cools too fast between layers on average. Some layers may behave differently—see observations.");
        } else if (direction == "OVERHEATING") {
            title_text = _L("Too hot (overall)");
            body_text = _L("The part stays too warm between layers on average. Some layers may behave differently—see observations.");
        } else {
            title_text = _L("Suggestions");
            body_text = wxString();
        }
        
        auto title_label = new Label(m_fix_suggestions_content, Label::Body_14, title_text);
        title_label->SetForegroundColour(theme.text);
        wxFont bold_font = title_label->GetFont();
        bold_font.SetWeight(wxFONTWEIGHT_BOLD);
        title_label->SetFont(bold_font);
        content_sizer->Add(title_label, 0, wxTOP, FromDIP(8));
        
        if (!body_text.IsEmpty()) {
            auto body_label = new Label(m_fix_suggestions_content, Label::Body_13, body_text);
            body_label->SetForegroundColour(theme.text);
            body_label->SetSize(wxSize(FromDIP(500), -1));
            body_label->SetMinSize(wxSize(FromDIP(500), -1));
            body_label->SetMaxSize(wxSize(FromDIP(500), -1));
            body_label->Wrap(FromDIP(500));
            content_sizer->Add(body_label, 0, wxTOP, FromDIP(4));
        }
        
        // Group fixes by category
        std::vector<HelioQuery::SuggestedFix> quick_fixes;
        std::vector<HelioQuery::SuggestedFix> advanced_fixes;
        std::vector<HelioQuery::SuggestedFix> expert_fixes;
        
        for (const auto& fix : m_simulation.suggestedFixes) {
            if (fix.category == "QUICK") {
                quick_fixes.push_back(fix);
            } else if (fix.category == "ADVANCED") {
                advanced_fixes.push_back(fix);
            } else if (fix.category == "EXPERT") {
                expert_fixes.push_back(fix);
            }
        }
        
        // Sort quick fixes by orderIndex
        std::sort(quick_fixes.begin(), quick_fixes.end(), [](const HelioQuery::SuggestedFix& a, const HelioQuery::SuggestedFix& b) {
            int a_order = a.orderIndex ? *a.orderIndex : INT_MAX;
            int b_order = b.orderIndex ? *b.orderIndex : INT_MAX;
            return a_order < b_order;
        });
        
        // Render quick fixes as numbered list
        if (!quick_fixes.empty()) {
            auto quick_fixes_title = new Label(m_fix_suggestions_content, Label::Body_13, _L("Quick fixes (beginner):"));
            quick_fixes_title->SetForegroundColour(theme.text);
            wxFont qf_bold = quick_fixes_title->GetFont();
            qf_bold.SetWeight(wxFONTWEIGHT_BOLD);
            quick_fixes_title->SetFont(qf_bold);
            content_sizer->Add(quick_fixes_title, 0, wxTOP, FromDIP(12));
            
            wxString quick_fixes_text;
            int idx = 1;
            for (const auto& fix : quick_fixes) {
                if (!quick_fixes_text.IsEmpty()) {
                    quick_fixes_text += "\n";
                }
                quick_fixes_text += wxString::Format("%d. %s", idx++, wxString::FromUTF8(fix.fix.c_str()));
            }
            
            auto quick_fixes_label = new Label(m_fix_suggestions_content, Label::Body_13, quick_fixes_text);
            quick_fixes_label->SetForegroundColour(theme.text);
            content_sizer->Add(quick_fixes_label, 0, wxTOP | wxLEFT, FromDIP(4));
        }
        
        // Render advanced fixes in expander
        if (!advanced_fixes.empty()) {
            auto advanced_sizer = create_nested_expander(m_fix_suggestions_content, _L("Advanced"),
                m_advanced_arrow, m_advanced_content, [this]() { toggle_advanced(); });
            wxBoxSizer* adv_content_sizer = new wxBoxSizer(wxVERTICAL);
            
            for (const auto& fix : advanced_fixes) {
                add_wrapped_label(m_advanced_content, adv_content_sizer, wxString::FromUTF8(fix.fix.c_str()));
                // Add extra details as bullet points
                for (const auto& detail : fix.extraDetails) {
                    add_wrapped_label(m_advanced_content, adv_content_sizer, 
                        wxString(L"\u2022 ") + wxString::FromUTF8(detail.c_str()));
                }
            }
            m_advanced_content->SetSizer(adv_content_sizer);
            content_sizer->Add(advanced_sizer, 0, wxLEFT, 0);
        }
        
        // Render expert fixes in expander
        if (!expert_fixes.empty()) {
            auto expert_sizer = create_nested_expander(m_fix_suggestions_content, _L("Expert"),
                m_expert_arrow, m_expert_content, [this]() { toggle_expert(); });
            wxBoxSizer* exp_content_sizer = new wxBoxSizer(wxVERTICAL);
            
            for (const auto& fix : expert_fixes) {
                add_wrapped_label(m_expert_content, exp_content_sizer, wxString::FromUTF8(fix.fix.c_str()));
                // Add extra details as bullet points
                for (const auto& detail : fix.extraDetails) {
                    add_wrapped_label(m_expert_content, exp_content_sizer, 
                        wxString(L"\u2022 ") + wxString::FromUTF8(detail.c_str()));
                }
            }
            m_expert_content->SetSizer(exp_content_sizer);
            content_sizer->Add(expert_sizer, 0, wxLEFT, 0);
        }
        
        // Learn more expander
        auto learn_more_sizer = create_nested_expander(m_fix_suggestions_content, _L("Learn more"),
            m_learn_more_arrow, m_learn_more_content, [this]() { toggle_learn_more(); });
        wxBoxSizer* learn_content_sizer = new wxBoxSizer(wxVERTICAL);
        
        auto link1 = new Label(m_learn_more_content, Label::Body_13, _L("Helio Flowchart"));
        link1->SetForegroundColour(theme.purple);
        link1->Bind(wxEVT_LEFT_DOWN, [](wxMouseEvent& e) {
            wxLaunchDefaultBrowser("https://wiki.helioadditive.com/en/flowchart");
        });
        link1->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
        link1->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
        learn_content_sizer->Add(link1, 0, wxTOP, FromDIP(4));
        
        // Add context-sensitive link based on temperature direction
        if (direction == "OVERCOOLING") {
            auto link2 = new Label(m_learn_more_content, Label::Body_13, _L("Fixing cold prints"));
            link2->SetForegroundColour(theme.purple);
            link2->Bind(wxEVT_LEFT_DOWN, [](wxMouseEvent& e) {
                wxLaunchDefaultBrowser("https://wiki.helioadditive.com/en/simulation/how-tos/fixing-cold-prints");
            });
            link2->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
            link2->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
            learn_content_sizer->Add(link2, 0, wxTOP, FromDIP(4));
        }
        
        m_learn_more_content->SetSizer(learn_content_sizer);
        content_sizer->Add(learn_more_sizer, 0, wxLEFT, 0);
    }
    
    m_fix_suggestions_content->SetSizer(content_sizer);
}

void HelioSimulationResultsDialog::on_enhance_speed_quality(wxMouseEvent& event)
{
    // Close this dialog
    EndModal(wxID_OK);
    
    // Trigger the Helio input dialog with optimization pre-selected
    // Use CallAfter to ensure this runs on the main thread after dialog closes
    wxGetApp().plater()->CallAfter([]() {
        // Set flag to pre-select optimization mode
        // Use the forward-declared function from file scope
        using Slic3r::GUI::get_helio_pre_select_optimization_flag;
        get_helio_pre_select_optimization_flag() = true;
        
        // Post event to trigger Helio input dialog
        SimpleEvent* evt = new SimpleEvent(EVT_HELIO_INPUT_DLG);
        wxQueueEvent(wxGetApp().plater(), evt);
    });
}

void HelioSimulationResultsDialog::on_view_details(wxMouseEvent& event)
{
    // Close the dialog
    EndModal(wxID_CANCEL);
    
    // Switch to the Preview panel to show details
    wxGetApp().plater()->CallAfter([]() {
        wxGetApp().mainframe->select_tab(size_t(MainFrame::tpPreview));
    });
}

void HelioSimulationResultsDialog::on_dpi_changed(const wxRect &suggested_rect)
{
}

}} // namespace Slic3r::GUI
