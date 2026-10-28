#include "Tab.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"

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
#include "DeviceWeb/DeviceWebHost.hpp"
#include "Plater.hpp"
#include "BindDialog.hpp"

#include "DeviceCore/DevFilaSystem.h"
#include "DeviceCore/DevManager.h"
#include "DeviceCore/DevHMSQuery.h"
#include "fila_manager/wgtFilaManagerCloudDispatcher.h"
#include "libslic3r/Log.hpp"

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
    m_button_add_machine = new Button(this, "", "monitor_add_machine", FromDIP(24));
    m_button_add_machine->SetCornerRadius(FromDIP(12));
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
    : wxPanel(parent, id, pos, size, style),
    m_select_machine(SelectMachinePopup(this))
{
    init_bitmap();

    init_tabpanel();

    m_main_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_main_sizer->Add(m_tabpanel, 1, wxEXPAND | wxLEFT, 0);
    SetSizerAndFit(m_main_sizer);

    init_timer();

    m_side_tools->get_panel()->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(MonitorPanel::on_printer_clicked), NULL, this);

    Bind(wxEVT_TIMER, &MonitorPanel::on_timer, this);
    Bind(wxEVT_SIZE, &MonitorPanel::on_size, this);
    Bind(wxEVT_COMMAND_CHOICE_SELECTED, &MonitorPanel::on_select_printer, this);

    m_select_machine.Bind(EVT_FINISHED_UPDATE_MACHINE_LIST, [this](wxCommandEvent& e) {
        m_side_tools->start_interval();
        });

}

MonitorPanel::~MonitorPanel()
{
    m_side_tools->get_panel()->Disconnect(wxEVT_LEFT_DOWN, wxMouseEventHandler(MonitorPanel::on_printer_clicked), NULL, this);

    if (m_refresh_timer)
        m_refresh_timer->Stop();
    delete m_refresh_timer;
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
    if (update_flag) { update_all();}
}

void MonitorPanel::init_tabpanel()
{
    m_side_tools = new SideTools(this, wxID_ANY);
    wxBoxSizer* sizer_side_tools = new wxBoxSizer(wxVERTICAL);
    sizer_side_tools->Add(m_side_tools, 1, wxEXPAND, 0);
    m_tabpanel             = new Tabbook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, sizer_side_tools, wxNB_LEFT | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME);
    m_side_tools->set_table_panel(m_tabpanel);
    m_tabpanel->SetBackgroundColour(wxColour("#FEFFFF"));
    m_tabpanel->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
        auto page = m_tabpanel->GetCurrentPage();
        if (page == m_media_file_panel) {
            auto title = m_tabpanel->GetPageText(m_tabpanel->GetSelection());
            m_media_file_panel->SwitchStorage(title == _L("Storage"));
        }
        page->SetFocus();
        update_all();
        if (page == m_print_queue_panel) {
            m_print_queue_panel->NavigateTo("/device_page/print_queue", true);
            update_print_queue_device(obj);
        }
        BOOST_LOG_TRIVIAL(info) << "select :" << get_string_from_tab(PrinterTab(m_tabpanel->GetSelection()))
            << " wxString:" << m_tabpanel->GetPageText(m_tabpanel->GetSelection()).ToStdString();
        NetworkAgent* agent = GUI::wxGetApp().getAgent();
        if (agent) {
            std::string name = get_string_from_tab(PrinterTab(m_tabpanel->GetSelection()));
            if (name != "") {
                std::string value = "";
                agent->track_get_property(name, value);
                int count = 0;
                if (value != "") {
                    try {
                        count = std::stoi(value);
                    }
                    catch (...) {
                        BOOST_LOG_TRIVIAL(error) << "String to integer error!";
                        count = 0;
                    }
                }
                agent->track_update_property(name, std::to_string(++count));
            }
        }
        }, m_tabpanel->GetId());

    //m_status_add_machine_panel = new AddMachinePanel(m_tabpanel);
    m_status_info_panel        = new StatusPanel(m_tabpanel);
    m_tabpanel->AddPage(m_status_info_panel, _L("Status"), "", true);

#if STUDIO_ENABLE_PRINT_QUENE
    m_print_queue_panel = new DeviceWebHost(m_tabpanel, DeviceWebHostMode::PrintQueue,
                                            "/device_page/print_queue", true);
    m_tabpanel->AddPage(m_print_queue_panel, _L("Print queue"), "", false);
#endif

    m_media_file_panel = new MediaFilePanel(m_tabpanel);
    m_tabpanel->AddPage(m_media_file_panel, _L("Storage"), "", false);
    //m_tabpanel->AddPage(m_media_file_panel, _L("Internal Storage"), "", false);

    m_upgrade_panel = new UpgradePanel(m_tabpanel);
    m_tabpanel->AddPage(m_upgrade_panel, _CTX(L_CONTEXT("Update", "Firmware"), "Firmware"), "", false);

    m_hms_panel = new HMSPanel(m_tabpanel);
    m_tabpanel->AddPage(m_hms_panel, _L("Assistant(HMS)"),    "", false);

    m_initialized = true;
    show_status((int)MonitorStatus::MONITOR_NO_PRINTER);
}

void MonitorPanel::set_default()
{
    obj = nullptr;
    last_conn_type = "undefined";

    /* reset status panel*/
    m_status_info_panel->set_default();

    /* reset side tool*/
    //m_bitmap_wifi_signal->SetBitmap(wxNullBitmap);
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

void MonitorPanel::on_sys_color_changed()
{
    m_status_info_panel->on_sys_color_changed();
    m_upgrade_panel->on_sys_color_changed();
    m_media_file_panel->Rescale();
}

void MonitorPanel::msw_rescale()
{
    init_bitmap();

    /* side_tool rescale */
    m_side_tools->msw_rescale();
    m_tabpanel->Rescale();
    //m_status_add_machine_panel->msw_rescale();
    m_status_info_panel->msw_rescale();
    m_media_file_panel->Rescale();
    m_upgrade_panel->msw_rescale();
    m_hms_panel->msw_rescale();

    Layout();
    Refresh();
}

void MonitorPanel::on_activate()
{
    DeviceManager* device_manager = wxGetApp().getDeviceManager();
    MachineObject* machine = device_manager ? device_manager->get_selected_machine() : nullptr;
    if (!machine || !machine->GetFilaSystem())
        return;

    const auto fila_system = machine->GetFilaSystem();
    const auto& settings = fila_system->GetAmsSystemSetting();
    if (!settings.SupportsFilamentTabAutoReadRfid() || !settings.HasFilamentTabAutoReadRfidState())
        return;

    const auto rfid_refresh_ctrl = fila_system->GetAmsRfidRefreshCtrl();
    if (!rfid_refresh_ctrl) {
        return;
    }

    return;// reset story-7043553223
    rfid_refresh_ctrl->TriggerRefresh();
}

void MonitorPanel::select_machine(std::string machine_sn)
{
    wxCommandEvent *event = new wxCommandEvent(wxEVT_COMMAND_CHOICE_SELECTED);
    event->SetString(machine_sn);
    wxQueueEvent(this, event);
}


void MonitorPanel::on_timer(wxTimerEvent& event)
{
    if (update_flag) {
        update_all();
        //Layout();
    }
}

void MonitorPanel::on_select_printer(wxCommandEvent& event)
{
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    const std::string target_machine = event.GetString().ToStdString();
    if (dev->get_selected_machine() && (dev->get_selected_machine()->get_dev_id() != target_machine) && m_hms_panel) {
        m_hms_panel->clear_hms_tag();
    }

    if (!dev->set_selected_machine(target_machine))
        return;

    set_default();
    update_all();

    MachineObject *obj_ = dev->get_selected_machine();
    if (m_tabpanel->GetCurrentPage() == m_print_queue_panel) {
        update_print_queue_device(obj_);
    }
    if (obj_) {
        obj_->GetCalib()->ResetCalibVersion();
        obj_->GetCalib()->ResetPAHistory();
        obj_->GetCalib()->ResetFlowRateResult();
        GUI::wxGetApp().sidebar().update_sync_status(obj_);
    }

    if (IsShownOnScreen())
        on_activate();

    Layout();
}

void MonitorPanel::on_printer_clicked(wxMouseEvent &event)
{
    auto mouse_pos = ClientToScreen(event.GetPosition());
    wxPoint rect = m_side_tools->ClientToScreen(wxPoint(0, 0));

    if (!m_side_tools->is_in_interval()) {
        wxPoint pos = m_side_tools->ClientToScreen(wxPoint(0, 0));
        pos.y += m_side_tools->GetRect().height;
        //pos.x = pos.x < 0? 0:pos.x;
        m_select_machine.Move(pos);

#ifdef __linux__
        m_select_machine.SetSize(wxSize(m_side_tools->GetSize().x, -1));
        m_select_machine.SetMaxSize(wxSize(m_side_tools->GetSize().x, -1));
        m_select_machine.SetMinSize(wxSize(m_side_tools->GetSize().x, -1));
#endif

        m_select_machine.Popup();
    }
}

void MonitorPanel::on_size(wxSizeEvent &event)
{
    Layout();
    //event.Skip();
    //Refresh();
}

void MonitorPanel::update_all()
{
    if (!m_initialized)
        return;

    NetworkAgent* m_agent = wxGetApp().getAgent();
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;
    obj = dev->get_selected_machine();

    if (!obj) {
        show_status((int)MONITOR_NO_PRINTER);
        m_hms_panel->clear_hms_tag();
        m_tabpanel->GetBtnsListCtrl()->showNewTag(PT_HMS, 0);
        m_status_info_panel->m_media_play_ctrl->SetMachineObject(obj);
        m_status_info_panel->update(obj);
        update_print_queue_device(nullptr);
        return;
    }

    if (m_tabpanel->GetCurrentPage() == m_print_queue_panel) {
        update_print_queue_device(obj);
    }

    if (obj->connection_type() != last_conn_type) {
        last_conn_type = obj->connection_type();
    }

    m_side_tools->update_status(obj);

    if (obj->is_connecting()) {
        show_status(MONITOR_CONNECTING);
        return;
    } else if (!obj->is_connected()) {
        int server_status = 0;
        // only disconnected server in cloud mode
        if (obj->connection_type() != "lan") {
            if (m_agent) {
                server_status = m_agent->is_server_connected() ? 0 : (int)MONITOR_DISCONNECTED_SERVER;
            }
        }
        show_status((int) MONITOR_DISCONNECTED + server_status);
        return;
    }

    show_status(MONITOR_NORMAL);

    auto current_page = m_tabpanel->GetCurrentPage();

    if (current_page == m_status_info_panel) {
        if (m_status_info_panel->IsShown()) {
            m_status_info_panel->sync_current_dev(obj);
            m_status_info_panel->m_media_play_ctrl->SetMachineObject(obj);
            m_status_info_panel->update(obj);
        }
    } else if (current_page == m_upgrade_panel) {
        m_upgrade_panel->update(obj);
    } else if (current_page == m_media_file_panel) {
        m_media_file_panel->UpdateByObj(obj);
    }

    if (current_page == m_hms_panel || (obj->GetHMS()->GetHMSItems().size() != m_hms_panel->temp_hms_list.size())) {
        m_hms_panel->update(obj);
    }

    update_hms_tag();
}

void MonitorPanel::update_print_queue_device(MachineObject* obj)
{
    if (!m_print_queue_panel) return;
    auto* vm = m_print_queue_panel->GetPrintQueueVM();
    if (!vm) return;
    vm->SetDeviceId(obj ? obj->get_dev_id() : std::string());
}

void MonitorPanel::update_hms_tag()
{
    m_tabpanel->GetBtnsListCtrl()->showNewTag(PT_HMS, (int)m_hms_panel->temp_hms_list.size());
}

bool MonitorPanel::Show(bool show)
{
#ifdef __APPLE__
    wxGetApp().mainframe->SetMinSize(wxGetApp().plater()->GetMinSize());
#endif
    NetworkAgent* m_agent = wxGetApp().getAgent();
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (show) {
        start_update();
        if (StatusPanel* sp = get_status_panel())
            sp->update_ctrl_area_masks();

        m_refresh_timer->Stop();
        m_refresh_timer->SetOwner(this);
        m_refresh_timer->Start(REFRESH_INTERVAL);
        if (update_flag) { update_all(); }

        if (dev) {
            //set a default machine when obj is null
            obj = dev->get_selected_machine();
            if (obj == nullptr) {
                dev->load_last_machine();
            } else {
                obj->reset_update_time();
            }
        }

    } else {
        stop_update();
        m_refresh_timer->Stop();
        if (StatusPanel* sp = get_status_panel())
            sp->hide_ctrl_area_overlays();
    }
    return wxPanel::Show(show);
}

void MonitorPanel::show_status(int status)
{
    if (!m_initialized) return;
    if (last_status == status)return;
    if ((last_status & (int)MonitorStatus::MONITOR_CONNECTING) != 0) {
        NetworkAgent* agent = wxGetApp().getAgent();
        json j;
        //j["dev_id"] = obj ? obj->get_dev_id() : "obj_nullptr";
        j["dev_id"] = "";
        if ((status & (int)MonitorStatus::MONITOR_DISCONNECTED) != 0) {
            j["result"] = "failed";
            if (agent) {
                agent->track_event("connect_dev", j.dump());
            }
        }
        else if ((status & (int)MonitorStatus::MONITOR_NORMAL) != 0) {
            j["result"] = "success";
            if (agent) {
                agent->track_event("connect_dev", j.dump());
            }
        }
    }
    last_status = status;

    BOOST_LOG_TRIVIAL(info) << "monitor: show_status = " << status;

    //Freeze();
    // update panels
    if (m_side_tools) { m_side_tools->show_status(status); };
    m_status_info_panel->show_status(status);
    m_hms_panel->show_status(status);
    m_upgrade_panel->show_status(status);

    if ((status & (int)MonitorStatus::MONITOR_NO_PRINTER) != 0) {
        set_default();
        m_tabpanel->Layout();
    } else if (((status & (int)MonitorStatus::MONITOR_NORMAL) != 0)
        || ((status & (int)MonitorStatus::MONITOR_DISCONNECTED) != 0)
        || ((status & (int) MonitorStatus::MONITOR_DISCONNECTED_SERVER) != 0)
        || ((status & (int)MonitorStatus::MONITOR_CONNECTING) != 0) )
    {

        if (((status & (int) MonitorStatus::MONITOR_DISCONNECTED) != 0)
            || ((status & (int) MonitorStatus::MONITOR_DISCONNECTED_SERVER) != 0)
            || ((status & (int)MonitorStatus::MONITOR_CONNECTING) != 0))
        {
            set_default();
        }
        m_tabpanel->Layout();
    }
    Layout();
    //Thaw();
}

std::string MonitorPanel::get_string_from_tab(PrinterTab tab)
{
    switch (tab) {
    case PT_STATUS :
        return "status";
    case PT_MEDIA:
        return "sd_card";
    case PT_UPDATE:
        return "update";
    case PT_HMS:
        return "HMS";
#if STUDIO_ENABLE_PRINT_QUENE
    case PT_PRINT_QUEUE:
        return "print_queue";
#endif
    case PT_DEBUG:
        return "debug";
    default:
        return "";
    }
    return "";
}

void MonitorPanel::jump_to_HMS()
{
    if (!this->IsShown())
        return;
    auto page = m_tabpanel->GetCurrentPage();
    if (page && page != m_hms_panel)
        m_tabpanel->SetSelection(PT_HMS);
}

void MonitorPanel::jump_to_Upgrade()
{
    if (this->IsShown()) {
        auto page = m_tabpanel->GetCurrentPage();
        if (page && page != m_upgrade_panel) {
            m_tabpanel->SetSelection(PT_UPDATE);
        }
    }
}

void MonitorPanel::jump_to_LiveView()
{
    if (!this->IsShown()) { return; }

    auto page = m_tabpanel->GetCurrentPage();
    if (page && page != m_hms_panel)
    {
        m_tabpanel->SetSelection(PT_STATUS);
    }

    m_status_info_panel->get_media_play_ctrl()->jump_to_play();
}

void MonitorPanel::jump_to_Rack()
{
    if (!this->IsShown()) {
        return;
    }

    auto page = m_tabpanel->GetCurrentPage();
    if (page && page != m_status_info_panel) {
        m_tabpanel->SetSelection(PT_STATUS);
    }

    m_status_info_panel->jump_to_Rack();
}

void MonitorPanel::jump_to_print_queue()
{
    if (!m_print_queue_panel) { return; }

    // Re-selecting the current page is a no-op, so refresh it explicitly.
    if (m_tabpanel->GetCurrentPage() == m_print_queue_panel) {
        m_print_queue_panel->NavigateTo("/device_page/print_queue", true);
        update_print_queue_device(obj);
    }
#if STUDIO_ENABLE_PRINT_QUENE
    else {
        m_tabpanel->SetSelection(PT_PRINT_QUEUE);
    }
#endif
}

} // GUI
} // Slic3r
