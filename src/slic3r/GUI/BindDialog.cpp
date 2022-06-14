#include "BindDialog.hpp"
#include "GUI_App.hpp"

#include <wx/wx.h> 
#include <wx/sizer.h>
#include <wx/statbox.h>
#include "wx/evtloop.h"

#include "libslic3r/Model.hpp"
#include "libslic3r/Polygon.hpp"
#include "MainFrame.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"

namespace Slic3r {
namespace GUI {

BindDialog::BindDialog(Plater *plater)
    : DPIDialog(plater, wxID_ANY, _L("Bind Dialog"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX), m_plater(plater)
{
    this->SetSizeHints(wxDefaultSize, wxDefaultSize);

    wxBoxSizer *top_sizer;
    top_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *ssdp_sizer;
    ssdp_sizer = new wxBoxSizer(wxHORIZONTAL);

    btn_start_ssdp = new wxButton(this, wxID_ANY, wxT("Start SSDP"), wxDefaultPosition, wxDefaultSize, 0);
    ssdp_sizer->Add(btn_start_ssdp, 0, wxALL, 5);

    btn_stop_ssdp = new wxButton(this, wxID_ANY, wxT("Stop SSDP"), wxDefaultPosition, wxDefaultSize, 0);
    ssdp_sizer->Add(btn_stop_ssdp, 0, wxALL, 5);

    ssdp_sizer->Add(0, 0, 1, wxEXPAND, 5);

    top_sizer->Add(ssdp_sizer, 0, wxEXPAND, 5);

    wxBoxSizer *sn_sizer;
    sn_sizer = new wxBoxSizer(wxHORIZONTAL);

    text_sn = new wxStaticText(this, wxID_ANY, wxT("Printer:"), wxDefaultPosition, wxDefaultSize, 0);
    text_sn->Wrap(-1);
    sn_sizer->Add(text_sn, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    cb_machine_sn = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, 0);
    sn_sizer->Add(cb_machine_sn, 1, wxALL, 5);

    btn_refresh = new wxButton(this, wxID_ANY, wxT("Refresh"), wxDefaultPosition, wxDefaultSize, 0);
    sn_sizer->Add(btn_refresh, 0, wxALL, 5);

    top_sizer->Add(sn_sizer, 0, wxEXPAND, 5);

    m_status_bar = std::make_shared<BBLStatusBar>(this);
    auto panel = m_status_bar->get_panel();
    top_sizer->Add(panel, 1, wxALL | wxEXPAND, 0);

    text_result = new wxStaticText(this, wxID_ANY, wxT("Result Info"), wxDefaultPosition, wxDefaultSize, 0);
    text_result->Wrap(-1);
    top_sizer->Add(text_result, 0, wxALL | wxEXPAND, 5);

    wxBoxSizer *btn_sizer;
    btn_sizer = new wxBoxSizer(wxHORIZONTAL);

    btn_bind = new wxButton(this, wxID_ANY, _L("Confirm"), wxDefaultPosition, wxDefaultSize, 0);
    btn_sizer->Add(btn_bind, 0, wxALL, 5);

    /*btn_unbind = new wxButton(this, wxID_ANY, wxT("Unbind"), wxDefaultPosition, wxDefaultSize, 0);
    btn_sizer->Add(btn_unbind, 0, wxALL, 5);*/

    btn_sizer->Add(0, 0, 1, wxEXPAND, 5);

    top_sizer->Add(btn_sizer, 1, wxEXPAND, 5);

    this->SetSizer(top_sizer);
    this->Layout();

    this->Centre(wxBOTH);

    btn_start_ssdp->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_start_ssdp), NULL, this);
    btn_stop_ssdp->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_stop_ssdp), NULL, this);
    btn_bind->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_bind_printer), NULL, this);
    btn_refresh->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_refresh), NULL, this);
}

BindDialog::~BindDialog() {
    btn_start_ssdp->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_start_ssdp), NULL, this);
    btn_stop_ssdp->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_stop_ssdp), NULL, this);
    btn_bind->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_bind_printer), NULL, this);
    btn_refresh->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_refresh), NULL, this);
}

void BindDialog::on_start_ssdp(wxCommandEvent &event)
{
    BBL::SsdpDiscovery *ssdp = wxGetApp().getSsdpDiscovery();
    if (ssdp) {
        ssdp->start();
        ssdp->set_ssdp_discovery(true);
    }
}

void BindDialog::on_stop_ssdp(wxCommandEvent &event)
{
    BBL::SsdpDiscovery * ssdp = wxGetApp().getSsdpDiscovery();
    if (ssdp) {
        ssdp->set_ssdp_discovery(false);
        ssdp->stop();
    }
}

void BindDialog::on_bind_printer(wxCommandEvent &event)
{
    int select = cb_machine_sn->GetSelection();
    std::string sn_str = printer_list_item[select].ToStdString();

    DeviceManager *dev_manager = wxGetApp().getDeviceManager();
    std::map<std::string, MachineObject*> list = dev_manager->get_all_machine_list();
    std::map<std::string, MachineObject *>::iterator it = list.find(sn_str);

    if (it != list.end()) {
        m_bind_job = std::make_shared<BindJob>(m_status_bar, wxGetApp().plater(), it->second->dev_id, it->second->dev_ip);
        m_bind_job->start();
    }
}

void BindDialog::on_refresh(wxCommandEvent &event)
{
    printer_list_item.clear();
    DeviceManager *dev_manager = wxGetApp().getDeviceManager();
    std::map<std::string, MachineObject*> list = dev_manager->get_all_machine_list();

    for (auto it = list.begin(); it != list.end(); it++) {
        printer_list_item.push_back(it->second->dev_id);
    }
    cb_machine_sn->Set(printer_list_item);
}

void BindDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    Fit();
    Refresh();
}

 BindMachineDilaog::BindMachineDilaog(Plater *plater /*= nullptr*/) 
     : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Log in printer"), wxDefaultPosition, wxDefaultSize, wxCAPTION)
 {
     std::string icon_path = (boost::format("%1%/images/BambuStudio.ico") % resources_dir()).str();
     SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

     SetBackgroundColour(*wxWHITE);
     wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
     auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
     m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
     m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(38));

     wxBoxSizer *m_sizer_body = new wxBoxSizer(wxHORIZONTAL);

     m_panel_left = new StaticBox(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(201), FromDIP(212)), wxBORDER_NONE);
     m_panel_left->SetMinSize(wxSize(FromDIP(201), FromDIP(212)));
     m_panel_left->SetCornerRadius(8);
     m_panel_left->SetBackgroundColor(BIND_DIALOG_GREY200);
     wxBoxSizer *m_sizere_left_h = new wxBoxSizer(wxHORIZONTAL);
     wxBoxSizer *m_sizere_left_v= new wxBoxSizer(wxVERTICAL);

     auto m_printer_img = new wxStaticBitmap(m_panel_left, wxID_ANY, create_scaled_bitmap("printer_thumbnail", nullptr, 96), wxDefaultPosition, wxSize(FromDIP(100), FromDIP(96)), 0);
     m_printer_name = new wxStaticText(m_panel_left, wxID_ANY, wxEmptyString);
     m_printer_name->SetFont(::Label::Head_14);
     m_sizere_left_v->Add(m_printer_img, 0, wxALIGN_CENTER, 0);
     m_sizere_left_v->Add(0, 0, 0, wxTOP, 5);
     m_sizere_left_v->Add(m_printer_name, 0, wxALIGN_CENTER, 0);
     m_sizere_left_h->Add(m_sizere_left_v, 1, wxALIGN_CENTER, 0);

     m_panel_left->SetSizer(m_sizere_left_h);
     m_panel_left->Layout();
     m_sizer_body->Add(m_panel_left, 0, wxEXPAND, 0);

     auto m_bind_icon = create_scaled_bitmap("bind_machine", nullptr, 14);
     m_sizer_body->Add(new wxStaticBitmap(this, wxID_ANY, m_bind_icon, wxDefaultPosition, wxSize(FromDIP(34), FromDIP(14)), 0), 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(20));

     m_panel_right = new StaticBox(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(201), FromDIP(212)), wxBORDER_NONE);
     m_panel_right->SetMinSize(wxSize(FromDIP(201), FromDIP(212)));
     m_panel_right->SetCornerRadius(8);
     m_panel_right->SetBackgroundColor(BIND_DIALOG_GREY200);

     m_user_name = new wxStaticText(m_panel_right, wxID_ANY, wxEmptyString);
     m_user_name->SetBackgroundColour(BIND_DIALOG_GREY200);
     m_user_name->SetFont(::Label::Head_14);
     wxBoxSizer *m_sizer_right_h = new wxBoxSizer(wxHORIZONTAL);
     wxBoxSizer *m_sizer_right_v = new wxBoxSizer(wxVERTICAL);

     BBL::AccountManager *c = Slic3r::GUI::wxGetApp().getAccountManager();
     m_user_name->SetLabelText(c->get_curr_user()->m_name);
    /* if (c->is_user_login()) {
         
     }*/

     m_avatar = new wxStaticBitmap(m_panel_right, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(FromDIP(60), FromDIP(60)), 0);

     wxWebRequest request = wxWebSession::GetDefault().CreateRequest(this, c->get_curr_user()->m_avatar);
     if (!request.IsOk()) {
         // todo request fail
     }

     Bind(wxEVT_WEBREQUEST_STATE, [this](wxWebRequestEvent &evt) {
         switch (evt.GetState()) {
         // Request completed
         case wxWebRequest::State_Completed: {
             wxImage avatar_stream = *evt.GetResponse().GetStream();
             if (avatar_stream.IsOk()) {
                 avatar_stream.Rescale(FromDIP(60), FromDIP(60));
                 auto bitmap = new wxBitmap(avatar_stream);
                 //bitmap->SetSize(wxSize(FromDIP(60), FromDIP(60)));
                 m_avatar->SetBitmap(*bitmap);
                 Layout();
             }
             break;
         }
         // Request failed
         case wxWebRequest::State_Failed: {
             break;
         }
         }
     });
     // Start the request
     request.Start();

     m_sizer_right_v->Add(m_avatar, 0, wxALIGN_CENTER, 0);
     m_sizer_right_v->Add(0, 0, 0, wxTOP, 7);
     m_sizer_right_v->Add(m_user_name, 0, wxALIGN_CENTER, 0);
     m_sizer_right_h->Add(m_sizer_right_v, 1, wxALIGN_CENTER, 0);

     m_panel_right->SetSizer(m_sizer_right_h);
     m_panel_right->Layout();
     m_sizer_body->Add(m_panel_right, 0, wxEXPAND, 0);

     m_sizer_main->Add(m_sizer_body, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));

     m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(20));

     m_status_text = new wxStaticText(this, wxID_ANY, _L("Would you like to log in this printer with current account?"), wxDefaultPosition,
                                           wxSize(BIND_DIALOG_BUTTON_PANEL_SIZE.x, -1), wxST_ELLIPSIZE_END);
     m_status_text->SetForegroundColour(wxColour(107, 107, 107));
     m_status_text->SetFont(::Label::Body_13);
     m_status_text->Wrap(-1);

     m_simplebook = new wxSimplebook(this, wxID_ANY, wxDefaultPosition,BIND_DIALOG_BUTTON_PANEL_SIZE, 0);
     m_simplebook->SetBackgroundColour(*wxWHITE);
    
     m_status_bar = std::make_shared<BBLStatusBarBind>(m_simplebook);
    
     auto        button_panel   = new wxPanel(m_simplebook, wxID_ANY, wxDefaultPosition, BIND_DIALOG_BUTTON_PANEL_SIZE);
     button_panel->SetBackgroundColour(*wxWHITE);
     wxBoxSizer *m_sizer_button = new wxBoxSizer(wxHORIZONTAL);
     m_sizer_button->Add(0, 0, 1, wxEXPAND, 5);
     m_button_bind = new Button(button_panel, _L("Confirm"));
     StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                             std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
     m_button_bind->SetBackgroundColor(btn_bg_green);
     m_button_bind->SetBorderColor(wxColour(0, 174, 66));
     m_button_bind->SetTextColor(*wxWHITE);
     m_button_bind->SetSize(BIND_DIALOG_BUTTON_SIZE);
     m_button_bind->SetMinSize(BIND_DIALOG_BUTTON_SIZE);
     m_button_bind->SetCornerRadius(10);
     

     StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

     m_button_cancel = new Button(button_panel, _L("Cancel"));
     m_button_cancel->SetBackgroundColor(btn_bg_white);
     m_button_cancel->SetBorderColor(BIND_DIALOG_GREY900);
     m_button_cancel->SetSize(BIND_DIALOG_BUTTON_SIZE);
     m_button_cancel->SetMinSize(BIND_DIALOG_BUTTON_SIZE);
     m_button_cancel->SetTextColor(BIND_DIALOG_GREY900);
     m_button_cancel->SetCornerRadius(10);

     m_sizer_button->Add(m_button_bind, 0, wxALIGN_CENTER, 0);
     m_sizer_button->Add(0, 0, 0, wxLEFT, FromDIP(13));
     m_sizer_button->Add(m_button_cancel, 0, wxALIGN_CENTER, 0);
     button_panel->SetSizer(m_sizer_button);
     button_panel->Layout();
     m_sizer_button->Fit(button_panel);

     m_simplebook->AddPage(m_status_bar->get_panel(), wxEmptyString, false);
     m_simplebook->AddPage(button_panel, wxEmptyString, false);

     //m_sizer_main->Add(m_sizer_button, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));

     m_sizer_main->Add(m_status_text, 0, wxALIGN_CENTER, 0);
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(10));
     m_sizer_main->Add(m_simplebook, 0, wxALIGN_CENTER, 0);
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(20));

     SetSizer(m_sizer_main);
     Layout();
     Fit();
     Centre(wxBOTH);

     Bind(wxEVT_SHOW, &BindMachineDilaog::on_show, this);
     m_button_bind->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindMachineDilaog::on_bind_printer), NULL, this);
     m_button_cancel->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindMachineDilaog::on_cancel), NULL, this);
     this->Connect(EVT_BIND_MACHINE_FAIL, wxCommandEventHandler(BindMachineDilaog::on_bind_fail), NULL, this);
     this->Connect(EVT_BIND_MACHINE_SUCCESS, wxCommandEventHandler(BindMachineDilaog::on_bind_success), NULL, this);
     this->Connect(EVT_BIND_UPDATE_MESSAGE, wxCommandEventHandler(BindMachineDilaog::on_update_message), NULL, this);
     m_simplebook->SetSelection(1);
 }

 BindMachineDilaog::~BindMachineDilaog() 
 {
     m_button_bind->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindMachineDilaog::on_bind_printer), NULL, this);
     m_button_cancel->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindMachineDilaog::on_cancel), NULL, this);
     this->Disconnect(EVT_BIND_MACHINE_FAIL, wxCommandEventHandler(BindMachineDilaog::on_bind_fail), NULL, this);
     this->Disconnect(EVT_BIND_MACHINE_SUCCESS, wxCommandEventHandler(BindMachineDilaog::on_bind_success), NULL, this);
     this->Disconnect(EVT_BIND_UPDATE_MESSAGE, wxCommandEventHandler(BindMachineDilaog::on_update_message), NULL, this);
 }

 //static  size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
 //{
 //    register int         realsize = size * nmemb;
 //    struct MemoryStruct *mem      = (struct MemoryStruct *) userp;
 //    mem->memory                   = (char *) realloc(mem->memory, mem->size + realsize + 1);
 //    if (mem->memory) {
 //        memcpy(&(mem->memory[mem->size]), contents, realsize);
 //        mem->size += realsize;
 //        mem->memory[mem->size] = 0;
 //    }
 //    return realsize;
 //}


 void BindMachineDilaog::on_cancel(wxCommandEvent &event)
 { 
      EndModal(wxID_CANCEL);
 }

 void BindMachineDilaog::on_bind_fail(wxCommandEvent &event) 
 { 
     m_simplebook->SetSelection(1);
 }

 void BindMachineDilaog::on_update_message(wxCommandEvent &event)
 {
     m_status_text->SetLabelText(event.GetString());
 }

 void BindMachineDilaog::on_bind_success(wxCommandEvent &event) 
 {
     EndModal(wxID_CANCEL);
     MessageDialog msg_wingow(nullptr, _L("Log in successful."), "", wxAPPLY | wxOK);
     if (msg_wingow.ShowModal() == wxOK) { return; }
 }

 void BindMachineDilaog::on_bind_printer(wxCommandEvent &event)
 {
     //check isset info
     if (m_machine_info == nullptr || m_machine_info == NULL) return;

     //check dev_id
     if (m_machine_info->dev_id.empty()) return;

     m_simplebook->SetSelection(0);
     m_bind_job = std::make_shared<BindJob>(m_status_bar, wxGetApp().plater(), m_machine_info->dev_id, m_machine_info->dev_ip);
     m_bind_job->set_event_handle(this);
     m_bind_job->start();
 }

void BindMachineDilaog::on_dpi_changed(const wxRect &suggested_rect) 
{
    m_button_bind->SetMinSize(BIND_DIALOG_BUTTON_SIZE);
    m_button_cancel->SetMinSize(BIND_DIALOG_BUTTON_SIZE);
}

void BindMachineDilaog::on_show(wxShowEvent &event) 
{
    m_printer_name->SetLabelText(m_machine_info->get_printer_type_string());
    Layout();
}


UnBindMachineDilaog::UnBindMachineDilaog(Plater *plater /*= nullptr*/) 
     : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Log out printer"), wxDefaultPosition, wxDefaultSize, wxCAPTION)
 {
     std::string icon_path = (boost::format("%1%/images/BambuStudio.ico") % resources_dir()).str();
     SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

     SetBackgroundColour(*wxWHITE);
     wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
     auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
     m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
     m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(38));

     wxBoxSizer *m_sizer_body = new wxBoxSizer(wxHORIZONTAL);

     auto  m_panel_left = new StaticBox(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(201), FromDIP(212)), wxBORDER_NONE);
     m_panel_left->SetMinSize(wxSize(FromDIP(201), FromDIP(212)));
     m_panel_left->SetCornerRadius(8);
     m_panel_left->SetBackgroundColor(BIND_DIALOG_GREY200);
     wxBoxSizer *m_sizere_left_h = new wxBoxSizer(wxHORIZONTAL);
     wxBoxSizer *m_sizere_left_v= new wxBoxSizer(wxVERTICAL);

     auto m_printer_img = new wxStaticBitmap(m_panel_left, wxID_ANY, create_scaled_bitmap("printer_thumbnail", nullptr, 96), wxDefaultPosition, wxSize(FromDIP(100), FromDIP(96)),
                                             0);
     m_printer_name     = new wxStaticText(m_panel_left, wxID_ANY, wxEmptyString);
     m_printer_name->SetFont(::Label::Head_14);
     m_sizere_left_v->Add(m_printer_img, 0, wxALIGN_CENTER, 0);
     m_sizere_left_v->Add(0, 0, 0, wxTOP, 5);
     m_sizere_left_v->Add(m_printer_name, 0, wxALIGN_CENTER, 0);
     m_sizere_left_h->Add(m_sizere_left_v, 1, wxALIGN_CENTER, 0);

     m_panel_left->SetSizer(m_sizere_left_h);
     m_panel_left->Layout();
     m_sizer_body->Add(m_panel_left, 0, wxEXPAND, 0);

     auto m_bind_icon = create_scaled_bitmap("unbind_machine", nullptr, 28);
     m_sizer_body->Add(new wxStaticBitmap(this, wxID_ANY, m_bind_icon, wxDefaultPosition, wxSize(FromDIP(36), FromDIP(28)), 0), 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(20));

     auto m_panel_right = new StaticBox(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(201), FromDIP(212)), wxBORDER_NONE);
     m_panel_right->SetMinSize(wxSize(FromDIP(201), FromDIP(212)));
     m_panel_right->SetCornerRadius(8);
     m_panel_right->SetBackgroundColor(BIND_DIALOG_GREY200);
     m_user_name = new wxStaticText(m_panel_right, wxID_ANY, wxEmptyString);
     m_user_name->SetBackgroundColour(BIND_DIALOG_GREY200);
     m_user_name->SetFont(::Label::Head_14);
     wxBoxSizer *m_sizer_right_h = new wxBoxSizer(wxHORIZONTAL);
     wxBoxSizer *m_sizer_right_v = new wxBoxSizer(wxVERTICAL);

     BBL::AccountManager *c = Slic3r::GUI::wxGetApp().getAccountManager();
     m_user_name->SetLabelText(c->get_curr_user()->m_name);
    /* if (c->is_user_login()) {
         
     }*/
    
     m_avatar = new wxStaticBitmap(m_panel_right, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(FromDIP(60), FromDIP(60)), 0);

     wxWebRequest request = wxWebSession::GetDefault().CreateRequest(this, c->get_curr_user()->m_avatar);
     if (!request.IsOk()) {
         // todo request fail
     }

     Bind(wxEVT_WEBREQUEST_STATE, [this](wxWebRequestEvent &evt) {
         switch (evt.GetState()) {
         // Request completed
         case wxWebRequest::State_Completed: {
             wxImage avatar_stream = *evt.GetResponse().GetStream();
             if (avatar_stream.IsOk()) {
                 avatar_stream.Rescale(FromDIP(60), FromDIP(60));
                 auto bitmap = new wxBitmap(avatar_stream);
                 //bitmap->SetSize(wxSize(FromDIP(60), FromDIP(60)));
                 m_avatar->SetBitmap(*bitmap);
                 Layout();
             }
             break;
         }
         // Request failed
         case wxWebRequest::State_Failed: {
             break;
         }
         }
     });
     // Start the request
     request.Start();


     m_sizer_right_v->Add(m_avatar, 0, wxALIGN_CENTER, 0);
     m_sizer_right_v->Add(0, 0, 0, wxTOP, 7);
     m_sizer_right_v->Add(m_user_name, 0, wxALIGN_CENTER, 0);
     m_sizer_right_h->Add(m_sizer_right_v, 1, wxALIGN_CENTER, 0);

     m_panel_right->SetSizer(m_sizer_right_h);
     m_panel_right->Layout();
     m_sizer_body->Add(m_panel_right, 0, wxEXPAND, 0);

     m_sizer_main->Add(m_sizer_body, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));

     m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(20));

     m_status_text = new wxStaticText(this, wxID_ANY, _L("Would you like to log out the printer?"), wxDefaultPosition,
                                           wxSize(BIND_DIALOG_BUTTON_PANEL_SIZE.x, -1), wxST_ELLIPSIZE_END);
     m_status_text->SetForegroundColour(wxColour(107, 107, 107));
     m_status_text->SetFont(::Label::Body_13);
     m_status_text->Wrap(-1);

    

     wxBoxSizer *m_sizer_button = new wxBoxSizer(wxHORIZONTAL);

     m_sizer_button->Add(0, 0, 1, wxEXPAND, 5);
     m_button_unbind = new Button(this, _L("Confirm"));
     StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                             std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
     m_button_unbind->SetBackgroundColor(btn_bg_green);
     m_button_unbind->SetBorderColor(wxColour(0, 174, 66));
     m_button_unbind->SetTextColor(*wxWHITE);
     m_button_unbind->SetSize(BIND_DIALOG_BUTTON_SIZE);
     m_button_unbind->SetMinSize(BIND_DIALOG_BUTTON_SIZE);
     m_button_unbind->SetCornerRadius(10);
     

     StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

     m_button_cancel = new Button(this, _L("Cancel"));
     m_button_cancel->SetBackgroundColor(btn_bg_white);
     m_button_cancel->SetBorderColor(BIND_DIALOG_GREY900);
     m_button_cancel->SetSize(BIND_DIALOG_BUTTON_SIZE);
     m_button_cancel->SetMinSize(BIND_DIALOG_BUTTON_SIZE);
     m_button_cancel->SetTextColor(BIND_DIALOG_GREY900);
     m_button_cancel->SetCornerRadius(10);

     m_sizer_button->Add(m_button_unbind, 0, wxALIGN_CENTER, 0);
     m_sizer_button->Add(0, 0, 0, wxLEFT, FromDIP(13));
     m_sizer_button->Add(m_button_cancel, 0, wxALIGN_CENTER, 0);

     m_sizer_main->Add(m_status_text, 0, wxALIGN_CENTER, 0);
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(10));
     m_sizer_main->Add(m_sizer_button, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(20));

     SetSizer(m_sizer_main);
     Layout();
     Fit();
     Centre(wxBOTH);

     Bind(wxEVT_SHOW, &UnBindMachineDilaog::on_show, this);
     m_button_unbind->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(UnBindMachineDilaog::on_unbind_printer), NULL, this);
     m_button_cancel->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(UnBindMachineDilaog::on_cancel), NULL, this);
 }

 UnBindMachineDilaog::~UnBindMachineDilaog() 
 {
     m_button_unbind->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(UnBindMachineDilaog::on_unbind_printer), NULL, this);
     m_button_cancel->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(UnBindMachineDilaog::on_cancel), NULL, this);
 }


void UnBindMachineDilaog::on_cancel(wxCommandEvent &event) 
{
    EndModal(wxID_CANCEL);
}

void UnBindMachineDilaog::on_unbind_printer(wxCommandEvent &event) 
{
    BBL::AccountManager *account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    if (!account_manager->is_user_login()) {
        m_status_text->SetLabelText(_L("Please log in first."));
        return;
    }


    if (!m_machine_info) {
        m_status_text->SetLabelText(_L("There was a problem connecting to the printer. Please try again."));
        return;
    }

    account_manager->request_unbind(m_machine_info->dev_id, [this](int result, std::string body) {
        if (result == 0) {
            DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
            dev->update_my_machine_list_info();

            m_status_text->SetLabelText(_L("Log out successful."));
            m_button_unbind->Hide();
        } else {
            m_status_text->SetLabelText(_L("Failed to log out."));
            return;
        }
    });

    //m_status_text->SetLabelText(_L("Log out failed."));
}

 void UnBindMachineDilaog::on_dpi_changed(const wxRect &suggested_rect) 
{
      m_button_unbind->SetMinSize(BIND_DIALOG_BUTTON_SIZE);
      m_button_cancel->SetMinSize(BIND_DIALOG_BUTTON_SIZE);
}

void UnBindMachineDilaog::on_show(wxShowEvent &event)
{
    m_printer_name->SetLabelText(m_machine_info->get_printer_type_string());
    Layout();
}

}} // namespace Slic3r::GUI
