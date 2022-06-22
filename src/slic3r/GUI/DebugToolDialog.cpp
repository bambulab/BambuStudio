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
#include <cctype>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/format.hpp>
#include <expat.h>
#include <miniz.h>
#include <codecvt>

#include "nlohmann/json.hpp"

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
#include "wxExtensions.hpp"
#include "libslic3r/PlaceholderParser.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/ProjectTask.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "slic3r/GUI/Search.hpp"

#define __CHECK_BIND_USER__


using namespace nlohmann;
namespace pt = boost::property_tree;
typedef pt::ptree JSON;



namespace Slic3r {
namespace GUI {

static wxString creating_stage_str = _L("Creating");
static wxString uploading_stage_str = _L("Uploading");
static wxString waiting_stage_str = _L("Waiting");
static wxString sending_stage_str = _L("Sending");
static wxString finish_stage_str = _L("Finished");

void GcodePrintJob::prepare()
{
    ;
}

void GcodePrintJob::on_exception(const std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
    } catch (std::exception &e) {
        Job::on_exception(eptr);
    }
}

void GcodePrintJob::process()
{
    /* print current gcode */
    BBL::AccountManager* acc = Slic3r::GUI::wxGetApp().getAccountManager();

#ifdef BBL_CHECK_USER_REPORT
    int task_id = 0;
    bool printable = true;
    acc->user_check_report(&task_id, &printable);
    if (task_id!=0 && !printable) {
        update_status(0, _L("Please fill report first."));
        std::string report_url = (boost::format("https://autotest.bambu-lab.com/slicerAddReport?task_id=%1%&token=%2%")
            % task_id
            % acc->get_curr_user()->m_autotest_token
            ).str();
        wxLaunchDefaultBrowser(report_url);
        return;
    }
#endif

    fs::path gcode_path(m_gcode_file_str);
    fs::path _3mf_path(wxStandardPaths::Get().GetTempDir().utf8_str().data());
    _3mf_path /= gcode_path.filename().string();
    std::string dst_gcode_file_str = "Metadata/plate_1.gcode";

    /* zip gcode to 3mf */
    std::string _3mf_file_str = _3mf_path.replace_extension("3mf").string();
    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);
    if (!open_zip_writer(&archive, _3mf_file_str)) {
        BOOST_LOG_TRIVIAL(trace) << "Unable to open the file";
        update_status(0, _L("Unable to create zip file"));
        return;
    }

    int res = 0;
    unsigned int http_code;
    std::string http_body;
    int curr_percent = 3;
    wxString msg = "prepare 3mf file";

    update_status(curr_percent, msg);
    std::string src_file_str = encode_path(m_gcode_file_str.c_str());
    bool result = mz_zip_writer_add_file(&archive, dst_gcode_file_str.c_str(), src_file_str.c_str(), "", 0, MZ_DEFAULT_LEVEL);
    result &= mz_zip_writer_finalize_archive(&archive);
    result &= close_zip_writer(&archive);
    if (!result) {
        update_status(curr_percent, "create 3mf failed!");
        return;
    }

    BBL::AccountManager::PrintParams params;
    params.project_name = "gcode_project";
    params.filename = _3mf_file_str;
    params.plate_index = 1;
    params.preset_name = "gcode_profile";
    params.dev_id = m_obj->dev_id;
    params.task_name = gcode_path.filename().string();


    res = acc->start_print(params,
        [this, &curr_percent, &msg](int stage, int code, std::string info) {
            if (stage == BBL::SendingPrintJobStage::PrintingStageCreate) {
                curr_percent = 25;
                msg = creating_stage_str;
            }
            else if (stage == BBL::SendingPrintJobStage::PrintingStageUpload) {
                curr_percent = 30;
                if (code == 0) {
                    msg = wxString::Format("%s %s", uploading_stage_str, info);
                }
                else {
                    msg = uploading_stage_str;
                }
            }
            else if (stage == BBL::SendingPrintJobStage::PrintingStageWaiting) {
                curr_percent = 50;
                msg = waiting_stage_str;
            }
            else if (stage == BBL::SendingPrintJobStage::PrintingStageSending) {
                curr_percent = 90;
                msg = sending_stage_str;
            }
            else if (stage == BBL::SendingPrintJobStage::PrintingStageFinished) {
                curr_percent = 100;
                msg = finish_stage_str;
            }
            update_status(curr_percent, msg);
        },
        [this]() {
            return was_canceled();
        }
    );

}

void GcodePrintJob::finalize() {
    // Ignore the arrange result if aborted.
    if (was_canceled()) return;

    Job::finalize();
}


DeviceSearchListModel::DeviceSearchListModel(wxWindow* parent) : wxDataViewVirtualListModel(0)
{
    ;
}

void DeviceSearchListModel::Clear()
{
    m_values.clear();
    Reset(0);
}

void DeviceSearchListModel::update_info(std::vector<wxString> list)
{
    m_values.clear();
    m_values.swap(list);
    Reset(m_values.size());
}


wxString DeviceSearchListModel::GetColumnType(unsigned int col) const 
{
    return "string";
}

void DeviceSearchListModel::GetValueByRow(wxVariant& variant,
    unsigned int row, unsigned int col) const
{
    switch (col)
    {
    case colDeviceInfo:
        variant = m_values[row];
        break;
    case colMax:
        wxFAIL_MSG("invalid column");
    default:
        break;
    }
}


DeviceSearchDialog::DeviceSearchDialog( wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style ) : wxDialog( parent, id, title, pos, size, style )
{
	this->SetSizeHints( wxDefaultSize, wxDefaultSize );

	wxBoxSizer* bSizer36;
	bSizer36 = new wxBoxSizer( wxVERTICAL );

	m_textCtrl_search_line = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	bSizer36->Add( m_textCtrl_search_line, 0, wxALL|wxEXPAND, 5 );

	m_dataViewCtrl = new wxDataViewCtrl( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 );
	bSizer36->Add( m_dataViewCtrl, 1, wxALL|wxEXPAND, 5 );


	this->SetSizer( bSizer36 );
	this->Layout();

	this->Centre( wxBOTH );

    default_string = _L("Enter a search term");

    search_list_model = new DeviceSearchListModel(this);
    m_dataViewCtrl->AssociateModel(search_list_model);

    m_dataViewCtrl->AppendTextColumn("Printer", 0, wxDATAVIEW_CELL_INERT, 400, wxALIGN_LEFT);
    m_dataViewCtrl->GetColumn(DeviceSearchListModel::colDeviceInfo)->SetWidth(400);

	// Connect Events
	m_textCtrl_search_line->Connect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( DeviceSearchDialog::OnInputText ), NULL, this );
	m_dataViewCtrl->Connect( wxEVT_COMMAND_DATAVIEW_ITEM_ACTIVATED, wxDataViewEventHandler( DeviceSearchDialog::OnActivate ), NULL, this );
	m_dataViewCtrl->Connect( wxEVT_COMMAND_DATAVIEW_SELECTION_CHANGED, wxDataViewEventHandler( DeviceSearchDialog::OnSelect ), NULL, this );

    update_list();
}

DeviceSearchDialog::~DeviceSearchDialog()
{
	// Disconnect Events
	m_textCtrl_search_line->Disconnect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( DeviceSearchDialog::OnInputText ), NULL, this );
	m_dataViewCtrl->Disconnect( wxEVT_COMMAND_DATAVIEW_ITEM_ACTIVATED, wxDataViewEventHandler( DeviceSearchDialog::OnActivate ), NULL, this );
	m_dataViewCtrl->Disconnect( wxEVT_COMMAND_DATAVIEW_SELECTION_CHANGED, wxDataViewEventHandler( DeviceSearchDialog::OnSelect ), NULL, this );

}


void DeviceSearchDialog::OnInputText(wxCommandEvent& event)
{
    m_textCtrl_search_line->SetInsertionPointEnd();

    wxString input_string = m_textCtrl_search_line->GetValue();
    if (input_string == default_string)
        input_string.Clear();

    update_list();
}

void DeviceSearchDialog::OnActivate(wxDataViewEvent& event)
{
    ProcessSelection(event.GetItem());
}

void DeviceSearchDialog::OnSelect(wxDataViewEvent& event)
{
    if (prevent_list_events)
        return;

#ifndef __APPLE__
    if (wxGetMouseState().LeftIsDown())
#endif
        ProcessSelection(m_dataViewCtrl->GetSelection());
}

void DeviceSearchDialog::ProcessSelection(wxDataViewItem selection)
{
    if (!selection.IsOk())
        return;

    wxPanel* debug_tool_panel = GUI::wxGetApp().mainframe->debug_panel();

    wxString selected = "";
    wxVariant val;
    search_list_model->GetValue(val, selection, 0);
    selected = val.GetString();
    ((DebugToolDialog*)debug_tool_panel)->jump_to_printer(selected);
    this->EndModal(wxID_CLOSE);
}

void DeviceSearchDialog::update_list()
{
    prevent_list_events = true;
    search_list_model->Clear();

    Slic3r::DeviceManager* dm = Slic3r::GUI::wxGetApp().getDeviceManager();

    std::map<std::string ,MachineObject*> list = dm->get_local_machine_list();

    std::map<std::string, MachineObject*>::iterator it;
    
    std::vector<wxString> found;

    std::string search_line = into_u8(m_textCtrl_search_line->GetValue());

    for (it = list.begin(); it != list.end(); it++) {
        if (it->second) {
            if (boost::contains(it->second->dev_ip, search_line)) {
                wxString info = wxString::Format("%-16s(%s)[bind:%s]", it->second->dev_ip, it->second->dev_id, it->second->get_bind_str());
                found.push_back(info);
            }
        }
    }
    search_list_model->update_info(found);

    if (search_list_model->GetCount() > 0)
        m_dataViewCtrl->Select(search_list_model->GetItem(0));

    prevent_list_events = false;
}



    wxDECLARE_EVENT(EVT_3MF_PROGRESS, wxCommandEvent);
    wxDECLARE_EVENT(EVT_WLAN_GCODE_PROGRESS, wxCommandEvent);
    wxDECLARE_EVENT(EVT_WLAN_3MF_PROGRESS, wxCommandEvent);
    wxDECLARE_EVENT(EVT_UPDATE_LIST, SimpleEvent);
    wxDECLARE_EVENT(EVT_REFRESH_LIST, SimpleEvent);
    wxDECLARE_EVENT(EVT_UPDATE_MYBIND_LIST, SimpleEvent);
    wxDECLARE_EVENT(EVT_MQTT_SUCCESS, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MQTT_FAILED, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MQTT_LOST, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MESSAGE_ARRIVED, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MESSAGE_SENT, wxCommandEvent);
    wxDECLARE_EVENT(EVT_LOG_INFO, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MQTT_CONNECTED, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MQTT_DISCONNECTED, wxCommandEvent);

    wxDEFINE_EVENT(EVT_3MF_PROGRESS, wxCommandEvent);
    wxDEFINE_EVENT(EVT_WLAN_GCODE_PROGRESS, wxCommandEvent);
    wxDEFINE_EVENT(EVT_WLAN_3MF_PROGRESS, wxCommandEvent);
    wxDEFINE_EVENT(EVT_UPDATE_LIST, SimpleEvent);
    wxDEFINE_EVENT(EVT_REFRESH_LIST, SimpleEvent);
    wxDEFINE_EVENT(EVT_UPDATE_MYBIND_LIST, SimpleEvent);
    wxDEFINE_EVENT(EVT_MQTT_SUCCESS, wxCommandEvent);
    wxDEFINE_EVENT(EVT_MQTT_FAILED, wxCommandEvent);
    wxDEFINE_EVENT(EVT_MQTT_LOST, wxCommandEvent);
    wxDEFINE_EVENT(EVT_MESSAGE_ARRIVED, wxCommandEvent);
    wxDEFINE_EVENT(EVT_MESSAGE_SENT, wxCommandEvent);
    wxDEFINE_EVENT(EVT_LOG_INFO, wxCommandEvent);
    wxDEFINE_EVENT(EVT_MQTT_CONNECTED, wxCommandEvent);
    wxDEFINE_EVENT(EVT_MQTT_DISCONNECTED, wxCommandEvent);


    std::string DebugToolDialog::_getNewLogFilename()
    {
        std::time_t t = std::time(0);
        std::tm* now_time = std::localtime(&t);
        std::stringstream buf;
        buf << std::put_time(now_time, "log_%a_%b_%d_%H_%M_%S.txt");
        std::string log_filename = buf.str();
        return log_filename;
    }


    DebugToolDialog::DebugToolDialog(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
        : DebugToolPanel(parent, id, pos, size, style)
        ,dev_manager_(*wxGetApp().getDeviceManager())
        , m_timer(new wxTimer)
        , m_deviceListTimer(new wxTimer(this, TIMER_ID))
{
    gcode_uploading = false;
    _3mf_uploading = false;

    init();

    init_model();

    init_bind();

    init_bind_handler();
}

    DebugToolDialog::~DebugToolDialog()
    {
        if (m_timer) {
            m_timer->Stop();
        }

        if (m_deviceListTimer)
            m_deviceListTimer->Stop();
    }

void DebugToolDialog::init()
{
    m_search_img = create_scaled_bitmap("search", nullptr, 24);

    m_bpButton_search->SetBitmap(m_search_img);

    search_dialog = new DeviceSearchDialog(this, wxID_ANY, "Search Printer");

    tray_model = new TrayListModel();
    m_dataViewCtrl_ams->AssociateModel(tray_model.get());

    m_dataViewCtrl_ams->AppendTextColumn("Name",
        TrayListModel::Col_TrayTitle,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Color",
        TrayListModel::Col_TrayColor,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Meterial",
        TrayListModel::Col_TrayMeterial,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("SN",
        TrayListModel::Col_TraySN,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Weight",
        TrayListModel::Col_TrayWeight,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Diameter",
        TrayListModel::Col_TrayDiameter,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Time",
        TrayListModel::Col_TrayTime,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Smooth",
        TrayListModel::Col_TraySmooth,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);

    cb_device_list->SetEditable(false);
    cb_device_list->Bind(wxEVT_COMBOBOX, &DebugToolDialog::on_select_device, this);
    btn_refresh_device_list->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        this->refresh_device_list();
        });

    btn_bind->Hide();
    btn_unbind->Hide();

    btn_connect->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        MachineObject* obj = dev_manager_.get_local_selected_machine();
        if (!obj) {
            this->send_log_evt("Invalid Printer! Please Select a Printer!");
            return;
        }

        std::string info = "MQTT connecting dev_id=" + obj->dev_id;
        this->send_log_evt(info);
        obj->connect();
    });
    btn_disconnect->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            MachineObject* obj = dev_manager_.get_local_selected_machine();
            if (!obj) {
                this->send_log_evt("Invalid Printer! Please Select a Printer!");
                return;
            }

            obj->disconnect();
            this->send_log_evt("disconnected with Printer=" + obj->dev_id);

            auto et = new wxCommandEvent(EVT_MQTT_DISCONNECTED, this->GetId());
            et->SetString("");
            wxQueueEvent(this, et);
        });

    cb_my_device_list->SetEditable(false);
    cb_my_device_list->Bind(wxEVT_COMBOBOX, &DebugToolDialog::on_select_mybind_device, this);

    btn_refresh_my_device->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
            dev->update_user_machine_list_info();
            wxQueueEvent(this, new SimpleEvent(EVT_UPDATE_MYBIND_LIST));
        });

    btn_get_version->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->get_version();
        });

    btn_refresh_upgrade_list->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->refresh_firmware_list(true);
        });

    btn_upgrade_firmware->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        std::string firmware_name = cb_upgrade_firmware->GetValue().ToStdString();
        if (firmware_name.empty()) {
            send_log_evt("Please select a firmware!");
            return;
        }

        if (cb_upgrade_module->GetValue().compare("") == 0) {
            send_log_evt("Please select a module!");
            return;
        }
        if (cb_upgrade_mode->GetValue().compare("") == 0) {
            send_log_evt("Please select a mode!");
            return;
        }

        int idx = cb_upgrade_firmware->GetSelection();
        if (idx >= upgrade_img_list.size() || idx < 0) return;

        std::string version = upgrade_img_list[idx].version;
        std::string dst_url = upgrade_img_list[idx].url;
        std::string module_name = get_curr_module_name();

        // send upgrade
        pt::ptree root, upgrade;
        upgrade.put<int>("sequence_id", this->m_sequence_id++);
        upgrade.put("command", "start");
        upgrade.put("url", dst_url);
        upgrade.put("module", module_name);
        upgrade.put("version", version);
        root.put_child("upgrade", upgrade);

        std::stringstream oss;
        pt::write_json(oss, root, false);
        std::string json_str = oss.str();
        json_str.erase(std::remove(json_str.begin(), json_str.end(), '\\'), json_str.end());
        if (this->publish_json(json_str) == 0) {
            this->log_info("Start Upgrading (Please wait several minutes)...");
        }
        });

    m_radioBox_server->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent &evt) {
        if (m_radioBox_server->GetSelection() == 0) {
            wxArrayString array_item;
            array_item.push_back(wxString("RK1126(AP)"));
            array_item.push_back(wxString("MC"));
            array_item.push_back(wxString("TH"));
            array_item.push_back(wxString("AMS"));
            array_item.push_back(wxString("OTA"));

            cb_upgrade_module->Set(array_item);
            cb_upgrade_module->SetSelection(0);
            cb_upgrade_mode->Enable();
            cb_upgrade_version->Enable();
        } else {
            wxArrayString array_item;
            array_item.push_back(wxString("OTA"));
            array_item.push_back(wxString("AMS"));
            cb_upgrade_module->Set(array_item);
            cb_upgrade_module->SetSelection(0);
            cb_upgrade_mode->Disable();
            cb_upgrade_version->Disable();
        }
    }
    );

    last_upgrade_module_sel = cb_upgrade_module->GetSelection();
    cb_upgrade_module->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& evt)
    {
        if (evt.GetSelection() != last_upgrade_module_sel)
            cb_upgrade_firmware->Clear();
        last_upgrade_module_sel = evt.GetSelection();
    });

    last_upgrade_mode_sel = cb_upgrade_mode->GetSelection();
    cb_upgrade_mode->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& evt)
    {
        if (evt.GetSelection() != last_upgrade_mode_sel)
            cb_upgrade_firmware->Clear();
        last_upgrade_mode_sel = evt.GetSelection();
    });

    last_upgrade_version_sel = cb_upgrade_version->GetSelection();
    cb_upgrade_version->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& evt)
    {
        if (evt.GetSelection() != last_upgrade_version_sel)
            cb_upgrade_firmware->Clear();
        last_upgrade_version_sel = evt.GetSelection();
    });


    cb_upgrade_module->SetEditable(false);
    cb_upgrade_firmware->SetEditable(false);
    cb_upgrade_mode->SetEditable(false);

    btn_run_gcode->Bind(wxEVT_BUTTON,
        [this](wxCommandEvent& evt) {
            Slic3r::DeviceManager* device_manager = Slic3r::GUI::wxGetApp().getDeviceManager();
            MachineObject* obj = device_manager->get_local_selected_machine();
            GcodePrintJob* m_print_job = new GcodePrintJob(m_status_bar, txt_gcode_filename->GetValue().ToUTF8().data(), obj);
            m_print_job->start();
        });

    btn_select_3mf_file->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        if (this->select3mfDialog->ShowModal() == wxID_CANCEL) return;

        txt_3mf_filename->SetValue(this->select3mfDialog->GetPath());
        this->SetFocus();
        });

    btn_select_gcode_file->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        if (this->selectGcodeDialog->ShowModal() == wxID_CANCEL) return;

        txt_gcode_filename->SetValue(this->selectGcodeDialog->GetPath());
        this->SetFocus();
        });
    
    btn_abort_print->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            json j;
            j["print"]["command"] = "stop";
            j["print"]["param"] = "";
            j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);

            int result = this->publish_json(j.dump());
        });

    btn_pause->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            pt::ptree root, print;
            print.put("command", "pause");
            print.put("param", "");
            print.put("sequence_id", this->m_sequence_id++);
            root.put_child("print", print);
            std::stringstream oss;
            pt::write_json(oss, root, false);
            std::string json_str = oss.str();

            int result = this->publish_json(json_str);
            if (result != 0) {
                this->log_info("publish_json failed");
            } else {
                this->send_log_evt("Pause Printing...");
            }
        });

    btn_resume->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            pt::ptree root, print;
            print.put("command", "resume");
            print.put("param", "");
            print.put("sequence_id", this->m_sequence_id++);
            root.put_child("print", print);
            std::stringstream oss;
            pt::write_json(oss, root, false);
            std::string json_str = oss.str();

            int result = this->publish_json(json_str);
            if (result != 0) {
                this->log_info("publish_json failed");
            } else {
                this->send_log_evt("Resume Printing...");
            }
        });

    selectGcodeDialog = new wxFileDialog(this, "Open Gcode File", "", "", "Gcode files(*.gcode)|*.gcode", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    select3mfDialog = new wxFileDialog(this, "Open 3MF File", "", "", "Slice files(*.3mf)|*.3mf", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    btn_return_home->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G28 \n");
        });

    m_button_calibration->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G28 \nG90 \nG0 X128 Y128 F30000\nM970 Q1 A7 B10 C130 K0\nM970 Q1 A7 B131 C250 K1\n"
            "M974 Q1 S2 P0 \nM970 Q0 A9 B10 C130 H20 K0 \nM970 Q0 A9 B131 C250 K1 \nM974 Q0 S2 P0 \nM500 \n"
            "G0 X120 Y240 Z100 F1800 \n");
        });

    btn_auto_leveling->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G29 \n");
        });
    btn_xyz_abs_mode->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G90 \n");
        });
    btn_fan_on->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M106 S255 \n");
        });
    btn_fan_off->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M106 S0 \n");
        });
    btn_set_hot_bed_temp->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        std::string gcode_str = "M140 S" + txt_set_hot_bed_temp->GetValue().ToStdString() + " \n";
        this->publishGcode(gcode_str);
        });
    btn_set_hot_end_temp->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        std::string gcode_str = "M104 S" + txt_set_hot_end_temp->GetValue().ToStdString() + " \n";
        this->publishGcode(gcode_str);
        });

    btn_switch_t->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        std::string gcode = this->switch_ams_gcode(txt_switch_val->GetValue().ToStdString());
        this->publishGcode(gcode);
        });

    m_button_ams_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        std::string gcode = this->switch_ams_gcode("0");
        this->publishGcode(gcode);
        });
    m_button_ams_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        std::string gcode = this->switch_ams_gcode("1");
        this->publishGcode(gcode);
        });
    m_button_ams_2->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        std::string gcode = this->switch_ams_gcode("2");
        this->publishGcode(gcode);
        });
    m_button_ams_3->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        std::string gcode = this->switch_ams_gcode("3");
        this->publishGcode(gcode);
        });
    m_button_ams_255->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        std::string gcode = this->switch_ams_gcode("255");
        this->publishGcode(gcode);
        });


    btn_send_gcode_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        std::string gcode1 = txt_custom_gcode1->GetValue().ToStdString() + "\n";
        this->publishGcode(txt_custom_gcode1->GetValue().ToStdString());
        });
    btn_send_gcode_2->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode(txt_custom_gcode2->GetValue().ToStdString());
        });
    btn_send_gcode_3->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode(txt_custom_gcode3->GetValue().ToStdString());
        });
    btn_send_gcode_4->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode(txt_custom_gcode4->GetValue().ToStdString());
        });
    btn_send_gcode_5->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode(txt_custom_gcode5->GetValue().ToStdString());
        });
    btn_send_gcode_6->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode(txt_custom_gcode6->GetValue().ToStdString());
        });
    btn_send_gcode_7->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode(txt_custom_gcode7->GetValue().ToStdString());
        });

    m_button_ams_pause->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            pt::ptree root, print;
            print.put("command", "ams_control");
            print.put("param", "pause");
            print.put("sequence_id", this->m_sequence_id++);
            root.put_child("print", print);
            std::stringstream oss;
            pt::write_json(oss, root, false);
            std::string json_str = oss.str();

            int result = this->publish_json(json_str);
            if (result != 0) {
                this->log_info("publish_json failed");
            } else {
                this->send_log_evt("Pause AMS...");
            }
        });

    m_button_ams_resume->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            pt::ptree root, print;
            print.put("command", "ams_control");
            print.put("param", "resume");
            print.put("sequence_id", this->m_sequence_id++);
            root.put_child("print", print);
            std::stringstream oss;
            pt::write_json(oss, root, false);
            std::string json_str = oss.str();

            int result = this->publish_json(json_str);
            if (result != 0) {
                this->log_info("publish_json failed");
            } else {
                this->send_log_evt("Resume AMS...");
            }
        });
    

    m_bpButton_search->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            if (btn_connect->IsEnabled()) {
                search_dialog->Show();
                search_dialog->SetFocus();
            } else {
                wxMessageBox("Please Disconnected First!");
            }
        });

    radio_btn_wan->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent& evt) {
            ;
    });

    m_radioBox_chamber_light->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent& evt) {
            MachineObject* obj = dev_manager_.get_local_selected_machine();
            if (!obj) {
                this->send_log_evt("Invalid Printer! Please Select a Printer!");
                return;
            }
            if (evt.GetInt() == 0)
                obj->command_set_chamber_light(MachineObject::LIGHT_EFFECT::LIGHT_EFFECT_ON);
            else if (evt.GetInt() == 1)
                obj->command_set_chamber_light(MachineObject::LIGHT_EFFECT::LIGHT_EFFECT_OFF);
            else if (evt.GetInt() == 2)
                obj->command_set_chamber_light(MachineObject::LIGHT_EFFECT::LIGHT_EFFECT_FLASHING);
        });

    m_radioBox_work_light->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent& evt) {
            MachineObject* obj = dev_manager_.get_local_selected_machine();
            if (!obj) {
                this->send_log_evt("Invalid Printer! Please Select a Printer!");
                return;
            }
            if (evt.GetInt() == 0)
                obj->command_set_work_light(MachineObject::LIGHT_EFFECT::LIGHT_EFFECT_ON);
            else if (evt.GetInt() == 1)
                obj->command_set_work_light(MachineObject::LIGHT_EFFECT::LIGHT_EFFECT_OFF);
            else if (evt.GetInt() == 2)
                obj->command_set_work_light(MachineObject::LIGHT_EFFECT::LIGHT_EFFECT_FLASHING);
        });
}

void DebugToolDialog::init_model()
{
    tray_model = new TrayListModel();
    m_dataViewCtrl_ams->AssociateModel(tray_model.get());

    m_dataViewCtrl_ams->AppendTextColumn("Name",
        TrayListModel::Col_TrayTitle,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Color",
        TrayListModel::Col_TrayColor,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Meterial",
        TrayListModel::Col_TrayMeterial,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("SN",
        TrayListModel::Col_TraySN,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Weight",
        TrayListModel::Col_TrayWeight,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Diameter",
        TrayListModel::Col_TrayDiameter,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Time",
        TrayListModel::Col_TrayTime,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Smooth",
        TrayListModel::Col_TraySmooth,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
}

void DebugToolDialog::init_bind()
{
    btn_set_x_pos_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 X0.1 F3000 \n");
        });
    btn_set_x_pos_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 X1.0 F3000 \n");
        });
    btn_set_x_pos_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 X10.0 F3000 \n");
        });
    
    btn_set_x_neg_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 X-0.1 F3000 \n");
        });
    
    btn_set_x_neg_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 X-1.0 F3000 \n");
        });
    
    btn_set_x_neg_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 X-10.0 F3000 \n");
        });
    
    btn_set_y_pos_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Y0.1 F3000 \n");
        });
    
    btn_set_y_pos_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Y1.0 F3000 \n");
        });
    
    btn_set_y_pos_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Y10.0 F3000 \n");
        });
    
    btn_set_y_neg_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Y-0.1 F3000 \n");
        });
    
    btn_set_y_neg_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Y-1.0 F3000 \n");
        });
    
    btn_set_y_neg_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Y-10.0 F3000 \n");
        });
    
    btn_set_z_pos_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Z0.1 F900 \n");
        });
    
    btn_set_z_pos_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Z1.0 F900 \n");
        });
    
    btn_set_z_pos_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Z10.0 F900 \n");
        });
    
    btn_set_z_neg_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Z-0.1 F900 \n");
        });
    
    btn_set_z_neg_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Z-1.0 F900 \n");
        });
    
    btn_set_z_neg_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G91 \nG0 Z-10.0 F900 \n");
        });
    
    btn_set_e_pos_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M83 \nG0 E0.1 F300 \n");
        });
    
    btn_set_e_pos_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M83 \nG0 E1.0 F300 \n");
        });
    
    btn_set_e_pos_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M83 \nG0 E10.0 F300 \n");
        });
    
    btn_set_e_neg_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M83 \nG0 E-0.1 F300 \n");
        });
    
    btn_set_e_neg_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M83 \nG0 E-1.0 F300 \n");
        });
    
    btn_set_e_neg_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M83 \nG0 E-10.0 F300 \n");
        });

    m_button_upgrade_confirm->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            pt::ptree root, upgrade;
            upgrade.put("command", "upgrade_confirm");
            upgrade.put("sequence_id", this->m_sequence_id++);
            root.put_child("upgrade", upgrade);
            std::stringstream oss;
            pt::write_json(oss, root, false);
            std::string json_str = oss.str();

            int result = this->publish_json(json_str);
            if (result != 0) {
                this->log_info("publish_json failed");
            } else {
                this->send_log_evt("upgrade new version confirm");
            }
        });

    m_button_consistency_upgrade_confirm->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            pt::ptree root, upgrade;
            upgrade.put("command", "consistency_confirm");
            upgrade.put("sequence_id", this->m_sequence_id++);
            root.put_child("upgrade", upgrade);
            std::stringstream oss;
            pt::write_json(oss, root, false);
            std::string json_str = oss.str();

            int result = this->publish_json(json_str);
            if (result != 0) {
                this->log_info("publish_json failed");
            } else {
                this->send_log_evt("consistency upgrade confirm");
            }
        }
    );

}

void DebugToolDialog::init_bind_handler()
{
    Bind(EVT_WLAN_3MF_PROGRESS, [this](wxCommandEvent& evt) {
        std::string text;
        text = std::to_string(evt.GetInt()) + "%";
        this->label_3mf_progress->SetLabelText(text);
        });

    Bind(EVT_UPDATE_LIST, &DebugToolDialog::on_update_list, this);
    Bind(EVT_REFRESH_LIST, &DebugToolDialog::on_update_list, this);
    Bind(EVT_UPDATE_MYBIND_LIST, &DebugToolDialog::on_update_mybind_list, this);
    Bind(EVT_MQTT_CONNECTED, &DebugToolDialog::on_mqtt_connected, this);
    Bind(EVT_MQTT_DISCONNECTED, &DebugToolDialog::on_mqtt_disconnected, this);
    Bind(EVT_MQTT_FAILED, &DebugToolDialog::on_mqtt_failed, this);
    Bind(EVT_MQTT_LOST, &DebugToolDialog::on_mqtt_lost, this);
    Bind(EVT_MESSAGE_ARRIVED, &DebugToolDialog::on_message_arrived, this);
    Bind(EVT_MESSAGE_SENT, &DebugToolDialog::on_message_sent, this);
    Bind(EVT_LOG_INFO, &DebugToolDialog::on_log_info, this);
}

std::string DebugToolDialog::get_curr_module_name()
{
    if (m_radioBox_server->GetSelection() == 0) {
        UPGRADE_MODULE upgrade_module = (UPGRADE_MODULE) cb_upgrade_module->GetCurrentSelection();
        return upgrade_module_name[upgrade_module];
    }
    if (m_radioBox_server->GetSelection() == 1) {
        if (cb_upgrade_module->GetCurrentSelection() == 0) 
            return "ota";
        if (cb_upgrade_module->GetCurrentSelection() == 1)
            return "ams";
    }
    return "";
}

void DebugToolDialog::on_update_list(SimpleEvent& evt)
{
    int select = -1;
    std::string last_dev_id;
    if (last_device_selection < machine_list_items.size()) {
        last_dev_id = machine_list_items[last_device_selection];
        BOOST_LOG_TRIVIAL(trace) << "last_dev_id = " << last_dev_id;
    }

    /* dislay list */
    BBL::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    std::string username = account_manager->get_user_name();

    if (!account_manager->is_user_login()) {
        wxGetApp().request_login();
        return;
    }

    std::map<std::string, MachineObject*> list = dev_manager_.get_local_machine_list();
    std::vector<MachineObject*> display_list;

    // coconut: sort the device list by: 1) own device first, then free, then others; 2) small dev_id (MAC address) (or may be dev_ip?)
    std::transform(list.begin(), list.end(), std::back_inserter(display_list), [](auto& a) {return a.second; });
    username = username.substr(0, username.find_first_of("@"));
    std::sort(display_list.begin(), display_list.end(), [&](auto a, auto b) {
            auto priority = [&](auto a, auto b) {
                int f = a->bind_user_name.compare(username) == 0 ? 1: 0;
                int c = a->bind_user_name.compare("null") == 0 ? 1: 0;
                int d = a->dev_id < b->dev_id ? 1: 0;
                return f * 100 + c * 10 + d * 1;
            };
            return priority(a, b) > priority(b, a);
        });

    std::vector<MachineObject*>::iterator iter;
    machine_list_items.clear();

    device_list_items.clear();
    for (iter = display_list.begin(); iter != display_list.end(); iter++) {
        wxString text = get_machine_display_item(*iter);
        if (!last_dev_id.empty() && (*iter)->dev_id.compare(last_dev_id) == 0) {
            select = device_list_items.size();
        }
        machine_list_items.push_back((*iter)->dev_id);
        device_list_items.Add(text);
    }

    cb_device_list->Set(device_list_items);
    if (select >= 0) {
        cb_device_list->Select(select);
        last_device_selection = select;
    }
}

void DebugToolDialog::on_update_mybind_list(SimpleEvent& evt)
{
    BBL::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    int select = -1;
    std::string last_my_bind_dev_id;
    if (last_wlan_device_selection < mybind_machine_list_items.size()) {
        last_my_bind_dev_id = mybind_machine_list_items[last_wlan_device_selection];
    }
    
    DeviceManager* dev = wxGetApp().getDeviceManager();
    std::map<std::string, MachineObject*> list = dev->userMachineList;
    std::map<std::string, MachineObject*>::iterator iter;
    mybind_machine_list_items.clear();
    wxArrayString new_items;
    for (iter = list.begin(); iter != list.end(); iter++) {
        wxString online_status = iter->second->is_online() ? _L("Online") : _L("Offline");
        wxString text = wxString::Format("%s(%s)[%s]", iter->second->dev_name, iter->second->dev_id, online_status);
        if (!last_my_bind_dev_id.empty() && iter->second->dev_id.compare(last_my_bind_dev_id) == 0) {
            select = new_items.size();
        }
        mybind_machine_list_items.push_back(iter->second->dev_id);
        new_items.Add(text);
    }

    cb_my_device_list->Set(new_items);
    if (select >= 0) {
        cb_my_device_list->Select(select);
        last_wlan_device_selection = select;
    }
    cb_my_device_list->Enable();
}

void DebugToolDialog::on_mqtt_failed(wxCommandEvent& evt)
{
    this->log_info("MQTT Connect Failed! client=" + evt.GetString().ToStdString());
    btn_disconnect->Disable();
    btn_connect->Enable();
    btn_refresh_device_list->Enable();
    cb_device_list->Enable();
    radio_btn_lan->SetValue(true);
    BBL::SsdpDiscovery* backend = wxGetApp().getSsdpDiscovery();
    backend->set_ssdp_discovery(true);
}

void DebugToolDialog::on_mqtt_lost(wxCommandEvent& evt)
{
    this->log_info("MQTT Lost... client=" + evt.GetString().ToStdString());
    btn_disconnect->Disable();

    btn_connect->Enable();
    btn_refresh_device_list->Enable();
    cb_device_list->Enable();
    radio_btn_lan->SetValue(true);
    BBL::SsdpDiscovery* ssdp = wxGetApp().getSsdpDiscovery();
    ssdp->set_ssdp_discovery(true);
}

void DebugToolDialog::on_mqtt_connected(wxCommandEvent& evt)
{
    btn_disconnect->Enable();
    btn_connect->Disable();
    btn_refresh_device_list->Disable();
    cb_device_list->Disable();
    radio_btn_lan->SetValue(true);
    BBL::SsdpDiscovery* backend = wxGetApp().getSsdpDiscovery();
    backend->set_ssdp_discovery(false);
}

void DebugToolDialog::on_mqtt_disconnected(wxCommandEvent& evt)
{
    btn_disconnect->Disable();
    btn_connect->Enable();
    btn_refresh_device_list->Enable();
    cb_device_list->Enable();
    radio_btn_lan->SetValue(true);
    BBL::SsdpDiscovery* backend = wxGetApp().getSsdpDiscovery();
    backend->set_ssdp_discovery(true);
}


void DebugToolDialog::get_version() {

    json j;
    j["info"]["sequence_id"] = std::to_string(this->m_sequence_id++);
    j["info"]["command"] = "get_version";
    this->publish_json(j.dump());
}

void DebugToolDialog::message_arrived(std::string dev_id, std::string msg)
{
    if (!radio_btn_lan->GetValue()) {
        this->mqtt_msg_queue_cloud.push(msg);
    } else {
        this->mqtt_msg_queue.push(msg);
    }
    auto evt = new wxCommandEvent(EVT_MESSAGE_ARRIVED, this->GetId());
    evt->SetString(dev_id);
    wxQueueEvent(this, evt);
}

void DebugToolDialog::jump_to_printer(wxString selected)
{
    if (selected.empty()) return;

    bool found = false;
    int selected_idx = 0;
    for (int i = 0; i < device_list_items.size(); i++)
    {
        if (boost::starts_with(device_list_items[i], selected.substr(0, 20))) {
            selected_idx = i;
            found = true;
            break;
        }
    }

    if (found) {
        cb_device_list->Select(selected_idx);
        wxCommandEvent* event = new wxCommandEvent(wxEVT_COMBOBOX, cb_device_list->GetId());
        event->SetInt(selected_idx);
        wxQueueEvent(cb_device_list, event);
    }
    else {
        cb_device_list->Clear();
        cb_device_list->Append(selected);
        cb_device_list->Select(0);
        wxCommandEvent* event = new wxCommandEvent(wxEVT_COMBOBOX, cb_device_list->GetId());
        event->SetInt(selected_idx);
        wxQueueEvent(cb_device_list, event);
    }
}

std::string DebugToolDialog::switch_ams_gcode(std::string t)
{
    Slic3r::Print& print = wxGetApp().plater()->get_partplate_list().get_current_fff_print();
    PlaceholderParser m_placeholder_parser;
    m_placeholder_parser = print.placeholder_parser();
    PlaceholderParser::ContextData      m_placeholder_parser_context;

    const PrintConfig& print_config = print.config();
    DynamicConfig dyn_config;
    int old_filament_temp = atoi(txt_ams_flush_temp1->GetValue().ToStdString().c_str());
    int new_filament_temp = atoi(txt_ams_flush_temp2->GetValue().ToStdString().c_str());
    old_filament_temp = std::min(old_filament_temp, 300);
    old_filament_temp = std::max(old_filament_temp, 120);
    new_filament_temp = std::min(new_filament_temp, 300);
    new_filament_temp = std::max(new_filament_temp, 120);
    dyn_config.set_key_value("previous_extruder", new ConfigOptionInt(-1));
    dyn_config.set_key_value("next_extruder",     new ConfigOptionInt(atoi(t.c_str())));
    dyn_config.set_key_value("layer_num",         new ConfigOptionInt(0));
    dyn_config.set_key_value("layer_z",           new ConfigOptionFloat(0.3));
    dyn_config.set_key_value("max_layer_z",       new ConfigOptionFloat(10.));
    dyn_config.set_key_value("relative_e_axis", new ConfigOptionBool(RELATIVE_E_AXIS));
    dyn_config.set_key_value("toolchange_count", new ConfigOptionInt(1));
    dyn_config.set_key_value("fan_speed", new ConfigOptionInt(0));
    dyn_config.set_key_value("old_retract_length", new ConfigOptionFloat(2.));
    dyn_config.set_key_value("new_retract_length", new ConfigOptionFloat(2.));
    dyn_config.set_key_value("old_retract_length_toolchange", new ConfigOptionFloat(3.0));
    dyn_config.set_key_value("new_retract_length_toolchange", new ConfigOptionFloat(3.0));
    dyn_config.set_key_value("old_filament_temp", new ConfigOptionInt(old_filament_temp));
    dyn_config.set_key_value("new_filament_temp", new ConfigOptionInt(new_filament_temp));
    dyn_config.set_key_value("x_after_toolchange", new ConfigOptionFloat(50.));
    dyn_config.set_key_value("y_after_toolchange", new ConfigOptionFloat(50.));
    dyn_config.set_key_value("z_after_toolchange", new ConfigOptionFloat(10.));
    dyn_config.set_key_value("first_flush_volume", new ConfigOptionFloat(5.f));
    dyn_config.set_key_value("second_flush_volume", new ConfigOptionFloat(5.f));

    try {
        std::string parsed_command = m_placeholder_parser.process(print_config.change_filament_gcode.value, std::stoi(t.c_str()), &dyn_config, &m_placeholder_parser_context);
        // config xyz coordinate mode
        std::string auto_home_command = cbox_ams_auto_home->GetValue() ? "G28 X\n" : "";
        parsed_command = "G90\n" + auto_home_command + parsed_command;
        std::regex match_pattern(";.*\n");
        std::string replace_pattern = "\n";
        char result[1024] = { 0 };
        std::regex_replace(result, parsed_command.begin(), parsed_command.end(), match_pattern, replace_pattern);
        result[1023] = 0;
        return result;
    }
    catch (Exception& e) {
        BOOST_LOG_TRIVIAL(trace) << "exception, e=" << e.what();
        return "";
    }
}

bool DebugToolDialog::Show(bool show)
{
    BBL::SsdpDiscovery* backend = wxGetApp().getSsdpDiscovery();
    BBL::AccountManager *c = Slic3r::GUI::wxGetApp().getAccountManager();
    if (show) {
        if (backend) {
            backend->start();
            if (btn_connect->IsEnabled()) {
                backend->set_ssdp_discovery(true);
            }
        }
        m_timer->Stop();
        m_timer->SetOwner(this);
        m_timer->Start(10000);

        if (c)
            c->start_subscribe("debug");
    }
    else {
        if (backend) {
            backend->stop();
            backend->set_ssdp_discovery(false);
        }
        m_timer->Stop();

        if (c)
            c->stop_subscribe("debug");
    }

    return wxPanel::Show(show);
}


int DebugToolDialog::publish_json(std::string json_str)
{
    BBL::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    /* lan send json */
    if (radio_btn_lan->GetValue()) {
        std::string user_name = account_manager->get_user_name();
        std::transform(user_name.begin(), user_name.end(), user_name.begin(),
            [](unsigned char c) { return std::tolower(c); });

        MachineObject* obj = dev_manager_.get_local_selected_machine();
        if (!obj) {
            this->send_log_evt("Invalid Printer! Please Select a Printer!");
            return -1;
        }
        obj->local_publish_json(json_str);

        auto evt = new wxCommandEvent(EVT_MESSAGE_SENT, this->GetId());
        std::string send_msg = "dev_id=" + obj->dev_id + ", send msg=" + json_str;
        evt->SetString(send_msg);
        wxQueueEvent(this, evt);
    }
    else {
        MachineObject* obj = dev_manager_.get_default_machine();
        if (!obj) {
            this->send_log_evt("Invalid Printer! Please Select a Printer!");
            return -1;
        }
        auto evt = new wxCommandEvent(EVT_MESSAGE_SENT, this->GetId());
        std::string send_msg = "dev_id=" + obj->dev_id + ", send msg=" + json_str;
        evt->SetString(send_msg);
        wxQueueEvent(this, evt);
        obj->publish_json(json_str);
    }

    return 0;
}


void DebugToolDialog::on_message_sent(wxCommandEvent& evt)
{
    this->log_info(evt.GetString().ToStdString());
}

void DebugToolDialog::on_local_connected(int state, std::string dev_id, std::string msg)
{
    MachineObject* obj = dev_manager_.get_local_selected_machine();
    if (state == BBL::ConnectStatus::ConnectStatusOk) {
        obj->set_connect_state(MachineObject::CONNECTION_STATE::STATE_CONNECTED);
        this->send_log_evt("Connected to Printer=" + dev_id);
        auto evt = new wxCommandEvent(EVT_MQTT_CONNECTED, this->GetId());
        evt->SetString(msg);
        wxQueueEvent(this, evt);
    }
    else if (state == BBL::ConnectStatus::ConnectStatusFailed) {
        obj->set_connect_state(MachineObject::CONNECTION_STATE::STATE_DISCONNECTED);
        auto evt = new wxCommandEvent(EVT_MQTT_FAILED, this->GetId());
        evt->SetString(msg);
        wxQueueEvent(this, evt);
    }
    else if (state == BBL::ConnectStatus::ConnectStatusLost) {
        obj->set_connect_state(MachineObject::CONNECTION_STATE::STATE_DISCONNECTED);
        auto evt = new wxCommandEvent(EVT_MQTT_LOST, this->GetId());
        evt->SetString(msg);
        wxQueueEvent(this, evt);
    }
}

void DebugToolDialog::on_log_info(wxCommandEvent& evt)
{
    this->log_info(evt.GetString().ToStdString());
}


void DebugToolDialog::on_message_arrived(wxCommandEvent &evt)
{
    MachineObject *obj = nullptr;
    if (radio_btn_lan->GetValue()) {
        obj = dev_manager_.get_local_selected_machine();
    } else {
        obj = dev_manager_.get_default_machine();
    }

    if (!obj) return;

    tray_model->update(obj);

    wxString big1_speed_text = wxString::Format("%d", obj->big_fan1_speed);
    m_staticText_big1_speed->SetLabelText(big1_speed_text);

    wxString big2_speed_text = wxString::Format("%d", obj->big_fan2_speed);
    m_staticText_big2_speed->SetLabelText(big2_speed_text);

    wxString cooling_speed_text = wxString::Format("%d", obj->cooling_fan_speed);
    m_staticText_cooling_speed->SetLabelText(cooling_speed_text);

    wxString heatbreak_speed_text = wxString::Format("%d", obj->heatbreak_fan_speed);
    m_staticText_heatbreak_speed->SetLabelText(heatbreak_speed_text);

    wxString print_state_text = wxString::Format("%d", obj->mc_print_stage);
    m_staticText_mc_print_stage->SetLabelText(print_state_text);

    wxString print_sub_state_text = wxString::Format("%d", obj->mc_print_sub_stage);
    m_staticText_mc_sub_stage_value->SetLabelText(print_sub_state_text);


    wxString print_err_code_text = wxString::Format("%d", obj->mc_print_error_code);
    m_staticText_mc_print_error_code->SetLabelText(print_err_code_text);

    wxString gcode_line_text = wxString::Format("%d", obj->mc_print_line_number);
    m_staticText_mc_print_line_number->SetLabelText(gcode_line_text);

    wxString chamber_text = wxString::Format("%0.2fC", obj->chamber_temp);
    m_staticText_volume_temp_val->SetLabelText(chamber_text);

    wxString frame_temp_text = wxString::Format("%0.2fC", obj->frame_temp);
    m_staticText_frame_temp_value->SetLabelText(frame_temp_text);

    wxString subtask_id = "N/A";
    if (obj->subtask_) {
        subtask_id = wxString::Format("%s", obj->subtask_->task_id);
        
    }

    wxString mc_percent_text = wxString::Format("%d", obj->mc_print_percent);
    label_print_progress_val->SetLabelText(mc_percent_text);
    m_staticText_subtask_id->SetLabelText(subtask_id);
    

    /* upgrade */
    if (obj->upgrade_new_version) {
        m_button_upgrade_confirm->Enable();
        m_staticText_new_version->SetLabelText("True");
    }
    else {
        m_button_upgrade_confirm->Disable();
        m_staticText_new_version->SetLabelText("False");
    }

    if (obj->upgrade_consistency_request) {
        m_button_consistency_upgrade_confirm->Enable();
        m_staticText_request_consisitency_upgrade->SetLabelText("True");
    }
    else {
        m_button_consistency_upgrade_confirm->Disable();
        m_staticText_request_consisitency_upgrade->SetLabelText("False");
    }

    m_staticText_upgrade_module_value->SetLabelText(obj->upgrade_module);
    label_upgrade_status_val->SetLabelText(obj->upgrade_status);
    label_upgrade_progress_val->SetLabelText(obj->upgrade_progress);
    label_upgrade_message_val->SetLabelText(obj->upgrade_message);
    wxString nozzle_temp_text = wxString::Format("%.2f/%.2f", obj->nozzle_temp, obj->nozzle_temp_target);
    label_hot_end_temp_val->SetLabelText(nozzle_temp_text);
    wxString bed_temp_text = wxString::Format("%.2f/%2.f", obj->bed_temp, obj->bed_temp_target);
    label_bed_end_temp_val->SetLabelText(bed_temp_text);


    m_staticText_upgrade_module_value->SetLabelText(obj->upgrade_module);
    label_upgrade_status_val->SetLabelText(obj->upgrade_status);
    label_upgrade_progress_val->SetLabelText(obj->upgrade_progress);
    label_upgrade_message_val->SetLabelText(obj->upgrade_message);
    label_wifi_signal_val->SetLabelText(obj->wifi_signal);
    label_force_upgrade_val->SetLabelText(obj->upgrade_force_upgrade ? "True": "False");

    std::string json_str;
    if (radio_btn_lan->GetValue()) {
        if (mqtt_msg_queue.empty()) {
            return;
        }
        json_str = mqtt_msg_queue.front();
        mqtt_msg_queue.pop();
    } else {
        if (mqtt_msg_queue_cloud.empty()) {
            return;
        }
        json_str = mqtt_msg_queue_cloud.front();
        mqtt_msg_queue_cloud.pop();
    }

    try {
        BOOST_LOG_TRIVIAL(trace) << "on_message_arrived: json_str=" << json_str;
        std::stringstream ss(json_str);
        pt::ptree root;
        pt::read_json(ss, root);
        
        if (root.empty()) {
            return;
        }

        if (root.get_child_optional("print") != boost::none) {
            pt::ptree print = root.get_child("print");
            /* Update labels */
            boost::optional<std::string> command = print.get_optional<std::string>("command");
            if (command.has_value() &&  command.value_or("").compare("push_status") == 0) {
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
                return;
            }
            this->log_info("received ack msg = " + json_str);
            return;
        }
        else if (root.get_child_optional("info") != boost::none) {
            this->log_info("received ack msg = " + json_str);
            return;
        }
        else if (root.get_child_optional("upgrade") != boost::none) {
            return;
        }
        else if (root.get_child_optional("system") != boost::none) {
            return;
        }
        this->log_info("dev_id=" + evt.GetString().ToStdString() + ", json=" + json_str);
    }
    catch (std::exception& e) {
        std::string info = "parsing report msg error, json_str=" + json_str;
        this->log_info(info);
    }
    catch (...) {
        BOOST_LOG_TRIVIAL(trace) << "Uknown Exception,  json_str=" << json_str;
    }
}

void DebugToolDialog::refresh_device_list()
{
    BBL::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    if (!account_manager->is_user_login()) {
        wxQueueEvent(this, new SimpleEvent(EVT_UPDATE_LIST));
        return;
    }

    dev_manager_.query_bind_status();
    wxQueueEvent(this, new SimpleEvent(EVT_UPDATE_LIST));
}

wxString DebugToolDialog::get_machine_display_item(MachineObject* obj)
{
    if (obj->dev_name.empty())
        return wxString::Format("%-16s(%s)[bind:%s]", obj->dev_ip, obj->dev_id, obj->get_bind_str());
    else
        return wxString::Format("%-16s(%s)[bind:%s]", obj->dev_ip, obj->dev_name, obj->get_bind_str());
}

void DebugToolDialog::refresh_firmware_list(bool show_error)
{
    cb_upgrade_firmware->Clear();
    upgrade_img_list.clear();
    upgrade_file_list.clear();
    if (cb_upgrade_module->GetValue().compare("") == 0) {
        std::string log = "Please select a module!";
        this->send_log_evt(log);
        return;
    }
    UPGRADE_MODULE upgrade_module = (UPGRADE_MODULE)cb_upgrade_module->GetCurrentSelection();
    UPGRADE_MODE upgrade_mode = (UPGRADE_MODE)cb_upgrade_mode->GetCurrentSelection();
    std::string hardware_version;
    if (cb_upgrade_version->GetCurrentSelection() == 0) {
        hardware_version = "v8";
    } else if (cb_upgrade_version->GetCurrentSelection() == 1) {
        hardware_version = "v7";
    } else if (cb_upgrade_version->GetCurrentSelection() == 2) {
        hardware_version = "v6";
    } else if (cb_upgrade_version->GetCurrentSelection() == 3) {
        hardware_version = "v5";
    } else {
        hardware_version = "v7";
    }

    MachineObject *obj        = dev_manager_.get_local_selected_machine();
    if (!obj)
        return;

    int server_sel = m_radioBox_server->GetSelection();
    if (server_sel == 1) {
        BBL::AccountManager* acc = Slic3r::GUI::wxGetApp().getAccountManager();
        if (!obj) {
            this->send_log_evt("Please Select a printer");
            return;
        }
        int result = 0;
        unsigned int http_code;
        std::string http_body;
        result = acc->get_machine_version(obj->dev_id, http_code, http_body);
        if (result < 0) {
            std::string error = (boost::format("get upgrade list failed! code = %1%, body = %2%") % http_code % http_body).str();
            this->send_log_evt(error);
            return;
        }
        try {
            json j = json::parse(http_body);
            if (j.contains("devices") && !j["devices"].is_null()) {
                for (json::iterator it = j["devices"].begin(); it != j["devices"].end(); it++) {
                    if ((*it)["dev_id"].get<std::string>() == obj->dev_id) {
                        //select ota
                        if (cb_upgrade_module->GetSelection() == 0) {
                            json firmware = (*it)["firmware"];
                            for (json::iterator firmware_it = firmware.begin(); firmware_it != firmware.end(); firmware_it++) {
                                UpgradeItem item;
                                item.version   = (*firmware_it)["version"].get<std::string>();
                                item.url       = (*firmware_it)["url"].get<std::string>();
                                int name_start = item.url.find_last_of('/') + 1;
                                if (name_start > 0) {
                                    item.name = item.url.substr(name_start, item.url.length() - name_start);
                                    upgrade_img_list.push_back(item);
                                    upgrade_file_list.push_back(item.name);
                                } else {
                                    BOOST_LOG_TRIVIAL(trace) << "skip";
                                }
                            }
                        }
                        try {
                            //select ams
                            if (cb_upgrade_module->GetSelection() == 1) {
                                if ((*it).contains("ams")) {
                                    json ams_list = (*it)["ams"];
                                    if (ams_list.size() > 0) {
                                        auto ams_front = ams_list.front();
                                        json firmware_ams = (ams_front)["firmware"];
                                        for (json::iterator ams_it = firmware_ams.begin(); ams_it != firmware_ams.end(); ams_it++) {
                                            UpgradeItem item;
                                            item.version   = (*ams_it)["version"].get<std::string>();
                                            item.url       = (*ams_it)["url"].get<std::string>();
                                            int name_start = item.url.find_last_of('/') + 1;
                                            if (name_start > 0) {
                                                item.name = item.url.substr(name_start, item.url.length() - name_start);
                                                upgrade_img_list.push_back(item);
                                                upgrade_file_list.push_back(item.name);
                                            } else {
                                                BOOST_LOG_TRIVIAL(trace) << "skip";
                                            }
                                        }    
                                    }
                                }
                            }
                        } catch(...) {
                            ;
                        }
                    }
                }
            }
            cb_upgrade_firmware->Set(upgrade_file_list);
            cb_upgrade_firmware->Select(0);
        }
        catch(...) {
            std::string error = (boost::format("get upgrade list failed! parse_error, code = %1%, body = %2%") % http_code % http_body).str();
            this->send_log_evt(error);
        }
    }
    else if (server_sel == 0) {
        std::string url = (boost::format("%1%?module_name=%2%&build_type=%3%&hardware_version=%4%&firmware_type=%5%")
                            % UPGRADE_URL
                            % upgrade_post_url[upgrade_module]
                            % upgrade_mode_name[upgrade_mode]
                            % hardware_version
                            % obj->get_firmware_type_str()).str();
        BBL::Http http = BBL::Http::get(url);
        http.auth_basic("slicer", "znFx94AAew8VVHv");
        http.on_complete([this](std::string body, unsigned) {
            try{
                json j = json::parse(body);
                for (json::iterator it = j.begin(); it != j.end(); ++it) {
                    UpgradeItem item;
                    item.name = (*it)["name"];
                    item.version = (*it)["version"];
                    item.url = (*it)["url"];
                    upgrade_file_list.push_back((*it)["name"]);
                    upgrade_img_list.push_back(item);
                }
            }
            catch (...) {
                ;
            }
            cb_upgrade_firmware->Set(upgrade_file_list);
            cb_upgrade_firmware->Select(0);
            }).on_error([this](std::string body, std::string error, unsigned status) {
                this->send_log_evt("Get Upgrade List Failed! error=" + error);
            }).perform();
    }
}

void DebugToolDialog::send_log_evt(std::string info)
{
    auto evt = new wxCommandEvent(EVT_LOG_INFO, this->GetId());
    evt->SetString(info);
    wxQueueEvent(this, evt);
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
    BBL::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    if (radio_btn_lan->GetValue()) {
        int result = 0;
        // can not publish gcode when logout
        if (!account_manager->is_user_login()) {
            this->log_info("Please login first!");
            return -1;
        }
        BBL::AccountInfo *info = account_manager->get_curr_user();
        if (!info) {
            this->log_info("User info is invalid!");
            return -1;
        }

#ifdef __CHECK_BIND_USER__
        /* compare with bind user */
        MachineObject *obj = dev_manager_.get_local_selected_machine();
        if (!obj) return -1;
        if (obj->bind_user_id.empty()) return -1;
        if (obj->bind_user_id.compare(account_manager->get_curr_user()->get_user_id()) != 0) {
            std::string log = "Please Bind dev=" + obj->dev_id + " first!";
            this->send_log_evt(log);
            return -1;
        }
#endif

        pt::ptree root, print;
        print.put("command", "gcode_line");
        print.put("param", gcode);
        print.put("sequence_id", this->m_sequence_id++);
        print.put("user_id", info->get_user_id());
        root.put_child("print", print);
        std::stringstream oss;
        pt::write_json(oss, root, false);
        std::string json_str = oss.str();

        result = this->publish_json(json_str);
        if (result != 0) { this->log_info("publish_json failed"); }
    } else {
        MachineObject *obj = dev_manager_.get_default_machine();
        if (!obj) {
            this->send_log_evt("Invalid Printer! Please Select a Printer!");
            return -1;
        }
        return obj->publish_gcode(gcode);
    }
    return 0;
}

void DebugToolDialog::on_select_device(wxCommandEvent& evt)
{
    MachineObject* last_obj = dev_manager_.get_local_selected_machine();
    //machine_list_items
    int selection = evt.GetSelection();
    if (selection < machine_list_items.size()) {
        dev_manager_.local_selected_machine = machine_list_items[selection];
        send_log_evt("Select Printer=" + dev_manager_.local_selected_machine);

        /* update widget values */
        last_device_selection = selection;
    }
    else {
        BOOST_LOG_TRIVIAL(error) << "selection=" << selection << ", list items size=" << machine_list_items.size();
    }
}

void DebugToolDialog::on_select_mybind_device(wxCommandEvent& evt)
{
    //machine_list_items
    int selection = evt.GetSelection();
    if (selection < mybind_machine_list_items.size()) {
        dev_manager_.set_monitoring_machine(mybind_machine_list_items[selection]);
        send_log_evt("Select Printer=" + mybind_machine_list_items[selection]);
        /* update widget values */
        last_wlan_device_selection = selection;
    }
    else {
        BOOST_LOG_TRIVIAL(error) << "selection=" << selection << ", list items size=" << mybind_machine_list_items.size();
    }
}


}
}
