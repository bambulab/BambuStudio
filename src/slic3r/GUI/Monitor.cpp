#include "Tab.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/Utils/Http.hpp"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/settings.h>
#include <wx/filedlg.h>
#include <wx/wupdlock.h>
#include <wx/dataview.h>
#include <wx/tglbtn.h>

#include "wxExtensions.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "Widgets/Label.hpp"
#include "format.hpp"
#include "MediaPlayCtrl.h"
#include "MediaFilePanel.h"
#include "Plater.hpp"
#include "BindDialog.hpp"

namespace Slic3r {
namespace GUI {

#define REFRESH_INTERVAL       1000

AddMachinePanel::AddMachinePanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
    : wxPanel(parent, id, pos, size, style)
{
    this->SetBackgroundColour(0xEEEEEE);

    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);

    topsizer->AddStretchSpacer();

    m_bitmap_empty = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    m_bitmap_empty->SetBitmap(create_scaled_bitmap("monitor_status_empty", nullptr, 250));
    topsizer->Add(m_bitmap_empty, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);
    topsizer->AddSpacer(46);

    wxBoxSizer* horiz_sizer = new wxBoxSizer(wxHORIZONTAL);
    horiz_sizer->Add(0, 0, 538, 0, 0);

    wxBoxSizer* btn_sizer = new wxBoxSizer(wxVERTICAL);
    m_button_add_machine = new Button(this, "", "monitor_add_machine", FromDIP(23));
    m_button_add_machine->SetCornerRadius(10);
    StateColor button_bg(
        std::pair<wxColour, int>(0xCECECE, StateColor::Pressed),
        std::pair<wxColour, int>(0xCECECE, StateColor::Hovered),
        std::pair<wxColour, int>(this->GetBackgroundColour(), StateColor::Normal)
    );
    m_button_add_machine->SetBackgroundColor(button_bg);
    m_button_add_machine->SetBorderColor(0x909090);
    m_button_add_machine->SetMinSize(wxSize(96, 39));
    btn_sizer->Add(m_button_add_machine, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);
    m_staticText_add_machine = new wxStaticText(this, wxID_ANY, wxT("click to add machine"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_add_machine->Wrap(-1);
    m_staticText_add_machine->SetForegroundColour(0x909090);
    btn_sizer->Add(m_staticText_add_machine, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);

    horiz_sizer->Add(btn_sizer);
    horiz_sizer->Add(0, 0, 624, 0, 0);

    topsizer->Add(horiz_sizer, 0, wxEXPAND, 0);

    topsizer->AddStretchSpacer();

    this->SetSizer(topsizer);
    this->Layout();

    // Connect Events
    m_button_add_machine->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AddMachinePanel::on_add_machine), NULL, this);
}

void AddMachinePanel::msw_rescale() {

}

void AddMachinePanel::on_add_machine(wxCommandEvent& event) {
    // load a url
}

AddMachinePanel::~AddMachinePanel() {
    m_button_add_machine->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AddMachinePanel::on_add_machine), NULL, this);
}

 MonitorPanel::MonitorPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    init_bitmap();

    init_tabpanel();

    m_main_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_main_sizer->Add(m_tabpanel, 1, wxEXPAND | wxLEFT, 0);
    SetSizerAndFit(m_main_sizer);

    init_timer();

    m_side_tools->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_printer_clicked), NULL, this);
    Bind(wxEVT_TIMER, &MonitorPanel::on_timer, this);
    Bind(wxEVT_SIZE, &MonitorPanel::on_size, this);
    Bind(wxEVT_COMMAND_CHOICE_SELECTED, &MonitorPanel::on_select_printer, this);
}

MonitorPanel::~MonitorPanel()
{
    m_side_tools->Disconnect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_printer_clicked), NULL, this);
    if (m_refresh_timer)
        m_refresh_timer->Stop();
}

 void MonitorPanel::init_bitmap()
{
    m_signal_strong_img = create_scaled_bitmap("monitor_signal_strong", nullptr, 24);
    m_signal_middle_img = create_scaled_bitmap("monitor_signal_middle", nullptr, 24);
    m_signal_weak_img = create_scaled_bitmap("monitor_signal_weak", nullptr, 24);
    m_signal_no_img   = create_scaled_bitmap("monitor_signal_no", nullptr, 24);
    m_printer_img = create_scaled_bitmap("monitor_printer", nullptr, 26);
    m_arrow_img = create_scaled_bitmap("monitor_arrow",nullptr, 14);
}

 void MonitorPanel::init_timer()
{
    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
    m_refresh_timer->Start(REFRESH_INTERVAL);
    wxPostEvent(this, wxTimerEvent());
}

 void MonitorPanel::init_tabpanel()
{
    m_side_tools = new SideTools(this, wxID_ANY);
    wxBoxSizer* sizer_side_tools = new wxBoxSizer(wxVERTICAL);
    sizer_side_tools->Add(m_side_tools, 1, wxEXPAND, 0);
    m_tabpanel             = new Tabbook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, sizer_side_tools, wxNB_LEFT | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME);
    m_tabpanel->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
        ;
    });

    m_status_add_machine_panel = new AddMachinePanel(m_tabpanel);
    m_status_info_panel        = new StatusPanel(m_tabpanel);
    m_media_file_panel         = new MediaFilePanel(m_tabpanel);
    m_upgrade_panel            = new UpgradePanel(m_tabpanel);
    m_hms_panel                = new HMSPanel(m_tabpanel);

    m_tabpanel->AddPage(m_status_info_panel, _L("Status"), "", true);
    m_tabpanel->AddPage(m_media_file_panel,  _L("Media"),  "", false);
    m_tabpanel->AddPage(m_upgrade_panel,     _L("Update"), "", false);
    m_tabpanel->AddPage(m_hms_panel,         _L("HMS"),    "", false);

    m_initialized = true;

    show_status((int)MonitorStatus::MONITOR_NO_PRINTER);
}

void MonitorPanel::set_default()
{
    obj = nullptr;
    /* set default value */

    /* reset status panel*/
    m_status_info_panel->set_default();

    /* reset side tool*/
    //m_bitmap_wifi_signal->SetBitmap(wxNullBitmap);

    /* reset time lapse panel */
    m_media_file_panel->SetMachineObject(nullptr);

    wxGetApp().sidebar().load_ams_list({});
}

wxWindow* MonitorPanel::create_side_tools()
{
    //TEST function
    //m_bitmap_wifi_signal->Connect(wxEVT_LEFT_DCLICK, wxMouseEventHandler(MonitorPanel::on_update_all), NULL, this);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    auto        panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(0, FromDIP(50)));
    panel->SetBackgroundColour(wxColour(135,206,250));
    panel->SetSizer(sizer);
    sizer->Layout();
    panel->Fit();
    return panel;
}

void MonitorPanel::msw_rescale()
{
    init_bitmap();

    /* side_tool rescale */
    m_side_tools->msw_rescale();

    m_tabpanel->Rescale();
    m_status_add_machine_panel->msw_rescale();
    m_status_info_panel->msw_rescale();
    m_media_file_panel->Rescale();
    m_upgrade_panel->msw_rescale();
    m_hms_panel->msw_rescale();
    
    Layout();
    Refresh();
}

void MonitorPanel::select_machine(std::string machine_sn)
{
    wxCommandEvent *event = new wxCommandEvent(wxEVT_COMMAND_CHOICE_SELECTED);
    event->SetString(machine_sn);
    wxQueueEvent(this, event);
}

void MonitorPanel::on_update_all(wxMouseEvent &event)
{
    update_all();
    Layout();
    Refresh();
}

 void MonitorPanel::on_timer(wxTimerEvent& event)
{
    update_all();

    Layout();
    Refresh();
}

 void MonitorPanel::on_select_printer(wxCommandEvent& event)
{
    Slic3r::AccountManager* c = Slic3r::GUI::wxGetApp().getAccountManager();
    c->set_monitor_machine(event.GetString().ToStdString());

    set_default();
    update_all();

    Layout();
    Refresh();
}

void MonitorPanel::on_printer_clicked(wxMouseEvent &event)
{
    Slic3r::AccountManager *account_manager = Slic3r::GUI::wxGetApp().getAccountManager();

    // BBS check user login status
    if (!account_manager->is_user_login()) {
        // tips to login
        return;
    }

    /* query print info */
    SelectMachinePopup *m_select_machine = new SelectMachinePopup(this);


    wxPoint pos = m_side_tools->ClientToScreen(wxPoint(0, 0));
    pos.y += m_side_tools->GetRect().height;
    m_select_machine->Position(pos, wxSize(0, 0));
    m_select_machine->Popup();
}


void MonitorPanel::on_size(wxSizeEvent &event)
{
    // limit size
    if (!wxGetApp().mainframe) return;

    //wxGetApp().mainframe->SetMinSize(wxSize(FromDIP(1500), FromDIP(700)));
    Layout();
    Refresh();
}

 void MonitorPanel::update_status(MachineObject* obj)
{
    if (!obj) return;

    /* Update Device Info */
    wxString machine_name_text = wxString::Format("%s", from_u8(obj->dev_name));
    m_side_tools->set_current_printer_name(machine_name_text);

    /*
    wxString printing_status_text = wxString::Format("%s", obj->print_status);
    m_staticText_capacity_val->SetLabelText(printing_status_text);
    */

    // update wifi signal image
    int wifi_signal_val = 0;
    if (!obj->wifi_signal.empty() && boost::ends_with(obj->wifi_signal, "dBm")) {
        try {
            wifi_signal_val = std::stoi(obj->wifi_signal.substr(0, obj->wifi_signal.size() - 3));
        }
        catch (...) {
            ;
        }

        if (last_wifi_signal != wifi_signal_val) {
            if (wifi_signal_val > -45) {
                m_side_tools->set_current_printer_sigin(WifiSignal::STRONG);
            }
            else if (wifi_signal_val <= -45 && wifi_signal_val >= -60) {
                m_side_tools->set_current_printer_sigin(WifiSignal::MIDDLE);
            }
            else {
                m_side_tools->set_current_printer_sigin(WifiSignal::WEAK);
            }
        }
        last_wifi_signal = wifi_signal_val;
    }
    else {
        m_side_tools->set_current_printer_sigin(WifiSignal::WEAK);
    }
}

void MonitorPanel::update_all()
{
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();

    //BBS check user login status
    if (!account_manager->is_user_login()) {
        show_status((int)MONITOR_NO_PRINTER);
        return;
    }

    obj = account_manager->get_default_machine();
    m_status_info_panel->obj = obj;
    m_upgrade_panel->update(obj);
    m_status_info_panel->m_media_play_ctrl->SetMachineObject(IsShown() ? obj : nullptr);
    //m_media_file_panel->SetMachineObject(obj);

    if (!obj) { 
        update_side_panel();
    }


    if (!obj) {
        show_status((int)MONITOR_NO_PRINTER);
        return;
    }

    if (!obj->is_connected()) {
        int server_status = account_manager->is_mqtt_connected() ? 0 : (int)MONITOR_DISCONNECTED_SERVER;
        show_status((int) MONITOR_DISCONNECTED + server_status);
        return;
    }

    show_status(MONITOR_NORMAL);

    update_status(obj);

    if (m_status_info_panel->IsShown()) {
        m_status_info_panel->update(obj);
    }

    if (m_hms_panel->IsShown()) {
        m_hms_panel->update(obj);
    }

    if (m_upgrade_panel->IsShown()) {
        m_upgrade_panel->update(obj);
    }
}

bool MonitorPanel::Show(bool show)
{
    Slic3r::AccountManager *c = Slic3r::GUI::wxGetApp().getAccountManager();
    if (show) {
        m_refresh_timer->Stop();
        m_refresh_timer->SetOwner(this);
        m_refresh_timer->Start(REFRESH_INTERVAL);
        wxPostEvent(this, wxTimerEvent());
        if (c)
            c->start_subscribe();

        //set a default machine when obj is null
        if (obj == nullptr && c) {
            c->load_last_machine();
        }
    }
    else {
        m_refresh_timer->Stop();
        m_status_info_panel->m_media_play_ctrl->SetMachineObject(nullptr);
        if (c)
            c->stop_subscribe();
    }
    return wxPanel::Show(show);
}

void MonitorPanel::update_side_panel() 
{
    Slic3r::AccountManager *account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    auto                    is_next_machine = false;
    for (auto it = account_manager->myBindMachineList.begin(); it != account_manager->myBindMachineList.end(); it++) {
        if (it->second && it->second->is_online()) {
            wxCommandEvent *event = new wxCommandEvent(wxEVT_COMMAND_CHOICE_SELECTED);
            event->SetString(it->second->dev_id);
            wxQueueEvent(this, event);
            is_next_machine = true;
            return;
        }
    }

    if (!is_next_machine) { m_side_tools->set_none_printer_mode(); }
}

void MonitorPanel::show_status(int status)
{
    if (!m_initialized) return;

    if (last_status == status)
        return;
    last_status = status;

    BOOST_LOG_TRIVIAL(trace) << "monitor: show_status = " << status;

    Freeze();
    if ((status & (int)MonitorStatus::MONITOR_NO_PRINTER) != 0) {
        set_default();
        //m_side_tools->set_none_printer_mode();
        m_status_info_panel->show_status(status);
        //m_tabpanel->RemovePage(0);
        //m_tabpanel->InsertNewPage(0, m_status_info_panel, _L("Status"), "", true);
        //m_tabpanel->InsertNewPage(0, m_status_add_machine_panel, _L("Status"), "", true);
        //m_tabpanel->SetSelection(0);
        m_tabpanel->Refresh();
        m_tabpanel->Layout();
    } else if (((status & (int)MonitorStatus::MONITOR_NORMAL) != 0) ||
        ((status & (int)MonitorStatus::MONITOR_DISCONNECTED) != 0) ||
        ((status & (int) MonitorStatus::MONITOR_DISCONNECTED_SERVER) != 0)
        ) {
        if (((status & (int) MonitorStatus::MONITOR_DISCONNECTED) != 0) || ((status & (int) MonitorStatus::MONITOR_DISCONNECTED_SERVER) != 0))
            m_side_tools->set_current_printer_sigin(WifiSignal::NONE);

        m_status_info_panel->show_status(status);
        //m_tabpanel->RemovePage(0);
        //m_tabpanel->InsertNewPage(0, m_status_info_panel, _L("Status"), "", true);
        //m_tabpanel->SetSelection(0);
        m_tabpanel->Refresh();
        m_tabpanel->Layout();
    }
    Layout();
    Thaw();
}

} // GUI
} // Slic3r
