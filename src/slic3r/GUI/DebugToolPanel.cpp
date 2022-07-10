///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 3.10.1-0-g8feb16b3)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#include "DebugToolPanel.h"
#include "I18N.hpp"

///////////////////////////////////////////////////////////////////////////
using namespace Slic3r::GUI;

DebugToolPanel::DebugToolPanel( wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name ) : wxPanel( parent, id, pos, size, style, name )
{
	wxBoxSizer* bSizer_top;
	bSizer_top = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_connect;
	bSizer_connect = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_lan;
	bSizer_lan = new wxBoxSizer( wxHORIZONTAL );

	radio_btn_lan = new wxRadioButton( this, wxID_ANY, _L("LOCAL CONN"), wxDefaultPosition, wxDefaultSize, 0 );
	radio_btn_lan->SetValue( true );
	radio_btn_lan->SetMinSize( wxSize( 100,-1 ) );

	bSizer_lan->Add( radio_btn_lan, 0, wxALIGN_CENTER|wxALL, 5 );

	m_staticText_lan = new wxStaticText( this, wxID_ANY, _L("SELECT:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_lan->Wrap( -1 );
	bSizer_lan->Add( m_staticText_lan, 0, wxALIGN_CENTER|wxALL, 5 );

	cb_device_list = new wxComboBox( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(400), -1), 0, NULL, 0 );
	cb_device_list->SetMinSize( wxSize(FromDIP(400), -1));

	bSizer_lan->Add( cb_device_list, 0, wxALL, 5 );

	m_bpButton_search = new wxBitmapButton( this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize( -1,-1 ), wxBU_AUTODRAW|0 );
	bSizer_lan->Add( m_bpButton_search, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );

	btn_refresh_device_list = new wxButton(this, wxID_ANY, _L("REFRESH"), wxDefaultPosition, wxDefaultSize, 0);
	bSizer_lan->Add( btn_refresh_device_list, 0, wxALIGN_CENTER|wxALL, 5 );

	btn_connect = new wxButton(this, wxID_ANY, _L("CONNECT"), wxDefaultPosition, wxDefaultSize, 0);
	bSizer_lan->Add( btn_connect, 0, wxALIGN_CENTER|wxALL, 5 );

	btn_disconnect = new wxButton(this, wxID_ANY, _L("DISCONNECT"), wxDefaultPosition, wxDefaultSize, 0);
	btn_disconnect->Enable( false );

	bSizer_lan->Add( btn_disconnect, 0, wxALIGN_CENTER|wxALL, 5 );

	btn_bind = new wxButton(this, wxID_ANY, _L("BIND"), wxDefaultPosition, wxDefaultSize, 0);
	bSizer_lan->Add( btn_bind, 0, wxALIGN_CENTER|wxALL, 5 );

	btn_unbind = new wxButton(this, wxID_ANY, _L("UNBIND"), wxDefaultPosition, wxDefaultSize, 0);
	bSizer_lan->Add( btn_unbind, 0, wxALL, 5 );


	bSizer_connect->Add( bSizer_lan, 1, wxEXPAND, 5 );

	wxBoxSizer* bSizer_wan;
	bSizer_wan = new wxBoxSizer( wxHORIZONTAL );

	radio_btn_wan = new wxRadioButton( this, wxID_ANY, _L("CLOUD CONN"), wxDefaultPosition, wxDefaultSize, 0 );
	radio_btn_wan->SetMinSize( wxSize( 100,-1 ) );

	bSizer_wan->Add( radio_btn_wan, 0, wxALIGN_CENTER|wxALL, 5 );

	m_staticText_wan = new wxStaticText(this, wxID_ANY, _L("SELECT:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_wan->Wrap( -1 );
	bSizer_wan->Add( m_staticText_wan, 0, wxALIGN_CENTER|wxALL, 5 );

	cb_my_device_list = new wxComboBox( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(300),-1 ), 0, NULL, 0 );
	cb_my_device_list->SetMinSize( wxSize(FromDIP(400),-1 ) );

	bSizer_wan->Add( cb_my_device_list, 0, wxALL, 5 );

	btn_refresh_my_device = new wxButton(this, wxID_ANY, _L("REFRESH"), wxDefaultPosition, wxDefaultSize, 0);
	bSizer_wan->Add( btn_refresh_my_device, 0, wxALIGN_CENTER|wxALL, 5 );


	bSizer_connect->Add( bSizer_wan, 1, wxEXPAND, 5 );


	bSizer_top->Add( bSizer_connect, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer_main;
	bSizer_main = new wxBoxSizer( wxHORIZONTAL );

	m_splitter1 = new wxSplitterWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_3D );
	m_splitter1->Connect( wxEVT_IDLE, wxIdleEventHandler( DebugToolPanel::m_splitter1OnIdle ), NULL, this );

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

	btn_get_version = new wxButton(m_panel_common, wxID_ANY, _L("Get Version"), wxDefaultPosition, wxDefaultSize, 0);
	bSizer12->Add( btn_get_version, 0, wxALL, 5 );

	wxBoxSizer* bSizer21;
	bSizer21 = new wxBoxSizer( wxHORIZONTAL );

	m_staticText6 = new wxStaticText(m_panel_common, wxID_ANY, _L("Force Upgrading:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText6->Wrap( -1 );
	bSizer21->Add( m_staticText6, 0, wxALL, 5 );

	label_force_upgrade_val = new wxStaticText( m_panel_common, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_force_upgrade_val->Wrap( -1 );
	bSizer21->Add( label_force_upgrade_val, 0, wxALL, 5 );


	bSizer12->Add( bSizer21, 1, wxEXPAND, 5 );


	m_panel_common->SetSizer( bSizer12 );
	m_panel_common->Layout();
	bSizer12->Fit( m_panel_common );
    m_notebook1->AddPage(m_panel_common, _L("Common"), false);
	m_panel_run_3mf = new wxPanel( m_notebook1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer13;
	bSizer13 = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer14;
	bSizer14 = new wxBoxSizer( wxHORIZONTAL );

	label_3mf_filename = new wxStaticText(m_panel_run_3mf, wxID_ANY, _L("3mf File:"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
	label_3mf_filename->Wrap( -1 );
	label_3mf_filename->SetMinSize( wxSize(FromDIP(100), -1));

	bSizer14->Add( label_3mf_filename, 0, wxALIGN_CENTER|wxALL, 5 );

	txt_3mf_filename = new wxTextCtrl( m_panel_run_3mf, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	txt_3mf_filename->SetMinSize( wxSize(FromDIP(300), -1));

	bSizer14->Add( txt_3mf_filename, 0, wxALIGN_CENTER|wxALL, 5 );

	btn_select_3mf_file = new wxButton(m_panel_run_3mf, wxID_ANY, _L("Select File"), wxDefaultPosition, wxDefaultSize, 0);
	bSizer14->Add( btn_select_3mf_file, 0, wxALL, 5 );


	bSizer13->Add( bSizer14, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer15;
	bSizer15 = new wxBoxSizer( wxHORIZONTAL );

	label_upload_progress = new wxStaticText( m_panel_run_3mf, wxID_ANY, wxT("3mf Upload:"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	label_upload_progress->Wrap( -1 );
	label_upload_progress->SetMinSize( wxSize(FromDIP(100),-1 ) );

	bSizer15->Add( label_upload_progress, 0, wxALL, 5 );

	label_3mf_progress = new wxStaticText( m_panel_run_3mf, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_3mf_progress->Wrap( -1 );
	bSizer15->Add( label_3mf_progress, 0, wxALL, 5 );


	bSizer13->Add( bSizer15, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer16;
	bSizer16 = new wxBoxSizer( wxHORIZONTAL );

	btn_run_3mf = new wxButton( m_panel_run_3mf, wxID_ANY, _L("Run Slice 3mf"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer16->Add( btn_run_3mf, 0, wxALL, 5 );

	btn_3mf_pause = new wxButton( m_panel_run_3mf, wxID_ANY, _L("Pause"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer16->Add( btn_3mf_pause, 0, wxALL, 5 );

	btn_3mf_resume = new wxButton( m_panel_run_3mf, wxID_ANY, _L("Resume"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer16->Add( btn_3mf_resume, 0, wxALL, 5 );

	btn_3mf_abort_print = new wxButton( m_panel_run_3mf, wxID_ANY, _L("Abort"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer16->Add( btn_3mf_abort_print, 0, wxALL, 5 );


	bSizer13->Add( bSizer16, 0, wxEXPAND, 5 );

	m_staticText_run_3mf_tips = new wxStaticText( m_panel_run_3mf, wxID_ANY, _L("Only Support Print First Plate Now!"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_run_3mf_tips->Wrap( -1 );
	bSizer13->Add( m_staticText_run_3mf_tips, 0, wxALL, 5 );


	bSizer13->Add( 0, 0, 1, wxEXPAND, 5 );


	m_panel_run_3mf->SetSizer( bSizer13 );
	m_panel_run_3mf->Layout();
	bSizer13->Fit( m_panel_run_3mf );
	m_panel_run_3mf->Hide();
	//m_notebook1->AddPage( m_panel_run_3mf, _L("Run Slice  3mf"), false );
	m_panel_run_gcode = new wxPanel( m_notebook1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer131;
	bSizer131 = new wxBoxSizer( wxHORIZONTAL );

	wxBoxSizer* bSizer331;
	bSizer331 = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer141;
	bSizer141 = new wxBoxSizer( wxHORIZONTAL );

	label_gcode_filename = new wxStaticText( m_panel_run_gcode, wxID_ANY, _L("Gcode File:"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	label_gcode_filename->Wrap( -1 );
	label_gcode_filename->SetMinSize( wxSize(FromDIP(100),-1 ) );

	bSizer141->Add( label_gcode_filename, 0, wxALIGN_CENTER|wxALL, 5 );

	txt_gcode_filename = new wxTextCtrl( m_panel_run_gcode, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	txt_gcode_filename->SetMinSize( wxSize(FromDIP(300), -1));

	bSizer141->Add( txt_gcode_filename, 0, wxALIGN_CENTER|wxALL, 5 );

	btn_select_gcode_file = new wxButton( m_panel_run_gcode, wxID_ANY, _L("Select File"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer141->Add( btn_select_gcode_file, 0, wxALL, 5 );


	bSizer331->Add( bSizer141, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer151;
	bSizer151 = new wxBoxSizer( wxHORIZONTAL );

	label_upload_progress1 = new wxStaticText( m_panel_run_gcode, wxID_ANY, _L("Status:"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	label_upload_progress1->Wrap( -1 );
	label_upload_progress1->SetMinSize( wxSize( 100,-1 ) );

	bSizer151->Add( label_upload_progress1, 0, wxALL, 5 );

	 m_status_bar = std::make_shared<BBLStatusBar>(m_panel_run_gcode);
    m_panel_status = m_status_bar->get_panel();
    bSizer151->Add( m_panel_status, 1, wxEXPAND | wxALL, 0 );


	bSizer331->Add( bSizer151, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer161;
	bSizer161 = new wxBoxSizer( wxHORIZONTAL );

	btn_run_gcode = new wxButton( m_panel_run_gcode, wxID_ANY, _L("Run Gcode"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer161->Add( btn_run_gcode, 0, wxALL, 5 );

	btn_pause = new wxButton( m_panel_run_gcode, wxID_ANY, _L("Pause"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer161->Add( btn_pause, 0, wxALL, 5 );

	btn_resume = new wxButton( m_panel_run_gcode, wxID_ANY, _L("Resume"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer161->Add( btn_resume, 0, wxALL, 5 );

	btn_abort_print = new wxButton( m_panel_run_gcode, wxID_ANY, _L("Abort"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer161->Add( btn_abort_print, 0, wxALL, 5 );


	bSizer331->Add( bSizer161, 0, wxEXPAND, 5 );


	bSizer331->Add( 0, 0, 1, wxEXPAND, 5 );


	bSizer131->Add( bSizer331, 0, wxEXPAND, 5 );


	bSizer131->Add( 0, 0, 1, wxEXPAND, 5 );


	m_panel_run_gcode->SetSizer( bSizer131 );
	m_panel_run_gcode->Layout();
	bSizer131->Fit( m_panel_run_gcode );
	m_notebook1->AddPage( m_panel_run_gcode, _L("Run Gcode"), false );
	m_panel_info_control = new wxPanel( m_notebook1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer17;
	bSizer17 = new wxBoxSizer( wxHORIZONTAL );

	wxStaticBoxSizer* sbSizer_info;
	sbSizer_info = new wxStaticBoxSizer( new wxStaticBox( m_panel_info_control, wxID_ANY, _L("Status") ), wxVERTICAL );

	wxGridSizer* bSizer_info;
	bSizer_info = new wxGridSizer( 0, 2, 0, 0 );

	m_staticText_nozzle_temp_title = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("Nozzle Temp:"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	m_staticText_nozzle_temp_title->Wrap( -1 );
	bSizer_info->Add( m_staticText_nozzle_temp_title, 0, wxALIGN_RIGHT|wxALL, 5 );

	label_hot_end_temp_val = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_hot_end_temp_val->Wrap( -1 );
	bSizer_info->Add( label_hot_end_temp_val, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_bed_temp_title = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("Bed Temp:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_bed_temp_title->Wrap( -1 );
	bSizer_info->Add( m_staticText_bed_temp_title, 0, wxALIGN_RIGHT|wxALL, 5 );

	label_bed_end_temp_val = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_bed_end_temp_val->Wrap( -1 );
	bSizer_info->Add( label_bed_end_temp_val, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_pocket_temp = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("Chamber Temp:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_pocket_temp->Wrap( -1 );
	bSizer_info->Add( m_staticText_pocket_temp, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_volume_temp_val = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_volume_temp_val->Wrap( -1 );
	bSizer_info->Add( m_staticText_volume_temp_val, 0, wxALL, 5 );

	m_staticText_frame_temp = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("Frame Temp:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_frame_temp->Wrap( -1 );
	bSizer_info->Add( m_staticText_frame_temp, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_frame_temp_value = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_frame_temp_value->Wrap( -1 );
	bSizer_info->Add( m_staticText_frame_temp_value, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_progress = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("Print Progress:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_progress->Wrap( -1 );
	bSizer_info->Add( m_staticText_progress, 0, wxALIGN_RIGHT|wxALL, 5 );

	label_print_progress_val = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_print_progress_val->Wrap( -1 );
	bSizer_info->Add( label_print_progress_val, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_wifi_signal = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("WiFi Signal:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_wifi_signal->Wrap( -1 );
	bSizer_info->Add( m_staticText_wifi_signal, 0, wxALIGN_RIGHT|wxALL, 5 );

	label_wifi_signal_val = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_wifi_signal_val->Wrap( -1 );
	bSizer_info->Add( label_wifi_signal_val, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_th_link = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("TH Link State:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_th_link->Wrap( -1 );
	bSizer_info->Add( m_staticText_th_link, 0, wxALIGN_RIGHT|wxALL, 5 );

	label_wifi_link_th_val = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_wifi_link_th_val->Wrap( -1 );
	bSizer_info->Add( label_wifi_link_th_val, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_ams_link = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("AMS Link State:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_ams_link->Wrap( -1 );
	bSizer_info->Add( m_staticText_ams_link, 0, wxALIGN_RIGHT|wxALL, 5 );

	label_wifi_link_ams_val = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	label_wifi_link_ams_val->Wrap( -1 );
	bSizer_info->Add( label_wifi_link_ams_val, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_big1_speed_title = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("BigFan1 Speed:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_big1_speed_title->Wrap( -1 );
	bSizer_info->Add( m_staticText_big1_speed_title, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_big1_speed = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_big1_speed->Wrap( -1 );
	bSizer_info->Add( m_staticText_big1_speed, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_big2_speed_title = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("BigFan2 Speed:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_big2_speed_title->Wrap( -1 );
	bSizer_info->Add( m_staticText_big2_speed_title, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_big2_speed = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_big2_speed->Wrap( -1 );
	bSizer_info->Add( m_staticText_big2_speed, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_cooling_speed_title = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("Cooling Fan Speed:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_cooling_speed_title->Wrap( -1 );
	bSizer_info->Add( m_staticText_cooling_speed_title, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_cooling_speed = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_cooling_speed->Wrap( -1 );
	bSizer_info->Add( m_staticText_cooling_speed, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_heatbreak_speed_title = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("Heatbreak Fan Speed:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_heatbreak_speed_title->Wrap( -1 );
	bSizer_info->Add( m_staticText_heatbreak_speed_title, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_heatbreak_speed = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_heatbreak_speed->Wrap( -1 );
	bSizer_info->Add( m_staticText_heatbreak_speed, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_print_stage = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("Print State(MC):"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_print_stage->Wrap( -1 );
	bSizer_info->Add( m_staticText_print_stage, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_mc_print_stage = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_mc_print_stage->Wrap( -1 );
	bSizer_info->Add( m_staticText_mc_print_stage, 0, wxALL, 5 );

	m_staticText_mc_sub_stage_title = new wxStaticText(sbSizer_info->GetStaticBox(), wxID_ANY, _L("Print Sub State(MC):"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_mc_sub_stage_title->Wrap(-1);
	bSizer_info->Add(m_staticText_mc_sub_stage_title, 0, wxALIGN_RIGHT | wxALL, 5);

	m_staticText_mc_sub_stage_value = new wxStaticText(sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_mc_sub_stage_value->Wrap(-1);
	bSizer_info->Add(m_staticText_mc_sub_stage_value, 0, wxALL, 5);

	m_staticText_print_error_code = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("PrintErrCode(MC):"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_print_error_code->Wrap( -1 );
	bSizer_info->Add( m_staticText_print_error_code, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_mc_print_error_code = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_mc_print_error_code->Wrap( -1 );
	bSizer_info->Add( m_staticText_mc_print_error_code, 0, wxALL, 5 );

	m_staticText_gcode_line_number = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("Gcode Line:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_gcode_line_number->Wrap( -1 );
	bSizer_info->Add( m_staticText_gcode_line_number, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_mc_print_line_number = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_mc_print_line_number->Wrap( -1 );
	bSizer_info->Add( m_staticText_mc_print_line_number, 0, wxALIGN_LEFT|wxALL, 5 );

	m_staticText_subtask_id_title = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("SubTask ID:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_subtask_id_title->Wrap( -1 );
	bSizer_info->Add( m_staticText_subtask_id_title, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_subtask_id = new wxStaticText( sbSizer_info->GetStaticBox(), wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_subtask_id->Wrap( -1 );
	bSizer_info->Add( m_staticText_subtask_id, 0, wxALIGN_LEFT|wxALL, 5 );


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
	btn_set_hot_bed_temp = new wxButton( m_panel_settings, wxID_ANY, _L("Set Bed Temp"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( btn_set_hot_bed_temp, 0, wxALIGN_RIGHT|wxALL, 5 );

	txt_set_hot_bed_temp = new wxTextCtrl( m_panel_settings, wxID_ANY, _L("60"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( txt_set_hot_bed_temp, 0, wxALIGN_LEFT|wxALL, 5 );

	btn_set_hot_end_temp = new wxButton( m_panel_settings, wxID_ANY, _L("Set Nozzle Temp"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( btn_set_hot_end_temp, 0, wxALIGN_RIGHT|wxALL, 5 );

	txt_set_hot_end_temp = new wxTextCtrl( m_panel_settings, wxID_ANY, _L("200"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( txt_set_hot_end_temp, 0, wxALIGN_LEFT|wxALL, 5 );

	btn_fan_on = new wxButton( m_panel_settings, wxID_ANY, _L("Fan On"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( btn_fan_on, 0, wxALIGN_RIGHT|wxALL, 5 );

	btn_fan_off = new wxButton( m_panel_settings, wxID_ANY, _L("Fan Off"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( btn_fan_off, 0, wxALIGN_LEFT|wxALL, 5 );

	btn_auto_leveling = new wxButton( m_panel_settings, wxID_ANY, _L("Auto Leveling(G29)"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( btn_auto_leveling, 0, wxALIGN_RIGHT|wxALL, 5 );

	btn_xyz_abs_mode = new wxButton( m_panel_settings, wxID_ANY, _L("XYZ-abs(G90)"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( btn_xyz_abs_mode, 0, wxALIGN_LEFT|wxALL, 5 );

	btn_return_home = new wxButton( m_panel_settings, wxID_ANY, _L("Return Home(G28)"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( btn_return_home, 0, wxALIGN_RIGHT|wxALL, 5 );

	m_button_calibration = new wxButton( m_panel_settings, wxID_ANY, _L("Start Calibration"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer1->Add( m_button_calibration, 0, wxALIGN_LEFT | wxALL, 5 );


	bSizer22->Add( gSizer1, 1, wxALL, 5 );

	wxBoxSizer* bSizer37;
	bSizer37 = new wxBoxSizer( wxHORIZONTAL );

	wxString m_radioBox_chamber_lightChoices[] = { _L("On"), _L("Off"), _L("Flash") };
	int m_radioBox_chamber_lightNChoices = sizeof( m_radioBox_chamber_lightChoices ) / sizeof( wxString );
	m_radioBox_chamber_light = new wxRadioBox( m_panel_settings, wxID_ANY, _L("Chamber Light"), wxDefaultPosition, wxDefaultSize, m_radioBox_chamber_lightNChoices, m_radioBox_chamber_lightChoices, 1, wxRA_SPECIFY_ROWS );
	m_radioBox_chamber_light->SetSelection( 0 );
	bSizer37->Add( m_radioBox_chamber_light, 1, wxALL|wxEXPAND, 5 );


	bSizer22->Add( bSizer37, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer38;
	bSizer38 = new wxBoxSizer( wxVERTICAL );

	wxString m_radioBox_work_lightChoices[] = { _L("On"), _L("Off"), _L("Flash") };
	int m_radioBox_work_lightNChoices = sizeof( m_radioBox_work_lightChoices ) / sizeof( wxString );
	m_radioBox_work_light = new wxRadioBox( m_panel_settings, wxID_ANY, _L("Work Light"), wxDefaultPosition, wxDefaultSize, m_radioBox_work_lightNChoices, m_radioBox_work_lightChoices, 1, wxRA_SPECIFY_ROWS );
	m_radioBox_work_light->SetSelection( 0 );
	bSizer38->Add( m_radioBox_work_light, 1, wxALL|wxEXPAND, 5 );


	bSizer22->Add( bSizer38, 0, wxEXPAND, 5 );


	m_panel_settings->SetSizer( bSizer22 );
	m_panel_settings->Layout();
	bSizer22->Fit( m_panel_settings );
	bSizer25->Add( m_panel_settings, 1, wxALL, 5 );

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
	m_notebook1->AddPage( m_panel_info_control, wxT("Info Control"), false );
	m_panel_upgrade = new wxPanel( m_notebook1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer28;
	bSizer28 = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer32;
	bSizer32 = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer40;
	bSizer40 = new wxBoxSizer(wxVERTICAL);

	wxString m_radioBox_serverChoices[] = { wxT("BBL Internal Server (192.168.0.12)"), wxT("Bambulab Web Server(OTA only)") };
	int m_radioBox_serverNChoices = sizeof(m_radioBox_serverChoices) / sizeof(wxString);
	m_radioBox_server = new wxRadioBox(m_panel_upgrade, wxID_ANY, wxT("Server Selection"), wxDefaultPosition, wxDefaultSize, m_radioBox_serverNChoices, m_radioBox_serverChoices, 1, wxRA_SPECIFY_ROWS);
	m_radioBox_server->SetSelection(0);
	bSizer40->Add(m_radioBox_server, 0, wxALL, 5);

	bSizer32->Add(bSizer40, 0, wxEXPAND, 5);

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
	cb_upgrade_module->Append(wxT("esp32"));
	cb_upgrade_module->Append(wxT("ahb"));
	cb_upgrade_module->Append(wxT("AMS0"));
	cb_upgrade_module->Append(wxT("AMS1"));
	cb_upgrade_module->Append(wxT("AMS2"));
	cb_upgrade_module->Append(wxT("AMS3"));
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
	cb_upgrade_mode->Append( wxT("Wip") );
	cb_upgrade_mode->SetSelection( 1 );
	cb_upgrade_mode->SetMinSize( wxSize( 100,-1 ) );

	bSizer34->Add( cb_upgrade_mode, 0, wxALL, 5 );


	bSizer32->Add( bSizer34, 1, wxEXPAND, 5 );

	wxBoxSizer* bSizer39;
	bSizer39 = new wxBoxSizer( wxHORIZONTAL );

	m_staticText_select_version = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("Select Version:"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	m_staticText_select_version->Wrap( -1 );
	m_staticText_select_version->SetMinSize( wxSize( 120,-1 ) );

	bSizer39->Add( m_staticText_select_version, 0, wxALL, 5 );

	cb_upgrade_version = new wxComboBox( m_panel_upgrade, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, 0 );
    cb_upgrade_version->Append( wxT("V8") );
    cb_upgrade_version->Append( wxT("V7") );
    cb_upgrade_version->Append( wxT("V6") );
	cb_upgrade_version->Append( wxT("V5") );
	cb_upgrade_version->Append(wxT("V4"));
	cb_upgrade_version->Append(wxT("V3"));
	cb_upgrade_version->Append(wxT("V2"));
	cb_upgrade_version->Append(wxT("V1"));
	cb_upgrade_version->Append(wxT("V0"));
	cb_upgrade_version->SetSelection( 1 );
	cb_upgrade_version->SetMinSize( wxSize( 100,-1 ) );

	bSizer39->Add( cb_upgrade_version, 0, wxALL, 5 );


	bSizer32->Add( bSizer39, 1, wxEXPAND, 5 );

	wxBoxSizer* bSizer35;
	bSizer35 = new wxBoxSizer( wxHORIZONTAL );

	m_staticText57 = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("Firmware:"), wxDefaultPosition, wxSize( 120,-1 ), wxALIGN_RIGHT );
	m_staticText57->Wrap( -1 );
	bSizer35->Add( m_staticText57, 0, wxALIGN_CENTER|wxALL, 5 );

	cb_upgrade_firmware = new wxComboBox( m_panel_upgrade, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, 0 );
	cb_upgrade_firmware->SetMinSize( wxSize( FromDIP(400), -1 ));

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

	m_staticText_new_version_title = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("New Version Available:"), wxDefaultPosition, wxSize( 200,-1 ), wxALIGN_RIGHT|wxST_ELLIPSIZE_MIDDLE );
	m_staticText_new_version_title->Wrap( -1 );
	gSizer10->Add( m_staticText_new_version_title, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT|wxALL, 5 );

	wxBoxSizer* bSizer341;
	bSizer341 = new wxBoxSizer( wxHORIZONTAL );

	bSizer341->SetMinSize( wxSize(FromDIP(120), -1));
	m_staticText_new_version = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxSize( -1,-1 ), 0 );
	m_staticText_new_version->Wrap( -1 );
	bSizer341->Add( m_staticText_new_version, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_LEFT|wxALL, 5 );

	m_button_upgrade_confirm = new wxButton( m_panel_upgrade, wxID_ANY, wxT("Confirm"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer341->Add( m_button_upgrade_confirm, 0, wxALL, 5 );


	gSizer10->Add( bSizer341, 0, wxALIGN_LEFT|wxEXPAND, 5 );

	m_staticText_consistency = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("Consistency Upgrade:"), wxDefaultPosition, wxSize( 140,-1 ), wxALIGN_RIGHT|wxST_ELLIPSIZE_MIDDLE );
	m_staticText_consistency->Wrap( -1 );
	gSizer10->Add( m_staticText_consistency, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT|wxALL, 5 );

	wxBoxSizer* bSizer351;
	bSizer351 = new wxBoxSizer( wxHORIZONTAL );

	m_staticText_request_consisitency_upgrade = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_request_consisitency_upgrade->Wrap( -1 );
	bSizer351->Add( m_staticText_request_consisitency_upgrade, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5 );

	m_button_consistency_upgrade_confirm = new wxButton( m_panel_upgrade, wxID_ANY, wxT("Confirm"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer351->Add( m_button_consistency_upgrade_confirm, 0, wxALL, 5 );


	gSizer10->Add( bSizer351, 1, wxEXPAND, 5 );

	m_staticText_status_title = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("Upgrade Status:"), wxDefaultPosition, wxSize( 120,-1 ), wxALIGN_RIGHT );
	m_staticText_status_title->Wrap( -1 );
	gSizer10->Add( m_staticText_status_title, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT|wxALL, 5 );

	label_upgrade_status_val = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	label_upgrade_status_val->Wrap( -1 );
	gSizer10->Add( label_upgrade_status_val, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_LEFT|wxALL, 5 );

	m_staticText_upgrade_module = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("Upgrade Module:"), wxDefaultPosition, wxSize( 120,-1 ), wxALIGN_RIGHT );
	m_staticText_upgrade_module->Wrap( -1 );
	gSizer10->Add( m_staticText_upgrade_module, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_upgrade_module_value = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_upgrade_module_value->Wrap( -1 );
	gSizer10->Add( m_staticText_upgrade_module_value, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_LEFT|wxALL, 5 );

	m_staticText_upgrade_progress = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("Upgrade Progress:"), wxDefaultPosition, wxSize( 120,-1 ), wxALIGN_RIGHT );
	m_staticText_upgrade_progress->Wrap( -1 );
	gSizer10->Add( m_staticText_upgrade_progress, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT|wxALL, 5 );

	label_upgrade_progress_val = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	label_upgrade_progress_val->Wrap( -1 );
	gSizer10->Add( label_upgrade_progress_val, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_LEFT|wxALL, 5 );

	m_staticText_upgrade_info = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("Upgrade Info:"), wxDefaultPosition, wxSize( 120,-1 ), wxALIGN_RIGHT );
	m_staticText_upgrade_info->Wrap( -1 );
	gSizer10->Add( m_staticText_upgrade_info, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT|wxALL, 5 );

	label_upgrade_message_val = new wxStaticText( m_panel_upgrade, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	label_upgrade_message_val->Wrap( -1 );
	gSizer10->Add( label_upgrade_message_val, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_LEFT|wxALL, 5 );


	bSizer31->Add( gSizer10, 0, 0, 5 );


	bSizer31->Add( 0, 0, 1, wxEXPAND, 5 );


	bSizer30->Add( bSizer31, 0, wxEXPAND, 5 );


	bSizer30->Add( 0, 0, 1, wxEXPAND, 5 );


	bSizer28->Add( bSizer30, 0, wxEXPAND, 5 );


	bSizer28->Add( 0, 0, 1, wxEXPAND, 5 );


	m_panel_upgrade->SetSizer( bSizer28 );
	m_panel_upgrade->Layout();
	bSizer28->Fit( m_panel_upgrade );
	m_notebook1->AddPage( m_panel_upgrade, wxT("Upgrade"), true );
	m_panel_ams = new wxPanel( m_notebook1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer321;
	bSizer321 = new wxBoxSizer( wxVERTICAL );

	wxGridSizer* gSizer5;
	gSizer5 = new wxGridSizer( 0, 2, 0, 0 );

	btn_switch_t = new wxButton( m_panel_ams, wxID_ANY, wxT("Switch AMS:"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer5->Add( btn_switch_t, 0, wxALIGN_RIGHT|wxALL, 5 );

	txt_switch_val = new wxTextCtrl( m_panel_ams, wxID_ANY, wxT("1"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer5->Add( txt_switch_val, 0, wxALL, 5 );

	label_ams_flush_temp1 = new wxStaticText( m_panel_ams, wxID_ANY, wxT("AMS Flush Temp 1:"), wxDefaultPosition, wxDefaultSize, 0 );
	label_ams_flush_temp1->Wrap( -1 );
	gSizer5->Add( label_ams_flush_temp1, 0, wxALIGN_RIGHT|wxALL, 5 );

	txt_ams_flush_temp1 = new wxTextCtrl( m_panel_ams, wxID_ANY, wxT("220"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer5->Add( txt_ams_flush_temp1, 0, wxALL, 5 );

	label_ams_flush_temp2 = new wxStaticText( m_panel_ams, wxID_ANY, wxT("AMS Flush Temp 2:"), wxDefaultPosition, wxDefaultSize, 0 );
	label_ams_flush_temp2->Wrap( -1 );
	gSizer5->Add( label_ams_flush_temp2, 0, wxALIGN_RIGHT|wxALL, 5 );

	txt_ams_flush_temp2 = new wxTextCtrl( m_panel_ams, wxID_ANY, wxT("220"), wxDefaultPosition, wxDefaultSize, 0 );
	gSizer5->Add( txt_ams_flush_temp2, 0, wxALL, 5 );

	cbox_ams_auto_home = new wxCheckBox( m_panel_ams, wxID_ANY, wxT("AMS Auto Home"), wxDefaultPosition, wxDefaultSize, 0 );
    cbox_ams_auto_home->SetValue(true);
	gSizer5->Add( cbox_ams_auto_home, 0, wxALIGN_RIGHT|wxLEFT|wxTOP, 5 );


	gSizer5->Add( 0, 0, 1, wxEXPAND, 5 );

	m_button_ams_pause = new wxButton( m_panel_ams, wxID_ANY, wxT("AMS PAUSE"), wxDefaultPosition, wxSize( 100,-1 ), 0 );
	gSizer5->Add( m_button_ams_pause, 0, wxALL, 5 );

	m_button_ams_resume = new wxButton( m_panel_ams, wxID_ANY, wxT("AMS RESUME"), wxDefaultPosition, wxSize( 100,-1 ), 0 );
	gSizer5->Add( m_button_ams_resume, 0, wxALL, 5 );


	bSizer321->Add( gSizer5, 0, 0, 5 );


	bSizer321->Add( 0, 15, 0, 0, 0 );

	wxBoxSizer* bSizer36;
	bSizer36 = new wxBoxSizer( wxHORIZONTAL );

	m_button_ams_0 = new wxButton( m_panel_ams, wxID_ANY, wxT("Switch AMS 0"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer36->Add( m_button_ams_0, 0, wxALL, 10 );

	m_button_ams_1 = new wxButton( m_panel_ams, wxID_ANY, wxT("Switch AMS 1"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer36->Add( m_button_ams_1, 0, wxALL, 10 );

	m_button_ams_2 = new wxButton( m_panel_ams, wxID_ANY, wxT("Switch AMS 2"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer36->Add( m_button_ams_2, 0, wxALL, 10 );

	m_button_ams_3 = new wxButton( m_panel_ams, wxID_ANY, wxT("Switch AMS 3"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer36->Add( m_button_ams_3, 0, wxALL, 10 );

	m_button_ams_255 = new wxButton( m_panel_ams, wxID_ANY, wxT("AMS Return"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer36->Add( m_button_ams_255, 0, wxALL, 10 );


	bSizer321->Add( bSizer36, 0, 0, 10 );

	m_dataViewCtrl_ams = new wxDataViewCtrl( m_panel_ams, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 );
	bSizer321->Add(m_dataViewCtrl_ams, 1, wxALL | wxEXPAND, 5);


	m_panel_ams->SetSizer( bSizer321 );
	m_panel_ams->Layout();
	bSizer321->Fit( m_panel_ams );
	m_notebook1->AddPage( m_panel_ams, wxT("AMS"), false );

	bSizer301->Add( m_notebook1, 1, wxEXPAND | wxALL, 5 );


	m_panel_left->SetSizer( bSizer301 );
	m_panel_left->Layout();
	bSizer301->Fit( m_panel_left );
	m_panel_log = new wxPanel( m_splitter1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	m_panel_log->SetMinSize( wxSize( 100,-1 ) );

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
	this->Layout();
}

DebugToolPanel::~DebugToolPanel()
{
}
