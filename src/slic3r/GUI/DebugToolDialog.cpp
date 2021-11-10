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
#include "slic3r/GUI/ProjectTask.hpp"
#include "libslic3r/miniz_extension.hpp"

namespace pt = boost::property_tree;
typedef pt::ptree JSON;


namespace Slic3r {
namespace GUI {

    wxDECLARE_EVENT(EVT_PROGRESS, wxCommandEvent);
    wxDECLARE_EVENT(EVT_3MF_PROGRESS, wxCommandEvent);
    wxDECLARE_EVENT(EVT_WLAN_GCODE_PROGRESS, wxCommandEvent);
    wxDECLARE_EVENT(EVT_UPDATE_LIST, SimpleEvent);
    wxDECLARE_EVENT(EVT_REFRESH_LIST, SimpleEvent);
    wxDECLARE_EVENT(EVT_UPDATE_MYBIND_LIST, SimpleEvent);
    wxDECLARE_EVENT(EVT_MQTT_SUCCESS, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MQTT_FAILED, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MQTT_LOST, wxCommandEvent);
    wxDECLARE_EVENT(EVT_PRINT_FINISH, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MESSAGE_ARRIVED, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MESSAGE_SENT, wxCommandEvent);
    wxDECLARE_EVENT(EVT_LOG_INFO, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MQTT_CONNECTED, wxCommandEvent);
    wxDECLARE_EVENT(EVT_MQTT_DISCONNECTED, wxCommandEvent);
    


    wxDEFINE_EVENT(EVT_PROGRESS, wxCommandEvent);
    wxDEFINE_EVENT(EVT_3MF_PROGRESS, wxCommandEvent);
    wxDEFINE_EVENT(EVT_WLAN_GCODE_PROGRESS, wxCommandEvent);
    wxDEFINE_EVENT(EVT_UPDATE_LIST, SimpleEvent);
    wxDEFINE_EVENT(EVT_REFRESH_LIST, SimpleEvent);
    wxDEFINE_EVENT(EVT_UPDATE_MYBIND_LIST, SimpleEvent);
    wxDEFINE_EVENT(EVT_MQTT_SUCCESS, wxCommandEvent);
    wxDEFINE_EVENT(EVT_MQTT_FAILED, wxCommandEvent);
    wxDEFINE_EVENT(EVT_MQTT_LOST, wxCommandEvent);
    wxDEFINE_EVENT(EVT_PRINT_FINISH, wxCommandEvent);
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

    DebugToolDialog::DebugToolDialog(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
        : wxPanel(parent, id, pos, size, style)
        ,dev_manager_(*wxGetApp().getDeviceManager())
        , m_timer(new wxTimer)
        , m_deviceListTimer(new wxTimer(this, TIMER_ID))
    {
        gcode_uploading = false;
        
        summary = new PrintSummary();

       wxBoxSizer* bSizer_top;
	bSizer_top = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_connect;
	bSizer_connect = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_lan;
	bSizer_lan = new wxBoxSizer( wxHORIZONTAL );

	radio_btn_lan = new wxRadioButton( this, wxID_ANY, wxT("LOCAL CONN"), wxDefaultPosition, wxDefaultSize, 0 );
	radio_btn_lan->SetValue( true );
	radio_btn_lan->SetMinSize( wxSize( 100,-1 ) );

	bSizer_lan->Add( radio_btn_lan, 0, wxALIGN_CENTER|wxALL, 5 );

	m_staticText_lan = new wxStaticText( this, wxID_ANY, wxT("SELECT:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_lan->Wrap( -1 );
	bSizer_lan->Add( m_staticText_lan, 0, wxALIGN_CENTER|wxALL, 5 );

	cb_device_list = new wxComboBox( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 350,-1 ), 0, NULL, 0 );
	cb_device_list->SetMinSize( wxSize( 300,-1 ) );

	bSizer_lan->Add( cb_device_list, 0, wxALL, 5 );

	btn_refresh_device_list = new wxButton( this, wxID_ANY, wxT("REFRESH"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer_lan->Add( btn_refresh_device_list, 0, wxALIGN_CENTER|wxALL, 5 );

	btn_connect = new wxButton( this, wxID_ANY, wxT("CONNECT"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer_lan->Add( btn_connect, 0, wxALIGN_CENTER|wxALL, 5 );

	btn_disconnect = new wxButton( this, wxID_ANY, wxT("DISCONNECT"), wxDefaultPosition, wxDefaultSize, 0 );
	btn_disconnect->Enable( false );

	bSizer_lan->Add( btn_disconnect, 0, wxALIGN_CENTER|wxALL, 5 );

	btn_bind = new wxButton( this, wxID_ANY, wxT("BIND"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer_lan->Add( btn_bind, 0, wxALIGN_CENTER|wxALL, 5 );

	btn_unbind = new wxButton( this, wxID_ANY, wxT("UNBIND"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer_lan->Add( btn_unbind, 0, wxALL, 5 );


	bSizer_connect->Add( bSizer_lan, 1, wxEXPAND, 5 );

	wxBoxSizer* bSizer_wan;
	bSizer_wan = new wxBoxSizer( wxHORIZONTAL );

	radio_btn_wan = new wxRadioButton( this, wxID_ANY, wxT("CLOUD CONN"), wxDefaultPosition, wxDefaultSize, 0 );
	radio_btn_wan->SetMinSize( wxSize( 100,-1 ) );

	bSizer_wan->Add( radio_btn_wan, 0, wxALIGN_CENTER|wxALL, 5 );

	m_staticText_wan = new wxStaticText( this, wxID_ANY, wxT("SELECT:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_wan->Wrap( -1 );
	bSizer_wan->Add( m_staticText_wan, 0, wxALIGN_CENTER|wxALL, 5 );

	cb_my_device_list = new wxComboBox( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 300,-1 ), 0, NULL, 0 );
	cb_my_device_list->SetMinSize( wxSize( 300,-1 ) );

	bSizer_wan->Add( cb_my_device_list, 0, wxALL, 5 );

	btn_refresh_my_device = new wxButton( this, wxID_ANY, wxT("REFRESH"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer_wan->Add( btn_refresh_my_device, 0, wxALIGN_CENTER|wxALL, 5 );


	bSizer_connect->Add( bSizer_wan, 1, wxEXPAND, 5 );


	bSizer_top->Add( bSizer_connect, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer_main;
	bSizer_main = new wxBoxSizer( wxHORIZONTAL );

	m_splitter1 = new wxSplitterWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_3D );

	m_panel_left = new wxPanel( m_splitter1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer301;
	bSizer301 = new wxBoxSizer( wxVERTICAL );

	m_notebook1 = new wxNotebook( m_panel_left, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 );
	m_panel_guide = new wxPanel( m_notebook1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer18;
	bSizer18 = new wxBoxSizer( wxVERTICAL );

	m_staticText_guide_title = new wxStaticText( m_panel_guide, wxID_ANY, wxT("User Guide:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_guide_title->Wrap( -1 );
	bSizer18->Add( m_staticText_guide_title, 0, wxALL, 5 );

	m_textCtrl10 = new wxTextCtrl( m_panel_guide, wxID_ANY, wxT("Please Bind After Connected!"), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE|wxTE_RICH );
	bSizer18->Add( m_textCtrl10, 1, wxALL|wxEXPAND, 5 );


	m_panel_guide->SetSizer( bSizer18 );
	m_panel_guide->Layout();
	bSizer18->Fit( m_panel_guide );
	m_notebook1->AddPage( m_panel_guide, wxT("Guide"), false );
	m_panel_common = new wxPanel( m_notebook1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer12;
	bSizer12 = new wxBoxSizer( wxVERTICAL );

	btn_get_version = new wxButton( m_panel_common, wxID_ANY, wxT("Get Version"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer12->Add( btn_get_version, 0, wxALL, 5 );

	wxBoxSizer* bSizer21;
	bSizer21 = new wxBoxSizer( wxHORIZONTAL );

	m_staticText6 = new wxStaticText( m_panel_common, wxID_ANY, wxT("Force Upgrading:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText6->Wrap( -1 );
	bSizer21->Add( m_staticText6, 0, wxALL, 5 );

	label_force_upgrade_val = new wxStaticText( m_panel_common, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_force_upgrade_val->Wrap( -1 );
	bSizer21->Add( label_force_upgrade_val, 0, wxALL, 5 );


	bSizer12->Add( bSizer21, 1, wxEXPAND, 5 );


	m_panel_common->SetSizer( bSizer12 );
	m_panel_common->Layout();
	bSizer12->Fit( m_panel_common );
	m_notebook1->AddPage( m_panel_common, wxT("Common"), false );
	m_panel_run_gcode = new wxPanel( m_notebook1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer13;
	bSizer13 = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer14;
	bSizer14 = new wxBoxSizer( wxHORIZONTAL );

	label_gcode_filename = new wxStaticText( m_panel_run_gcode, wxID_ANY, wxT("Gcode File:"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	label_gcode_filename->Wrap( -1 );
	label_gcode_filename->SetMinSize( wxSize( 100,-1 ) );

	bSizer14->Add( label_gcode_filename, 0, wxALIGN_CENTER|wxALL, 5 );

	txt_gcode_filename = new wxTextCtrl( m_panel_run_gcode, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	txt_gcode_filename->SetMinSize( wxSize( 300,-1 ) );

	bSizer14->Add( txt_gcode_filename, 0, wxALIGN_CENTER|wxALL, 5 );

	btn_select_gcode_file = new wxButton( m_panel_run_gcode, wxID_ANY, wxT("Select File"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer14->Add( btn_select_gcode_file, 0, wxALL, 5 );


	bSizer13->Add( bSizer14, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer15;
	bSizer15 = new wxBoxSizer( wxHORIZONTAL );

	label_upload_progress = new wxStaticText( m_panel_run_gcode, wxID_ANY, wxT("Gcode Upload:"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	label_upload_progress->Wrap( -1 );
	label_upload_progress->SetMinSize( wxSize( 100,-1 ) );

	bSizer15->Add( label_upload_progress, 0, wxALL, 5 );

	label_gcode_progress = new wxStaticText( m_panel_run_gcode, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_gcode_progress->Wrap( -1 );
	bSizer15->Add( label_gcode_progress, 0, wxALL, 5 );


	bSizer13->Add( bSizer15, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer16;
	bSizer16 = new wxBoxSizer( wxHORIZONTAL );

	btn_run_gcode = new wxButton( m_panel_run_gcode, wxID_ANY, wxT("Run Gcode"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer16->Add( btn_run_gcode, 0, wxALL, 5 );

	btn_pause = new wxButton( m_panel_run_gcode, wxID_ANY, wxT("Pause"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer16->Add( btn_pause, 0, wxALL, 5 );

	btn_resume = new wxButton( m_panel_run_gcode, wxID_ANY, wxT("Resume"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer16->Add( btn_resume, 0, wxALL, 5 );

	btn_abort_print = new wxButton( m_panel_run_gcode, wxID_ANY, wxT("Abort"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer16->Add( btn_abort_print, 0, wxALL, 5 );


	bSizer13->Add( bSizer16, 0, wxEXPAND, 5 );


	bSizer13->Add( 0, 0, 1, wxEXPAND, 5 );


	m_panel_run_gcode->SetSizer( bSizer13 );
	m_panel_run_gcode->Layout();
	bSizer13->Fit( m_panel_run_gcode );
	m_notebook1->AddPage( m_panel_run_gcode, wxT("Run Gcode"), false );
	m_panel_info_control = new wxPanel( m_notebook1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer17;
	bSizer17 = new wxBoxSizer( wxHORIZONTAL );

	wxStaticBoxSizer* sbSizer_info;
	sbSizer_info = new wxStaticBoxSizer( new wxStaticBox( m_panel_info_control, wxID_ANY, wxT("Status") ), wxVERTICAL );

	wxGridSizer* bSizer_info;
	bSizer_info = new wxGridSizer( 0, 2, 0, 0 );

	m_staticText_nozzle_temp_title = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("Nozzle Temp:"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	m_staticText_nozzle_temp_title->Wrap( -1 );
	bSizer_info->Add( m_staticText_nozzle_temp_title, 0, wxALIGN_RIGHT|wxALL, 5 );

	label_hot_end_temp_val = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_hot_end_temp_val->Wrap( -1 );
	bSizer_info->Add( label_hot_end_temp_val, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_bed_temp_title = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("Bed Temp:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_bed_temp_title->Wrap( -1 );
	bSizer_info->Add( m_staticText_bed_temp_title, 0, wxALIGN_RIGHT|wxALL, 5 );

	label_bed_end_temp_val = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_bed_end_temp_val->Wrap( -1 );
	bSizer_info->Add( label_bed_end_temp_val, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_progress = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("Print Progress:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_progress->Wrap( -1 );
	bSizer_info->Add( m_staticText_progress, 0, wxALIGN_RIGHT|wxALL, 5 );

	label_print_progress_val = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_print_progress_val->Wrap( -1 );
	bSizer_info->Add( label_print_progress_val, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_wifi_signal = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("WiFi Signal:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_wifi_signal->Wrap( -1 );
	bSizer_info->Add( m_staticText_wifi_signal, 0, wxALIGN_RIGHT|wxALL, 5 );

	label_wifi_signal_val = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_wifi_signal_val->Wrap( -1 );
	bSizer_info->Add( label_wifi_signal_val, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_th_link = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("TH Link State:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_th_link->Wrap( -1 );
	bSizer_info->Add( m_staticText_th_link, 0, wxALIGN_RIGHT|wxALL, 5 );

	label_wifi_link_th_val = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_wifi_link_th_val->Wrap( -1 );
	bSizer_info->Add( label_wifi_link_th_val, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_ams_link = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("AMS Link State:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_ams_link->Wrap( -1 );
	bSizer_info->Add( m_staticText_ams_link, 0, wxALIGN_RIGHT|wxALL, 5 );

	label_wifi_link_ams_val = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_wifi_link_ams_val->Wrap( -1 );
	bSizer_info->Add( label_wifi_link_ams_val, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_big1_speed_title = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("BigFan1 Speed:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_big1_speed_title->Wrap( -1 );
	bSizer_info->Add( m_staticText_big1_speed_title, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_big1_speed = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_big1_speed->Wrap( -1 );
	bSizer_info->Add( m_staticText_big1_speed, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_big2_speed_title = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("BigFan2 Speed:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_big2_speed_title->Wrap( -1 );
	bSizer_info->Add( m_staticText_big2_speed_title, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_big2_speed = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_big2_speed->Wrap( -1 );
	bSizer_info->Add( m_staticText_big2_speed, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_cooling_speed_title = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("Cooling Fan Speed:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_cooling_speed_title->Wrap( -1 );
	bSizer_info->Add( m_staticText_cooling_speed_title, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_cooling_speed = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_cooling_speed->Wrap( -1 );
	bSizer_info->Add( m_staticText_cooling_speed, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_heatbreak_speed_title = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("Heatbreak Fan Speed:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_heatbreak_speed_title->Wrap( -1 );
	bSizer_info->Add( m_staticText_heatbreak_speed_title, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_heatbreak_speed = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_heatbreak_speed->Wrap( -1 );
	bSizer_info->Add( m_staticText_heatbreak_speed, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_print_stage = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("Print State(MC):"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_print_stage->Wrap( -1 );
	bSizer_info->Add( m_staticText_print_stage, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_mc_print_stage = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_mc_print_stage->Wrap( -1 );
	bSizer_info->Add( m_staticText_mc_print_stage, 0, wxALL, 5 );

	m_staticText_print_error_code = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("PrintErrCode(MC):"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_print_error_code->Wrap( -1 );
	bSizer_info->Add( m_staticText_print_error_code, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_mc_print_error_code = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_mc_print_error_code->Wrap( -1 );
	bSizer_info->Add( m_staticText_mc_print_error_code, 0, wxALL, 5 );

	m_staticText_gcode_line_number = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("Gcode Line:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_gcode_line_number->Wrap( -1 );
	bSizer_info->Add( m_staticText_gcode_line_number, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_mc_print_line_number = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_mc_print_line_number->Wrap( -1 );
	bSizer_info->Add( m_staticText_mc_print_line_number, 0, wxALIGN_LEFT|wxALL, 5 );


	sbSizer_info->Add( bSizer_info, 0, wxALL, 5 );


	bSizer17->Add( sbSizer_info, 0, wxALL|wxEXPAND, 5 );

	wxBoxSizer* bSizer25;
	bSizer25 = new wxBoxSizer( wxVERTICAL );

	m_panel_settings = new wxPanel( m_panel_info_control, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer22;
	bSizer22 = new wxBoxSizer( wxVERTICAL );

	wxGridSizer* gSizer1;
	gSizer1 = new wxGridSizer( 0, 2, 0, 0 );

	gSizer1->SetMinSize( wxSize( 240,-1 ) );
	btn_set_hot_bed_temp = new wxButton( m_panel_settings, wxID_ANY, wxT("Set Bed Temp"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( btn_set_hot_bed_temp, 0, wxALIGN_RIGHT|wxALL, 5 );

	txt_set_hot_bed_temp = new wxTextCtrl( m_panel_settings, wxID_ANY, wxT("60"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( txt_set_hot_bed_temp, 0, wxALIGN_LEFT|wxALL, 5 );

	btn_set_hot_end_temp = new wxButton( m_panel_settings, wxID_ANY, wxT("Set Nozzle Temp"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( btn_set_hot_end_temp, 0, wxALIGN_RIGHT|wxALL, 5 );

	txt_set_hot_end_temp = new wxTextCtrl( m_panel_settings, wxID_ANY, wxT("200"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( txt_set_hot_end_temp, 0, wxALIGN_LEFT|wxALL, 5 );

	btn_fan_on = new wxButton( m_panel_settings, wxID_ANY, wxT("Fan On"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( btn_fan_on, 0, wxALIGN_RIGHT|wxALL, 5 );

	btn_fan_off = new wxButton( m_panel_settings, wxID_ANY, wxT("Fan Off"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( btn_fan_off, 0, wxALIGN_LEFT|wxALL, 5 );

	btn_auto_leveling = new wxButton( m_panel_settings, wxID_ANY, wxT("Auto Leveling(G29)"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( btn_auto_leveling, 0, wxALIGN_RIGHT|wxALL, 5 );

	btn_xyz_abs_mode = new wxButton( m_panel_settings, wxID_ANY, wxT("XYZ-abs(G90)"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( btn_xyz_abs_mode, 0, wxALIGN_LEFT|wxALL, 5 );

	btn_return_home = new wxButton( m_panel_settings, wxID_ANY, wxT("Return Home(G28)"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( btn_return_home, 0, wxALIGN_RIGHT|wxALL, 5 );


	gSizer1->Add( 0, 0, 1, wxEXPAND, 5 );

	btn_switch_t = new wxButton( m_panel_settings, wxID_ANY, wxT("Switch AMS:"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( btn_switch_t, 0, wxALIGN_RIGHT|wxALL, 5 );

	txt_switch_val = new wxTextCtrl( m_panel_settings, wxID_ANY, wxT("1"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( txt_switch_val, 0, wxALL, 5 );

	label_ams_flush_temp1 = new wxStaticText( m_panel_settings, wxID_ANY, wxT("AMS Flush Temp 1:"), wxDefaultPosition, wxDefaultSize, 0 );
	label_ams_flush_temp1->Wrap( -1 );
	gSizer1->Add( label_ams_flush_temp1, 0, wxALIGN_RIGHT|wxALL, 5 );

	txt_ams_flush_temp1 = new wxTextCtrl( m_panel_settings, wxID_ANY, wxT("220"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( txt_ams_flush_temp1, 0, wxALL, 5 );

	label_ams_flush_temp2 = new wxStaticText( m_panel_settings, wxID_ANY, wxT("AMS Flush Temp 2:"), wxDefaultPosition, wxDefaultSize, 0 );
	label_ams_flush_temp2->Wrap( -1 );
	gSizer1->Add( label_ams_flush_temp2, 0, wxALIGN_RIGHT|wxALL, 5 );

	txt_ams_flush_temp2 = new wxTextCtrl( m_panel_settings, wxID_ANY, wxT("220"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( txt_ams_flush_temp2, 0, wxALL, 5 );

	cbox_ams_auto_home = new wxCheckBox( m_panel_settings, wxID_ANY, wxT("AMS Auto Home"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( cbox_ams_auto_home, 0, wxALIGN_RIGHT|wxLEFT|wxTOP, 5 );


	bSizer22->Add( gSizer1, 1, wxALL, 5 );


	m_panel_settings->SetSizer( bSizer22 );
	m_panel_settings->Layout();
	bSizer22->Fit( m_panel_settings );
	bSizer25->Add( m_panel_settings, 0, wxALL, 5 );

	m_panel__control = new wxPanel( m_panel_info_control, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	m_panel__control->SetMinSize( wxSize( 300,-1 ) );

	wxBoxSizer* bSizer19;
	bSizer19 = new wxBoxSizer( wxVERTICAL );

	wxGridSizer* pos_btns_sizer;
	pos_btns_sizer = new wxGridSizer( 0, 3, 0, 0 );

	btn_set_x_pos_0_1 = new wxButton( m_panel__control, wxID_ANY, wxT("X+0.1"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_x_pos_0_1, 0, wxALL, 5 );

	btn_set_x_pos_1_0 = new wxButton( m_panel__control, wxID_ANY, wxT("X+1.0"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_x_pos_1_0, 0, wxALL, 5 );

	btn_set_x_pos_10_0 = new wxButton( m_panel__control, wxID_ANY, wxT("X+10.0"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_x_pos_10_0, 0, wxALL, 5 );

	btn_set_x_neg_0_1 = new wxButton( m_panel__control, wxID_ANY, wxT("X-0.1"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_x_neg_0_1, 0, wxALL, 5 );

	btn_set_x_neg_1_0 = new wxButton( m_panel__control, wxID_ANY, wxT("X-1.0"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_x_neg_1_0, 0, wxALL, 5 );

	btn_set_x_neg_10_0 = new wxButton( m_panel__control, wxID_ANY, wxT("X-10.0"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_x_neg_10_0, 0, wxALL, 5 );

	btn_set_y_pos_0_1 = new wxButton( m_panel__control, wxID_ANY, wxT("Y+0.1"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_y_pos_0_1, 0, wxALL, 5 );

	btn_set_y_pos_1_0 = new wxButton( m_panel__control, wxID_ANY, wxT("Y+1.0"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_y_pos_1_0, 0, wxALL, 5 );

	btn_set_y_pos_10_0 = new wxButton( m_panel__control, wxID_ANY, wxT("Y+10.0"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_y_pos_10_0, 0, wxALL, 5 );

	btn_set_y_neg_0_1 = new wxButton( m_panel__control, wxID_ANY, wxT("Y-0.1"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_y_neg_0_1, 0, wxALL, 5 );

	btn_set_y_neg_1_0 = new wxButton( m_panel__control, wxID_ANY, wxT("Y-1.0"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_y_neg_1_0, 0, wxALL, 5 );

	btn_set_y_neg_10_0 = new wxButton( m_panel__control, wxID_ANY, wxT("Y-10.0"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_y_neg_10_0, 0, wxALL, 5 );

	btn_set_z_pos_0_1 = new wxButton( m_panel__control, wxID_ANY, wxT("Z+0.1"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_z_pos_0_1, 0, wxALL, 5 );

	btn_set_z_pos_1_0 = new wxButton( m_panel__control, wxID_ANY, wxT("Z+1.0"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_z_pos_1_0, 0, wxALL, 5 );

	btn_set_z_pos_10_0 = new wxButton( m_panel__control, wxID_ANY, wxT("Z+10.0"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_z_pos_10_0, 0, wxALL, 5 );

	btn_set_z_neg_0_1 = new wxButton( m_panel__control, wxID_ANY, wxT("Z-0.1"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_z_neg_0_1, 0, wxALL, 5 );

	btn_set_z_neg_1_0 = new wxButton( m_panel__control, wxID_ANY, wxT("Z-1.0"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_z_neg_1_0, 0, wxALL, 5 );

	btn_set_z_neg_10_0 = new wxButton( m_panel__control, wxID_ANY, wxT("Z-10.0"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_z_neg_10_0, 0, wxALL, 5 );

	btn_set_e_pos_0_1 = new wxButton( m_panel__control, wxID_ANY, wxT("E+0.1"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_e_pos_0_1, 0, wxALL, 5 );

	btn_set_e_pos_1_0 = new wxButton( m_panel__control, wxID_ANY, wxT("E+1.0"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_e_pos_1_0, 0, wxALL, 5 );

	btn_set_e_pos_10_0 = new wxButton( m_panel__control, wxID_ANY, wxT("E+10.0"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_e_pos_10_0, 0, wxALL, 5 );

	btn_set_e_neg_0_1 = new wxButton( m_panel__control, wxID_ANY, wxT("E-0.1"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_e_neg_0_1, 0, wxALL, 5 );

	btn_set_e_neg_1_0 = new wxButton( m_panel__control, wxID_ANY, wxT("E-1.0"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_e_neg_1_0, 0, wxALL, 5 );

	btn_set_e_neg_10_0 = new wxButton( m_panel__control, wxID_ANY, wxT("E-10.0"), wxDefaultPosition, wxDefaultSize, 0 );
	pos_btns_sizer->Add( btn_set_e_neg_10_0, 0, wxALL, 5 );


	bSizer19->Add( pos_btns_sizer, 0, wxALL, 5 );


	m_panel__control->SetSizer( bSizer19 );
	m_panel__control->Layout();
	bSizer19->Fit( m_panel__control );
	bSizer25->Add( m_panel__control, 1, wxALL, 5 );


	bSizer17->Add( bSizer25, 0, wxALL, 5 );

	wxBoxSizer* bSizer27;
	bSizer27 = new wxBoxSizer( wxVERTICAL );

	m_scrolledWindow_custom = new wxScrolledWindow( m_panel_info_control, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL|wxVSCROLL );
	m_scrolledWindow_custom->SetScrollRate( 5, 5 );
	m_scrolledWindow_custom->SetMinSize( wxSize( 300,-1 ) );

	wxBoxSizer* bSizer24;
	bSizer24 = new wxBoxSizer( wxVERTICAL );

	wxFlexGridSizer* fgSizer1;
	fgSizer1 = new wxFlexGridSizer( 0, 2, 0, 0 );
	fgSizer1->AddGrowableCol( 1 );
	fgSizer1->SetFlexibleDirection( wxBOTH );
	fgSizer1->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

	btn_send_gcode_1 = new wxButton( m_scrolledWindow_custom, wxID_ANY, wxT("Send Custom Gcode 1"), wxDefaultPosition, wxDefaultSize, 0 );
	fgSizer1->Add( btn_send_gcode_1, 1, wxALL|wxEXPAND, 5 );

	txt_custom_gcode1 = new wxTextCtrl( m_scrolledWindow_custom, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE );
	txt_custom_gcode1->SetMinSize( wxSize( -1,80 ) );

	fgSizer1->Add( txt_custom_gcode1, 1, wxALL|wxEXPAND, 5 );

	btn_send_gcode_2 = new wxButton( m_scrolledWindow_custom, wxID_ANY, wxT("Send Custom Gcode 2"), wxDefaultPosition, wxDefaultSize, 0 );
	fgSizer1->Add( btn_send_gcode_2, 1, wxALL|wxEXPAND, 5 );

	txt_custom_gcode2 = new wxTextCtrl( m_scrolledWindow_custom, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE );
	txt_custom_gcode2->SetMinSize( wxSize( -1,80 ) );

	fgSizer1->Add( txt_custom_gcode2, 0, wxALL|wxEXPAND, 5 );

	btn_send_gcode_3 = new wxButton( m_scrolledWindow_custom, wxID_ANY, wxT("Send Custom Gcode 3"), wxDefaultPosition, wxDefaultSize, 0 );
	fgSizer1->Add( btn_send_gcode_3, 0, wxALL|wxEXPAND, 5 );

	txt_custom_gcode3 = new wxTextCtrl( m_scrolledWindow_custom, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE );
	txt_custom_gcode3->SetMinSize( wxSize( -1,80 ) );

	fgSizer1->Add( txt_custom_gcode3, 0, wxALL|wxEXPAND, 5 );

	btn_send_gcode_4 = new wxButton( m_scrolledWindow_custom, wxID_ANY, wxT("Send Custom Gcode 4"), wxDefaultPosition, wxDefaultSize, 0 );
	fgSizer1->Add( btn_send_gcode_4, 0, wxALL|wxEXPAND, 5 );

	txt_custom_gcode4 = new wxTextCtrl( m_scrolledWindow_custom, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE );
	txt_custom_gcode4->SetMinSize( wxSize( -1,80 ) );

	fgSizer1->Add( txt_custom_gcode4, 0, wxALL|wxEXPAND, 5 );

	btn_send_gcode_5 = new wxButton( m_scrolledWindow_custom, wxID_ANY, wxT("Send Custom Gcode 5"), wxDefaultPosition, wxDefaultSize, 0 );
	fgSizer1->Add( btn_send_gcode_5, 0, wxALL|wxEXPAND, 5 );

	txt_custom_gcode5 = new wxTextCtrl( m_scrolledWindow_custom, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE );
	txt_custom_gcode5->SetMinSize( wxSize( -1,80 ) );

	fgSizer1->Add( txt_custom_gcode5, 0, wxALL|wxEXPAND, 5 );

	btn_send_gcode_6 = new wxButton( m_scrolledWindow_custom, wxID_ANY, wxT("Send Custom Gcode 6"), wxDefaultPosition, wxDefaultSize, 0 );
	fgSizer1->Add( btn_send_gcode_6, 0, wxALL|wxEXPAND, 5 );

	txt_custom_gcode6 = new wxTextCtrl( m_scrolledWindow_custom, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE );
	txt_custom_gcode6->SetMinSize( wxSize( -1,80 ) );

	fgSizer1->Add( txt_custom_gcode6, 0, wxALL|wxEXPAND, 5 );

	btn_send_gcode_7 = new wxButton( m_scrolledWindow_custom, wxID_ANY, wxT("Send Custom Gcode 7"), wxDefaultPosition, wxDefaultSize, 0 );
	fgSizer1->Add( btn_send_gcode_7, 0, wxALL|wxEXPAND, 5 );

	txt_custom_gcode7 = new wxTextCtrl( m_scrolledWindow_custom, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE );
	txt_custom_gcode7->SetMinSize( wxSize( -1,80 ) );

	fgSizer1->Add( txt_custom_gcode7, 0, wxALL|wxEXPAND, 5 );


	bSizer24->Add( fgSizer1, 1, wxALL|wxEXPAND, 5 );


	m_scrolledWindow_custom->SetSizer( bSizer24 );
	m_scrolledWindow_custom->Layout();
	bSizer24->Fit( m_scrolledWindow_custom );
	bSizer27->Add( m_scrolledWindow_custom, 1, wxALL|wxEXPAND, 5 );


	bSizer17->Add( bSizer27, 1, wxEXPAND, 5 );


	m_panel_info_control->SetSizer( bSizer17 );
	m_panel_info_control->Layout();
	bSizer17->Fit( m_panel_info_control );
	m_notebook1->AddPage( m_panel_info_control, wxT("Info Control"), true );
	m_panel_upgrade = new wxPanel( m_notebook1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer28;
	bSizer28 = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer32;
	bSizer32 = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer33;
	bSizer33 = new wxBoxSizer( wxHORIZONTAL );

	m_staticText66 = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("Select Module:"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	m_staticText66->Wrap( -1 );
	m_staticText66->SetMinSize( wxSize( 120,-1 ) );

	bSizer33->Add( m_staticText66, 0, wxALIGN_CENTER|wxALL, 5 );

	cb_upgrade_module = new wxComboBox( m_panel_upgrade, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, 0 );
	cb_upgrade_module->Append( wxT("RK1126(AP)") );
	cb_upgrade_module->Append( wxT("MC") );
	cb_upgrade_module->Append( wxT("TH") );
	cb_upgrade_module->Append( wxT("AMS") );
	cb_upgrade_module->Append( wxT("OTA") );
	cb_upgrade_module->SetSelection( 0 );
	cb_upgrade_module->SetMinSize( wxSize( 100,-1 ) );

	bSizer33->Add( cb_upgrade_module, 0, wxALL, 5 );


	bSizer32->Add( bSizer33, 1, wxEXPAND, 5 );

	wxBoxSizer* bSizer34;
	bSizer34 = new wxBoxSizer( wxHORIZONTAL );

	m_staticText67 = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("Select Folder:"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	m_staticText67->Wrap( -1 );
	m_staticText67->SetMinSize( wxSize( 120,-1 ) );

	bSizer34->Add( m_staticText67, 0, wxALIGN_CENTER|wxALL, 5 );

	cb_upgrade_mode = new wxComboBox( m_panel_upgrade, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, 0 );
	cb_upgrade_mode->Append( wxT("DailyBuild") );
	cb_upgrade_mode->Append( wxT("Release") );
	cb_upgrade_mode->Append( wxT("Debug") );
	cb_upgrade_mode->SetSelection( 1 );
	cb_upgrade_mode->SetMinSize( wxSize( 100,-1 ) );

	bSizer34->Add( cb_upgrade_mode, 0, wxALL, 5 );


	bSizer32->Add( bSizer34, 1, wxEXPAND, 5 );

	wxBoxSizer* bSizer35;
	bSizer35 = new wxBoxSizer( wxHORIZONTAL );

	m_staticText57 = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("Firmware:"), wxDefaultPosition, wxSize( 120,-1 ), wxALIGN_RIGHT );
	m_staticText57->Wrap( -1 );
	bSizer35->Add( m_staticText57, 0, wxALIGN_CENTER|wxALL, 5 );

	cb_upgrade_firmware = new wxComboBox( m_panel_upgrade, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, 0 );
	cb_upgrade_firmware->SetMinSize( wxSize( 320,-1 ) );

	bSizer35->Add( cb_upgrade_firmware, 0, wxALL, 5 );

	btn_refresh_upgrade_list = new wxButton( m_panel_upgrade, wxID_ANY, wxT("Refresh"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer35->Add( btn_refresh_upgrade_list, 0, wxALIGN_CENTER, 5 );

	btn_upgrade_firmware = new wxButton( m_panel_upgrade, wxID_ANY, wxT("Upgrade"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer35->Add( btn_upgrade_firmware, 0, wxALIGN_CENTER|wxALL, 5 );


	bSizer32->Add( bSizer35, 1, wxEXPAND, 5 );


	bSizer28->Add( bSizer32, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer30;
	bSizer30 = new wxBoxSizer( wxHORIZONTAL );

	wxBoxSizer* bSizer31;
	bSizer31 = new wxBoxSizer( wxHORIZONTAL );

	wxGridSizer* gSizer10;
	gSizer10 = new wxGridSizer( 0, 2, 0, 0 );

	m_staticText_status_title = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("Upgrade Status:"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	m_staticText_status_title->Wrap( -1 );
	m_staticText_status_title->SetMinSize( wxSize( 120,-1 ) );

	gSizer10->Add( m_staticText_status_title, 0, wxALL, 5 );

	label_upgrade_status_val = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_upgrade_status_val->Wrap( -1 );
	gSizer10->Add( label_upgrade_status_val, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_upgrade_module = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("Upgrade Module:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_upgrade_module->Wrap( -1 );
	gSizer10->Add( m_staticText_upgrade_module, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_upgrade_module_value = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_upgrade_module_value->Wrap( -1 );
	gSizer10->Add( m_staticText_upgrade_module_value, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_upgrade_progress = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("Upgrade Progress:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_upgrade_progress->Wrap( -1 );
	gSizer10->Add( m_staticText_upgrade_progress, 0, wxALIGN_RIGHT|wxALL, 5 );

	label_upgrade_progress_val = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_upgrade_progress_val->Wrap( -1 );
	gSizer10->Add( label_upgrade_progress_val, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_upgrade_info = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("Upgrade Info:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_upgrade_info->Wrap( -1 );
	gSizer10->Add( m_staticText_upgrade_info, 0, wxALIGN_RIGHT|wxALL, 5 );

	label_upgrade_message_val = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_upgrade_message_val->Wrap( -1 );
	gSizer10->Add( label_upgrade_message_val, 0, wxALIGN_LEFT|wxALL, 5 );


	bSizer31->Add( gSizer10, 0, 0, 5 );


	bSizer31->Add( 0, 0, 1, wxEXPAND, 5 );


	bSizer30->Add( bSizer31, 0, wxEXPAND, 5 );


	bSizer30->Add( 0, 0, 1, wxEXPAND, 5 );


	bSizer28->Add( bSizer30, 0, wxEXPAND, 5 );


	bSizer28->Add( 0, 0, 1, wxEXPAND, 5 );


	m_panel_upgrade->SetSizer( bSizer28 );
	m_panel_upgrade->Layout();
	bSizer28->Fit( m_panel_upgrade );
	m_notebook1->AddPage( m_panel_upgrade, wxT("Upgrade"), false );

	bSizer301->Add( m_notebook1, 1, wxEXPAND | wxALL, 5 );


	m_panel_left->SetSizer( bSizer301 );
	m_panel_left->Layout();
	bSizer301->Fit( m_panel_left );
	m_panel_log = new wxPanel( m_splitter1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer_log;
	bSizer_log = new wxBoxSizer( wxVERTICAL );

	m_staticText_log = new wxStaticText( m_panel_log, wxID_ANY, wxT("Log Info:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_log->Wrap( -1 );
	bSizer_log->Add( m_staticText_log, 0, wxALL, 5 );

	txt_string_info = new wxTextCtrl( m_panel_log, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE );
	txt_string_info->SetMinSize( wxSize( 300,-1 ) );

	bSizer_log->Add( txt_string_info, 1, wxALL|wxEXPAND, 5 );


	m_panel_log->SetSizer( bSizer_log );
	m_panel_log->Layout();
	bSizer_log->Fit( m_panel_log );
	m_splitter1->SplitVertically( m_panel_left, m_panel_log, 971 );
	bSizer_main->Add( m_splitter1, 1, wxEXPAND, 5 );


	bSizer_top->Add( bSizer_main, 1, wxEXPAND, 5 );


	this->SetSizer( bSizer_top );

    init();

	this->Layout();

    init_bind();

    init_bind_handler();
}

void DebugToolDialog::init()
{
    cb_device_list->SetEditable(false);
    cb_device_list->Bind(wxEVT_COMBOBOX, &DebugToolDialog::on_select_device, this);
    btn_refresh_device_list->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        this->refresh_device_list();
        });

    btn_unbind->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
            if (!account_manager->is_user_login()) {
                std::string log = "Please login first!";
                this->send_log_evt(log);
            }

            MachineObject* obj = dev_manager_.get_default();
            if (!obj) {
                this->send_log_evt("Invalid Printer! Please Select a Printer!");
                return;
            }

            obj->request_unbind(
            [this, obj](int result, std::string body) {
                if (result == 0) {
                    std::string log = "Unbind device=" + obj->dev_id + " ok!";
                    send_log_evt(log);
                    this->refresh_device_list();
                }
                else {
                    std::string log = "Unbind device=" + obj->dev_id + " failed!";
                    send_log_evt(log);
                }
            });
        });
    btn_bind->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        MachineObject* obj = dev_manager_.get_default();
        if (!obj) {
            this->send_log_evt("Invalid Printer! Please Select a Printer!");
            return;
        }

        if (!obj->mqtt_cli || !obj->mqtt_cli->is_connected()) {
            send_log_evt("Please login or connect first!");
            return;
        }

        if (obj->command_bind() < 0) {
            send_log_evt("Please login or connect first!");
        }
    });
    btn_connect->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        MachineObject* obj = dev_manager_.get_default();
        if (!obj) {
            this->send_log_evt("Invalid Printer! Please Select a Printer!");
            return;
        }
        std::string info = "MQTT connecting dev_id=" + obj->dev_id;
        this->send_log_evt(info);

        obj->set_callbacks(
            //success
            [this, obj](std::string name) {
                this->send_log_evt("Connected to Printer=" + obj->dev_id);
                auto evt = new wxCommandEvent(EVT_MQTT_CONNECTED, this->GetId());
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
        obj->connect();
    });
    btn_disconnect->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            MachineObject* obj = dev_manager_.get_default();
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
            Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
            account_manager->request_bind_list(
                [this](int result, std::string info) {
                    if (result == 0) {
                        wxQueueEvent(this, new SimpleEvent(EVT_UPDATE_MYBIND_LIST));
                    }
                }
            );
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
        pt::write_json(oss, root, false);
        std::string json_str = oss.str();
        json_str.erase(std::remove(json_str.begin(), json_str.end(), '\\'), json_str.end());
        if (this->publish_json(json_str) == 0) {
            this->log_info("Start Upgrading (Please wait several minutes)...");
        }
        });

    cb_upgrade_module->SetEditable(false);
    cb_upgrade_firmware->SetEditable(false);
    cb_upgrade_mode->SetEditable(false);

    btn_run_gcode->Bind(wxEVT_BUTTON,
        [this](wxCommandEvent& evt) {
            if (radio_btn_lan->GetValue()) {

                if (gcode_uploading) {
                    this->send_log_evt("Gcode is uploading...");
                    return;
                }
                this->gcode_uploading = true;
                /* collection summary info */
                summary->time_start = std::time(0);
                std::tm* now_time = std::localtime(&summary->time_start);
                std::stringstream buf;
                buf << std::put_time(now_time, "%a %b %d %H:%M:%S");
                summary->start_time = buf.str();
                summary->has_time_start = true;
                wxString path = txt_gcode_filename->GetValue();

                /* create a subtask */
                BBLSubTask* task = new BBLSubTask();
                task->task_file = txt_gcode_filename->GetValue().ToUTF8().data();

                /* send print task */
                MachineObject* obj = dev_manager_.get_default();
                if (!obj) {
                    this->send_log_evt("Invalid Printer! Please Select a Printer!");
                    gcode_uploading = false;
                    return;
                }

                obj->send_print_subtask(task,
                    [this]() {
                        auto evt = new wxCommandEvent(EVT_PROGRESS, this->GetId());
                        evt->SetInt(100);
                        gcode_uploading = false;
                        wxQueueEvent(this, evt);
                    },
                    [this](int progress) {
                        auto evt = new wxCommandEvent(EVT_PROGRESS, this->GetId());
                        evt->SetInt(progress);
                        wxQueueEvent(this, evt);
                    },
                    [this](std::string error) {
                    gcode_uploading = false;
                    BOOST_LOG_TRIVIAL(trace) << "transform gcode error=" << error;
                    send_log_evt("trasform gcode failed, error=" + error);
                    });
            }
            else {
                std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

                /* print current 3mf */
                Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();

                std::string gcode_file_str = txt_gcode_filename->GetValue().ToUTF8().data();
                fs::path gcode_path(gcode_file_str);
                fs::path _3mf_path(gcode_path);

                std::string dst_gcode_file_str = gcode_path.filename().string();

                /* zip gcode to 3mf */
                std::string _3mf_file_str = _3mf_path.replace_extension("3mf").string();
                mz_zip_archive archive;
                mz_zip_zero_struct(&archive);
                if (!open_zip_writer(&archive, _3mf_file_str)) {
                    BOOST_LOG_TRIVIAL(trace) << "Unable to open the file";
                    return;
                }
                mz_zip_writer_add_file(&archive, dst_gcode_file_str.c_str(), gcode_file_str.c_str(), "", 0, MZ_DEFAULT_COMPRESSION);
                mz_zip_writer_finalize_archive(&archive);
                close_zip_writer(&archive);

                /* create subtask info */
                BBLSubTask* subtask = new BBLSubTask();
                subtask->task_id = "0";
                subtask->task_path = _3mf_path;
                subtask->task_name = gcode_path.filename().string();
                subtask->task_gcode_in_3mf = gcode_path.filename().string();


                /* send task */
                MachineObject* obj = account_manager->get_default_machine();
                if (obj) {
                    obj->send_wan_print_subtask(subtask,
                        [this, _3mf_file_str]() {
                            auto evt = new wxCommandEvent(EVT_WLAN_GCODE_PROGRESS, this->GetId());
                            evt->SetInt(100);
                            wxQueueEvent(this, evt);
                            boost::filesystem::remove(_3mf_file_str);
                        },
                        [this](int progress) {
                            auto evt = new wxCommandEvent(EVT_WLAN_GCODE_PROGRESS, this->GetId());
                            evt->SetInt(progress);
                            wxQueueEvent(this, evt);
                        },
                            [this, _3mf_file_str](std::string info) {
                            boost::filesystem::remove(_3mf_file_str);
                            this->send_log_evt(info);
                        }
                        );
                }
            }
        });

    btn_select_gcode_file->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        if (this->selectGcodeDialog->ShowModal() == wxID_CANCEL) return;

        txt_gcode_filename->SetValue(this->selectGcodeDialog->GetPath());
        this->SetFocus();
        });

    
    btn_abort_print->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("M0\n");

        auto et = new wxCommandEvent(EVT_PRINT_FINISH, this->GetId());
        et->SetInt(0);
        wxQueueEvent(this, et);
        });
    
    btn_pause->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("M400 W1\n");
            this->send_log_evt("Pause Printing...");
        });
    
    btn_resume->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("M400 W0\n");
            this->send_log_evt("Resume Printing...");
        });

    selectGcodeDialog = new wxFileDialog(this, "Open Gcode File", "", "", "Gcode files(*.gcode)|*.gcode", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    btn_return_home->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        this->publishGcode("G28 \n");
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
}

void DebugToolDialog::init_bind_handler()
{
    Bind(EVT_PROGRESS, [this](wxCommandEvent& evt) {
        std::string text;
        text = std::to_string(evt.GetInt()) + "%";
        this->label_gcode_progress->SetLabelText(text);
        });

    Bind(EVT_WLAN_GCODE_PROGRESS, [this](wxCommandEvent& evt) {
        std::string text;
        text = std::to_string(evt.GetInt()) + "%";
        this->label_gcode_progress->SetLabelText(text);
        });

    Bind(wxEVT_TIMER, &DebugToolDialog::on_timer, this);
    Bind(EVT_UPDATE_LIST, &DebugToolDialog::on_update_list, this);
    Bind(EVT_REFRESH_LIST, &DebugToolDialog::on_update_list, this);
    Bind(EVT_UPDATE_MYBIND_LIST, &DebugToolDialog::on_update_mybind_list, this);
    Bind(EVT_MQTT_CONNECTED, &DebugToolDialog::on_mqtt_connected, this);
    Bind(EVT_MQTT_DISCONNECTED, &DebugToolDialog::on_mqtt_disconnected, this);
    Bind(EVT_MQTT_FAILED, &DebugToolDialog::on_mqtt_failed, this);
    Bind(EVT_MQTT_LOST, &DebugToolDialog::on_mqtt_lost, this);
    Bind(EVT_PRINT_FINISH, &DebugToolDialog::on_print_end, this);
    Bind(EVT_MESSAGE_ARRIVED, &DebugToolDialog::on_message_arrived, this);
    Bind(EVT_MESSAGE_SENT, &DebugToolDialog::on_message_sent, this);
    Bind(EVT_LOG_INFO, &DebugToolDialog::on_log_info, this);
}

void DebugToolDialog::on_update_list(SimpleEvent& evt)
{
    int select = -1;
    std::string last_dev_id;
    if (last_device_selection < machine_list_items.size()) {
        last_dev_id = machine_list_items[last_device_selection];
    }

    /* dislay list */
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    std::string username = account_manager->get_user_name();

    std::map<std::string, MachineObject*> list = dev_manager_.get_all_machine_list();
    std::vector<MachineObject*> display_list;

    // coconut: sort the device list by: 1) own device first, then free, then others; 2) small dev_id (MAC address) (or may be dev_ip?)
    std::transform(list.begin(), list.end(), std::back_inserter(display_list), [](auto& a) {return a.second; });
    username = username.substr(0, username.find_first_of("@"));
    std::sort(display_list.begin(), display_list.end(), [&](auto a, auto b)
        {
            auto priority = [&](auto a, auto b) {
                return (a->get_bind_str().compare(username) == 0) * 100
                    + (a->dev_bind_status == MachineObject::MachineBindStatus::MACHINE_BIND_FREE) * 10
                    + (a->dev_id < b->dev_id) * 1;
            };
            return priority(a, b) > priority(b, a);
        });

    std::vector<MachineObject*>::iterator iter;
    machine_list_items.clear();
    wxArrayString new_items;
    for (iter = display_list.begin(); iter != display_list.end(); iter++) {
        wxString text = get_machine_display_item(*iter);
        if (!last_dev_id.empty() && (*iter)->dev_id.compare(last_dev_id) == 0) {
            select = new_items.size();
        }
        machine_list_items.push_back((*iter)->dev_id);
        new_items.Add(text);
    }

    cb_device_list->Set(new_items);
    if (select >= 0) {
        cb_device_list->Select(select);
        last_device_selection = select;
    }
}

void DebugToolDialog::on_update_mybind_list(SimpleEvent& evt)
{
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    int select = -1;
    std::string last_my_bind_dev_id;
    if (last_wlan_device_selection < mybind_machine_list_items.size()) {
        last_my_bind_dev_id = mybind_machine_list_items[last_wlan_device_selection];
    }
    
    std::map<std::string, MachineObject*> list = account_manager->myBindMachineList;
    std::map<std::string, MachineObject*>::iterator iter;
    mybind_machine_list_items.clear();
    wxArrayString new_items;
    for (iter = list.begin(); iter != list.end(); iter++) {
        wxString online_status = iter->second->is_online ? "Online" : "Offline";
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
}

void DebugToolDialog::on_mqtt_failed(wxCommandEvent& evt)
{
    this->log_info("MQTT Connect Failed! client=" + evt.GetString().ToStdString());
    btn_disconnect->Disable();
    btn_connect->Enable();
    btn_refresh_device_list->Enable();
    cb_device_list->Enable();
    radio_btn_lan->SetValue(true);
}

void DebugToolDialog::on_mqtt_lost(wxCommandEvent& evt)
{
    this->log_info("MQTT Lost... client=" + evt.GetString().ToStdString());
    btn_disconnect->Disable();

    btn_connect->Enable();
    btn_refresh_device_list->Enable();
    cb_device_list->Enable();
    radio_btn_lan->SetValue(true);
}

void DebugToolDialog::on_mqtt_connected(wxCommandEvent& evt)
{
    btn_disconnect->Enable();
    btn_connect->Disable();
    btn_refresh_device_list->Disable();
    cb_device_list->Disable();
    radio_btn_lan->SetValue(true);
}

void DebugToolDialog::on_mqtt_disconnected(wxCommandEvent& evt)
{
    btn_disconnect->Disable();
    btn_connect->Enable();
    btn_refresh_device_list->Enable();
    cb_device_list->Enable();
    radio_btn_lan->SetValue(true);
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

    MachineObject* obj = device_manager->get_default();
    if (obj) {
        /* get slicer version */
        summary->slicer_version = SLIC3R_RC_VERSION;

        /* get device_id */
        summary->device_id = obj->dev_id;
        /* get device_ip */
        summary->device_ip = obj->dev_ip;
        /* get host ip */
        summary->host_ip = "192.168.0.1";
    }

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
    pt::write_json(oss, root, false);
    std::string json_str = oss.str();
    json_str.erase(std::remove(json_str.begin(), json_str.end(), '\\'), json_str.end());
    this->publish_json(json_str);
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
    dyn_config.set_key_value("use_relative_e_distances", new ConfigOptionBool(1));
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

    try {
        std::string parsed_command = m_placeholder_parser.process(print_config.toolchange_gcode.value, std::stoi(t.c_str()), &dyn_config, &m_placeholder_parser_context);
        // config xyz coordinate mode
        std::string auto_home_command = cbox_ams_auto_home->GetValue() ? "G28\n" : "";
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
    if (show) {
        m_timer->Stop();
        m_timer->SetOwner(this);
        m_timer->Start(10000);
    }
    else {
        m_timer->Stop();
    }

    return wxPanel::Show(show);
}


int DebugToolDialog::publish_json(std::string json_str)
{
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    /* lan send json */
    if (radio_btn_lan->GetValue()) {
        std::string user_name = account_manager->get_user_name();
        std::transform(user_name.begin(), user_name.end(), user_name.begin(),
            [](unsigned char c) { return std::tolower(c); });

        MachineObject* obj = dev_manager_.get_default();
        if (!obj) {
            this->send_log_evt("Invalid Printer! Please Select a Printer!");
            return -1;
        }
#ifdef __CHECK_BIND_USER__
        /* compare with bind user */
        if (obj->get_bind_str().compare(user_name) != 0 || user_name.empty()) {
            std::string log = "Please Bind dev=" + obj->dev_id + " first!";
            this->send_log_evt(log);
            return -1;
        }
#endif

        obj->publish_json(json_str,
            [this](int result, std::string info) {
                if (result < 0) {
                    this->send_log_evt(info);
                }
            }
        );
    }
    else {
        MachineObject* obj = account_manager->get_default_machine();
        if (!obj) {
            this->send_log_evt("Invalid Printer! Please Select a Printer!");
            return -1;
        }

        obj->publish_json(json_str,
            [this](int result, std::string info) {
                if (result < 0) {
                    this->send_log_evt(info);
                }
            }
        , MachineObject::CONNECTION_TYPE::CONNECTION_WAN);
    }

    return 0;
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
    Slic3r::DeviceManager* device_manager = Slic3r::GUI::wxGetApp().getDeviceManager();

    MachineObject* obj = device_manager->get_default();

    if (!obj) return;

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

    wxString print_err_code_text = wxString::Format("%d", obj->mc_print_error_code);
    m_staticText_mc_print_error_code->SetLabelText(print_err_code_text);

    wxString gcode_line_text = wxString::Format("%d", obj->mc_print_line_bumber);
    m_staticText_mc_print_line_number->SetLabelText(gcode_line_text);


    if (mqtt_msg_queue.empty()) {
        return;
    }
    
    std::string json_str = mqtt_msg_queue.front();
    mqtt_msg_queue.pop();

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
                boost::optional<std::string> sequence_id = print.get_optional<std::string>("sequence_id");
                boost::optional<std::string> nozzle_temp = print.get_optional<std::string>("nozzle_temp");
                boost::optional<std::string> nozzle_temp_target = print.get_optional<std::string>("nozzle_target_temp");
                boost::optional<std::string> bed_temp = print.get_optional<std::string>("bed_temp");
                boost::optional<std::string> bed_temp_target = print.get_optional<std::string>("bed_target_temp");


                boost::optional<std::string> force_upgrade = print.get_optional<std::string>("force_upgrade");
                if (force_upgrade.has_value()) {
                    label_force_upgrade_val->SetLabelText(force_upgrade.value());
                }

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
                    pt::write_json(oss, version, false);
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
                        pt::write_json(oss, version, false);
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
                m_staticText_upgrade_module_value->SetLabelText(upgrade_module.value());
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
            return;
        }
        else if (root.get_child_optional("bind") != boost::none) {
            pt::ptree bind = root.get_child("bind");
            boost::optional<std::string> command = bind.get_optional<std::string>("command");
            boost::optional<std::string> result = bind.get_optional<std::string>("result");
            boost::optional<std::string> reason = bind.get_optional<std::string>("reason");
            boost::optional<std::string> user_id = bind.get_optional<std::string>("user_id");

            if (command.has_value())
            {
                if (command.value().compare("bind") == 0) {
                    if (result.has_value()) {
                        if (result.value().compare("success") == 0) {
                            this->log_info("bind device ok!");
                        }
                        else if (result.value().compare("fail") == 0) {
                            this->log_info("bind device failed!");
                        }
                    }
                }
                else if (command.value().compare("unbind") == 0) {
                    if (result.has_value()) {
                        if (result.value().compare("success") == 0) {
                            this->log_info("unbind device ok!");
                        }
                        else if (result.value().compare("fail") == 0) {
                            this->log_info("unbind device failed!");
                        }
                    }
                }
                /* refresh after bind */
                this->refresh_device_list();
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

void DebugToolDialog::refresh_device_list()
{
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    if (!account_manager->is_user_login()) {
        wxQueueEvent(this, new SimpleEvent(EVT_UPDATE_LIST));
        return;
    }

    dev_manager_.query_bind_status(
        // CompleteFn
        [this](std::string body) {
            wxQueueEvent(this, new SimpleEvent(EVT_UPDATE_LIST));
        }, 
        // ErrorFn
        [this](int status, std::string error, std::string body) {
            std::string error_str = (boost::format("Query Status Error, status=%1%, error=%2%, body=%3%") % status % error % body).str();
            this->send_log_evt(error_str);
            
        });
}

wxString DebugToolDialog::get_machine_display_item(MachineObject* obj)
{
    return wxString::Format("%-16s(%s)[bind:%s]", obj->dev_ip, obj->dev_id, obj->get_bind_str());
}

void DebugToolDialog::refresh_firmware_list(bool show_error)
{
    cb_upgrade_firmware->Clear();
    upgrade_file_list.clear();
    if (cb_upgrade_module->GetValue().compare("") == 0) {
        std::string log = "Please select a module!";
        this->send_log_evt(log);
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
        })
        .on_error([this](std::string body, std::string error, unsigned status) {
            this->send_log_evt("Get Upgrade List Failed! error=" + error);
        }).perform();
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
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    int result = 0;
    //can not publish gcode when logout
    if (!account_manager->is_user_login()) {
        this->log_info("Please login first!");
        return -1;
    }
    Slic3r::AccountInfo* info = account_manager->get_curr_user();
    if (!info) {
        this->log_info("User info is invalid!");
        return -1;
    }

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
    if (result != 0) {
        this->log_info("publish_json failed");
    }

    return result;
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
    MachineObject* last_obj = dev_manager_.get_default();
    if (last_obj) {
        last_obj->set_msg_recv_fn(nullptr);
        last_obj->set_msg_send_fn(nullptr);
    }

    //machine_list_items
    int selection = evt.GetSelection();
    if (selection < machine_list_items.size()) {
        dev_manager_.default_machine = machine_list_items[selection];
        send_log_evt("Select Printer=" + dev_manager_.default_machine);

        /* update widget values */
        last_device_selection = selection;
    }
    else {
        BOOST_LOG_TRIVIAL(error) << "selection=" << selection << ", list items size=" << machine_list_items.size();
    }

    MachineObject* obj = dev_manager_.get_default();
    if (!obj) return;

    obj->set_msg_recv_fn([this](std::string topic, std::string payload) {
        mqtt_msg_queue.push(payload);
        auto evt = new wxCommandEvent(EVT_MESSAGE_ARRIVED, this->GetId());
        evt->SetString(payload);
        wxQueueEvent(this, evt);
        });
    obj->set_msg_send_fn([this](std::string topic, std::string payload) {
        auto evt = new wxCommandEvent(EVT_MESSAGE_SENT, this->GetId());
        std::string send_msg = "send topic=" + topic + ", msg=" + payload;
        evt->SetString(send_msg);
        wxQueueEvent(this, evt);
        });
}

void DebugToolDialog::on_select_mybind_device(wxCommandEvent& evt)
{
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    MachineObject* last_obj = account_manager->get_default_machine();
    if (last_obj) {
        last_obj->set_msg_recv_fn(nullptr);
        last_obj->set_msg_send_fn(nullptr);
    }

    //machine_list_items
    int selection = evt.GetSelection();
    if (selection < mybind_machine_list_items.size()) {
        account_manager->default_machine = mybind_machine_list_items[selection];
        send_log_evt("Select Printer=" + account_manager->default_machine);

        /* update widget values */
        last_wlan_device_selection = selection;
    }
    else {
        BOOST_LOG_TRIVIAL(error) << "selection=" << selection << ", list items size=" << mybind_machine_list_items.size();
    }

    MachineObject* obj = account_manager->get_default_machine();
    if (!obj) return;

    obj->set_msg_recv_fn([this](std::string topic, std::string payload) {
            auto evt = new wxCommandEvent(EVT_MESSAGE_ARRIVED, this->GetId());
            evt->SetString(payload);
            wxQueueEvent(this, evt);
        });

    obj->set_msg_send_fn([this](std::string topic, std::string payload) {
        auto evt = new wxCommandEvent(EVT_MESSAGE_SENT, this->GetId());
        std::string send_msg = "send topic=" + topic + ", msg=" + payload;
        evt->SetString(send_msg);
        wxQueueEvent(this, evt);
        });
}


}
}
