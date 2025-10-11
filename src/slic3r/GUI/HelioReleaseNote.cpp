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
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Third-Party Extension"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
     shared_ptr = std::make_shared<int>(0);

     std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
     SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

     SetBackgroundColour(*wxWHITE);

     wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);

     wxPanel* line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
     line->SetBackgroundColour(wxColour(166, 169, 170));

     wxBoxSizer* helio_top_hsizer = new wxBoxSizer(wxHORIZONTAL);
     wxBoxSizer* helio_top_vsizer = new wxBoxSizer(wxVERTICAL);
     wxBoxSizer* helio_top_content_sizer = new wxBoxSizer(wxHORIZONTAL);

     auto helio_top_background = new wxPanel(this);
     helio_top_background->SetBackgroundColour(wxColour(16, 16, 16));
     helio_top_background->SetMinSize(wxSize(-1, FromDIP(70)));
     helio_top_background->SetMaxSize(wxSize(-1, FromDIP(70)));
     auto helio_top_icon = new wxStaticBitmap(helio_top_background, wxID_ANY, create_scaled_bitmap("helio_icon", helio_top_background, 32), wxDefaultPosition, wxSize(FromDIP(32), FromDIP(32)), 0);
     auto helio_top_label = new Label(helio_top_background, Label::Body_16 , L("HELIO ADDITIVE"));
     wxFont bold_font = helio_top_label->GetFont();
     bold_font.SetWeight(wxFONTWEIGHT_BOLD);
     helio_top_label->SetFont(bold_font);
     helio_top_label->SetForegroundColour(wxColour("#FEFEFF"));
     //helio_top_hsizer->Add(0, 0, wxLEFT, FromDIP(40));
     helio_top_content_sizer->Add(helio_top_icon, 0, wxLEFT|wxALIGN_CENTER, FromDIP(45));
     helio_top_content_sizer->Add(helio_top_label, 0, wxLEFT|wxALIGN_CENTER,FromDIP(8));
     helio_top_vsizer->Add(helio_top_content_sizer, 0, wxALIGN_CENTER, 0);
     helio_top_hsizer->Add( helio_top_vsizer, 0, wxALIGN_CENTER, 0 );
     helio_top_background->SetSizer(helio_top_hsizer);
     helio_top_background->Layout();

     //page 1
     wxBoxSizer* page1_sizer = new wxBoxSizer(wxVERTICAL);
     page1_panel = new wxPanel(this);
     page1_panel->SetBackgroundColour(*wxWHITE);

     wxWebView* m_vebview = WebView::CreateWebView(page1_panel, "");
     m_vebview->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, [this](const wxWebViewEvent& evt) {
         open_url(evt.GetString().ToStdString());
     });

     std::string phurl1;
     std::string phurl2;
     if (GUI::wxGetApp().app_config->get("region") == "China") {
         m_vebview->SetMinSize(wxSize(FromDIP(720), FromDIP(460)));
         m_vebview->SetMaxSize(wxSize(FromDIP(720), FromDIP(460)));
         phurl1 = "web/helio/helio_service_cn.html";
         phurl2 = "web/helio/helio_service_snote_cn.html";
     }
     else {
         m_vebview->SetMinSize(wxSize(FromDIP(720), FromDIP(650)));
         m_vebview->SetMaxSize(wxSize(FromDIP(720), FromDIP(650)));
         phurl1 = "web/helio/helio_service_en.html";
         phurl2 = "web/helio/helio_service_snote_en.html";
     }

     phurl1 += GUI::wxGetApp().dark_mode() ? "?darkmode=1" : "?darkmode=0";
     phurl2 += GUI::wxGetApp().dark_mode() ? "?darkmode=1" : "?darkmode=0";

     auto _language = GUI::into_u8(GUI::wxGetApp().current_language_code());
     fs::path ph(resources_dir());
     ph /= phurl1;
     if (!fs::exists(ph)) {
         ph =  fs::path(resources_dir());
         ph /= phurl1;
     }
     auto url = ph.string();
     std::replace(url.begin(), url.end(), '\\', '/');
     url = "file:///" + url;
     m_vebview->LoadURL(from_u8(url));


     page1_sizer->Add(m_vebview, 0, wxLEFT|wxRIGHT, FromDIP(33));
     page1_panel->SetSizer(page1_sizer);
     page1_panel->Layout();
     page1_panel->Fit();

     //page 2
     wxBoxSizer* page2_sizer = new wxBoxSizer(wxVERTICAL);
     page2_panel = new wxPanel(this);

     wxWebView* m_vebview2 = WebView::CreateWebView(page2_panel, "");
     m_vebview2->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, [this](const wxWebViewEvent& evt) {
         open_url(evt.GetString().ToStdString());
     });
     m_vebview2->SetMinSize(wxSize(FromDIP(720), FromDIP(200)));
     m_vebview2->SetMaxSize(wxSize(FromDIP(720), FromDIP(200)));

     fs::path ph2(resources_dir());
     ph2 /= phurl2;
     if (!fs::exists(ph2)) {
         ph2 = fs::path(resources_dir());
         ph2 /= phurl2;
     }
     auto url2 = ph2.string();
     std::replace(url2.begin(), url2.end(), '\\', '/');
     url2 = "file:///" + url2;
     m_vebview2->LoadURL(from_u8(url2));

     page2_sizer->Add(m_vebview2, 0, wxLEFT | wxRIGHT, FromDIP(33));
     page2_panel->SetSizer(page2_sizer);
     page2_panel->Layout();
     page2_panel->Fit();

     //page 3
     wxBoxSizer* page3_sizer = new wxBoxSizer(wxVERTICAL);
     wxBoxSizer* page3_content_sizer = new wxBoxSizer(wxVERTICAL);
     page3_panel = new wxPanel(this);
     auto page3_content_panel = new wxPanel(page3_panel);
     page3_content_panel->SetBackgroundColour(*wxWHITE);
     page3_content_panel->SetMinSize(wxSize(FromDIP(720), FromDIP(200)));
     page3_content_panel->SetMaxSize(wxSize(FromDIP(720), FromDIP(200)));

     auto enable_pat_title = new Label(page3_content_panel, Label::Head_14, _L("Helio Additive third - party software service feature has been successfully enabled!"));
     bold_font = enable_pat_title->GetFont();
     bold_font.SetWeight(wxFONTWEIGHT_BOLD);
     enable_pat_title->SetFont(bold_font);

     auto split_line = new wxPanel(page3_content_panel, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
     split_line->SetMaxSize(wxSize(-1, FromDIP(1)));
     split_line->SetBackgroundColour(wxColour(236, 236, 236));

     wxBoxSizer* pat_token_sizer = new wxBoxSizer(wxHORIZONTAL);
     auto helio_pat_title = new Label(page3_content_panel, Label::Body_15, L("Helio-PAT"));
     helio_input_pat = new ::TextInput(page3_content_panel, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER | wxTE_RIGHT);
     helio_input_pat->SetFont(Label::Body_15);
     helio_input_pat->SetMinSize(wxSize(FromDIP(530), FromDIP(22)));
     helio_input_pat->SetMaxSize(wxSize(FromDIP(530), FromDIP(22)));
     helio_input_pat->Disable();
     wxString pat = Slic3r::HelioQuery::get_helio_pat();
     pat = wxString(pat.Length(), '*');
     helio_input_pat->SetLabel(pat);
     helio_pat_refresh = new wxStaticBitmap(page3_content_panel, wxID_ANY, create_scaled_bitmap("helio_refesh", page3_content_panel, 24), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)), 0);
     helio_pat_refresh->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
     helio_pat_refresh->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
     helio_pat_refresh->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        request_pat();
     });

     helio_pat_eview = new wxStaticBitmap(page3_content_panel, wxID_ANY, create_scaled_bitmap("helio_eview", page3_content_panel, 24), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)), 0);
     helio_pat_eview->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
     helio_pat_eview->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
     helio_pat_eview->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
         wxString pat = helio_input_pat->GetLabel();
         pat = wxString(pat.Length(), '*');
         helio_input_pat->SetLabel(pat);
         show_pat_option("dview");
     });

     helio_pat_dview = new wxStaticBitmap(page3_content_panel, wxID_ANY, create_scaled_bitmap("helio_dview", page3_content_panel, 24), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)), 0);
     helio_pat_dview->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
     helio_pat_dview->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
     helio_pat_dview->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
         helio_input_pat->SetLabel(Slic3r::HelioQuery::get_helio_pat());
         show_pat_option("eview");
     });

     helio_pat_copy = new wxStaticBitmap(page3_content_panel, wxID_ANY, create_scaled_bitmap("helio_copy", page3_content_panel, 24), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)), 0);
     helio_pat_copy->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
     helio_pat_copy->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });


     helio_pat_copy->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
         bool copySuccess = false;
         if (wxTheClipboard->Open()){
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

     //pat failed
     pat_err_label = new Label(page3_content_panel, Label::Body_14, wxEmptyString);
     pat_err_label->SetMinSize(wxSize(FromDIP(720), -1));
     pat_err_label->SetMaxSize(wxSize(FromDIP(720), -1));
     pat_err_label->Wrap(FromDIP(720));
     pat_err_label->SetForegroundColour(wxColour("#FC8800"));

     wxBoxSizer* helio_links_sizer = new wxBoxSizer(wxHORIZONTAL);
     LinkLabel* helio_home_link =  new LinkLabel(page3_content_panel, _L("Helio Additive"), "https://www.helioadditive.com/");
     LinkLabel* helio_privacy_link = nullptr;
     LinkLabel* helio_tou_link =  nullptr;

     if (GUI::wxGetApp().app_config->get("region") == "China") {
         helio_privacy_link = new LinkLabel(page3_content_panel, _L("Privacy Policy of Helio Additive"), "https://www.helioadditive.com/zh-cn/policies/privacy");
         helio_tou_link     = new LinkLabel(page3_content_panel, _L("Terms of Use of Helio Additive"), "https://www.helioadditive.com/zh-cn/policies/terms");
     }
     else {
         helio_privacy_link = new LinkLabel(page3_content_panel, _L("Privacy Policy of Helio Additive"), "https://www.helioadditive.com/en-us/policies/privacy");
         helio_tou_link     = new LinkLabel(page3_content_panel, _L("Terms of Use of Helio Additive"), "https://www.helioadditive.com/en-us/policies/terms");
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


     StateColor btn_bg_green = StateColor(std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered), std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

     page3_content_sizer->Add(enable_pat_title, 0, wxTOP, FromDIP(2));
     page3_content_sizer->Add(0, 0, 0, wxTOP, FromDIP(14));
     page3_content_sizer->Add(split_line, 0, wxEXPAND, 0);
     page3_content_sizer->Add(0, 0, 0, wxTOP, FromDIP(20));
     page3_content_sizer->Add(pat_token_sizer, 0, wxEXPAND, 0);
     page3_content_sizer->Add(pat_err_label, 0, wxTOP, FromDIP(10));
     page3_content_sizer->Add(0, 0, 0, wxTOP, FromDIP(28));
     page3_content_sizer->Add(helio_links_sizer, 0, wxEXPAND, 0);
     page3_content_sizer->Add(0, 0, 0, wxTOP, FromDIP(12));
     //page3_content_sizer->Add(m_button_uninstall, 0, wxLEFT, 0);


     page3_content_panel->SetSizer(page3_content_sizer);
     page3_content_panel->Layout();
     page3_sizer->Add(page3_content_panel, 0, wxLEFT | wxRIGHT, FromDIP(33));
     page3_panel->SetSizer(page3_sizer);
     page3_panel->Layout();
     page3_panel->Fit();

     m_button_confirm = new Button(this, _L("Agree and proceed"));
     m_button_confirm->SetBackgroundColor(btn_bg_green);
     m_button_confirm->SetBorderColor(*wxWHITE);
     m_button_confirm->SetTextColor(wxColour(255, 255, 254));
     m_button_confirm->SetFont(Label::Body_12);
     m_button_confirm->SetSize(wxSize(FromDIP(58), FromDIP(26)));
     m_button_confirm->SetMinSize(wxSize(FromDIP(58), FromDIP(26)));
     m_button_confirm->SetCornerRadius(FromDIP(12));
     m_button_confirm->Bind(wxEVT_LEFT_DOWN, &HelioStatementDialog::on_confirm, this);

     m_button_cancel = new Button(this, _L("Got it"));
     m_button_cancel->SetBackgroundColor(btn_bg_green);
     m_button_cancel->SetBorderColor(*wxWHITE);
     m_button_cancel->SetTextColor(wxColour(255, 255, 254));
     m_button_cancel->SetFont(Label::Body_12);
     m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(26)));
     m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(26)));
     m_button_cancel->SetCornerRadius(FromDIP(12));
     m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { EndModal(wxID_NO); });


     wxBoxSizer* button_sizer = new wxBoxSizer(wxHORIZONTAL);
     button_sizer->Add(0, 0, 1, wxEXPAND, 0);
     button_sizer->Add(m_button_confirm, 0, 0, 0);
     button_sizer->Add(m_button_cancel, 0, wxLEFT, FromDIP(20));
     button_sizer->Add(0, 0, 0, wxRIGHT, FromDIP(50));

     main_sizer->Add(line, 0, wxEXPAND, 0);
     main_sizer->Add(helio_top_background, 0, wxEXPAND, 0);
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(16));
     main_sizer->Add(page1_panel, 0, wxEXPAND, 0);
     main_sizer->Add(page2_panel, 0, wxEXPAND, 0);
     main_sizer->Add(page3_panel, 0, wxEXPAND, 0);
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
     main_sizer->Add(button_sizer, 0, wxEXPAND, 0);
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(16));

     //page show/hide

     if (GUI::wxGetApp().app_config->get("helio_enable") == "true") {
         show_pat_page();
         request_pat();
         m_button_confirm->Hide();
         //m_button_uninstall->Show();
     }
     else {
         show_agreement_page1();
         m_button_confirm->Show();
         //m_button_uninstall->Hide();
     }


     SetSizer(main_sizer);
     Layout();
     Fit();

     CentreOnParent();
     wxGetApp().UpdateDlgDarkUI(this);
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
    if (current_page == 0) {
        page1_agree = true;
        show_agreement_page2();
        m_button_confirm->Refresh();
    }
    else if (current_page == 1) {
        page2_agree = true;
        //m_button_uninstall->Show();
        m_button_confirm->Hide();
    }

    if (page1_agree && page2_agree) {
        wxGetApp().app_config->set_bool("helio_enable", true);
        if (wxGetApp().getAgent()) {
            json j;
            j["operate"] = "switch";
            j["content"] = "enable";
            wxGetApp().getAgent()->track_event("helio_state", j.dump());
        }

        show_pat_page();
        report_consent_install();
        request_pat();

        /*hide helio on main windows*/
        if (wxGetApp().mainframe->expand_program_holder) {
            wxGetApp().mainframe->expand_program_holder->ShowExpandButton(wxGetApp().mainframe->expand_helio_id, true);
            wxGetApp().mainframe->Layout();
        }
    }

    current_page++;
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

void HelioStatementDialog::show_agreement_page1()
{
    page1_panel->Show();
    page2_panel->Hide();
    page3_panel->Hide();
}

void HelioStatementDialog::show_agreement_page2()
{
    page1_panel->Hide();
    page2_panel->Show();
    page3_panel->Hide();
}

void HelioStatementDialog::show_pat_page()
{
    page1_panel->Hide();
    page2_panel->Hide();
    page3_panel->Show();
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

    /*advanced Options*/
    wxBoxSizer* advace_setting_sizer = new wxBoxSizer(wxHORIZONTAL);
    advanced_settings_link = new wxPanel(panel_optimization);
    advanced_settings_link->SetBackgroundColour(*wxWHITE);

    Label* more_setting_tips = new Label(advanced_settings_link, Label::Head_14 ,_L("Advanced Settings"));
    wxFont bold_font = more_setting_tips->GetFont();
    bold_font.SetWeight(wxFONTWEIGHT_BOLD);
    more_setting_tips->SetFont(bold_font);
    advace_setting_sizer->Add(more_setting_tips, 0, wxALIGN_LEFT | wxTOP, FromDIP(4));
    advanced_options_icon = new wxStaticBitmap(advanced_settings_link, wxID_ANY, create_scaled_bitmap("helio_advanced_option0", panel_optimization, 18), wxDefaultPosition,
        wxSize(FromDIP(18), FromDIP(18)));
    advace_setting_sizer->Add(advanced_options_icon, 0, wxALIGN_LEFT | wxTOP, FromDIP(4));
    advanced_settings_link->SetSizer(advace_setting_sizer);
    advanced_settings_link->Layout();
    advanced_settings_link->Fit();

    /*buy now*/
    //more_setting_tips->SetForegroundColour(wxColour(49, 49, 49));
    more_setting_tips->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_HAND); });
    more_setting_tips->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_ARROW); });
    advanced_settings_link->Hide();

    /*advanced option*/
    panel_advanced_option = new wxPanel(panel_optimization);
    if (wxGetApp().dark_mode()) {
        panel_advanced_option->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    } else {
        panel_advanced_option->SetBackgroundColour(*wxWHITE);
    }
    wxBoxSizer* sizer_advanced_option = new wxBoxSizer(wxVERTICAL);

    auto double_min_checker = TextInputValChecker::CreateDoubleMinChecker(0);

    // velocity
    float min_speed = 0.0;
    float max_speed = 0.0;
    auto plater = Slic3r::GUI::wxGetApp().plater();
    if (plater) { plater->get_preview_min_max_value_of_option((int) gcode::EViewType::Feedrate, min_speed, max_speed); }
    wxBoxSizer* min_velocity_item = create_input_item(panel_advanced_option, "min_velocity", _L("Min Velocity"), wxT("mm/s"), { double_min_checker } );
    m_input_items["min_velocity"]->GetTextCtrl()->SetLabel(wxString::Format("%.0f", s_round(min_speed, 0)));

    wxBoxSizer* max_velocity_item = create_input_item(panel_advanced_option, "max_velocity", _L("Max Velocity"), wxT("mm/s"), { double_min_checker });
    m_input_items["max_velocity"]->GetTextCtrl()->SetLabel(wxString::Format("%.0f", s_round(max_speed, 0)));

    // volumetric speed
    float min_volumetric_speed = 0.0;
    float max_volumetric_speed = 0.0;
    if (plater) { plater->get_preview_min_max_value_of_option((int) gcode::EViewType::VolumetricRate, min_volumetric_speed, max_volumetric_speed); }
    wxBoxSizer* min_volumetric_speed_item = create_input_item(panel_advanced_option, "min_volumetric_speed", _L("Min Volumetric Speed"), wxT("mm\u00B3/s"), { double_min_checker });
    m_input_items["min_volumetric_speed"]->GetTextCtrl()->SetLabel(wxString::Format("%.2f", s_round(min_volumetric_speed, 2)));

    wxBoxSizer* max_volumetric_speed_item = create_input_item(panel_advanced_option, "max_volumetric_speed", _L("Max Volumetric Speed"), wxT("mm\u00B3/s"), { double_min_checker });
    m_input_items["max_volumetric_speed"]->GetTextCtrl()->SetLabel(wxString::Format("%.2f", s_round(max_volumetric_speed, 2)));

    // layers to optimize
    int layer_count = plater ? plater->get_gcode_layers_count() : 0;
    wxBoxSizer* layers_to_optimize_item = create_input_optimize_layers(panel_advanced_option, layer_count);

    sizer_advanced_option->Add(min_velocity_item, 0, wxEXPAND, FromDIP(0));
    sizer_advanced_option->Add(max_velocity_item, 0, wxEXPAND|wxTOP, FromDIP(6));
    sizer_advanced_option->Add(min_volumetric_speed_item, 0, wxEXPAND|wxTOP, FromDIP(6));
    sizer_advanced_option->Add(max_volumetric_speed_item, 0, wxEXPAND|wxTOP, FromDIP(6));
    sizer_advanced_option->Add(layers_to_optimize_item, 0, wxEXPAND|wxTOP, FromDIP(6));
    panel_advanced_option->SetSizer(sizer_advanced_option);
    panel_advanced_option->Layout();
    panel_advanced_option->Fit();

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
    sizer_optimization->Add(advanced_settings_link, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(8));
    sizer_optimization->Add(panel_advanced_option, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(8));

    panel_optimization->SetSizer(sizer_optimization);
    panel_optimization->Layout();
    panel_optimization->Fit();

    panel_optimization->Hide();
    panel_advanced_option->Hide();

    more_setting_tips->Bind(wxEVT_LEFT_DOWN, [=](wxMouseEvent& e) {
        if (only_advanced_settings)
            return;

        if (!use_advanced_settings) {
            use_advanced_settings = true;
        }
        else {
            use_advanced_settings = false;
        }
        show_advanced_mode();
    });

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

    buy_now_link = new LinkLabel(this, _L("Buy Now"), "https://wiki.helioadditive.com/");
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
                        advanced_settings_link->Show();
                        m_button_confirm->Enable();
                    }
                    else {
                        advanced_settings_link->Hide();
                        if (times <= 0) {
                            buy_now_link->setLabel(_L("Buy Now"));
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
            /*hide based mode when nozzle diameter  = 0.2*/
            const auto& full_config = wxGetApp().preset_bundle->full_config();
            const auto& project_config = wxGetApp().preset_bundle->project_config;
            double nozzle_diameter = full_config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values[0];

            if (boost::str(boost::format("%.1f") % nozzle_diameter) == "0.2") {
                only_advanced_settings = true;
                use_advanced_settings = true;
                show_advanced_mode();
            }

            /*hide based mode when preser has nozzle diameter  = 0.2*/
            auto edited_preset = wxGetApp().preset_bundle->prints.get_edited_preset().config;
            auto layer_height = edited_preset.option<ConfigOptionFloat>("layer_height")->value;
            if (layer_height < 0.2) {
                only_advanced_settings = true;
                use_advanced_settings = true;
                show_advanced_mode();
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
    if (use_advanced_settings) {
        advanced_options_icon->SetBitmap(create_scaled_bitmap("helio_advanced_option1", panel_optimization, 18));
        panel_advanced_option->Show();
    }
    else {
        advanced_options_icon->SetBitmap(create_scaled_bitmap("helio_advanced_option0", panel_optimization, 18));
        panel_advanced_option->Hide();
    }
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

wxBoxSizer* HelioInputDialog::create_combo_item(wxWindow* parent, std::string key,  wxString name, std::map<int, wxString> combolist, int def)
{
    wxBoxSizer* item_sizer = new wxBoxSizer(wxHORIZONTAL);
    Label* inout_title = new Label(parent, Label::Body_14, name);
    inout_title->SetFont(::Label::Body_14);
    inout_title->SetForegroundColour(wxColour("#262E30"));

    auto combobox = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(120), -1), 0, nullptr, wxCB_READONLY);
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

    if (!use_advanced_settings) {
        ok = true;
        return data;
    }

    if (!s_get_double_val(m_input_items["min_velocity"], data.min_velocity)) { return data; }
    if (!s_get_double_val(m_input_items["max_velocity"], data.max_velocity)) { return data; }
    if (!s_get_double_val(m_input_items["min_volumetric_speed"], data.min_volumetric_speed)) { return data; }
    if (!s_get_double_val(m_input_items["max_volumetric_speed"], data.max_volumetric_speed)) { return data; }
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
