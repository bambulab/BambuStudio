#include "HelioReleaseNote.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Thread.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Preview.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "GLToolbar.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/StaticBox.hpp"
#include "Widgets/WebView.hpp"
#include "Widgets/SwitchButton.hpp"
#include "slic3r/GUI/GCodeRenderer/BaseRenderer.hpp"
#include <wx/regex.h>
#include <wx/progdlg.h>
#include <wx/clipbrd.h>
#include <wx/dcgraph.h>
#include <miniz.h>
#include <wx/valnum.h>
#include <algorithm>
#include "Plater.hpp"
#include "BitmapCache.hpp"

#include "DeviceCore/DevManager.h"
#include "DeviceCore/DevStorage.h"

namespace Slic3r { namespace GUI {
 HelioStatementDialog::HelioStatementDialog(wxWindow *parent /*= nullptr*/)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Legal & Activation Terms"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
     shared_ptr = std::make_shared<int>(0);

     std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
     SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

     SetBackgroundColour(wxColour(45, 45, 49)); // Dark background

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
     m_button_cancel->SetTextColor(wxColour(255, 255, 254));
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
         // Force refresh to ensure visual update (especially important on macOS)
         // The internal handler already called update(), but we need to ensure repaint
         m_agree_checkbox->Refresh(false);
         m_agree_checkbox->Update();
         // Also refresh parent panel to ensure checkbox is visible
         page_legal_panel->Refresh(false);
         update_confirm_button_state();
         e.Skip(); // Ensure event propagates
     });

     main_sizer->Add(page_legal_panel, 1, wxEXPAND, 0);
     main_sizer->Add(page_pat_panel, 0, wxEXPAND, 0);
     main_sizer->Add(pat_button_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(30));

     // Page show/hide based on helio_enable state
     if (GUI::wxGetApp().app_config->get("helio_enable") == "true") {
         SetTitle(_L("Third-Party Extension"));
         show_pat_page();
         request_pat();
         m_button_cancel->Show();
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
        helio_pat_refresh->Show();
        helio_pat_eview->Hide();
        helio_pat_dview->Hide();
        helio_pat_copy->Hide();
    }
    else if (opt == "eview") {
        helio_pat_refresh->Hide();
        helio_pat_eview->Show();
        helio_pat_dview->Hide();
        helio_pat_copy->Show();
    }
    else if (opt == "dview") {
        helio_pat_refresh->Hide();
        helio_pat_eview->Hide();
        helio_pat_dview->Show();
        helio_pat_copy->Show();
    }
    Layout();
    Fit();
}

void HelioStatementDialog::create_legal_page()
{
    page_legal_panel = new wxPanel(this);
    page_legal_panel->SetBackgroundColour(wxColour(45, 45, 49));
    
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
    wxScrolledWindow* scroll_panel = new wxScrolledWindow(page_legal_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    scroll_panel->SetScrollRate(0, 10);
    scroll_panel->SetBackgroundColour(wxColour(45, 45, 49));
    wxBoxSizer* scroll_sizer = new wxBoxSizer(wxVERTICAL);
    
    // Accordion Section 1: Software Service Terms & Conditions
    terms_section_panel = new wxPanel(scroll_panel);
    terms_section_panel->SetBackgroundColour(wxColour(55, 55, 59));
    wxBoxSizer* terms_section_sizer = new wxBoxSizer(wxVERTICAL);
    
    wxBoxSizer* terms_header_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto terms_title = new Label(terms_section_panel, Label::Body_14, _L("Software Service Terms & Conditions"));
    terms_title->SetForegroundColour(wxColour("#FFFFFF"));
    wxFont section_font = terms_title->GetFont();
    section_font.SetWeight(wxFONTWEIGHT_BOLD);
    terms_title->SetFont(section_font);
    
    auto terms_arrow = new Label(terms_section_panel, Label::Body_14, L("^"));
    terms_arrow->SetForegroundColour(wxColour("#FFFFFF"));
    
    terms_header_sizer->Add(terms_title, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(15));
    terms_header_sizer->Add(terms_arrow, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(15));
    
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
    
    // Build HTML content with embedded links - properly escape the text
    wxString terms_html = _L("Unless otherwise specified, Bambu Lab only provides support for the software features officially provided. The slicing evaluation and slicing optimization features based on <a href=\"") + 
        helio_home_url + _L("\" style=\"color:#00AE42; text-decoration:underline;\">Helio Additive</a>'s cloud service in this software will be developed, operated, provided, and maintained by <a href=\"") +
        helio_home_url + _L("\" style=\"color:#00AE42; text-decoration:underline;\">Helio Additive</a>. Helio Additive is responsible for the effectiveness and availability of this service. The optimization feature of this service may modify the default print commands, posing a risk of printer damage. These features will collect necessary user information and data to achieve relevant service functions. Subscriptions and payments may be involved. Please visit <a href=\"") +
        helio_home_url + _L("\" style=\"color:#00AE42; text-decoration:underline;\">Helio Additive</a> and refer to the <a href=\"") +
        helio_privacy_url + _L("\" style=\"color:#00AE42; text-decoration:underline;\">Helio Additive Privacy Agreement</a> and <a href=\"") +
        helio_tou_url + _L("\" style=\"color:#00AE42; text-decoration:underline;\">Helio Additive User Agreement</a> for detailed information.<br><br>") +
        _L("Meanwhile, you understand that this product is provided to you \"as is\" based on <a href=\"") +
        helio_home_url + _L("\" style=\"color:#00AE42; text-decoration:underline;\">Helio Additive</a>'s services, and Bambu Lab makes no express or implied warranties of any kind, nor can it control the service effects. To the fullest extent permitted by applicable law, Bambu Lab or its licensors/affiliates do not provide any express or implied representations or warranties, including but not limited to warranties regarding merchantability, satisfactory quality, fitness for a particular purpose, accuracy, confidentiality, and non-infringement of third-party rights. Due to the nature of network services, Bambu Lab cannot guarantee that the service will be available at all times, and Bambu Lab reserves the right to terminate the service based on relevant circumstances. You agree not to use this product and its related updates to engage in the following activities:<br><br>") +
        _L("1. Copy or use any part of this product outside the authorized scope of Helio Additive and Bambu Lab;<br>") +
        _L("2. Attempt to disrupt, bypass, alter, invalidate, or evade any Digital Rights Management system related to and/or an integral part of this product;<br>") +
        _L("3. Using this software and services for any improper or illegal activities.<br><br>") +
        _L("When you confirm to enable this feature, it means that you have confirmed and agreed to the above statements.");
    
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
    
    terms_section_sizer->Add(terms_header_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(12));
    terms_section_sizer->Add(terms_content_panel, 0, wxEXPAND, 0);
    terms_section_panel->SetSizer(terms_section_sizer);
    
    // Make header clickable
    terms_section_panel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { toggle_terms_section(); });
    
    // Accordion Section 2: Special Note & Privacy Policy
    privacy_section_panel = new wxPanel(scroll_panel);
    privacy_section_panel->SetBackgroundColour(wxColour(55, 55, 59));
    wxBoxSizer* privacy_section_sizer = new wxBoxSizer(wxVERTICAL);
    
    wxBoxSizer* privacy_header_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto privacy_title = new Label(privacy_section_panel, Label::Body_14, _L("Special Note & Privacy Policy"));
    privacy_title->SetForegroundColour(wxColour("#FFFFFF"));
    privacy_title->SetFont(section_font);
    
    auto privacy_arrow = new Label(privacy_section_panel, Label::Body_14, L("^"));
    privacy_arrow->SetForegroundColour(wxColour("#FFFFFF"));
    
    privacy_header_sizer->Add(privacy_title, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(15));
    privacy_header_sizer->Add(privacy_arrow, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(15));
    
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
    
    privacy_section_sizer->Add(privacy_header_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(12));
    privacy_section_sizer->Add(privacy_content_panel, 0, wxEXPAND, 0);
    privacy_section_panel->SetSizer(privacy_section_sizer);
    
    // Make header clickable
    privacy_section_panel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { toggle_privacy_section(); });
    
    // Add sections to scroll panel
    scroll_sizer->Add(terms_section_panel, 0, wxEXPAND, 0);
    scroll_sizer->Add(0, 0, 0, wxTOP, FromDIP(10));
    scroll_sizer->Add(privacy_section_panel, 0, wxEXPAND, 0);
    scroll_panel->SetSizer(scroll_sizer);
    
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
        // Force refresh to ensure visual update (especially important on macOS)
        m_agree_checkbox->Refresh(false);
        m_agree_checkbox->Update();
        // Also refresh parent to ensure the checkbox is repainted
        page_legal_panel->Refresh(false);
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
    m_button_confirm->SetTextColor(wxColour(255, 255, 254));
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
    legal_sizer->Add(scroll_panel, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));
    legal_sizer->Add(0, 0, 0, wxTOP, FromDIP(20));
    legal_sizer->Add(bottom_row_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(30));
    
    page_legal_panel->SetSizer(legal_sizer);
    page_legal_panel->SetMinSize(wxSize(FromDIP(680), FromDIP(600)));
    page_legal_panel->Layout();
}

void HelioStatementDialog::create_pat_page()
{
    page_pat_panel = new wxPanel(this);
    page_pat_panel->SetBackgroundColour(*wxWHITE);
    
    wxBoxSizer* pat_sizer = new wxBoxSizer(wxVERTICAL);
    
    auto enable_pat_title = new Label(page_pat_panel, Label::Head_14, _L("Helio Additive third - party software service feature has been successfully enabled!"));
    wxFont bold_font = enable_pat_title->GetFont();
    bold_font.SetWeight(wxFONTWEIGHT_BOLD);
    enable_pat_title->SetFont(bold_font);
    
    auto split_line = new wxPanel(page_pat_panel, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    split_line->SetMaxSize(wxSize(-1, FromDIP(1)));
    split_line->SetBackgroundColour(wxColour(236, 236, 236));
    
    wxBoxSizer* pat_token_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto helio_pat_title = new Label(page_pat_panel, Label::Body_15, L("Helio-PAT"));
    helio_input_pat = new ::TextInput(page_pat_panel, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER | wxTE_RIGHT);
    helio_input_pat->SetFont(Label::Body_15);
    helio_input_pat->SetMinSize(wxSize(FromDIP(400), FromDIP(22)));
    helio_input_pat->SetMaxSize(wxSize(FromDIP(400), FromDIP(22)));
    helio_input_pat->Disable();
    wxString pat = Slic3r::HelioQuery::get_helio_pat();
    pat = wxString(pat.Length(), '*');
    helio_input_pat->SetLabel(pat);
    
    helio_pat_refresh = new wxStaticBitmap(page_pat_panel, wxID_ANY, create_scaled_bitmap("helio_refesh", page_pat_panel, 24), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)), 0);
    helio_pat_refresh->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    helio_pat_refresh->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
    helio_pat_refresh->Bind(wxEVT_LEFT_DOWN, [this](auto& e) { request_pat(); });
    
    helio_pat_eview = new wxStaticBitmap(page_pat_panel, wxID_ANY, create_scaled_bitmap("helio_eview", page_pat_panel, 24), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)), 0);
    helio_pat_eview->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    helio_pat_eview->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
    helio_pat_eview->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        wxString pat = helio_input_pat->GetLabel();
        pat = wxString(pat.Length(), '*');
        helio_input_pat->SetLabel(pat);
        show_pat_option("dview");
    });
    
    helio_pat_dview = new wxStaticBitmap(page_pat_panel, wxID_ANY, create_scaled_bitmap("helio_dview", page_pat_panel, 24), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)), 0);
    helio_pat_dview->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    helio_pat_dview->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
    helio_pat_dview->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        helio_input_pat->SetLabel(Slic3r::HelioQuery::get_helio_pat());
        show_pat_option("eview");
    });
    
    helio_pat_copy = new wxStaticBitmap(page_pat_panel, wxID_ANY, create_scaled_bitmap("helio_copy", page_pat_panel, 24), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)), 0);
    helio_pat_copy->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    helio_pat_copy->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
    helio_pat_copy->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->Clear();
            wxTextDataObject* dataObj = new wxTextDataObject(Slic3r::HelioQuery::get_helio_pat());
            wxTheClipboard->SetData(dataObj);
            wxTheClipboard->Close();
        }
        MessageDialog msg(this, _L("Copy successful!"), _L("Copy"), wxOK | wxYES_DEFAULT);
        msg.ShowModal();
    });
    
    helio_pat_refresh->Hide();
    helio_pat_eview->Hide();
    helio_pat_dview->Hide();
    helio_pat_copy->Hide();
    
    pat_token_sizer->Add(helio_pat_title, 0, wxALIGN_CENTER, 0);
    pat_token_sizer->Add(0, 0, 0, wxLEFT, FromDIP(10));
    pat_token_sizer->Add(helio_input_pat, 0, wxALIGN_CENTER, 0);
    pat_token_sizer->Add(0, 0, 0, wxLEFT, FromDIP(10));
    pat_token_sizer->Add(helio_pat_eview, 0, wxALIGN_CENTER, 0);
    pat_token_sizer->Add(helio_pat_dview, 0, wxALIGN_CENTER, 0);
    pat_token_sizer->Add(helio_pat_refresh, 0, wxALIGN_CENTER, 0);
    pat_token_sizer->Add(0, 0, 0, wxLEFT, FromDIP(10));
    pat_token_sizer->Add(helio_pat_copy, 0, wxALIGN_CENTER, 0);
    
    pat_err_label = new Label(page_pat_panel, Label::Body_14, wxEmptyString);
    pat_err_label->SetMinSize(wxSize(FromDIP(500), -1));
    pat_err_label->SetMaxSize(wxSize(FromDIP(500), -1));
    pat_err_label->Wrap(FromDIP(500));
    pat_err_label->SetForegroundColour(wxColour("#FC8800"));
    
    wxBoxSizer* helio_links_sizer = new wxBoxSizer(wxHORIZONTAL);
    LinkLabel* helio_home_link = new LinkLabel(page_pat_panel, _L("Helio Additive"), "https://www.helioadditive.com/");
    LinkLabel* helio_privacy_link = nullptr;
    LinkLabel* helio_tou_link = nullptr;
    
    if (GUI::wxGetApp().app_config->get("region") == "China") {
        helio_privacy_link = new LinkLabel(page_pat_panel, _L("Privacy Policy of Helio Additive"), "https://www.helioadditive.com/zh-cn/policies/privacy");
        helio_tou_link = new LinkLabel(page_pat_panel, _L("Terms of Use of Helio Additive"), "https://www.helioadditive.com/zh-cn/policies/terms");
    } else {
        helio_privacy_link = new LinkLabel(page_pat_panel, _L("Privacy Policy of Helio Additive"), "https://www.helioadditive.com/en-us/policies/privacy");
        helio_tou_link = new LinkLabel(page_pat_panel, _L("Terms of Use of Helio Additive"), "https://www.helioadditive.com/en-us/policies/terms");
    }
    
    helio_home_link->SetFont(Label::Body_13);
    helio_tou_link->SetFont(Label::Body_13);
    helio_privacy_link->SetFont(Label::Body_13);
    helio_home_link->SeLinkLabelFColour(wxColour(0, 119, 250));
    helio_tou_link->SeLinkLabelFColour(wxColour(0, 119, 250));
    helio_privacy_link->SeLinkLabelFColour(wxColour(0, 119, 250));
    
    helio_links_sizer->Add(helio_home_link, 0, wxLEFT, 0);
    helio_links_sizer->Add(helio_privacy_link, 0, wxLEFT, FromDIP(40));
    helio_links_sizer->Add(helio_tou_link, 0, wxLEFT, FromDIP(40));
    
    pat_sizer->Add(0, 0, 0, wxTOP, FromDIP(20));
    pat_sizer->Add(enable_pat_title, 0, wxLEFT, FromDIP(30));
    pat_sizer->Add(0, 0, 0, wxTOP, FromDIP(14));
    pat_sizer->Add(split_line, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));
    pat_sizer->Add(0, 0, 0, wxTOP, FromDIP(20));
    pat_sizer->Add(pat_token_sizer, 0, wxLEFT, FromDIP(30));
    pat_sizer->Add(pat_err_label, 0, wxLEFT | wxTOP, FromDIP(10));
    pat_sizer->Add(0, 0, 0, wxTOP, FromDIP(28));
    pat_sizer->Add(helio_links_sizer, 0, wxLEFT, FromDIP(30));
    
    page_pat_panel->SetSizer(pat_sizer);
    page_pat_panel->SetMinSize(wxSize(FromDIP(550), FromDIP(200)));
    page_pat_panel->Layout();
}

void HelioStatementDialog::toggle_terms_section()
{
    terms_expanded = !terms_expanded;
    if (terms_expanded) {
        terms_content_panel->Show();
    } else {
        terms_content_panel->Hide();
    }
    page_legal_panel->Layout();
    Layout();
    Fit();
}

void HelioStatementDialog::toggle_privacy_section()
{
    privacy_expanded = !privacy_expanded;
    if (privacy_expanded) {
        privacy_content_panel->Show();
    } else {
        privacy_content_panel->Hide();
    }
    page_legal_panel->Layout();
    Layout();
    Fit();
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
    SetBackgroundColour(wxColour(45, 45, 49));
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
    SetBackgroundColour(*wxWHITE);
    SetTitle(_L("Third-Party Extension"));
    page_legal_panel->Hide();
    page_pat_panel->Show();
    // m_button_confirm is part of page_legal_panel, hidden with the panel
    m_button_cancel->Show();
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

                    /*request helio data*/
                    wxGetApp().request_helio_supported_data();
                }
            }
        });
    }
    else {
        show_err_info("");
        show_pat_option("dview");
    }
}

HelioRemainUsageTime::HelioRemainUsageTime(wxWindow* parent,  wxString label) : wxPanel(parent)
{
    Create(label);
}

void HelioRemainUsageTime::Create(wxString label)
{
    SetBackgroundColour(*wxWHITE);

    Label* label_prefix = new Label(this, label);
    label_prefix->SetMaxSize(wxSize(FromDIP(400), -1));
    label_prefix->SetToolTip(label);

    m_label_remain_usage_time = new Label(this, "0");
    wxFont bold_font = m_label_remain_usage_time->GetFont();
    bold_font.SetWeight(wxFONTWEIGHT_BOLD);
    m_label_remain_usage_time->SetMinSize(wxSize(FromDIP(40), -1));
    m_label_remain_usage_time->SetMaxSize(wxSize(FromDIP(40), -1));
    m_label_remain_usage_time->SetFont(bold_font);

    wxBoxSizer* remain_sizer = new wxBoxSizer(wxHORIZONTAL);
    remain_sizer->Add(label_prefix, 0, wxALIGN_CENTER_VERTICAL);
    remain_sizer->Add(m_label_remain_usage_time, 0, wxALIGN_CENTER_VERTICAL);

    /*label_click_to_use = new Label(this, _L("Click Confirm to start this optimization immediately."));
    label_click_to_use->SetMaxSize(wxSize(FromDIP(400), -1));
    label_click_to_use->Wrap(FromDIP(400));

    label_click_to_buy = new Label(this, _L("Please click the link below to make a purchase or log in."));
    label_click_to_buy->SetMaxSize(wxSize(FromDIP(400), -1));
    label_click_to_buy->Wrap(FromDIP(400));

    label_click_to_use->Hide();
    label_click_to_buy->Hide();*/

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(remain_sizer);
    //sizer->Add(label_click_to_use, 0, wxALIGN_CENTER_VERTICAL);
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

 HelioInputDialog::HelioInputDialog(wxWindow *parent /*= nullptr*/)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, wxString("Helio Additive"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    shared_ptr = std::make_shared<int>(0);

    if (wxGetApp().dark_mode()) {
        SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    } else {
        SetBackgroundColour(*wxWHITE);
    }

    SetMinSize(wxSize(FromDIP(500), -1));
    SetMaxSize(wxSize(FromDIP(500), -1));

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
    wxPanel *line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    line->SetBackgroundColour(wxColour(166, 169, 170));

    wxBoxSizer* control_sizer = new wxBoxSizer(wxHORIZONTAL);
    togglebutton_simulate = new CustomToggleButton(this, _L("Simulation"));
    togglebutton_optimize = new CustomToggleButton(this, _L("Optimization"));

    togglebutton_simulate->set_primary_colour(wxColour("#AF7CFF"));
    togglebutton_simulate->set_secondary_colour(wxColour("#F5EEFF"));
    togglebutton_simulate->SetSelectedIcon("helio_switch_send_mode_tag_on");

    togglebutton_optimize->set_primary_colour(wxColour("#AF7CFF"));
    togglebutton_optimize->set_secondary_colour(wxColour("#F5EEFF"));
    togglebutton_optimize->SetSelectedIcon("helio_switch_send_mode_tag_on");

    togglebutton_simulate->SetMinSize(wxSize(FromDIP(124), FromDIP(30)));
    togglebutton_optimize->SetMinSize(wxSize(FromDIP(124), FromDIP(30)));
    control_sizer->Add(togglebutton_simulate, 0, wxCENTER, 0);
    control_sizer->Add(0, 0, 0, wxLEFT, FromDIP(10));
    control_sizer->Add(togglebutton_optimize, 0, wxCENTER, 0);

    togglebutton_simulate->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_selected_simulation, this);
    togglebutton_optimize->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_selected_optimaztion, this);

    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    auto source_model = preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle);
    is_no_chamber = !DevPrinterConfigUtil::get_value_from_config<bool>(source_model, "print", "support_chamber");

    /*Simulation panel*/
    panel_simulation = new wxPanel(this);
    if (wxGetApp().dark_mode()) {
        panel_simulation->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    } else {
        panel_simulation->SetBackgroundColour(*wxWHITE);
    }
    wxBoxSizer *sizer_simulation = new wxBoxSizer(wxVERTICAL);

    /*chamber temp*/
    //int min_chamber_temp = s_get_min_chamber_temp();
    auto chamber_temp_checker = TextInputValChecker::CreateDoubleRangeChecker(5.0, 70.0, true);
    wxBoxSizer* chamber_temp_item_for_simulation = create_input_item(panel_simulation, "chamber_temp_for_simulation", is_no_chamber?_L("Environment Temperature"):_L("Chamber Temperature"), wxT("\u00B0C"), {chamber_temp_checker});

    //m_input_items["chamber_temp_for_simulation"]->GetTextCtrl()->SetLabel(wxString::Format("%d", min_chamber_temp));
    m_input_items["chamber_temp_for_simulation"]->GetTextCtrl()->SetHint(wxT("5-70"));

    Label* sub_simulation = new Label(panel_simulation, _L("Note: Adjust the above temperature based on actual conditions. More accurate data ensures more precise analysis results."));
    sub_simulation->SetForegroundColour(wxColour(144, 144, 144));
    sub_simulation->SetSize(wxSize(FromDIP(440), -1));
    sub_simulation->Wrap(FromDIP(440));

    sizer_simulation->Add(0, 0, 0, wxTOP, FromDIP(24));
    sizer_simulation->Add(chamber_temp_item_for_simulation, 0, wxEXPAND, 0);
    sizer_simulation->Add(0, 0, 0, wxTOP, FromDIP(10));
    sizer_simulation->Add(sub_simulation, 0, wxEXPAND, 0);

    panel_simulation->SetSizer(sizer_simulation);
    panel_simulation->Layout();
    panel_simulation->Fit();

    /*Optimization Pay panel*/
    panel_pay_optimization = new wxPanel(this);
    if (wxGetApp().dark_mode()) {
        panel_pay_optimization->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    } else {
        panel_pay_optimization->SetBackgroundColour(*wxWHITE);
    }
    wxBoxSizer* sizer_pay_optimization = new wxBoxSizer(wxVERTICAL);
    wxStaticBitmap* panel_pay_loading_icon = new wxStaticBitmap(panel_pay_optimization, wxID_ANY, create_scaled_bitmap("helio_loading", panel_optimization, 109), wxDefaultPosition,
        wxSize(FromDIP(136), FromDIP(109)));

    Label* pay_sub = new Label(panel_pay_optimization, _L("Helio's advanced feature for power users: available post purchase on its official website."));
    pay_sub->SetForegroundColour(wxColour(144, 144, 144));
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
    if (wxGetApp().dark_mode()) {
        panel_optimization->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    } else {
        panel_optimization->SetBackgroundColour(*wxWHITE);
    }
    wxBoxSizer *sizer_optimization = new wxBoxSizer(wxVERTICAL);

    m_remain_usage_time = new HelioRemainUsageTime(panel_optimization, _L("Monthly quota remaining: "));
    m_remain_usage_time->UpdateRemainTime(0);

    m_remain_purchased_time = new HelioRemainUsageTime(panel_optimization, _L("Add-ons available: "));
    m_remain_purchased_time->UpdateRemainTime(0);

    /*split line*/
    auto split_line = new wxPanel(panel_optimization, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    split_line->SetMaxSize(wxSize(-1, FromDIP(1)));
    split_line->SetBackgroundColour(wxColour(236, 236, 236));

    wxBoxSizer* chamber_temp_item_for_optimization = nullptr;
    Label* sub_optimization = nullptr;
    if (is_no_chamber) {
        chamber_temp_item_for_optimization = create_input_item(panel_optimization, "chamber_temp_for_optimization", is_no_chamber?_L("Environment Temperature"):_L("Chamber Temperature"), wxT("\u00B0C"), {chamber_temp_checker});
        m_input_items["chamber_temp_for_optimization"]->GetTextCtrl()->SetHint(wxT("5-70"));

        sub_optimization = new Label(panel_optimization, _L("Note: Adjust the above temperature based on actual conditions. More accurate data ensures more precise analysis results."));
        sub_optimization->SetForegroundColour(wxColour(144, 144, 144));
        sub_optimization->SetSize(wxSize(FromDIP(440), -1));
        sub_optimization->Wrap(FromDIP(440));
    }

    std::map<int, wxString> config_outerwall;
    config_outerwall[0] = _L("No");
    config_outerwall[1] = _L("Yes");
    auto outerwall =  create_combo_item(panel_optimization, "optimiza_outerwall", _L("Optimise Outer Walls"), config_outerwall, 0);

    // layers to optimize - now top-level
    auto plater = Slic3r::GUI::wxGetApp().plater();
    int layer_count = plater ? plater->get_gcode_layers_count() : 0;
    wxBoxSizer* layers_to_optimize_item = create_input_optimize_layers(panel_optimization, layer_count);

    // Limits dropdown
    std::map<int, wxString> config_limits;
    config_limits[LIMITS_HELIO_DEFAULT] = _L("Helio default (recommended)");
    config_limits[LIMITS_SLICER_DEFAULT] = _L("Slicer default");
    // Create Limits dropdown with wider width to fit "Helio default (recommended)" text
    auto limits = create_combo_item(panel_optimization, "limits", _L("Limits"), config_limits, LIMITS_HELIO_DEFAULT, LIMITS_DROPDOWN_WIDTH);

    // Create panel for velocity and volumetric speed fields - visibility controlled by Limits selection
    panel_velocity_volumetric = new wxPanel(panel_optimization);
    if (wxGetApp().dark_mode()) {
        panel_velocity_volumetric->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    } else {
        panel_velocity_volumetric->SetBackgroundColour(*wxWHITE);
    }
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
            // Force it back to "Slicer default"
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
        panel_optimization->Layout();
        panel_optimization->Fit();
        Layout();
        Fit();
    });

    // Create empty panel_advanced_option for backward compatibility (kept hidden)
    panel_advanced_option = new wxPanel(panel_optimization);
    if (wxGetApp().dark_mode()) {
        panel_advanced_option->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    } else {
        panel_advanced_option->SetBackgroundColour(*wxWHITE);
    }
    panel_advanced_option->Hide();

    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(24));
    sizer_optimization->Add(m_remain_usage_time, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(3));
    sizer_optimization->Add(m_remain_purchased_time, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(10));
    sizer_optimization->Add(split_line, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(12));
    if (is_no_chamber) {
        sizer_optimization->Add(chamber_temp_item_for_optimization, 0, wxEXPAND, 0);
        sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(10));
        sizer_optimization->Add(sub_optimization, 0, wxEXPAND, 0);
    }
    sizer_optimization->Add(outerwall, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(12));
    sizer_optimization->Add(layers_to_optimize_item, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(12));
    sizer_optimization->Add(limits, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(12));
    sizer_optimization->Add(panel_velocity_volumetric, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(8));

    panel_optimization->SetSizer(sizer_optimization);
    panel_optimization->Layout();
    panel_optimization->Fit();

    panel_optimization->Hide();

    /*last trace id*/
    last_tid_panel = new wxPanel(this);
    wxBoxSizer* last_tid_sizer = new wxBoxSizer(wxHORIZONTAL);

    Label* last_tid_title = new Label(last_tid_panel, _L("Last Trace ID: "));
    last_tid_title->SetBackgroundColour(*wxWHITE);
    last_tid_title->SetForegroundColour(wxColour(144, 144, 144));
    last_tid_label = new Label(last_tid_panel, wxEmptyString);
    last_tid_label->SetBackgroundColour(*wxWHITE);
    last_tid_label->SetForegroundColour(wxColour(144, 144, 144));
    wxStaticBitmap* helio_tid_copy = new wxStaticBitmap(last_tid_panel, wxID_ANY, create_scaled_bitmap("helio_copy", last_tid_panel, 24), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)), 0);
    helio_tid_copy->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    helio_tid_copy->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });


    helio_tid_copy->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        bool copySuccess = false;
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

    buy_now_link = new LinkLabel(this, _L("Buy Now / Manage Account"), "https://wiki.helioadditive.com/");
    buy_now_link->SeLinkLabelFColour(wxColour(175, 124, 255));
    buy_now_link->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    buy_now_link->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
    buy_now_link->Hide();

    /*set buy url*/
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

    /*helio wiki*/
    helio_wiki_link = new LinkLabel(this, _L("Click for more details"), wxGetApp().app_config->get("language") =="zh_CN"? "https://wiki.helioadditive.com/zh/home" : "https://wiki.helioadditive.com/en/home");
    helio_wiki_link->SeLinkLabelFColour(wxColour(175, 124, 255));
    helio_wiki_link->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    helio_wiki_link->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });

    /*confirm*/
    wxBoxSizer* button_sizer = new wxBoxSizer(wxHORIZONTAL);
    StateColor btn_bg_green( std::pair<wxColour, int>(wxColour(175, 124, 255), StateColor::Enabled), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Disabled), std::pair<wxColour, int>(wxColour(175, 124, 255), StateColor::Pressed), std::pair<wxColour, int>(wxColour(175, 124, 255), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    m_button_confirm = new Button(this, _L("Confirm"));
    m_button_confirm->SetBackgroundColor(btn_bg_green);
    m_button_confirm->SetBorderColor(*wxWHITE);
    m_button_confirm->SetTextColor(wxColour(255, 255, 254));
    m_button_confirm->SetFont(Label::Body_12);
    m_button_confirm->SetSize(wxSize(-1, FromDIP(24)));
    m_button_confirm->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_confirm->SetCornerRadius(FromDIP(12));
    m_button_confirm->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_confirm, this);
    m_button_confirm->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    m_button_confirm->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });

    button_sizer->Add(0, 0, 1, wxEXPAND, 0);
    button_sizer->Add(m_button_confirm, 0, 0, 0);
    button_sizer->Add(0, 0, 0, wxLEFT, FromDIP(25));

    main_sizer->Add(line, 0, wxEXPAND, 0);
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(20));
    main_sizer->Add(control_sizer, 0, wxCENTER, FromDIP(30));
    main_sizer->Add(panel_simulation, 0,wxEXPAND | wxLEFT | wxRIGHT, FromDIP(25));
    main_sizer->Add(panel_pay_optimization, 0,wxEXPAND | wxLEFT | wxRIGHT, FromDIP(25));
    main_sizer->Add(panel_optimization, 0,wxEXPAND | wxLEFT | wxRIGHT, FromDIP(25));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(4));
    main_sizer->Add(last_tid_panel, 0, wxLEFT | wxRIGHT, FromDIP(25));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(4));
    main_sizer->Add(buy_now_link, 0, wxLEFT | wxRIGHT, FromDIP(25));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(8));
    main_sizer->Add(helio_wiki_link, 0, wxLEFT | wxRIGHT, FromDIP(25));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(11));
    main_sizer->Add(button_sizer, 0, wxEXPAND, 0);
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(16));

    update_action(0);

    SetSizer(main_sizer);
    Layout();
    Fit();

    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void HelioInputDialog::update_action(int action)
{
    if (current_action == action) {
        return;
    }

    if (action == 0) {
        m_button_confirm->SetLabel(_L("Start Simulation"));
        m_button_confirm->Layout();
        m_button_confirm->Fit();
        togglebutton_simulate->SetIsSelected(true);
        togglebutton_optimize->SetIsSelected(false);
        panel_simulation->Show();
        buy_now_link->Hide();
        panel_pay_optimization->Hide();
        panel_optimization->Hide();
        m_button_confirm->Enable();
        helio_wiki_link->setLinkUrl( wxGetApp().app_config->get("language") =="zh_CN"? "https://wiki.helioadditive.com/zh/home" : "https://wiki.helioadditive.com/en/home");


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
        m_button_confirm->SetLabel(_L("Start Optimization"));
        m_button_confirm->Layout();
        m_button_confirm->Fit();
        m_button_confirm->Disable();
        std::string helio_api_url = Slic3r::HelioQuery::get_helio_api_url();
        std::string helio_api_key = Slic3r::HelioQuery::get_helio_pat();

        std::weak_ptr<int> weak_ptr = shared_ptr;
        HelioQuery::request_remaining_optimizations(helio_api_url, helio_api_key, [this, weak_ptr](int times, int addons) {
            if (auto temp_ptr = weak_ptr.lock()) {
                CallAfter([=]() {
                    if (times > 0 || addons > 0) {
                        m_button_confirm->Enable();
                    }
                    else {
                        if (times <= 0) {
                            buy_now_link->setLabel(_L("Buy Now / Manage Account"));
                            if (m_remain_usage_time) { m_remain_usage_time->UpdateHelpTips(0); }
                        }
                        else {
                            buy_now_link->setLabel(_L("Buy add-ons"));
                            if (m_remain_usage_time) { m_remain_usage_time->UpdateHelpTips(1); }
                        }
                    }
                    Layout();
                    Fit();

                    if (m_remain_usage_time) { m_remain_usage_time->UpdateRemainTime(times); }
                    if (m_remain_purchased_time) { m_remain_purchased_time->UpdateRemainTime(addons); }
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
        helio_wiki_link->setLinkUrl( wxGetApp().app_config->get("language") =="zh_CN"? "https://wiki.helioadditive.com/zh/optimization/quick-start" : "https://wiki.helioadditive.com/en/optimization/quick-start");

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
                // Force select "Slicer default" to show velocity/volumetric speed fields
                only_advanced_settings = true;
                use_advanced_settings = true;
                m_combo_items["limits"]->SetSelection(LIMITS_SLICER_DEFAULT);
                m_combo_items["limits"]->Disable(); // Disable dropdown to prevent changing
                panel_velocity_volumetric->Show();
                panel_optimization->Layout();
                panel_optimization->Fit();
                Layout();
                Fit();
            }

            /*force use Slicer default when preset has layer height < 0.2*/
            auto edited_preset = wxGetApp().preset_bundle->prints.get_edited_preset().config;
            auto layer_height = edited_preset.option<ConfigOptionFloat>("layer_height")->value;
            if (layer_height < 0.2) {
                // Force select "Slicer default" to show velocity/volumetric speed fields
                only_advanced_settings = true;
                use_advanced_settings = true;
                m_combo_items["limits"]->SetSelection(LIMITS_SLICER_DEFAULT);
                m_combo_items["limits"]->Disable(); // Disable dropdown to prevent changing
                panel_velocity_volumetric->Show();
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

wxBoxSizer* HelioInputDialog::create_input_item(wxWindow* parent, std::string key, wxString name, wxString unit,
                                                const std::vector<std::shared_ptr<TextInputValChecker>>& checkers)
{
    wxBoxSizer* item_sizer = new wxBoxSizer(wxHORIZONTAL);
    Label* inout_title = new Label(parent, Label::Body_14, name);
    inout_title->SetFont(::Label::Body_14);
    inout_title->SetForegroundColour(wxColour("#262E30"));

    TextInput *m_input_item = new TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(120), -1), wxTE_PROCESS_ENTER, unit);
    wxColour   parent_bg    = parent->GetBackgroundColour();
    //TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, DESIGN_INPUT_SIZE, wxTE_PROCESS_ENTER);
    StateColor input_bg(std::pair<wxColour, int>(wxColour("#F0F0F1"), StateColor::Disabled), std::pair<wxColour, int>(parent_bg, StateColor::Enabled));
    m_input_item->SetBackgroundColor(input_bg);
    wxTextValidator validator(wxFILTER_NUMERIC);

    m_input_item->GetTextCtrl()->SetBackgroundColour(parent_bg);
    m_input_item->GetTextCtrl()->SetWindowStyleFlag(wxBORDER_NONE | wxTE_PROCESS_ENTER);
    m_input_item->GetTextCtrl()->Bind(wxEVT_TEXT, [=](wxCommandEvent &) {
        wxTextCtrl *ctrl = m_input_item->GetTextCtrl();
        wxColour new_bg = ctrl->GetValue().IsEmpty() && (key != "chamber_temp_for_simulation") ? wxColour(255, 182, 193) : parent_bg;
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
    wxBoxSizer* item_sizer = new wxBoxSizer(wxHORIZONTAL);
    Label* inout_title = new Label(parent, Label::Body_14, name);
    inout_title->SetFont(::Label::Body_14);
    inout_title->SetForegroundColour(wxColour("#262E30"));

    auto combobox = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(width), -1), 0, nullptr, wxCB_READONLY);
    combobox->SetFont(::Label::Body_13);
    combobox->GetDropDown().SetFont(::Label::Body_13);

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
    wxBoxSizer* item_sizer = new wxBoxSizer(wxHORIZONTAL);
    Label* inout_title = new Label(parent, Label::Body_14, _L("Layers to Optimize "));
    inout_title->SetFont(::Label::Body_14);
    inout_title->SetForegroundColour(wxColour("#262E30"));

    StateColor input_bg(std::pair<wxColour, int>(wxColour("#F0F0F1"), StateColor::Disabled), std::pair<wxColour, int>(*wxWHITE, StateColor::Enabled));
    auto layer_range_checker = TextInputValChecker::CreateIntRangeChecker(0, layer_count);
    wxColour parent_bg = parent->GetBackgroundColour();

    TextInput* m_layer_min_item = new TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(76), -1), wxTE_PROCESS_ENTER);
    m_layer_min_item->SetBackgroundColor(input_bg);
    m_layer_min_item->GetTextCtrl()->SetWindowStyleFlag(wxBORDER_NONE | wxTE_PROCESS_ENTER);
    m_layer_min_item->GetTextCtrl()->SetBackgroundColour(parent_bg);
    m_layer_min_item->GetTextCtrl()->Bind(wxEVT_TEXT, [=](wxCommandEvent &) {
        wxTextCtrl *ctrl = m_layer_min_item->GetTextCtrl();
        wxColour new_bg = ctrl->GetValue().IsEmpty() ? wxColour(255, 182, 193) : parent_bg;
        if (ctrl->GetBackgroundColour() != new_bg) {
            ctrl->SetBackgroundColour(new_bg);
            ctrl->Refresh();
        }
    });
    m_layer_min_item->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_layer_min_item->GetTextCtrl()->SetMaxLength(10);
    m_layer_min_item->GetTextCtrl()->SetLabel("2");
    m_layer_min_item->SetValCheckers({layer_range_checker});

    TextInput* m_layer_max_item = new TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(76), -1), wxTE_PROCESS_ENTER);
    m_layer_max_item->SetBackgroundColor(input_bg);
    m_layer_min_item->GetTextCtrl()->SetWindowStyleFlag(wxBORDER_NONE | wxTE_PROCESS_ENTER);
    m_layer_max_item->GetTextCtrl()->SetBackgroundColour(parent_bg);
    m_layer_max_item->GetTextCtrl()->Bind(wxEVT_TEXT, [=](wxCommandEvent &) {
        wxTextCtrl *ctrl = m_layer_max_item->GetTextCtrl();
        wxColour new_bg = ctrl->GetValue().IsEmpty() ? wxColour(255, 182, 193) : parent_bg;
        if (ctrl->GetBackgroundColour() != new_bg) {
            ctrl->SetBackgroundColour(new_bg);
            ctrl->Refresh();
        }
    });
    m_layer_max_item->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_layer_max_item->GetTextCtrl()->SetMaxLength(10);
    m_layer_max_item->GetTextCtrl()->SetLabel(wxString::Format("%d", layer_count));
    m_layer_max_item->SetValCheckers({ layer_range_checker });

    item_sizer->Add(inout_title, 0, wxALIGN_CENTER, 0);
    item_sizer->Add(0, 0, 1, wxEXPAND, 0);
    item_sizer->Add(m_layer_min_item);
    item_sizer->AddSpacer(FromDIP(1));
    item_sizer->Add(new Label(parent, Label::Body_13, "-"), 0, wxALIGN_CENTER_VERTICAL, 0);
    item_sizer->AddSpacer(FromDIP(1));
    item_sizer->Add(m_layer_max_item);

    m_input_items.insert(std::pair<std::string, TextInput*>("layers_to_optimize_min", m_layer_min_item));
    m_input_items.insert(std::pair<std::string, TextInput*>("layers_to_optimize_max", m_layer_max_item));

    return item_sizer;
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
    
    data.outer_wall = m_combo_items["optimiza_outerwall"]->GetSelection();

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
        EndModal(wxID_OK);
    }
}

void HelioInputDialog::on_dpi_changed(const wxRect &suggested_rect)
{
}

HelioPatNotEnoughDialog::HelioPatNotEnoughDialog(wxWindow* parent /*= nullptr*/)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, wxString("Helio Additive"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    SetBackgroundColour(*wxWHITE);

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
    helio_wiki_link->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    helio_wiki_link->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));


    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    auto m_button_ok = new Button(this, _L("Confirm"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour(255, 255, 254));
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

    SetBackgroundColour(*wxWHITE);
    shared_ptr = std::make_shared<int>(0);

    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);


    wxBoxSizer *helio_top_hsizer        = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *helio_top_vsizer        = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *helio_top_content_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto helio_top_background = new wxPanel(this);
    helio_top_background->SetBackgroundColour(wxColour(16, 16, 16));
    helio_top_background->SetMinSize(wxSize(-1, FromDIP(70)));
    helio_top_background->SetMaxSize(wxSize(-1, FromDIP(70)));
    auto   helio_top_icon  = new wxStaticBitmap(helio_top_background, wxID_ANY, create_scaled_bitmap("helio_icon", helio_top_background, 32), wxDefaultPosition,
                                             wxSize(FromDIP(32), FromDIP(32)), 0);
    auto   helio_top_label = new Label(helio_top_background, Label::Body_16, L("HELIO ADDITIVE"));
    wxFont bold_font       = helio_top_label->GetFont();
    bold_font.SetWeight(wxFONTWEIGHT_BOLD);
    helio_top_label->SetFont(bold_font);
    helio_top_label->SetForegroundColour(wxColour("#FEFEFF"));
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


    title_time_impro->SetForegroundColour(wxColour("#262E30"));
    title_average_impro->SetForegroundColour(wxColour("#262E30"));
    title_consistency_impro->SetForegroundColour(wxColour("#262E30"));

    title_time_impro->SetMinSize(wxSize(FromDIP(225), -1));
    title_average_impro->SetMinSize(wxSize(FromDIP(225), -1));
    title_consistency_impro->SetMinSize(wxSize(FromDIP(225), -1));

    wxString txt_average_impro;
    wxString txt_consistency_impro;

    quality_mean_improvement.MakeLower();
    quality_std_improvement.MakeLower();   

    auto label_original_time = wxString::Format("%s", short_time(get_time_dhms(original_time)));
    auto label_optimized_time = wxString::Format("%s", short_time(get_time_dhms(optimized_time)));
    auto label_thrifty_time = wxString::Format("%s", short_time(get_time_dhms(original_time - optimized_time)));

    auto label_time_impro        = new Label(this, Label::Body_14,label_thrifty_time + " (" + label_original_time + " -> " + label_optimized_time + ")");
    auto label_average_impro     = new Label(this, Label::Body_14, format_improvement(quality_mean_improvement));
    auto label_consistency_impro = new Label(this, Label::Body_14, format_improvement(quality_std_improvement));

    label_time_impro->SetForegroundColour(wxColour("#262E30"));
    label_average_impro->SetForegroundColour(wxColour("#262E30"));
    label_consistency_impro->SetForegroundColour(wxColour("#262E30"));

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
    line->SetBackgroundColour(wxColour(166, 169, 170));

    auto tips = new Label(this, Label::Body_14, L("Your gcode has been improved for the best possible print. To further improve your print please check out our wiki for tips & tricks on what to do next."));
    tips->SetForegroundColour(wxColour(144, 144, 144));
    tips->SetSize(wxSize(FromDIP(410), -1));
    tips->SetMinSize(wxSize(FromDIP(410), -1));
    tips->SetMaxSize(wxSize(FromDIP(410), -1));
    tips->Wrap(FromDIP(410));

    /*helio wiki*/
    auto helio_wiki_link = new LinkLabel(this, _L("Click for more details"),
                                    wxGetApp().app_config->get("language") == "zh_CN" ? "https://wiki.helioadditive.com/zh/home" : "https://wiki.helioadditive.com/en/home");
    helio_wiki_link->SeLinkLabelFColour(wxColour(175, 124, 255));
    helio_wiki_link->Bind(wxEVT_ENTER_WINDOW, [this](auto &e) { SetCursor(wxCURSOR_HAND); });
    helio_wiki_link->Bind(wxEVT_LEAVE_WINDOW, [this](auto &e) { SetCursor(wxCURSOR_ARROW); });

    wxBoxSizer *sizer_bottom = new wxBoxSizer(wxHORIZONTAL);

    /*rating*/
    wxBoxSizer *sizer_rating = new wxBoxSizer(wxHORIZONTAL);
    std::vector<wxStaticBitmap *> stars;
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

    save_icon->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    save_icon->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
    save_icon->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxPostEvent(wxGetApp().plater(), SimpleEvent(EVT_GLTOOLBAR_EXPORT_SLICED_FILE));
    });

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(175, 124, 255), StateColor::Enabled), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Disabled), std::pair<wxColour, int>(wxColour(175, 124, 255), StateColor::Pressed), std::pair<wxColour, int>(wxColour(175, 124, 255), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    auto m_button_confirm = new Button(this, _L("OK"));
    m_button_confirm->SetBackgroundColor(btn_bg_green);
    m_button_confirm->SetBorderColor(*wxWHITE);
    m_button_confirm->SetTextColor(wxColour(255, 255, 254));
    m_button_confirm->SetFont(Label::Body_12);
    m_button_confirm->SetSize(wxSize(FromDIP(48), FromDIP(24)));
    m_button_confirm->SetMinSize(wxSize(FromDIP(48), FromDIP(24)));
    m_button_confirm->SetCornerRadius(FromDIP(12));
    m_button_confirm->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        EndModal(wxID_CLOSE);
    });
    m_button_confirm->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    m_button_confirm->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });

    sizer_bottom->Add(sizer_rating, 0, wxLEFT|wxALIGN_CENTER, 0);
    sizer_bottom->Add( 0, 0, 1, wxEXPAND, 0 );
    sizer_bottom->Add(save_icon, 0, wxLEFT|wxALIGN_CENTER, 0);
    sizer_bottom->Add(0, 0, 0, wxLEFT, FromDIP(14));
    sizer_bottom->Add(m_button_confirm, 0, wxLEFT|wxALIGN_CENTER, 0);
    
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

 }} // namespace Slic3r::GUI
