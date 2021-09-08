#include "DebugToolDialog.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <regex>
#include <wx/frame.h>
#include <wx/progdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/button.h>
#include <wx/dataview.h>
#include <wx/wupdlock.h>
#include <wx/debug.h>
#include <wx/msgdlg.h>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/format.hpp>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "libslic3r/AppConfig.hpp"
#include "NotificationManager.hpp"
#include "libslic3r/Time.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/Sftp.hpp"
#include "wxExtensions.hpp"
#include "libslic3r/PlaceholderParser.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <expat.h>

namespace pt = boost::property_tree;
typedef pt::ptree JSON;


namespace Slic3r {
namespace GUI {

    wxDECLARE_EVENT(EVT_PROGRESS, wxCommandEvent);
    wxDECLARE_EVENT(EVT_UPDATE_LIST, SimpleEvent);
    wxDECLARE_EVENT(EVT_REFRESH_LIST, SimpleEvent);
    wxDECLARE_EVENT(EVT_ERROR_MSG, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MQTT_SUCCESS, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MQTT_FAILED, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MQTT_LOST, wxCommandEvent);
    wxDECLARE_EVENT(EVT_PRINT_FINISH, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MESSAGE_ARRIVED, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MESSAGE_SENT, wxCommandEvent);
    wxDECLARE_EVENT(EVT_LOG_INFO, wxCommandEvent);


    wxDEFINE_EVENT(EVT_PROGRESS, wxCommandEvent);
    wxDEFINE_EVENT(EVT_UPDATE_LIST, SimpleEvent);
    wxDEFINE_EVENT(EVT_REFRESH_LIST, SimpleEvent);
    wxDEFINE_EVENT(EVT_ERROR_MSG, wxCommandEvent);
    wxDEFINE_EVENT(EVT_MQTT_SUCCESS, wxCommandEvent);
    wxDEFINE_EVENT(EVT_MQTT_FAILED, wxCommandEvent);
    wxDEFINE_EVENT(EVT_MQTT_LOST, wxCommandEvent);
    wxDEFINE_EVENT(EVT_PRINT_FINISH, wxCommandEvent);
    wxDEFINE_EVENT(EVT_MESSAGE_ARRIVED, wxCommandEvent);
    wxDEFINE_EVENT(EVT_MESSAGE_SENT, wxCommandEvent);
    wxDEFINE_EVENT(EVT_LOG_INFO, wxCommandEvent);


    std::string DebugToolDialog::_getNewLogFilename()
    {
        std::time_t t = std::time(0);
        std::tm* now_time = std::localtime(&t);
        std::stringstream buf;
        buf << std::put_time(now_time, "log_%a_%b_%d_%H_%M_%S.txt");
        std::string log_filename = buf.str();
        return log_filename;
    }

    /* upgrade */
    void XML_StartElementHandler(void* userData, const XML_Char* name, const XML_Char** atts) {
        if (strcmp(name, "a") == 0) {
            if (strcmp(atts[0], "href") == 0) {
                DebugToolDialog* dlg = (DebugToolDialog*)userData;
                std::string firmware_value(atts[1]);
                dlg->add_firmware(atts[1]);
            }
        }
    };
    void XML_EndElementHandler(void* userData, const XML_Char* name) {
        ;
    };
    void XML_CharacterDataHandler(void* userData, const XML_Char* s, int len) {
        ;
    };      

    void DebugToolDialog::add_firmware(std::string firmware)
    {
        UPGRADE_MODULE upgrade_module = (UPGRADE_MODULE)cb_upgrade_module->GetCurrentSelection();
        if (upgrade_module == MODULE_RK) {
            if ((firmware.find("update") == 0) && firmware.find("img") > 0) {
                upgrade_file_list.push_back(firmware);
            }
        }
        else if (upgrade_module == MODULE_MC) {
            if (firmware.find("mc") == 0) {
                upgrade_file_list.push_back(firmware);
            }
        }
        else if (upgrade_module == MODULE_TH) {
            if (firmware.find("th") == 0) {
                upgrade_file_list.push_back(firmware);
            }
        }
        else if (upgrade_module == MODULE_AMS) {
            if (firmware.find("ams") == 0) {
                upgrade_file_list.push_back(firmware);
            }
        }
        else if (upgrade_module == MODULE_OTA) {
            if (firmware.find("ota") == 0) {
                upgrade_file_list.push_back(firmware);
            }
        }
        else {
            upgrade_file_list.push_back(firmware);
        }
    }

    DebugToolDialog::DebugToolDialog(wxWindow* parent)
        : DPIDialog(parent, wxID_ANY, _L("Debug Tool"), wxDefaultPosition, wxSize(900, 820), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        m_deviceListTimer(new wxTimer(this, TIMER_ID)),
        m_timer(new wxTimer)
    {
        summary = new PrintSummary();

        // Layout Sizer
        top_sizer = new wxBoxSizer(wxVERTICAL);
        auto* conn_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto* h_sizer = new wxBoxSizer(wxHORIZONTAL);

        nb_main = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP);
        common_panel = new wxPanel(nb_main, wxID_ANY, wxDefaultPosition, wxDefaultSize);
        upgrade_panel = new wxPanel(nb_main, wxID_ANY, wxDefaultPosition, wxDefaultSize);
        run_gcode_panel = new wxPanel(nb_main, wxID_ANY, wxDefaultPosition, wxDefaultSize);
        ctrl_panel = new wxPanel(nb_main, wxID_ANY, wxDefaultPosition, wxDefaultSize);
        auto log_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);

        nb_main->AddPage(common_panel, "Common");
        nb_main->AddPage(run_gcode_panel, "Run Gcode");
        nb_main->AddPage(ctrl_panel, "Custom Ctrl");
        nb_main->AddPage(upgrade_panel, "Upgrade");

        init_device();
        init_common(common_panel);
        init_upgrade(upgrade_panel);
        init_gcode_run_file(run_gcode_panel);
        init_custom_ctrl(ctrl_panel);
        init_log_panel(log_panel);

        h_sizer->Add(nb_main);
        h_sizer->Add(log_panel, 1, wxALL | wxEXPAND, 0);
        
        init_layout();
        //top sizer
        top_sizer->Add(-1, 8);
        top_sizer->Add(conn_device_sizer, 0, wxLEFT | wxRIGHT | wxEXPAND, 0);
        top_sizer->Add(-1, 8);
        top_sizer->Add(h_sizer, 1, wxALL | wxEXPAND, 0);

        SetSizerAndFit(top_sizer);
        Refresh();

        SetMinSize(wxSize(600, 400));
        Layout();

        init_bind_handler();
}

void DebugToolDialog::init_layout()
{
    ;
}

void DebugToolDialog::init_bind_handler()
{
    Bind(wxEVT_TIMER, &DebugToolDialog::on_timer, this);
    Bind(EVT_UPDATE_LIST, &DebugToolDialog::on_update_list, this);
    Bind(EVT_REFRESH_LIST, &DebugToolDialog::on_refresh_list, this);
    Bind(EVT_ERROR_MSG, &DebugToolDialog::on_error_msg, this);
    Bind(EVT_MQTT_SUCCESS, &DebugToolDialog::on_mqtt_success, this);
    Bind(EVT_MQTT_FAILED, &DebugToolDialog::on_mqtt_failed, this);
    Bind(EVT_MQTT_LOST, &DebugToolDialog::on_mqtt_lost, this);
    Bind(EVT_PRINT_FINISH, &DebugToolDialog::on_print_end, this);
    Bind(EVT_MESSAGE_ARRIVED, &DebugToolDialog::on_message_arrived, this);
    Bind(EVT_MESSAGE_SENT, &DebugToolDialog::on_message_sent, this);
    Bind(EVT_LOG_INFO, &DebugToolDialog::on_log_info, this);
}

void DebugToolDialog::on_update_list(SimpleEvent& evt)
{
    Slic3r::DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!manager) return;

    wxArrayString new_items;
    std::vector<DeviceInfo*> list = manager->get_connected_device_info();

    std::vector<DeviceInfo*>::iterator it;
    for (it = list.begin(); it != list.end(); it++) {
        new_items.Add(get_device_list_item(*it));
        cb_device_list->Set(new_items);
    }
}

void DebugToolDialog::on_error_msg(wxCommandEvent& evt)
{
    wxMessageBox(evt.GetString());
}


void DebugToolDialog::on_mqtt_success(wxCommandEvent& evt)
{
    this->log_info("MQTT Connected! client=" + evt.GetString().ToStdString());
    btn_connect->SetLabelText("Disconnect");
    cb_device_list->Disable();
    btn_refresh_device_list->Disable();
}

void DebugToolDialog::on_mqtt_failed(wxCommandEvent& evt)
{
    this->log_info("MQTT Connect Failed! client=" + evt.GetString().ToStdString());
    btn_connect->SetLabelText("Connect");
    cb_device_list->Enable();
    btn_refresh_device_list->Enable();
}

void DebugToolDialog::on_mqtt_lost(wxCommandEvent& evt)
{
    this->log_info("MQTT Lost... client=" + evt.GetString().ToStdString());
    btn_connect->SetLabelText("Connect");
    cb_device_list->Enable();
    btn_refresh_device_list->Enable();
}

void DebugToolDialog::on_print_end(wxCommandEvent& evt)
{
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    Slic3r::DeviceManager* device_manager = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (account_manager && account_manager->is_user_login()) {
        AccountInfo* user_info = account_manager->get_curr_user();
        if (user_info) {
            summary->username = user_info->get_account();
            summary->user_id = user_info->get_user_id();
        }
        /* request to get version*/
        this->get_version();
    }
    
    /* get slicer version */
    summary->slicer_version = SLIC3R_RC_VERSION;

    /* get device_id */
    std::string device_id;
    if (get_current_device_id(device_id) < 0) {
        wxMessageBox("Get current device id failed!");
        return;
    }
    summary->device_id = device_id;

    /* get device_ip */
    summary->device_ip = device_manager->get_ip(device_id);

    /* get host ip */
    summary->host_ip = "192.168.0.1";

    /* get duration */
    if (summary->has_time_start) {
        std::time_t t = std::time(0);
        summary->duration = std::difftime(t, summary->time_start);
    }
    
    PrintResultDialog dlg(summary);
    dlg.ShowModal();
}

void DebugToolDialog::get_version() {
    pt::ptree root, info;
    info.put<int>("sequence_id", this->m_sequence_id++);
    info.put("command", "get_version");
    root.put_child("info", info);

    std::stringstream oss;
    pt::write_json(oss, root);
    std::string json_str = oss.str();
    json_str.erase(std::remove(json_str.begin(), json_str.end(), '\\'), json_str.end());
    this->publish_json(json_str);
}

void DebugToolDialog::init_device()
{
    cb_device_list = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize);
    cb_device_list->SetEditable(false);
    cb_device_list->Bind(wxEVT_COMBOBOX, &DebugToolDialog::on_select_device, this);
    btn_refresh_device_list = new wxButton(this, wxID_ANY, _L("Refresh"), wxDefaultPosition, wxDefaultSize);
    btn_refresh_device_list->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        this->refresh_device_list();
        });

    //label_device_status = new wxStaticText(this, wxID_ANY, _L("Unkown"), wxDefaultPosition, wxDefaultSize);
    btn_bind = new wxButton(this, wxID_ANY, _L("Bind"), wxDefaultPosition, wxDefaultSize);
    btn_bind->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
            std::string device_id;
            if (get_current_device_id(device_id) < 0) return;
            if (!account_manager->is_user_login()) {
                wxMessageBox("Please login first!");
                return;
            }

            account_manager->request_bind(device_id,
                [this, device_id]() {
                    std::string log = "Bind device=" + device_id + " success!";
                    auto evt = new wxCommandEvent(EVT_LOG_INFO, this->GetId());
                    evt->SetString(log);
                    wxQueueEvent(this, evt);

                    this->refresh_device_list();
                });
        });
    btn_unbind = new wxButton(this, wxID_ANY, _L("Unbind"), wxDefaultPosition, wxDefaultSize);
    btn_unbind->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
            std::string device_id;
            if (get_current_device_id(device_id) < 0) return;
            if (!account_manager->is_user_login()) {
                std::string log = "Please login first!";
                auto evt = new wxCommandEvent(EVT_LOG_INFO, this->GetId());
                evt->SetString(log);
                wxQueueEvent(this, evt);
                return;
            }

            account_manager->request_unbind(device_id,
                [this, device_id]() {
                    std::string log = "Unbind device=" + device_id + " ok!";
                    auto evt = new wxCommandEvent(EVT_LOG_INFO, this->GetId());
                    evt->SetString(log);
                    wxQueueEvent(this, evt);

                    this->refresh_device_list();
                });

            
        });
    btn_connect = new wxButton(this, wxID_ANY, _L("Connect"), wxDefaultPosition, wxDefaultSize);
    btn_connect->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        if (btn_connect->GetLabelText().ToStdString().compare("Connect") == 0) {
            Slic3r::CommuBackend* backend = Slic3r::GUI::wxGetApp().getCommuBackend();
            Slic3r::DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();

            std::string device = cb_device_list->GetValue().ToStdString();
            std::string content = cb_device_list->GetValue().ToStdString();
            size_t start = content.find_last_of("(") + 1;
            std::string device_id = content.substr(start, content.find_last_of(")") - start);

            std::string ip_str = manager->get_ip(device_id);
            Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
            std::string user_id = account_manager->get_user_id();
            if (ip_str.empty()) {
                wxMessageBox("Invalid ip string");
                return;
            }
            backend->connect_to_client(ip_str, user_id, device_id,
                //success
                [this](std::string name) {
                    auto evt = new wxCommandEvent(EVT_MQTT_SUCCESS, this->GetId());
                    evt->SetString(name);
                    wxQueueEvent(this, evt);
                },
                //failed
                    [this](std::string name) {
                    auto evt = new wxCommandEvent(EVT_MQTT_FAILED, this->GetId());
                    evt->SetString(name);
                    wxQueueEvent(this, evt);
                },
                    //lost
                    [this](std::string name) {
                    auto evt = new wxCommandEvent(EVT_MQTT_LOST, this->GetId());
                    evt->SetString(name);
                    wxQueueEvent(this, evt);
                });
        }
        else {
            Slic3r::CommuBackend* backend = Slic3r::GUI::wxGetApp().getCommuBackend();
            backend->disconnect_to_client();
            btn_connect->SetLabelText("Connect");
            cb_device_list->Enable();
            btn_refresh_device_list->Enable();
        }
    });

    label_device_list = new wxStaticText(this, wxID_ANY, _L("Device List:"), wxDefaultPosition, wxDefaultSize);

    conn_device_sizer = new wxBoxSizer(wxHORIZONTAL);
    conn_device_sizer->Add(label_device_list, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, SPACING);
    conn_device_sizer->Add(cb_device_list, 1, wxALL | wxEXPAND, SPACING);
    conn_device_sizer->Add(btn_refresh_device_list, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, SPACING);
    conn_device_sizer->Add(btn_bind, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, SPACING);
    conn_device_sizer->Add(btn_unbind, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, SPACING);
    conn_device_sizer->Add(btn_connect, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, SPACING);
}

void DebugToolDialog::init_common(wxWindow* parent)
{
    auto sizer = new wxBoxSizer(wxVERTICAL);
    btn_get_version = new wxButton(parent, wxID_ANY, _L("Get Version"));
    btn_get_version->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->get_version();
        });
    sizer->Add(-1, 8);
    sizer->Add(btn_get_version, 0, wxLEFT | wxALIGN_LEFT);
    parent->SetSizer(sizer);
}

void DebugToolDialog::init_upgrade(wxWindow* parent)
{
    btn_refresh_upgrade_list = new wxButton(parent, wxID_ANY, _L("Refresh"), wxDefaultPosition, wxDefaultSize);
    btn_refresh_upgrade_list->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->refresh_firmware_list(true);
        });

    btn_upgrade_firmware = new wxButton(parent, wxID_ANY, _L("Upgrade Firmware"), wxDefaultPosition, wxSize(180, -1));
    btn_upgrade_firmware->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        std::string firmware_name = cb_upgrade_firmware->GetValue().ToStdString();

        if (firmware_name.empty()) return;

        if (cb_upgrade_module->GetValue().compare("") == 0) {
            wxMessageBox("Please select a module");
            return;
        }
        if (cb_upgrade_mode->GetValue().compare("") == 0) {
            wxMessageBox("Please select a upgrade mode");
            return;
        }
        UPGRADE_MODULE upgrade_module = (UPGRADE_MODULE)cb_upgrade_module->GetCurrentSelection();
        UPGRADE_MODE upgrade_mode = (UPGRADE_MODE)cb_upgrade_mode->GetCurrentSelection();
        std::string dst_url = (boost::format("%1%%2%%3%%4%") % UPGRADE_URL % upgrade_post_url[upgrade_module] % upgrade_mode_name[upgrade_mode] %firmware_name).str();
        std::string version = firmware_name.substr(firmware_name.rfind("-v") + 2, 11);

        // send upgrade
        pt::ptree root, upgrade;
        upgrade.put<int>("sequence_id", this->m_sequence_id++);
        upgrade.put("command", "start");
        upgrade.put("url", dst_url);
        upgrade.put("module", upgrade_module_name[upgrade_module]);
        upgrade.put("version", version);
        root.put_child("upgrade", upgrade);

        std::stringstream oss;
        pt::write_json(oss, root);
        std::string json_str = oss.str();
        json_str.erase(std::remove(json_str.begin(), json_str.end(), '\\'), json_str.end());
        if (this->publish_json(json_str) == 0) {
            wxMessageBox("Start Upgrading (Please wait several minutes)...");
        }
        });
    wxArrayString module_items;
    module_items.Add(_L("RK1126(AP)"));
    module_items.Add(_L("MC"));
    module_items.Add(_L("TH"));
    module_items.Add(_L("AMS"));
    module_items.Add(_L("OTA"));
    cb_upgrade_module = new wxComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, module_items);
    cb_upgrade_module->SetEditable(false);
    cb_upgrade_module->SetSelection(0);
    cb_upgrade_firmware = new wxComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(300, -1));
    cb_upgrade_firmware->SetEditable(false);

    wxArrayString mode_items;
    mode_items.Add(_L("DailyBuild"));
    mode_items.Add(_L("Release"));
    mode_items.Add(_L("Debug"));

    cb_upgrade_mode = new wxComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, mode_items);
    cb_upgrade_mode->SetEditable(false);
    cb_upgrade_mode->SetSelection(0);

    label_upgrade_filename = new wxStaticText(parent, wxID_ANY, _L("File Path:"), wxDefaultPosition, wxDefaultSize);
    auto label_upgrade_status = new wxStaticText(parent, wxID_ANY, _L("Status:"), wxDefaultPosition, wxDefaultSize);
    label_upgrade_status_val = new wxStaticText(parent, wxID_ANY, _L("NA"), wxDefaultPosition, wxDefaultSize);
    auto label_upgrade_progress = new wxStaticText(parent, wxID_ANY, _L("Progress:"), wxDefaultPosition, wxDefaultSize);
    label_upgrade_progress_val = new wxStaticText(parent, wxID_ANY, _L("NA"), wxDefaultPosition, wxDefaultSize);
    auto label_upgrade_module = new wxStaticText(parent, wxID_ANY, _L("Module:"), wxDefaultPosition, wxDefaultSize);
    label_upgrade_module_val = new wxStaticText(parent, wxID_ANY, _L("NA"), wxDefaultPosition, wxDefaultSize);
    auto label_upgrade_message = new wxStaticText(parent, wxID_ANY, _L("Info:"), wxDefaultPosition, wxDefaultSize);
    label_upgrade_message_val = new wxStaticText(parent, wxID_ANY, _L("NA"), wxDefaultPosition, wxDefaultSize);

    /* select upgrade file*/
    auto line_sizer = new wxBoxSizer(wxHORIZONTAL);
    line_sizer->Add(btn_upgrade_firmware, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, SPACING);
    line_sizer->Add(label_upgrade_filename, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, SPACING);
    line_sizer->Add(cb_upgrade_firmware, 1, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, SPACING);
    line_sizer->Add(cb_upgrade_module, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, SPACING);
    line_sizer->Add(cb_upgrade_mode, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, SPACING);
    line_sizer->Add(btn_refresh_upgrade_list, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, SPACING);

    /* upgrade status */
    auto grid_sizer = new wxGridSizer(4, 2, 5, 5);
    grid_sizer->Add(label_upgrade_status, 0, wxRIGHT | wxALIGN_RIGHT);
    grid_sizer->Add(label_upgrade_status_val, 0, wxLEFT | wxALIGN_LEFT);
    grid_sizer->Add(label_upgrade_progress, 0, wxRIGHT | wxALIGN_RIGHT);
    grid_sizer->Add(label_upgrade_progress_val, 0, wxLEFT | wxALIGN_LEFT);
    grid_sizer->Add(label_upgrade_module, 0, wxRIGHT | wxALIGN_RIGHT);
    grid_sizer->Add(label_upgrade_module_val, 0, wxLEFT | wxALIGN_LEFT);
    grid_sizer->Add(label_upgrade_message, 0, wxRIGHT | wxALIGN_RIGHT);
    grid_sizer->Add(label_upgrade_message_val, 0, wxLEFT | wxALIGN_LEFT);

    auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(-1, 8);
    sizer->Add(line_sizer, 0, wxLEFT | wxTOP, SPACING);
    sizer->Add(grid_sizer, 0, wxLEFT | wxTOP, SPACING);

    parent->SetSizer(sizer);
}

void DebugToolDialog::init_gcode_run_file(wxWindow *parent)
{
    selectGcodeDialog = new wxFileDialog(parent, "Open Gcode File", "", "", "Gcode files(*.gcode)|*.gcode", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    Bind(EVT_PROGRESS, [this](wxCommandEvent& evt) {
        std::string text = "upload:";
        text += std::to_string(evt.GetInt()) + "%";
        this->label_progress->SetLabelText(text);
        });

    btn_run_gcode = new wxButton(parent, wxID_ANY, _L("Run Gcode"), wxDefaultPosition, wxSize(180, -1));
    btn_run_gcode->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        Slic3r::DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();

        std::string filepath = txt_gcode_filename->GetValue().ToStdString();
        int name_start = filepath.find_last_of("\\");
        if (name_start <= 0) {
            wxMessageBox("init_gcode_run_file name start");
            BOOST_LOG_TRIVIAL(trace) << "gcode file " << filepath;
            return;
        }

        std::string filename = filepath.substr(name_start + 1, filepath.size());
        std::string dstname = "/data/" + filename;

        std::string device = cb_device_list->GetValue().ToStdString();
        size_t start = device.find_last_of("(") + 1;
        std::string device_id = device.substr(start, device.find_last_of(")") - start);
        std::string ip_str = manager->get_ip(device_id);

        /* record filename */
        summary->print_filename = dstname;

        /* record current time */
        summary->time_start = std::time(0);
        std::tm* now_time = std::localtime(&summary->time_start);
        std::stringstream buf;
        buf << std::put_time(now_time, "%a %b %d %H:%M:%S");
        summary->start_time = buf.str();
        summary->has_time_start = true;


        Sftp sftp = Sftp::upload(ip_str, dstname, filepath, "root", "root");
        sftp.on_complete([this, filename, dstname](std::string body) {
                /* boost::filesystem::file_size not right*/
                std::string text = "upload:100%";
                this->label_progress->SetLabelText(text);
                BOOST_LOG_TRIVIAL(trace) << "transform gcode=" << filename << " ok!";
                /* send json command */
                pt::ptree root, print;
                print.put("sequence_id", m_sequence_id++);
                print.put("command", "gcode_file");
                print.put<std::string>("param", dstname);
                root.put_child("print", print);
                std::stringstream oss;
                pt::write_json(oss, root);
                std::string json_str = oss.str();
                json_str.erase(std::remove(json_str.begin(), json_str.end(), '\\'), json_str.end());
                int res = this->publish_json(json_str);
                if (res == 0) {
                    wxMessageBox("Run Gcode ...");
                }
            })
            .on_error([this, filename](std::string error) {
                BOOST_LOG_TRIVIAL(trace) << "transform gcode=" << filename << " error, error=" << error;
                wxMessageBox("error:" + error);
                })
                .on_progress([this](Slic3r::Sftp::Progress progress, bool& cancel) {
                    BOOST_LOG_TRIVIAL(trace) << " progress:" << progress.ulnow << "/" << progress.ultotal;
                    int percent = 0;
                    if (progress.ultotal != 0) {
                        percent = progress.ulnow * 100 / progress.ultotal;
                    }

                    auto evt = new wxCommandEvent(EVT_PROGRESS, this->GetId());
                    evt->SetInt(percent);
                    wxQueueEvent(this, evt);
            })
            .perform();
        });

    label_gcode_filename = new wxStaticText(parent, wxID_ANY, _L("File Path:"), wxDefaultPosition, wxDefaultSize);
    label_progress = new wxStaticText(parent, wxID_ANY, _L("upload: 0%"), wxDefaultPosition, wxSize(160, -1));

    btn_select_gcode_file = new wxButton(parent, wxID_ANY, _L("Select File"), wxDefaultPosition, wxDefaultSize);
    btn_select_gcode_file->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        if (this->selectGcodeDialog->ShowModal() == wxID_CANCEL) return;

        txt_gcode_filename->SetValue(this->selectGcodeDialog->GetPath());
        txt_gcode_filename->SetFocus();
        this->SetFocus();
        });

    txt_gcode_filename = new wxTextCtrl(parent, wxID_ANY, _L(""), wxDefaultPosition, wxSize(300, -1));

    btn_abort_print = new  wxButton(parent, wxID_ANY, _L("Abort"), wxDefaultPosition, wxDefaultSize);
    btn_abort_print->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M0\n");
        auto et = new wxCommandEvent(EVT_PRINT_FINISH, this->GetId());
        et->SetInt(0);
        wxQueueEvent(this, et);
        });

    auto run_gcode_sizer = new wxBoxSizer(wxHORIZONTAL);
    run_gcode_sizer->Add(btn_run_gcode, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, SPACING);
    run_gcode_sizer->Add(label_gcode_filename, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, SPACING);
    run_gcode_sizer->Add(txt_gcode_filename, 1, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, SPACING);
    run_gcode_sizer->Add(btn_select_gcode_file, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, SPACING);
    run_gcode_sizer->Add(btn_abort_print, 0, wxALIGN_RIGHT | wxRIGHT | wxALIGN_CENTER_VERTICAL, SPACING);
    run_gcode_sizer->Add(label_progress, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, SPACING);
    auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(-1, 8);
    sizer->Add(run_gcode_sizer, 0, wxLEFT, 0);
    parent->SetSizer(sizer);
}

void DebugToolDialog::init_custom_ctrl(wxWindow* parent)
{
    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    auto control_sizer = new wxBoxSizer(wxVERTICAL);

    init_gcode_control(parent, control_sizer);
    sizer->Add(-1, 8);
    sizer->Add(control_sizer, 0, wxLEFT | wxALIGN_LEFT, 0);

    init_gcode_custom(parent, sizer);
    
    parent->SetSizer(sizer);
}

void DebugToolDialog::init_log_panel(wxWindow* parent)
{
    auto sizer = new wxBoxSizer(wxVERTICAL);
    label_output_string = new wxStaticText(parent, wxID_ANY, _L("Log Info:"), wxDefaultPosition, wxDefaultSize);
    txt_string_info = new wxTextCtrl(parent, wxID_ANY, _L(""), wxDefaultPosition, wxSize(300, -1), wxTE_MULTILINE);
    sizer->Add(label_output_string, 0);
    sizer->Add(txt_string_info, 1, wxALL | wxEXPAND);
    parent->SetSizer(sizer);
}

std::string DebugToolDialog::switch_ams_gcode(std::string t)
{
    Slic3r::Print& print = wxGetApp().plater()->get_partplate_list().get_current_fff_print();
    PlaceholderParser m_placeholder_parser;
    m_placeholder_parser = print.placeholder_parser();
    PlaceholderParser::ContextData      m_placeholder_parser_context;

    const PrintConfig& print_config = print.config();
    DynamicConfig dyn_config;
    dyn_config.set_key_value("previous_extruder", new ConfigOptionInt(-1));
    dyn_config.set_key_value("next_extruder",     new ConfigOptionInt(atoi(t.c_str())));
    dyn_config.set_key_value("layer_num",         new ConfigOptionInt(0));
    dyn_config.set_key_value("layer_z",           new ConfigOptionFloat(0.3));
    dyn_config.set_key_value("max_layer_z",       new ConfigOptionFloat(10.));
    dyn_config.set_key_value("use_relative_e_distances", new ConfigOptionBool(1));
    dyn_config.set_key_value("toolchange_count", new ConfigOptionInt(1));
    dyn_config.set_key_value("fan_speed", new ConfigOptionInt(0));
    dyn_config.set_key_value("old_retract_length", new ConfigOptionFloat(2.));
    dyn_config.set_key_value("new_retract_length", new ConfigOptionFloat(2.));
    dyn_config.set_key_value("old_retract_length_toolchange", new ConfigOptionFloat(3.0));
    dyn_config.set_key_value("new_retract_length_toolchange", new ConfigOptionFloat(3.0));
    dyn_config.set_key_value("old_filament_temp", new ConfigOptionInt(210));
    dyn_config.set_key_value("new_filament_temp", new ConfigOptionInt(210));
    dyn_config.set_key_value("x_after_toolchange", new ConfigOptionFloat(50.));
    dyn_config.set_key_value("y_after_toolchange", new ConfigOptionFloat(50.));
    dyn_config.set_key_value("z_after_toolchange", new ConfigOptionFloat(10.));

    try {
        std::string parsed_command = m_placeholder_parser.process(print_config.toolchange_gcode.value, std::stoi(t.c_str()), &dyn_config, &m_placeholder_parser_context);
        // config xyz coordinate mode
        parsed_command = "G90\n" + parsed_command;
        std::regex match_pattern(";.*\n");
        std::string replace_pattern = "\n";
        char result[1024] = { 0 };
        std::regex_replace(result, parsed_command.begin(), parsed_command.end(), match_pattern, replace_pattern);
        result[1023] = 0;
        return result;
    }
    catch (Exception& e) {
        wxMessageBox(e.what());
        return "";
    }
}

void DebugToolDialog::init_gcode_control(wxWindow* parent, wxBoxSizer* sizer)
{
    btn_set_x_pos_0_1 = new wxButton(parent, wxID_ANY, _L("X+0.1"), wxDefaultPosition, wxDefaultSize);
    btn_set_x_pos_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 X0.1 F3000 \n");
        });
    btn_set_x_pos_1_0 = new wxButton(parent, wxID_ANY, _L("X+1.0"), wxDefaultPosition, wxDefaultSize);
    btn_set_x_pos_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 X1.0 F3000 \n");
        });
    btn_set_x_pos_10_0 = new wxButton(parent, wxID_ANY, _L("X+10.0"), wxDefaultPosition, wxDefaultSize);
    btn_set_x_pos_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 X10.0 F3000 \n");
        });
    btn_set_x_neg_0_1 = new wxButton(parent, wxID_ANY, _L("X-0.1"), wxDefaultPosition, wxDefaultSize);
    btn_set_x_neg_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 X-0.1 F3000 \n");
        });
    btn_set_x_neg_1_0 = new wxButton(parent, wxID_ANY, _L("X-1.0"), wxDefaultPosition, wxDefaultSize);
    btn_set_x_neg_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 X-1.0 F3000 \n");
        });
    btn_set_x_neg_10_0 = new wxButton(parent, wxID_ANY, _L("X-10.0"), wxDefaultPosition, wxDefaultSize);
    btn_set_x_neg_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 X-10.0 F3000 \n");
        });
    btn_set_y_pos_0_1 = new wxButton(parent, wxID_ANY, _L("Y+0.1"), wxDefaultPosition, wxDefaultSize);
    btn_set_y_pos_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Y0.1 F3000 \n");
        });
    btn_set_y_pos_1_0 = new wxButton(parent, wxID_ANY, _L("Y+1.0"), wxDefaultPosition, wxDefaultSize);
    btn_set_y_pos_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Y1.0 F3000 \n");
        });
    btn_set_y_pos_10_0 = new wxButton(parent, wxID_ANY, _L("Y+10.0"), wxDefaultPosition, wxDefaultSize);
    btn_set_y_pos_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Y10.0 F3000 \n");
        });
    btn_set_y_neg_0_1 = new wxButton(parent, wxID_ANY, _L("Y-0.1"), wxDefaultPosition, wxDefaultSize);
    btn_set_y_neg_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Y-0.1 F3000 \n");
        });
    btn_set_y_neg_1_0 = new wxButton(parent, wxID_ANY, _L("Y-1.0"), wxDefaultPosition, wxDefaultSize);
    btn_set_y_neg_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Y-1.0 F3000 \n");
        });
    btn_set_y_neg_10_0 = new wxButton(parent, wxID_ANY, _L("Y-10.0"), wxDefaultPosition, wxDefaultSize);
    btn_set_y_neg_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Y-10.0 F3000 \n");
        });
    btn_set_z_pos_0_1 = new wxButton(parent, wxID_ANY, _L("Z+0.1"), wxDefaultPosition, wxDefaultSize);
    btn_set_z_pos_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Z0.1 F900 \n");
        });
    btn_set_z_pos_1_0 = new wxButton(parent, wxID_ANY, _L("Z+1.0"), wxDefaultPosition, wxDefaultSize);
    btn_set_z_pos_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Z1.0 F900 \n");
        });
    btn_set_z_pos_10_0 = new wxButton(parent, wxID_ANY, _L("Z+10.0"), wxDefaultPosition, wxDefaultSize);
    btn_set_z_pos_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Z10.0 F900 \n");
        });
    btn_set_z_neg_0_1 = new wxButton(parent, wxID_ANY, _L("Z-0.1"), wxDefaultPosition, wxDefaultSize);
    btn_set_z_neg_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Z-0.1 F900 \n");
        });
    btn_set_z_neg_1_0 = new wxButton(parent, wxID_ANY, _L("Z-1.0"), wxDefaultPosition, wxDefaultSize);
    btn_set_z_neg_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Z-1.0 F900 \n");
        });
    btn_set_z_neg_10_0 = new wxButton(parent, wxID_ANY, _L("Z-10.0"), wxDefaultPosition, wxDefaultSize);
    btn_set_z_neg_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Z-10.0 F900 \n");
        });
    btn_set_e_pos_0_1 = new wxButton(parent, wxID_ANY, _L("E+0.1"), wxDefaultPosition, wxDefaultSize);
    btn_set_e_pos_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M83 \nG0 E0.1 F300 \n");
        });
    btn_set_e_pos_1_0 = new wxButton(parent, wxID_ANY, _L("E+1.0"), wxDefaultPosition, wxDefaultSize);
    btn_set_e_pos_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M83 \nG0 E1.0 F300 \n");
        });
    btn_set_e_pos_10_0 = new wxButton(parent, wxID_ANY, _L("E+10.0"), wxDefaultPosition, wxDefaultSize);
    btn_set_e_pos_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M83 \nG0 E10.0 F300 \n");
        });
    btn_set_e_neg_0_1 = new wxButton(parent, wxID_ANY, _L("E-0.1"), wxDefaultPosition, wxDefaultSize);
    btn_set_e_neg_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M83 \nG0 E-0.1 F300 \n");
        });
    btn_set_e_neg_1_0 = new wxButton(parent, wxID_ANY, _L("E-1.0"), wxDefaultPosition, wxDefaultSize);
    btn_set_e_neg_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M83 \nG0 E-1.0 F300 \n");
        });
    btn_set_e_neg_10_0 = new wxButton(parent, wxID_ANY, _L("E-10.0"), wxDefaultPosition, wxDefaultSize);
    btn_set_e_neg_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M83 \nG0 E-10.0 F300 \n");
        });

    pos_btns_sizer = new wxGridSizer(9, 3, 5, 5);
    pos_btns_sizer->Add(btn_set_x_pos_0_1);
    pos_btns_sizer->Add(btn_set_x_pos_1_0);
    pos_btns_sizer->Add(btn_set_x_pos_10_0);
    pos_btns_sizer->Add(btn_set_x_neg_0_1);
    pos_btns_sizer->Add(btn_set_x_neg_1_0);
    pos_btns_sizer->Add(btn_set_x_neg_10_0);
    pos_btns_sizer->Add(btn_set_y_pos_0_1);
    pos_btns_sizer->Add(btn_set_y_pos_1_0);
    pos_btns_sizer->Add(btn_set_y_pos_10_0);
    pos_btns_sizer->Add(btn_set_y_neg_0_1);
    pos_btns_sizer->Add(btn_set_y_neg_1_0);
    pos_btns_sizer->Add(btn_set_y_neg_10_0);

    pos_btns_sizer->Add(btn_set_z_pos_0_1);
    pos_btns_sizer->Add(btn_set_z_pos_1_0);
    pos_btns_sizer->Add(btn_set_z_pos_10_0);
    pos_btns_sizer->Add(btn_set_z_neg_0_1);
    pos_btns_sizer->Add(btn_set_z_neg_1_0);
    pos_btns_sizer->Add(btn_set_z_neg_10_0);
    pos_btns_sizer->Add(btn_set_e_pos_0_1);
    pos_btns_sizer->Add(btn_set_e_pos_1_0);
    pos_btns_sizer->Add(btn_set_e_pos_10_0);
    pos_btns_sizer->Add(btn_set_e_neg_0_1);
    pos_btns_sizer->Add(btn_set_e_neg_1_0);
    pos_btns_sizer->Add(btn_set_e_neg_10_0);


    btn_return_home = new wxButton(parent, wxID_ANY, _L("Return Home:G28"));
    btn_return_home->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G28 \n");
        });
    btn_auto_leveling = new wxButton(parent, wxID_ANY, _L("Auto Leveling:G29"));
    btn_auto_leveling->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G29 \n");
        });
    btn_xyz_abs_mode = new wxButton(parent, wxID_ANY, _L("XYZ - abs mode:G90"));
    btn_xyz_abs_mode->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G90 \n");
        });

    btn_fan_on = new wxButton(parent, wxID_ANY, _L("Cooling Fan ON"));
    btn_fan_on->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M106 S255 \n");
        });
    btn_fan_off = new wxButton(parent, wxID_ANY, _L("Cooling Fan OFF"));
    btn_fan_off->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M106 S0 \n");
        });
    btn_set_hot_bed_temp = new wxButton(parent, wxID_ANY, _L("Set Bed Temp"));
    btn_set_hot_bed_temp->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        std::string gcode_str = "M140 S" + txt_set_hot_bed_temp->GetValue().ToStdString() + " \n";
        this->publishGcode(gcode_str);
        });
    btn_set_hot_end_temp = new wxButton(parent, wxID_ANY, _L("Set Hot End Temp"), wxDefaultPosition, wxDefaultSize);
    btn_set_hot_end_temp->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        std::string gcode_str = "M104 S" + txt_set_hot_end_temp->GetValue().ToStdString() + " \n";
        this->publishGcode(gcode_str);
        });

    btn_switch_t = new wxButton(parent, wxID_ANY, _L("Swtich AMS:"), wxDefaultPosition, wxDefaultSize);
    btn_switch_t->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        std::string gcode = this->switch_ams_gcode(txt_switch_val->GetValue().ToStdString());
        this->publishGcode(gcode);
        });
    txt_switch_val = new wxTextCtrl(parent, wxID_ANY, _L("1"), wxDefaultPosition, wxDefaultSize);
    txt_set_hot_bed_temp = new wxTextCtrl(parent, wxID_ANY, _L("60"), wxDefaultPosition, wxDefaultSize);
    txt_set_hot_end_temp = new wxTextCtrl(parent, wxID_ANY, _L("200"), wxDefaultPosition, wxDefaultSize);

    label_pos_x = new wxStaticText(parent, wxID_ANY, _L("Pos X: "), wxDefaultPosition, wxDefaultSize);
    label_pos_x_val = new wxStaticText(parent, wxID_ANY, _L("0.00"), wxDefaultPosition, wxDefaultSize);
    label_pos_y = new wxStaticText(parent, wxID_ANY, _L("Pos Y: "), wxDefaultPosition, wxDefaultSize);
    label_pos_y_val = new wxStaticText(parent, wxID_ANY, _L("0.00"), wxDefaultPosition, wxDefaultSize);
    label_pos_z = new wxStaticText(parent, wxID_ANY, _L("Pos Z: "), wxDefaultPosition, wxDefaultSize);
    label_pos_z_val = new wxStaticText(parent, wxID_ANY, _L("0.00"), wxDefaultPosition, wxDefaultSize);
    label_pos_e = new wxStaticText(parent, wxID_ANY, _L("Pos E: "), wxDefaultPosition, wxDefaultSize);
    label_pos_e_val = new wxStaticText(parent, wxID_ANY, _L("0.00"), wxDefaultPosition, wxDefaultSize);
    label_hot_end_temp = new wxStaticText(parent, wxID_ANY, _L("Nozzle Temp: "), wxDefaultPosition, wxDefaultSize);
    label_hot_end_temp_val = new wxStaticText(parent, wxID_ANY, _L("0/0"), wxDefaultPosition, wxDefaultSize);
    label_bed_end_temp = new wxStaticText(parent, wxID_ANY, _L("Bed Temp: "), wxDefaultPosition, wxDefaultSize);
    label_bed_end_temp_val = new wxStaticText(parent, wxID_ANY, _L("0/0"), wxDefaultPosition, wxDefaultSize);
    label_print_progress = new wxStaticText(parent, wxID_ANY, _L("Print Progress: "), wxDefaultPosition, wxDefaultSize);
    label_print_progress_val = new wxStaticText(parent, wxID_ANY, _L("0%"), wxDefaultPosition, wxDefaultSize);
    label_wifi_signal = new wxStaticText(parent, wxID_ANY, _L("WiFi Signal:"), wxDefaultPosition, wxDefaultSize);
    label_wifi_signal_val = new wxStaticText(parent, wxID_ANY, _L("0"), wxDefaultPosition, wxDefaultSize);
    label_wifi_link_th = new wxStaticText(parent, wxID_ANY, _L("Link TH State:"), wxDefaultPosition, wxDefaultSize);
    label_wifi_link_th_val = new wxStaticText(parent, wxID_ANY, _L("NA"), wxDefaultPosition, wxDefaultSize);
    label_wifi_link_ams = new wxStaticText(parent, wxID_ANY, _L("Link AMS State:"), wxDefaultPosition, wxDefaultSize);
    label_wifi_link_ams_val = new wxStaticText(parent, wxID_ANY, _L("NA"), wxDefaultPosition, wxDefaultSize);
    auto label = new wxStaticText(parent, wxID_ANY, _L(""), wxDefaultPosition, wxDefaultSize);

    auto temp_btns_sizer = new wxGridSizer(5, 2, 5, 5);
    temp_btns_sizer->Add(btn_set_hot_bed_temp, 0, wxRIGHT | wxALIGN_RIGHT);
    temp_btns_sizer->Add(txt_set_hot_bed_temp, 0, wxLEFT | wxALIGN_LEFT);
    temp_btns_sizer->Add(btn_set_hot_end_temp, 0, wxRIGHT | wxALIGN_RIGHT);
    temp_btns_sizer->Add(txt_set_hot_end_temp, 0, wxLEFT | wxALIGN_LEFT);
    temp_btns_sizer->Add(btn_fan_on, 0, wxRIGHT | wxALIGN_RIGHT);
    temp_btns_sizer->Add(btn_fan_off, 0, wxLEFT | wxALIGN_LEFT);
    temp_btns_sizer->Add(btn_auto_leveling, 0, wxRIGHT | wxALIGN_RIGHT);
    temp_btns_sizer->Add(btn_xyz_abs_mode, 0, wxLEFT | wxALIGN_LEFT);
    temp_btns_sizer->Add(btn_return_home, 0, wxRIGHT | wxALIGN_RIGHT);
    temp_btns_sizer->Add(label, 0, wxLEFT | wxALIGN_LEFT);
    temp_btns_sizer->Add(btn_switch_t, 0, wxRIGHT | wxALIGN_RIGHT);
    temp_btns_sizer->Add(txt_switch_val, 0, wxLEFT | wxALIGN_LEFT);
    temp_btns_sizer->Add(label_pos_x, 0, wxRIGHT | wxALIGN_RIGHT);
    temp_btns_sizer->Add(label_pos_x_val, 0, wxLEFT | wxALIGN_LEFT);
    temp_btns_sizer->Add(label_pos_y, 0, wxRIGHT | wxALIGN_RIGHT);
    temp_btns_sizer->Add(label_pos_y_val, 0, wxLEFT | wxALIGN_LEFT);
    temp_btns_sizer->Add(label_pos_z, 0, wxRIGHT | wxALIGN_RIGHT);
    temp_btns_sizer->Add(label_pos_z_val, 0, wxLEFT | wxALIGN_LEFT);
    temp_btns_sizer->Add(label_pos_e, 0, wxRIGHT | wxALIGN_RIGHT);
    temp_btns_sizer->Add(label_pos_e_val, 0, wxLEFT | wxALIGN_LEFT);
    temp_btns_sizer->Add(label_hot_end_temp, 0, wxRIGHT | wxALIGN_RIGHT);
    temp_btns_sizer->Add(label_hot_end_temp_val, 0, wxLEFT | wxALIGN_LEFT);
    temp_btns_sizer->Add(label_bed_end_temp, 0, wxRIGHT | wxALIGN_RIGHT);
    temp_btns_sizer->Add(label_bed_end_temp_val, 0, wxLEFT | wxALIGN_LEFT);
    temp_btns_sizer->Add(label_print_progress, 0, wxRIGHT | wxALIGN_RIGHT);
    temp_btns_sizer->Add(label_print_progress_val, 0, wxLEFT | wxALIGN_LEFT);
    temp_btns_sizer->Add(label_wifi_signal, 0, wxRIGHT | wxALIGN_RIGHT);
    temp_btns_sizer->Add(label_wifi_signal_val, 0, wxLEFT | wxALIGN_LEFT);
    temp_btns_sizer->Add(label_wifi_link_th, 0, wxRIGHT | wxALIGN_RIGHT);
    temp_btns_sizer->Add(label_wifi_link_th_val, 0, wxLEFT | wxALIGN_LEFT);
    temp_btns_sizer->Add(label_wifi_link_ams, 0, wxRIGHT | wxALIGN_RIGHT);
    temp_btns_sizer->Add(label_wifi_link_ams_val, 0, wxLEFT | wxALIGN_LEFT);

    sizer->Add(pos_btns_sizer, 0, wxTOP | wxALIGN_CENTER_VERTICAL | wxALIGN_CENTER_HORIZONTAL, SPACING);
    sizer->Add(temp_btns_sizer, 1, wxTOP | wxLEFT);
}

void DebugToolDialog::init_gcode_custom(wxWindow *parent, wxBoxSizer* sizer)
{
    btn_send_gcode_1 = new wxButton(parent, wxID_ANY, _L("Send Custom\nGcode 1"), wxDefaultPosition, wxSize(BTN_SEND_WIDTH, TXT_GCODE_HEIGHT));
    btn_send_gcode_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        std::string gcode1 = txt_custom_gcode1->GetValue().ToStdString() + "\n";
        this->publishGcode(txt_custom_gcode1->GetValue().ToStdString());
        });
    btn_send_gcode_2 = new wxButton(parent, wxID_ANY, _L("Send Custom\nGcode 2"), wxDefaultPosition, wxSize(BTN_SEND_WIDTH, TXT_GCODE_HEIGHT));
    btn_send_gcode_2->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode(txt_custom_gcode2->GetValue().ToStdString());
        });
    btn_send_gcode_3 = new wxButton(parent, wxID_ANY, _L("Send Custom\nGcode 3"), wxDefaultPosition, wxSize(BTN_SEND_WIDTH, TXT_GCODE_HEIGHT));
    btn_send_gcode_3->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode(txt_custom_gcode3->GetValue().ToStdString());
        });
    btn_send_gcode_4 = new wxButton(parent, wxID_ANY, _L("Send Custom\nGcode 4"), wxDefaultPosition, wxSize(BTN_SEND_WIDTH, TXT_GCODE_HEIGHT));
    btn_send_gcode_4->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode(txt_custom_gcode4->GetValue().ToStdString());
        });
    btn_send_gcode_5 = new wxButton(parent, wxID_ANY, _L("Send Custom\nGcode 5"), wxDefaultPosition, wxSize(BTN_SEND_WIDTH, TXT_GCODE_HEIGHT));
    btn_send_gcode_5->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode(txt_custom_gcode5->GetValue().ToStdString());
        });
    btn_send_gcode_6 = new wxButton(parent, wxID_ANY, _L("Send Custom\nGcode 6"), wxDefaultPosition, wxSize(BTN_SEND_WIDTH, TXT_GCODE_HEIGHT));
    btn_send_gcode_6->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode(txt_custom_gcode6->GetValue().ToStdString());
        });
    btn_send_gcode_7 = new wxButton(parent, wxID_ANY, _L("Send Custom\nGcode 7"), wxDefaultPosition, wxSize(BTN_SEND_WIDTH, TXT_GCODE_HEIGHT));
    btn_send_gcode_7->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode(txt_custom_gcode7->GetValue().ToStdString());
        });
    txt_custom_gcode1 = new wxTextCtrl(parent, wxID_ANY, _L(""), wxDefaultPosition, wxSize(300, TXT_GCODE_HEIGHT), wxTE_MULTILINE);
    txt_custom_gcode2 = new wxTextCtrl(parent, wxID_ANY, _L(""), wxDefaultPosition, wxSize(300, TXT_GCODE_HEIGHT), wxTE_MULTILINE);
    txt_custom_gcode3 = new wxTextCtrl(parent, wxID_ANY, _L(""), wxDefaultPosition, wxSize(300, TXT_GCODE_HEIGHT), wxTE_MULTILINE);
    txt_custom_gcode4 = new wxTextCtrl(parent, wxID_ANY, _L(""), wxDefaultPosition, wxSize(300, TXT_GCODE_HEIGHT), wxTE_MULTILINE);
    txt_custom_gcode5 = new wxTextCtrl(parent, wxID_ANY, _L(""), wxDefaultPosition, wxSize(300, TXT_GCODE_HEIGHT), wxTE_MULTILINE);
    txt_custom_gcode6 = new wxTextCtrl(parent, wxID_ANY, _L(""), wxDefaultPosition, wxSize(300, TXT_GCODE_HEIGHT), wxTE_MULTILINE);
    txt_custom_gcode7 = new wxTextCtrl(parent, wxID_ANY, _L(""), wxDefaultPosition, wxSize(300, TXT_GCODE_HEIGHT), wxTE_MULTILINE);

    // Init custom_gcode
    pt::ptree custom_gocde_root;
    try {
        std::string name = "CustomGcode.json";
        std::ifstream f(name.c_str());
        if (f.good())
        {
            pt::read_json(name, custom_gocde_root);
            std::string gcode1 = custom_gocde_root.get<std::string>("custom_gcode_1");
            txt_custom_gcode1->SetValue(wxString(gcode1));
            std::string gcode2 = custom_gocde_root.get<std::string>("custom_gcode_2");
            txt_custom_gcode2->SetValue(wxString(gcode2));
            std::string gcode3 = custom_gocde_root.get<std::string>("custom_gcode_3");
            txt_custom_gcode3->SetValue(wxString(gcode3));
            std::string gcode4 = custom_gocde_root.get<std::string>("custom_gcode_4");
            txt_custom_gcode4->SetValue(wxString(gcode4));
            std::string gcode5 = custom_gocde_root.get<std::string>("custom_gcode_5");
            txt_custom_gcode5->SetValue(wxString(gcode5));
            std::string gcode6 = custom_gocde_root.get<std::string>("custom_gcode_6");
            txt_custom_gcode6->SetValue(wxString(gcode6));
            std::string gcode7 = custom_gocde_root.get<std::string>("custom_gcode_7");
            txt_custom_gcode7->SetValue(wxString(gcode7));
        }
    }
    catch (...) {
        ;
    }

    auto custom_gcode_sizer = new wxFlexGridSizer(7, 2, 0, 5);
    custom_gcode_sizer->AddGrowableCol(1, 1);
    custom_gcode_sizer->Add(btn_send_gcode_1, 0, wxRIGHT | wxALIGN_RIGHT, SPACING);
    custom_gcode_sizer->Add(txt_custom_gcode1, 1, wxLEFT | wxALIGN_LEFT | wxEXPAND, SPACING);
    custom_gcode_sizer->Add(btn_send_gcode_2, 0, wxRIGHT | wxALIGN_RIGHT, SPACING);
    custom_gcode_sizer->Add(txt_custom_gcode2, 1, wxLEFT | wxALIGN_LEFT | wxEXPAND, SPACING);
    custom_gcode_sizer->Add(btn_send_gcode_3, 0, wxRIGHT | wxALIGN_RIGHT, SPACING);
    custom_gcode_sizer->Add(txt_custom_gcode3, 1, wxLEFT | wxALIGN_LEFT | wxEXPAND, SPACING);
    custom_gcode_sizer->Add(btn_send_gcode_4, 0, wxRIGHT | wxALIGN_RIGHT, SPACING);
    custom_gcode_sizer->Add(txt_custom_gcode4, 1, wxLEFT | wxALIGN_LEFT | wxEXPAND, SPACING);
    custom_gcode_sizer->Add(btn_send_gcode_5, 0, wxRIGHT | wxALIGN_RIGHT, SPACING);
    custom_gcode_sizer->Add(txt_custom_gcode5, 1, wxLEFT | wxALIGN_LEFT | wxEXPAND, SPACING);
    custom_gcode_sizer->Add(btn_send_gcode_6, 0, wxRIGHT | wxALIGN_RIGHT, SPACING);
    custom_gcode_sizer->Add(txt_custom_gcode6, 1, wxLEFT | wxALIGN_LEFT | wxEXPAND, SPACING);
    custom_gcode_sizer->Add(btn_send_gcode_7, 0, wxRIGHT | wxALIGN_RIGHT, SPACING);
    custom_gcode_sizer->Add(txt_custom_gcode7, 1, wxLEFT | wxALIGN_LEFT | wxEXPAND, SPACING);

    sizer->Add(custom_gcode_sizer, 0, wxLEFT | wxRIGHT, 5);
}

bool DebugToolDialog::Show(bool show)
{
    bool result = DPIDialog::Show(show);
    if (show) {
        m_timer->Stop();
        m_timer->SetOwner(this);
        m_timer->Start(10000);

        Slic3r::CommuBackend* backend = Slic3r::GUI::wxGetApp().getCommuBackend();
        backend->set_msg_recv_fn([this](std::string topic, std::string payload) {
                auto evt = new wxCommandEvent(EVT_MESSAGE_ARRIVED, this->GetId());
                evt->SetString(payload);
                wxQueueEvent(this, evt);
            });
        backend->set_msg_send_fn([this](std::string topic, std::string payload) {
                auto evt = new wxCommandEvent(EVT_MESSAGE_SENT, this->GetId());
                std::string send_msg = "send topic=" + topic + ", msg=" + payload;
                evt->SetString(send_msg);
                wxQueueEvent(this, evt);
            });
        
        Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
        if (account_manager && account_manager->is_user_login()) {
            account_manager->request_bind_list(account_manager->get_user_id());
        }
    }
    else {
        m_timer->Stop();
    }

    return result;
}


void DebugToolDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();
    msw_buttons_rescale(this, em, { wxID_DELETE, wxID_CANCEL, btn_run_gcode->GetId() });

    SetMinSize(wxSize(HEIGHT * em, WIDTH * em));

    Fit();
    Refresh();
}

int DebugToolDialog::publish_json(std::string json_str)
{
    /* Check device is online */
    Slic3r::DeviceManager *manager = Slic3r::GUI::wxGetApp().getDeviceManager();
    Slic3r::CommuBackend* backend = Slic3r::GUI::wxGetApp().getCommuBackend();
    try
{
        std::string content = cb_device_list->GetValue().ToStdString();
        size_t start = content.find_last_of("(") + 1;
        std::string device_id = content.substr(start, content.find_last_of(")") - start);
        if (device_id.compare("") == 0) {
            wxMessageBox("Please select a device!");
            return -1;
        }
        
        if (!manager->is_bind_self(device_id))
        {
            wxMessageBox("Please Bind first!");
            return -1;
        }
        
        if (backend->publish_json_to_client(device_id, json_str) == 1) {
            wxMessageBox("Disconnect with device, Connect First!");
            return -1;
        }
        return 0;
    }
    catch (std::exception& e) {
        BOOST_LOG_TRIVIAL(trace) << "Exception in publish json, std::exception=" << e.what() << ", json = " << json_str;
        return -1;
    }
    catch (...) {
        BOOST_LOG_TRIVIAL(trace) << "Unknown Exception in publish json, json=" << json_str;
        return -1;
    }
}


void DebugToolDialog::on_message_sent(wxCommandEvent& evt)
{
    this->log_info(evt.GetString().ToStdString());
}

void DebugToolDialog::on_log_info(wxCommandEvent& evt)
{
    this->log_info(evt.GetString().ToStdString());
}


void DebugToolDialog::on_message_arrived(wxCommandEvent &evt)
{
    std::string json_str = evt.GetString().ToStdString();
    try {
        BOOST_LOG_TRIVIAL(trace) << "handle_report_print: json_str=" << json_str;
        std::stringstream ss(json_str);
        pt::ptree root;
        pt::read_json(ss, root);
        if (root.get_child_optional("print") != boost::none) {
            pt::ptree print = root.get_child("print");
            /* Update labels */
            boost::optional<std::string> command = print.get_optional<std::string>("command");
            if (command.has_value() &&  command.value_or("").compare("push_status") == 0) {
                boost::optional<std::string> sequence_id = print.get_optional<std::string>("sequence_id");
                boost::optional<std::string> nozzle_temp = print.get_optional<std::string>("nozzle_temp");
                boost::optional<std::string> nozzle_temp_target = print.get_optional<std::string>("nozzle_target_temp");
                boost::optional<std::string> bed_temp = print.get_optional<std::string>("bed_temp");
                boost::optional<std::string> bed_temp_target = print.get_optional<std::string>("bed_target_temp");


                boost::optional<std::string> pos_x = print.get_optional<std::string>("pos_x");
                boost::optional<std::string> pos_y = print.get_optional<std::string>("pos_y");
                boost::optional<std::string> pos_z = print.get_optional<std::string>("pos_z");
                boost::optional<std::string> pos_e = print.get_optional<std::string>("pos_e");

                if (pos_x.has_value()) label_pos_x_val->SetLabelText(pos_x.value());
                if (pos_y.has_value()) label_pos_y_val->SetLabelText(pos_y.value());
                if (pos_z.has_value()) label_pos_z_val->SetLabelText(pos_z.value());
                if (pos_e.has_value()) label_pos_e_val->SetLabelText(pos_e.value());


                boost::optional<std::string> gcode_start_time = print.get_optional<std::string>("gcode_start_time");
                boost::optional<std::string> gcode_duration = print.get_optional<std::string>("gcode_duration");
                boost::optional<std::string> gcode_file = print.get_optional<std::string>("gcode_file");

                if (gcode_start_time.has_value()) {
                    summary->start_time = gcode_start_time.value();
                    BOOST_LOG_TRIVIAL(trace) << "summary start_time=" << summary->start_time;
                }
                if (gcode_duration.has_value()) {
                    try {
                        summary->duration = std::stoi(gcode_duration.value());
                        BOOST_LOG_TRIVIAL(trace) << "summary duration=" << summary->duration;
                    }
                    catch (...) {
                        ;
                    }
                }
                if (gcode_file.has_value()) {
                    summary->print_filename = gcode_file.value();
                }


                boost::optional<std::string> nozzle_temp_raw = print.get_optional<std::string>("nozzle_temp_raw");
                boost::optional<std::string> nozzle_temp_target_raw = print.get_optional<std::string>("nozzle_target_temp_raw");
                boost::optional<std::string> bed_temp_raw = print.get_optional<std::string>("bed_temp_raw");
                boost::optional<std::string> bed_temp_target_raw = print.get_optional<std::string>("bed_target_temp_raw");
                std::string nozzle_temp_str = "na";
                std::string nozzle_target_temp_str = "na";
                std::string bed_temp_str = "na";
                std::string bed_temp_target_str = "na";
                if (nozzle_temp_raw.has_value()) {
                    try {
                        int nozzle_temp_int = std::stoi(nozzle_temp_raw.value());
                        float temp_float = (float)nozzle_temp_int;
                        temp_float = temp_float / 32.0f;
                        std::stringstream tempBuf;
                        tempBuf.precision(2);
                        tempBuf.setf(std::ios::fixed);
                        tempBuf << temp_float;
                        nozzle_temp_str = tempBuf.str();
                    }
                    catch (std::exception& e) {
                        ;
                    }
                }
                if (nozzle_temp_target_raw.has_value()) {
                    try {
                        int temp_int = std::stoi(nozzle_temp_target_raw.value());
                        float temp_float = (float)temp_int;
                        temp_float = temp_float / 32.0f;
                        std::stringstream tempBuf;
                        tempBuf.precision(2);
                        tempBuf.setf(std::ios::fixed);
                        tempBuf << temp_float;
                        nozzle_target_temp_str = tempBuf.str();
                    }
                    catch (std::exception& e) {
                        ;
                    }
                }

                if (bed_temp_raw.has_value()) {
                    try {
                        int temp_int = std::stoi(bed_temp_raw.value());
                        float temp_float = (float)temp_int;
                        temp_float = temp_float / 32.0f;
                        std::stringstream tempBuf;
                        tempBuf.precision(2);
                        tempBuf.setf(std::ios::fixed);
                        tempBuf << temp_float;
                        bed_temp_str = tempBuf.str();
                    }
                    catch (...) {
                        ;
                    }
                }

                if (bed_temp_target_raw.has_value()) {
                    try {
                        int temp_int = std::stoi(bed_temp_target_raw.value());
                        float temp_float = (float)temp_int;
                        temp_float = temp_float / 32.0f;
                        std::stringstream tempBuf;
                        tempBuf.precision(2);
                        tempBuf.setf(std::ios::fixed);
                        tempBuf << temp_float;
                        bed_temp_target_str = tempBuf.str();
                    }
                    catch (...) {
                        ;
                    }
                }

                if (nozzle_temp_raw.has_value() && nozzle_temp_target_raw.has_value()) {
                    label_hot_end_temp_val->SetLabelText(nozzle_temp_str + "/" + nozzle_target_temp_str);
                }
                else {
                    if (nozzle_temp.has_value() && nozzle_temp_target.has_value()) {
                        label_hot_end_temp_val->SetLabelText(nozzle_temp.value() + "/" + nozzle_temp_target.value());
                    }
                }

                if (bed_temp_raw.has_value() && bed_temp_target_raw.has_value()) {
                    label_bed_end_temp_val->SetLabelText(bed_temp_str + "/" + bed_temp_target_str);
                }
                else {
                    if (bed_temp.has_value() && bed_temp_target.has_value()) {
                        label_bed_end_temp_val->SetLabelText(bed_temp.value() + "/" + bed_temp_target.value());
                    }
                }

                boost::optional<std::string> progress = print.get_optional<std::string>("progress");
                if (progress.has_value()) {
                    label_print_progress_val->SetLabelText(progress.value());
                    /* parse progress*/
                    int progress_int = 0;
                    int before_progress = progress.value().find_last_of(' ');
                    int after_progress = progress.value().find_last_of('%');
                    if (after_progress >= 0) {
                        if (after_progress > before_progress) {
                            std::string prog_str = progress.value().substr(before_progress, after_progress - before_progress);
                            try {
                                progress_int = stoi(prog_str);
                            }
                            catch (std::exception& e) {
                                ;
                            }
                            catch (...) {
                                ;
                            }
                        }
                    } else {
                        if (progress.value().compare("100") == 0) {
                            progress_int = 100;
                        }
                        else {
                            progress_int = 99;
                        }
                    }

                    if ((last_progress != progress_int) && (last_progress < progress_int) && progress_int == 100) {
                        auto et = new wxCommandEvent(EVT_PRINT_FINISH, this->GetId());
                        et->SetInt(0);
                        wxQueueEvent(this, et);
                    }
                    last_progress = progress_int;

                    /*parse filename, update summary */
                    try {
                        if (before_progress > 0) {
                            std::string filename = progress.value().substr(0, before_progress);
                            summary->print_filename = filename;
                        }
                    }
                    catch (std::exception& e) {
                        ;
                    }
                    catch (...) {
                        ;
                    }
                }

                boost::optional<std::string> link_th = print.get_optional<std::string>("link_th_state");
                std::string link_th_str = "na";
                if (link_th.has_value()) {
                    try {
                        int temp_int = std::stoi(link_th.value());
                        float temp_float = (float)temp_int;
                        temp_float = temp_float / 100.0f;
                        std::stringstream tempBuf;
                        tempBuf.precision(2);
                        tempBuf.setf(std::ios::fixed);
                        tempBuf << temp_float;
                        link_th_str = tempBuf.str();
                    }
                    catch (...) {
                        ;
                    }

                    label_wifi_link_th_val->SetLabelText(link_th_str);
                }

                boost::optional<std::string> link_ams = print.get_optional<std::string>("link_ams_state");
                std::string link_ams_str = "na";
                if (link_ams.has_value()) {
                    try {
                        int temp_int = std::stoi(link_ams.value());
                        float temp_float = (float)temp_int;
                        temp_float = temp_float / 100.0f;
                        std::stringstream tempBuf;
                        tempBuf.precision(2);
                        tempBuf.setf(std::ios::fixed);
                        tempBuf << temp_float;
                        link_ams_str = tempBuf.str();
                    }
                    catch (...) {
                        ;
                    }

                    label_wifi_link_ams_val->SetLabelText(link_ams_str);
                }

                boost::optional<std::string> wifi_signal = print.get_optional<std::string>("wifi_signal");
                if (wifi_signal.has_value()) {
                    label_wifi_signal_val->SetLabelText(wifi_signal.value());
                }

                return;
            }
            else if (command.has_value() && command.value().compare("gcode_line") == 0) {
                boost::optional<std::string> sequence_id = print.get_optional<std::string>("sequence_id");
            }
            else if (command.has_value() && command.value().compare("gcode_file") == 0) {
                boost::optional<std::string> sequence_id = print.get_optional<std::string>("sequence_id");
            }
            else if (command.has_value() && command.value().compare("get_version") == 0) {
                if (root.get_child_optional("sw_ver") != boost::none) {
                    pt::ptree version = root.get_child("sw_ver");
                    try {
                        std::stringstream oss;
                        pt::write_json(oss, version);
                        std::string json_str = oss.str();
                        summary->device_version = json_str;
                    }
                    catch (std::exception& e) {
                        ;
                    }
                    catch (...) {
                        ;
                    }
                }
            }
            this->log_info("received ack msg = " + json_str);
            return;
        }
        else if (root.get_child_optional("info") != boost::none) {
            pt::ptree info = root.get_child("info");
            /* Update labels */
            boost::optional<std::string> command = info.get_optional<std::string>("command");
            if (command.has_value() && command.value().compare("get_version") == 0) {
                if (info.get_child_optional("sw_ver") != boost::none) {
                    pt::ptree version = info.get_child("sw_ver");
                    try {
                        std::stringstream oss;
                        pt::write_json(oss, version);
                        std::string version_str = oss.str();
                        summary->device_version = version_str;
                    }
                    catch (std::exception& e) {
                        ;
                    }
                    catch (...) {
                        ;
                    }
                }
            }
            this->log_info("received ack msg = " + json_str);
            return;
        }
        else if (root.get_child_optional("upgrade") != boost::none) {
            pt::ptree upgrade = root.get_child("upgrade");
            boost::optional<std::string> upgrade_module = upgrade.get_optional<std::string>("module");
            if (upgrade_module.has_value()) {
                label_upgrade_module_val->SetLabelText(upgrade_module.value());
            }
            boost::optional<std::string> upgrade_status = upgrade.get_optional<std::string>("status");
            if (upgrade_status.has_value()) {
                label_upgrade_status_val->SetLabelText(upgrade_status.value());
            }
            boost::optional<std::string> upgrade_progress = upgrade.get_optional<std::string>("progress");
            if (upgrade_progress.has_value()) {
                label_upgrade_progress_val->SetLabelText(upgrade_progress.value());

            }
            boost::optional<std::string> upgrade_message = upgrade.get_optional<std::string>("message");
            if (upgrade_message.has_value()) {
                label_upgrade_message_val->SetLabelText(upgrade_message.value());
            }
        }
        this->log_info("json=" + json_str);
    }
    catch (std::exception& e) {
        std::string info = "parsing report msg error, json_str=" + json_str;
        this->log_info(info);
    }
    catch (...) {
        BOOST_LOG_TRIVIAL(trace) << "Uknown Exception,  json_str=" << json_str;
    }
}

void DebugToolDialog::on_refresh_list(SimpleEvent&evt)
{
    Slic3r::DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();
    wxArrayString new_items;
    if (!manager) return;
    std::vector<DeviceInfo*> list = manager->get_connected_device_info();

    std::vector<DeviceInfo*>::iterator it;
    for (it = list.begin(); it != list.end(); it++) {
        new_items.Add(get_device_list_item(*it));
    }

    cb_device_list->Set(new_items);
}

void DebugToolDialog::refresh_device_list()
{
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    Slic3r::DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!manager) return;

    std::vector<DeviceInfo*> list = manager->get_connected_device_info();
    std::vector<std::string> device_id_list;

    std::vector<DeviceInfo*>::iterator it;
    for (it = list.begin(); it != list.end(); it++) {
        device_id_list.push_back((*it)->get_dev_id());
    }

    if (!account_manager) return;

    if (account_manager->is_user_login()) {
        account_manager->query_bind_status(device_id_list, [this]() {
                wxQueueEvent(this, new SimpleEvent(EVT_UPDATE_LIST));
            }, [this](int status, std::string error, std::string body) {
                auto evt = new wxCommandEvent(EVT_ERROR_MSG, this->GetId());
                std::string error_str = (boost::format("Query Status Error, status=%1%, error=%2%, body=%3%") % status % error % body).str();
                evt->SetString(error_str);
                wxQueueEvent(this, evt);
            });
    }
    else {
        wxQueueEvent(this, new SimpleEvent(EVT_UPDATE_LIST));
    }
}

std::string DebugToolDialog::get_device_list_item(DeviceInfo* info)
{
    return (boost::format("[%1%]%2%(%3%)") % info->get_bind_status_str() % info->m_dev_name % info->m_dev_id).str();
}

void DebugToolDialog::refresh_firmware_list(bool show_error)
{
    cb_upgrade_firmware->Clear();
    upgrade_file_list.clear();
    if (cb_upgrade_module->GetValue().compare("") == 0) {
        wxMessageBox("Please select a module");
        return;
    }
    UPGRADE_MODULE upgrade_module = (UPGRADE_MODULE)cb_upgrade_module->GetCurrentSelection();
    UPGRADE_MODE upgrade_mode = (UPGRADE_MODE)cb_upgrade_mode->GetCurrentSelection();
    Http http = Http::get(UPGRADE_URL + upgrade_post_url[upgrade_module] + upgrade_mode_name[upgrade_mode]);
    http.on_complete([&](std::string body, unsigned) {
        BOOST_LOG_TRIVIAL(trace) << "get firmware request: body=" << body;
        XML_Parser parser = XML_ParserCreate(nullptr);
        XML_SetUserData(parser, this);
        XML_SetElementHandler(parser, XML_StartElementHandler, XML_EndElementHandler);
        XML_SetCharacterDataHandler(parser, XML_CharacterDataHandler);
        XML_Parse(parser, body.c_str(), body.size(), 1);
        XML_ParserFree(parser);
        cb_upgrade_firmware->Set(upgrade_file_list);
        cb_upgrade_firmware->Select(0);

        }).on_error([&](std::string body, std::string error, unsigned status) {
            if (show_error)
                wxMessageBox("Get Upgrade List Failed! error " + error);
            }).perform();
}

int DebugToolDialog::log_info(std::string line)
{
    std::time_t t = std::time(0);
    std::tm* now_time = std::localtime(&t);
    std::stringstream buf;
    buf << std::put_time(now_time, "%a %b %d %H:%M:%S");
    std::string info = buf.str() + ":" + line + "\n";
 
    try {
        // display
        txt_string_info->AppendText(wxString(info));
    }
    catch (std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Unkown Exception in log_info, exception=" << e.what();
        return -1;
    }
    catch (...) {
        BOOST_LOG_TRIVIAL(error) << "Unkown Exception in log_info";
        return -1;
    }
    return 0;
}

int DebugToolDialog::publishGcode(std::string gcode)
{
    int result = 0;
    pt::ptree root, print;
    print.put("command", "gcode_line");
    print.put("param", gcode);
    print.put("sequence_id", this->m_sequence_id++);
    root.put_child("print", print);
    std::stringstream oss;
    pt::write_json(oss, root);
    std::string json_str = oss.str();

    result = this->publish_json(json_str);
    if (result != 0) {
        this->log_info("publish_json failed");
    }

    return result;
}

int DebugToolDialog::set_current_device_id()
{
    std::string content = cb_device_list->GetValue().ToStdString();
    if (content.empty()) {
        m_curr_dev_id = "";
        return 0;
    }
    size_t start = content.find_last_of("(") + 1;
    std::string device_id = content.substr(start, content.find_last_of(")") - start);
    if (device_id.compare("") == 0) {
        return -1;
    }
    m_curr_dev_id = device_id;
    return 0;
}

int DebugToolDialog::get_current_device_id(std::string& dev_id)
{
    dev_id = std::string(m_curr_dev_id);
    return 0;
}

void DebugToolDialog::on_timer(wxTimerEvent& event)
{
    //auto save custom_gcode
    pt::ptree custom_gcode_root;
    custom_gcode_root.put("custom_gcode_1", txt_custom_gcode1->GetValue().ToStdString());
    custom_gcode_root.put("custom_gcode_2", txt_custom_gcode2->GetValue().ToStdString());
    custom_gcode_root.put("custom_gcode_3", txt_custom_gcode3->GetValue().ToStdString());
    custom_gcode_root.put("custom_gcode_4", txt_custom_gcode4->GetValue().ToStdString());
    custom_gcode_root.put("custom_gcode_5", txt_custom_gcode5->GetValue().ToStdString());
    custom_gcode_root.put("custom_gcode_6", txt_custom_gcode6->GetValue().ToStdString());
    custom_gcode_root.put("custom_gcode_7", txt_custom_gcode7->GetValue().ToStdString());
    pt::write_json("CustomGcode.json", custom_gcode_root);
}

void DebugToolDialog::on_select_device(wxCommandEvent& evt)
{
    this->set_current_device_id();
}

}
}
