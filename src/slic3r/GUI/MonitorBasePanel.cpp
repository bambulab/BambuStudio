///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 3.10.1-0-g8feb16b3)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#include "MonitorBasePanel.h"
#include "Widgets/SwitchButton.hpp"

#ifdef __WXMAC__
#include "wxMediaCtrl2.h"
#endif

///////////////////////////////////////////////////////////////////////////
using namespace Slic3r::GUI;

MonitorBasePanel::MonitorBasePanel( wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name ) : wxPanel( parent, id, pos, size, style, name )
{
	this->SetMinSize( wxSize( 600,400 ) );

	wxBoxSizer* bSizer_top;
	bSizer_top = new wxBoxSizer( wxHORIZONTAL );

	m_splitter = new wxSplitterWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_3D|wxSP_BORDER );
	m_splitter->SetSashGravity( 0 );
	m_splitter->SetSashSize( 2 );
	m_splitter->Connect( wxEVT_IDLE, wxIdleEventHandler( MonitorBasePanel::m_splitterOnIdle ), NULL, this );
	m_splitter->SetMinimumPaneSize( 238 );

	m_panel_splitter_left = new wxPanel( m_splitter, wxID_ANY, wxDefaultPosition, wxSize( 240,-1 ), wxTAB_TRAVERSAL );
	m_panel_splitter_left->SetMinSize( wxSize( 238,-1 ) );
	m_panel_splitter_left->SetMaxSize( wxSize( 400,-1 ) );

	wxBoxSizer* bSizer_left_top;
	bSizer_left_top = new wxBoxSizer( wxHORIZONTAL );

	wxBoxSizer* bSizerleft;
	bSizerleft = new wxBoxSizer( wxVERTICAL );

	m_panel_machine_status_title = new wxPanel( m_panel_splitter_left, wxID_ANY, wxDefaultPosition, wxSize( -1,36 ), wxTAB_TRAVERSAL );
	m_panel_machine_status_title->SetBackgroundColour( wxColour( 233, 233, 233 ) );

	wxBoxSizer* bSizer_status_caption;
	bSizer_status_caption = new wxBoxSizer( wxHORIZONTAL );

	bSizer_status_caption->SetMinSize( wxSize( -1,36 ) );

	bSizer_status_caption->Add( 22, 0, 0, 0, 0 );

	m_staticText_status = new wxStaticText( m_panel_machine_status_title, wxID_ANY, wxT("Status Info"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_status->Wrap( -1 );
	m_staticText_status->SetFont( wxFont( 12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxEmptyString ) );

	bSizer_status_caption->Add( m_staticText_status, 1, wxALIGN_CENTER_VERTICAL|wxALL, 0 );


	bSizer_status_caption->Add( 5, 0, 0, 0, 5 );


	m_panel_machine_status_title->SetSizer( bSizer_status_caption );
	m_panel_machine_status_title->Layout();
	bSizerleft->Add( m_panel_machine_status_title, 0, wxALL|wxEXPAND, 0 );

	m_panel_machine_status_content = new wxPanel( m_panel_splitter_left, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	m_panel_machine_status_content->SetBackgroundColour( wxColour( 255, 255, 255 ) );

	wxBoxSizer* bSizer_status;
	bSizer_status = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer291;
	bSizer291 = new wxBoxSizer( wxHORIZONTAL );


	bSizer291->Add( 10, 0, 0, wxALL, 5 );

	wxBoxSizer* bSizer_left_up;
	bSizer_left_up = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_device;
	bSizer_device = new wxBoxSizer( wxHORIZONTAL );


	bSizer_left_up->Add( bSizer_device, 0, wxALL|wxEXPAND, 0 );

	wxFlexGridSizer* fgSizer_status;
	fgSizer_status = new wxFlexGridSizer( 1, 2, 0, 0 );
	fgSizer_status->AddGrowableCol( 1 );
	fgSizer_status->SetFlexibleDirection( wxHORIZONTAL );
	fgSizer_status->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

	m_staticText_machine_status = new wxStaticText( m_panel_machine_status_content, wxID_ANY, wxT("Printer"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_machine_status->Wrap( -1 );
	m_staticText_machine_status->SetMinSize( wxSize( 80,-1 ) );

	fgSizer_status->Add( m_staticText_machine_status, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );

	wxBoxSizer* bSizer_machine_values;
	bSizer_machine_values = new wxBoxSizer( wxHORIZONTAL );

	bSizer_machine_values->SetMinSize( wxSize( -1,34 ) );
	m_staticText_machine_name = new wxStaticText( m_panel_machine_status_content, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT|wxST_ELLIPSIZE_END );
	m_staticText_machine_name->Wrap( -1 );
	bSizer_machine_values->Add( m_staticText_machine_name, 1, wxALIGN_CENTER_VERTICAL|wxALL, 0 );

	m_staticText_wifi_signal = new wxStaticText( m_panel_machine_status_content, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_wifi_signal->Wrap( -1 );
	bSizer_machine_values->Add( m_staticText_wifi_signal, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );

	m_bitmap_signal = new wxStaticBitmap( m_panel_machine_status_content, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize( 20,20 ), 0 );
	bSizer_machine_values->Add( m_bitmap_signal, 0, wxALIGN_CENTER|wxALL, 5 );


	fgSizer_status->Add( bSizer_machine_values, 1, wxALIGN_CENTER_VERTICAL|wxEXPAND, 0 );

	m_staticText_printing_title = new wxStaticText( m_panel_machine_status_content, wxID_ANY, wxT("Status"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_printing_title->Wrap( -1 );
	m_staticText_printing_title->SetMinSize( wxSize( 80,-1 ) );

	fgSizer_status->Add( m_staticText_printing_title, 0, wxALIGN_CENTER_VERTICAL|wxALL|wxEXPAND, 0 );

	wxBoxSizer* bSizer83;
	bSizer83 = new wxBoxSizer( wxHORIZONTAL );

	bSizer83->SetMinSize( wxSize( -1,34 ) );
	m_staticText_printing_val = new wxStaticText( m_panel_machine_status_content, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxSize( -1,-1 ), wxALIGN_LEFT|wxST_ELLIPSIZE_END );
	m_staticText_printing_val->Wrap( -1 );
	bSizer83->Add( m_staticText_printing_val, 1, wxALIGN_CENTER_VERTICAL|wxALL, 0 );


	fgSizer_status->Add( bSizer83, 1, wxALIGN_CENTER_VERTICAL|wxEXPAND, 0 );

	m_staticText_capacity_title = new wxStaticText( m_panel_machine_status_content, wxID_ANY, wxT("Capacity"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_capacity_title->Wrap( -1 );
	m_staticText_capacity_title->SetMinSize( wxSize( 80,-1 ) );

	fgSizer_status->Add( m_staticText_capacity_title, 0, wxALIGN_CENTER_VERTICAL|wxALL|wxEXPAND, 0 );

	wxBoxSizer* bSizer84;
	bSizer84 = new wxBoxSizer( wxHORIZONTAL );

	bSizer84->SetMinSize( wxSize( -1,34 ) );
	m_staticText_capacity_val = new wxStaticText( m_panel_machine_status_content, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT|wxST_ELLIPSIZE_END );
	m_staticText_capacity_val->Wrap( -1 );
	bSizer84->Add( m_staticText_capacity_val, 1, wxALIGN_CENTER_VERTICAL, 0 );


	fgSizer_status->Add( bSizer84, 1, wxALIGN_CENTER_VERTICAL|wxEXPAND, 0 );


	bSizer_left_up->Add( fgSizer_status, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer_ams_left1;
	bSizer_ams_left1 = new wxBoxSizer( wxHORIZONTAL );

	m_staticText_filament_left_title = new wxStaticText( m_panel_machine_status_content, wxID_ANY, wxT("Filament Left"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_filament_left_title->Wrap( -1 );
	m_staticText_filament_left_title->Hide();
	m_staticText_filament_left_title->SetMinSize( wxSize( 80,-1 ) );

	bSizer_ams_left1->Add( m_staticText_filament_left_title, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );

	m_scrolledWindow_ams = new wxScrolledWindow( m_panel_machine_status_content, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL|wxVSCROLL );
	m_scrolledWindow_ams->SetScrollRate( 5, 5 );
	m_scrolledWindow_ams->Hide();

	wxBoxSizer* bSizer_ams_left;
	bSizer_ams_left = new wxBoxSizer( wxVERTICAL );


	m_scrolledWindow_ams->SetSizer( bSizer_ams_left );
	m_scrolledWindow_ams->Layout();
	bSizer_ams_left->Fit( m_scrolledWindow_ams );
	bSizer_ams_left1->Add( m_scrolledWindow_ams, 1, wxEXPAND | wxALL, 5 );


	bSizer_left_up->Add( bSizer_ams_left1, 1, wxEXPAND, 5 );


	bSizer291->Add( bSizer_left_up, 1, wxEXPAND, 5 );


	bSizer291->Add( 5, 0, 0, wxALL, 5 );


	bSizer_status->Add( bSizer291, 0, wxEXPAND, 5 );


	m_panel_machine_status_content->SetSizer( bSizer_status );
	m_panel_machine_status_content->Layout();
	bSizer_status->Fit( m_panel_machine_status_content );
	bSizerleft->Add( m_panel_machine_status_content, 0, wxEXPAND | wxALL, 0 );

	m_staticline1 = new wxStaticLine( m_panel_splitter_left, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
	m_staticline1->SetBackgroundColour( wxColour( 166, 169, 170 ) );

	bSizerleft->Add( m_staticline1, 0, wxEXPAND | wxALL, 0 );

	m_panel_tasklist_title = new wxPanel( m_panel_splitter_left, wxID_ANY, wxDefaultPosition, wxSize( -1,36 ), wxTAB_TRAVERSAL );
	m_panel_tasklist_title->SetBackgroundColour( wxColour( 233, 233, 233 ) );

	wxBoxSizer* bSizer_tasklist_caption;
	bSizer_tasklist_caption = new wxBoxSizer( wxHORIZONTAL );

	bSizer_tasklist_caption->SetMinSize( wxSize( -1,36 ) );

	bSizer_tasklist_caption->Add( 22, 0, 0, wxALL, 0 );

	m_staticText_subtask_list_title = new wxStaticText( m_panel_tasklist_title, wxID_ANY, wxT("Task List"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_subtask_list_title->Wrap( -1 );
	m_staticText_subtask_list_title->SetFont( wxFont( 12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxEmptyString ) );
	m_staticText_subtask_list_title->SetMinSize( wxSize( 80,-1 ) );

	bSizer_tasklist_caption->Add( m_staticText_subtask_list_title, 1, wxALIGN_CENTER_VERTICAL|wxALL, 0 );


	bSizer_tasklist_caption->Add( 0, 0, 0, wxALL, 5 );


	m_panel_tasklist_title->SetSizer( bSizer_tasklist_caption );
	m_panel_tasklist_title->Layout();
	bSizerleft->Add( m_panel_tasklist_title, 0, wxEXPAND | wxALL, 0 );

	m_panel_tasklist_content = new wxPanel( m_panel_splitter_left, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer34;
	bSizer34 = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer55;
	bSizer55 = new wxBoxSizer( wxVERTICAL );

	m_scrolledWindow_tasklist = new wxScrolledWindow( m_panel_tasklist_content, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL|wxVSCROLL );
	m_scrolledWindow_tasklist->SetScrollRate( 5, 5 );
	m_scrolledWindow_tasklist->SetBackgroundColour( wxColour( 255, 255, 255 ) );
	m_scrolledWindow_tasklist->SetMinSize( wxSize( -1,200 ) );

	wxBoxSizer* bSizer_tasklist;
	bSizer_tasklist = new wxBoxSizer( wxVERTICAL );


	m_scrolledWindow_tasklist->SetSizer( bSizer_tasklist );
	m_scrolledWindow_tasklist->Layout();
	bSizer_tasklist->Fit( m_scrolledWindow_tasklist );
	bSizer55->Add( m_scrolledWindow_tasklist, 1, wxEXPAND | wxALL, 0 );


	bSizer34->Add( bSizer55, 1, wxEXPAND, 5 );


	m_panel_tasklist_content->SetSizer( bSizer34 );
	m_panel_tasklist_content->Layout();
	bSizer34->Fit( m_panel_tasklist_content );
	bSizerleft->Add( m_panel_tasklist_content, 1, wxEXPAND | wxALL, 0 );

	m_staticline8 = new wxStaticLine( m_panel_splitter_left, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
	bSizerleft->Add( m_staticline8, 0, wxEXPAND | wxALL, 0 );

	m_panel_notification = new wxPanel( m_panel_splitter_left, wxID_ANY, wxDefaultPosition, wxSize( -1,36 ), wxTAB_TRAVERSAL );
	m_panel_notification->SetBackgroundColour( wxColour( 233, 233, 233 ) );

	wxBoxSizer* bSizer_notification_caption;
	bSizer_notification_caption = new wxBoxSizer( wxHORIZONTAL );

	bSizer_notification_caption->SetMinSize( wxSize( -1,36 ) );

	bSizer_notification_caption->Add( 22, 0, 0, wxALL, 0 );

	m_staticText_notification = new wxStaticText( m_panel_notification, wxID_ANY, wxT("Notification"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_notification->Wrap( -1 );
	m_staticText_notification->SetFont( wxFont( 12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxEmptyString ) );
	m_staticText_notification->SetMinSize( wxSize( 80,-1 ) );

	bSizer_notification_caption->Add( m_staticText_notification, 1, wxALIGN_CENTER_VERTICAL|wxALL, 0 );


	bSizer_notification_caption->Add( 0, 0, 0, wxALL, 5 );


	m_panel_notification->SetSizer( bSizer_notification_caption );
	m_panel_notification->Layout();
	bSizerleft->Add( m_panel_notification, 0, wxEXPAND | wxALL, 0 );

	m_panel_notification_content = new wxPanel( m_panel_splitter_left, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	m_panel_notification_content->SetBackgroundColour( wxColour( 255, 255, 255 ) );

	wxBoxSizer* bSizer_hms;
	bSizer_hms = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer40;
	bSizer40 = new wxBoxSizer( wxHORIZONTAL );

	m_textCtrl_notification = new wxTextCtrl( m_panel_notification_content, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE );
	bSizer40->Add( m_textCtrl_notification, 1, wxALL|wxEXPAND, 5 );


	bSizer40->Add( 5, 0, 0, 0, 5 );


	bSizer_hms->Add( bSizer40, 1, wxEXPAND, 5 );


	m_panel_notification_content->SetSizer( bSizer_hms );
	m_panel_notification_content->Layout();
	bSizer_hms->Fit( m_panel_notification_content );
	bSizerleft->Add( m_panel_notification_content, 1, wxEXPAND | wxALL, 0 );


	bSizerleft->Add( 0, 0, 0, wxEXPAND, 0 );


	bSizer_left_top->Add( bSizerleft, 1, wxEXPAND, 0 );


	m_panel_splitter_left->SetSizer( bSizer_left_top );
	m_panel_splitter_left->Layout();
	m_panel_splitter_right = new wxPanel( m_splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	m_panel_splitter_right->SetBackgroundColour( wxColour( 250, 250, 250 ) );
	m_panel_splitter_right->SetMinSize( wxSize( 880,670 ) );

	wxBoxSizer* bSizer66;
	bSizer66 = new wxBoxSizer( wxVERTICAL );


	bSizer66->Add( 0, 10, 0, wxALL, 5 );

	wxBoxSizer* bSizer45;
	bSizer45 = new wxBoxSizer( wxHORIZONTAL );

	wxBoxSizer* bSizer59;
	bSizer59 = new wxBoxSizer( wxHORIZONTAL );

	wxBoxSizer* bSizer54;
	bSizer54 = new wxBoxSizer( wxVERTICAL );

	m_panel29 = new wxPanel( m_panel_splitter_right, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	m_panel29->SetMaxSize( wxSize( 600,-1 ) );

	wxBoxSizer* bSizer_middle;
	bSizer_middle = new wxBoxSizer( wxVERTICAL );

	bSizer_middle->SetMinSize( wxSize( 300,-1 ) );
	wxBoxSizer* bSizer_notebook_buttons;
	bSizer_notebook_buttons = new wxBoxSizer( wxHORIZONTAL );

	bSizer_notebook_buttons->SetMinSize( wxSize( -1,40 ) );
	m_staticText_live = new wxStaticText( m_panel29, wxID_ANY, wxT("Live"), wxDefaultPosition, wxSize( 100,-1 ), 0 );
	m_staticText_live->Wrap( -1 );
	bSizer_notebook_buttons->Add( m_staticText_live, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5 );

	m_staticText_timelapse = new wxStaticText( m_panel29, wxID_ANY, wxT("Timelapse"), wxDefaultPosition, wxSize( 100,-1 ), 0 );
	m_staticText_timelapse->Wrap( -1 );
	bSizer_notebook_buttons->Add( m_staticText_timelapse, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5 );


	bSizer_middle->Add( bSizer_notebook_buttons, 0, wxALL, 0 );

	m_simplebook_middle = new wxSimplebook( m_panel29, wxID_ANY, wxDefaultPosition, wxSize( -1,-1 ), 0 );
	m_panel_monitor = new wxPanel( m_simplebook_middle, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer_tab_monitor;
	bSizer_tab_monitor = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_live;
	bSizer_live = new wxBoxSizer( wxVERTICAL );

	m_panel_live = new wxPanel( m_panel_monitor, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer29;
	bSizer29 = new wxBoxSizer( wxVERTICAL );

	//m_bitmap_live_default = new wxStaticBitmap( m_panel_live, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize( -1,-1 ), 0 );
	//m_bitmap_live_default->SetMinSize( wxSize( -1,300 ) );
#ifdef __WXMAC__
    m_media_ctrl = new wxMediaCtrl2(m_panel_live);
#else
    m_media_ctrl = new wxMediaCtrl(m_panel_live, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxMEDIACTRLPLAYERCONTROLS_NONE);
#endif
	m_media_ctrl->SetMinSize(wxSize(400, 300));
	m_media_ctrl->Bind(wxEVT_MEDIA_STATECHANGED, [this](wxMediaEvent& e) {
        wxSize size = m_media_ctrl->GetBestSize();
        if (size.GetWidth() > 1000)
			m_media_ctrl->Play();
		});

	bSizer29->Add(m_media_ctrl, 0, wxALL|wxEXPAND, 5 );


	m_panel_live->SetSizer( bSizer29 );
	m_panel_live->Layout();
	bSizer29->Fit( m_panel_live );
	bSizer_live->Add( m_panel_live, 0, wxALL|wxEXPAND, 0 );


	bSizer_tab_monitor->Add( bSizer_live, 1, wxEXPAND, 5 );


	m_panel_monitor->SetSizer( bSizer_tab_monitor );
	m_panel_monitor->Layout();
	bSizer_tab_monitor->Fit( m_panel_monitor );
	m_simplebook_middle->AddPage( m_panel_monitor, wxT("a page"), false );
	m_panel_timelapse = new wxPanel( m_simplebook_middle, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	m_simplebook_middle->AddPage( m_panel_timelapse, wxT("a page"), false );

	bSizer_middle->Add( m_simplebook_middle, 1, wxALIGN_CENTER|wxALL|wxEXPAND, 0 );

	m_panel_printing_content = new wxPanel( m_panel29, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer57;
	bSizer57 = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_subtask_title;
	bSizer_subtask_title = new wxBoxSizer( wxHORIZONTAL );

	bSizer_subtask_title->SetMinSize( wxSize( -1,50 ) );
	m_staticText_task_caption = new wxStaticText( m_panel_printing_content, wxID_ANY, wxT("Printing"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_task_caption->Wrap( -1 );
	m_staticText_task_caption->SetFont( wxFont( 12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxEmptyString ) );
	m_staticText_task_caption->SetMinSize( wxSize( 80,-1 ) );

	bSizer_subtask_title->Add( m_staticText_task_caption, 1, wxALIGN_CENTER_VERTICAL|wxALL, 0 );

	m_bpButton_open_project = new wxBitmapButton( m_panel_printing_content, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	m_bpButton_open_project->Hide();

	bSizer_subtask_title->Add( m_bpButton_open_project, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );


	bSizer57->Add( bSizer_subtask_title, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer_task;
	bSizer_task = new wxBoxSizer( wxHORIZONTAL );

	m_bitmap_thumbnail = new wxStaticBitmap( m_panel_printing_content, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize( 160,160 ), 0 );
	m_bitmap_thumbnail->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ) );

	bSizer_task->Add( m_bitmap_thumbnail, 0, wxALIGN_BOTTOM|wxALIGN_CENTER_VERTICAL|wxALL, 0 );


	bSizer_task->Add( 5, 0, 0, wxEXPAND, 0 );

	wxBoxSizer* bSizer_task_internal;
	bSizer_task_internal = new wxBoxSizer( wxVERTICAL );


	bSizer_task_internal->Add( 0, 10, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer_subtask_info;
	bSizer_subtask_info = new wxBoxSizer( wxVERTICAL );


	bSizer_subtask_info->Add( 0, 0, 1, wxEXPAND, 5 );

	wxBoxSizer* bSizer63;
	bSizer63 = new wxBoxSizer( wxHORIZONTAL );

	m_staticText_subtask_value = new wxStaticText( m_panel_printing_content, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT|wxST_ELLIPSIZE_END );
	m_staticText_subtask_value->Wrap( -1 );
	bSizer63->Add( m_staticText_subtask_value, 1, wxALIGN_CENTER_VERTICAL, 0 );

	m_staticText_subtask_progress = new wxStaticText( m_panel_printing_content, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_subtask_progress->Wrap( -1 );
	bSizer63->Add( m_staticText_subtask_progress, 0, wxALL, 5 );


	bSizer_subtask_info->Add( bSizer63, 0, wxEXPAND, 0 );

	wxBoxSizer* bSizer64;
	bSizer64 = new wxBoxSizer( wxVERTICAL );

	m_gauge_progress = new wxGauge( m_panel_printing_content, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize, wxGA_HORIZONTAL );
	m_gauge_progress->SetValue( 0 );
	bSizer64->Add( m_gauge_progress, 1, wxALL|wxEXPAND, 0 );


	bSizer_subtask_info->Add( bSizer64, 0, wxEXPAND, 0 );

	wxBoxSizer* bSizer65;
	bSizer65 = new wxBoxSizer( wxHORIZONTAL );

	m_staticText_progress_duration = new wxStaticText( m_panel_printing_content, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_progress_duration->Wrap( -1 );
	bSizer65->Add( m_staticText_progress_duration, 1, wxALIGN_CENTER_VERTICAL|wxALL|wxEXPAND, 0 );

	m_staticText_progress_left = new wxStaticText( m_panel_printing_content, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_progress_left->Wrap( -1 );
	bSizer65->Add( m_staticText_progress_left, 0, wxALL, 5 );


	bSizer_subtask_info->Add( bSizer65, 0, wxEXPAND, 0 );


	bSizer_subtask_info->Add( 0, 15, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer_task_btn;
	bSizer_task_btn = new wxBoxSizer( wxHORIZONTAL );


	bSizer_task_btn->Add( 0, 0, 1, wxEXPAND, 5 );

	m_button_report = new wxButton( m_panel_printing_content, wxID_ANY, wxT("Report"), wxDefaultPosition, wxSize( 100,36 ), 0 );
	bSizer_task_btn->Add( m_button_report, 0, wxALIGN_CENTER|wxALIGN_RIGHT|wxALL, 5 );


	bSizer_task_btn->Add( 20, 0, 0, wxEXPAND, 5 );

	m_button_pause_resume = new wxButton( m_panel_printing_content, wxID_ANY, wxT("Pause"), wxDefaultPosition, wxSize( 100,36 ), 0 );
	bSizer_task_btn->Add( m_button_pause_resume, 0, wxALIGN_CENTER|wxALIGN_RIGHT|wxALL, 0 );


	bSizer_task_btn->Add( 20, 0, 0, wxEXPAND, 0 );

	m_button_abort = new wxButton( m_panel_printing_content, wxID_ANY, wxT("Abort"), wxDefaultPosition, wxSize( 100,36 ), 0 );
	bSizer_task_btn->Add( m_button_abort, 0, wxALIGN_CENTER|wxALIGN_RIGHT|wxALL, 0 );


	bSizer_subtask_info->Add( bSizer_task_btn, 0, wxALIGN_BOTTOM|wxALIGN_CENTER|wxBOTTOM|wxEXPAND, 0 );


	bSizer_task_internal->Add( bSizer_subtask_info, 1, wxALL|wxEXPAND, 0 );


	bSizer_task->Add( bSizer_task_internal, 1, wxALL|wxEXPAND, 0 );


	bSizer57->Add( bSizer_task, 0, wxALL|wxEXPAND, 5 );


	bSizer57->Add( 0, 0, 1, wxEXPAND, 5 );


	m_panel_printing_content->SetSizer( bSizer57 );
	m_panel_printing_content->Layout();
	bSizer57->Fit( m_panel_printing_content );
	bSizer_middle->Add( m_panel_printing_content, 0, wxEXPAND | wxALL, 0 );


	bSizer_middle->Add( 0, 0, 1, wxEXPAND, 5 );


	m_panel29->SetSizer( bSizer_middle );
	m_panel29->Layout();
	bSizer_middle->Fit( m_panel29 );
	bSizer54->Add( m_panel29, 1, wxALIGN_CENTER|wxALL|wxEXPAND, 10 );


	bSizer59->Add( bSizer54, 1, wxALL|wxEXPAND, 0 );

	m_staticline6 = new wxStaticLine( m_panel_splitter_right, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL );
	bSizer59->Add( m_staticline6, 0, wxEXPAND | wxALL, 0 );


	bSizer45->Add( bSizer59, 1, wxEXPAND, 5 );

	m_panel27 = new wxPanel( m_panel_splitter_right, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE|wxTAB_TRAVERSAL );
	m_panel27->SetMaxSize( wxSize( 400,-1 ) );

	bSizer45->Add( m_panel27, 0, wxEXPAND, 0 );

	wxBoxSizer* bSizer651;
	bSizer651 = new wxBoxSizer( wxHORIZONTAL );


	bSizer651->Add( 10, 0, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer_right_control;
	bSizer_right_control = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_control;
	bSizer_control = new wxBoxSizer( wxVERTICAL );

	bSizer_control->SetMinSize( wxSize( 400,-1 ) );
	wxBoxSizer* bSizer_temp_caption;
	bSizer_temp_caption = new wxBoxSizer( wxVERTICAL );

	m_staticText_temp_caption = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("Temp Control"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_temp_caption->Wrap( -1 );
	bSizer_temp_caption->Add( m_staticText_temp_caption, 0, wxALL, 5 );


	bSizer_control->Add( bSizer_temp_caption, 1, wxEXPAND, 5 );

	wxBoxSizer* bSizer_temp_ctrl;
	bSizer_temp_ctrl = new wxBoxSizer( wxVERTICAL );

	wxFlexGridSizer* fgSizer_temp;
	fgSizer_temp = new wxFlexGridSizer( 0, 5, 0, 0 );
	fgSizer_temp->SetFlexibleDirection( wxBOTH );
	fgSizer_temp->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_NONE );


	fgSizer_temp->Add( 60, 0, 0, wxEXPAND, 0 );

	wxBoxSizer* bSizer68;
	bSizer68 = new wxBoxSizer( wxVERTICAL );

	bSizer68->SetMinSize( wxSize( 75,-1 ) );
	m_bitmap_bed = new wxStaticBitmap( m_panel_splitter_right, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0 );
	bSizer68->Add( m_bitmap_bed, 0, wxALIGN_CENTER|wxALL, 5 );


	fgSizer_temp->Add( bSizer68, 1, wxEXPAND, 5 );

	wxBoxSizer* bSizer67;
	bSizer67 = new wxBoxSizer( wxVERTICAL );

	bSizer67->SetMinSize( wxSize( 75,-1 ) );
	m_bitmap_nozzle = new wxStaticBitmap( m_panel_splitter_right, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0 );
	bSizer67->Add( m_bitmap_nozzle, 0, wxALIGN_CENTER|wxALL, 5 );


	fgSizer_temp->Add( bSizer67, 1, wxEXPAND, 5 );

	wxBoxSizer* bSizer69;
	bSizer69 = new wxBoxSizer( wxVERTICAL );

	bSizer69->SetMinSize( wxSize( 75,-1 ) );
	m_bitmap_pocket = new wxStaticBitmap( m_panel_splitter_right, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0 );
	bSizer69->Add( m_bitmap_pocket, 0, wxALIGN_CENTER|wxALL, 5 );


	fgSizer_temp->Add( bSizer69, 1, wxEXPAND, 5 );


	fgSizer_temp->Add( 0, 0, 1, wxEXPAND, 5 );

	m_staticText_current = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("Current"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_current->Wrap( -1 );
	m_staticText_current->SetMinSize( wxSize( 60,-1 ) );

	fgSizer_temp->Add( m_staticText_current, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );

	m_staticText_bed_current = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxSize( 75,-1 ), wxALIGN_CENTER_HORIZONTAL );
	m_staticText_bed_current->Wrap( -1 );
	fgSizer_temp->Add( m_staticText_bed_current, 1, wxALIGN_CENTER|wxALL, 0 );

	m_staticText_nozzle_current = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxSize( 75,-1 ), wxALIGN_CENTER_HORIZONTAL );
	m_staticText_nozzle_current->Wrap( -1 );
	fgSizer_temp->Add( m_staticText_nozzle_current, 1, wxALIGN_CENTER|wxALL, 0 );

	m_staticText_pocket_current = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxSize( 75,-1 ), wxALIGN_CENTER_HORIZONTAL );
	m_staticText_pocket_current->Wrap( -1 );
	fgSizer_temp->Add( m_staticText_pocket_current, 0, wxALIGN_CENTER|wxALL, 5 );

	m_staticText_current_unit = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("C"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL );
	m_staticText_current_unit->Wrap( -1 );
	fgSizer_temp->Add( m_staticText_current_unit, 0, wxALL, 5 );

	m_staticText_txt_target = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("Target"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_txt_target->Wrap( -1 );
	m_staticText_txt_target->SetMinSize( wxSize( 60,-1 ) );

	fgSizer_temp->Add( m_staticText_txt_target, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );

	m_textCtrl_bed = new wxTextCtrl( m_panel_splitter_right, wxID_ANY, wxT("0"), wxDefaultPosition, wxSize( 75,-1 ), wxTE_CENTER|wxTE_PROCESS_ENTER|wxBORDER_NONE );
	m_textCtrl_bed->SetMinSize( wxSize( 50,-1 ) );

	fgSizer_temp->Add( m_textCtrl_bed, 0, wxALIGN_CENTER|wxALL, 0 );

	m_textCtrl_nozzle = new wxTextCtrl( m_panel_splitter_right, wxID_ANY, wxT("0"), wxDefaultPosition, wxSize( 75,-1 ), wxTE_CENTER|wxTE_PROCESS_ENTER|wxBORDER_NONE );
	m_textCtrl_nozzle->SetMinSize( wxSize( 50,-1 ) );

	fgSizer_temp->Add( m_textCtrl_nozzle, 0, wxALIGN_CENTER|wxALL, 0 );


	fgSizer_temp->Add( 75, 0, 0, wxEXPAND, 5 );

	m_staticText_target_unit = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("C"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL );
	m_staticText_target_unit->Wrap( -1 );
	fgSizer_temp->Add( m_staticText_target_unit, 0, wxALL, 5 );


	fgSizer_temp->Add( 0, 0, 1, wxEXPAND, 5 );


	bSizer_temp_ctrl->Add( fgSizer_temp, 0, wxALIGN_CENTER|wxALL, 5 );


	bSizer_control->Add( bSizer_temp_ctrl, 0, wxALL|wxEXPAND, 5 );

	m_staticline3 = new wxStaticLine( m_panel_splitter_right, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
	bSizer_control->Add( m_staticline3, 0, wxEXPAND | wxALL, 0 );

	wxBoxSizer* bSizer_axis_ctrl;
	bSizer_axis_ctrl = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_axis_ctrl_caption;
	bSizer_axis_ctrl_caption = new wxBoxSizer( wxVERTICAL );

	m_staticText_ctrl_caption = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("Axis Control"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_ctrl_caption->Wrap( -1 );
	bSizer_axis_ctrl_caption->Add( m_staticText_ctrl_caption, 0, wxALL, 5 );


	bSizer_axis_ctrl->Add( bSizer_axis_ctrl_caption, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer73;
	bSizer73 = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer62;
	bSizer62 = new wxBoxSizer( wxHORIZONTAL );

	wxBoxSizer* bSizer76;
	bSizer76 = new wxBoxSizer( wxVERTICAL );

	bSizer76->SetMinSize( wxSize( 140,-1 ) );
	m_staticText57 = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("X/Y"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText57->Wrap( -1 );
	bSizer76->Add( m_staticText57, 0, wxALIGN_CENTER_HORIZONTAL|wxALL, 5 );

	wxGridBagSizer* gbSizer_control;
	gbSizer_control = new wxGridBagSizer( 0, 0 );
	gbSizer_control->SetFlexibleDirection( wxBOTH );
	gbSizer_control->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

	m_bpButton_y_up = new wxBitmapButton( m_panel_splitter_right, wxID_ANY, wxNullBitmap, wxPoint( 1,0 ), wxSize( 40,40 ), wxBU_AUTODRAW|0|wxBORDER_NONE );
	gbSizer_control->Add( m_bpButton_y_up, wxGBPosition( 0, 1 ), wxGBSpan( 1, 1 ), wxALIGN_CENTER_HORIZONTAL|wxALL, 0 );

	m_bpButton_y_down = new wxBitmapButton( m_panel_splitter_right, wxID_ANY, wxNullBitmap, wxPoint( 0,1 ), wxSize( 40,40 ), wxBU_AUTODRAW|0|wxBORDER_NONE );
	gbSizer_control->Add( m_bpButton_y_down, wxGBPosition( 2, 1 ), wxGBSpan( 1, 1 ), wxALIGN_CENTER_HORIZONTAL|wxALL, 0 );

	m_bpButton_x_left = new wxBitmapButton( m_panel_splitter_right, wxID_ANY, wxNullBitmap, wxPoint( -1,-1 ), wxSize( 40,40 ), wxBU_AUTODRAW|0|wxBORDER_NONE );
	gbSizer_control->Add( m_bpButton_x_left, wxGBPosition( 1, 0 ), wxGBSpan( 1, 1 ), wxALIGN_CENTER_HORIZONTAL|wxALL, 0 );

	m_bpButton_xy_home = new wxBitmapButton( m_panel_splitter_right, wxID_ANY, wxNullBitmap, wxPoint( 2,1 ), wxSize( 40,40 ), wxBU_AUTODRAW|0|wxBORDER_NONE );
	gbSizer_control->Add( m_bpButton_xy_home, wxGBPosition( 1, 1 ), wxGBSpan( 1, 1 ), wxALIGN_CENTER_HORIZONTAL|wxALL, 0 );

	m_bpButton_x_right = new wxBitmapButton( m_panel_splitter_right, wxID_ANY, wxNullBitmap, wxPoint( 1,2 ), wxSize( 40,40 ), wxBU_AUTODRAW|0|wxBORDER_NONE );
	gbSizer_control->Add( m_bpButton_x_right, wxGBPosition( 1, 2 ), wxGBSpan( 1, 1 ), wxALIGN_CENTER_HORIZONTAL|wxALL, 0 );


	bSizer76->Add( gbSizer_control, 0, wxALIGN_CENTER|wxALL, 5 );


	bSizer62->Add( bSizer76, 0, wxALL, 5 );

	wxBoxSizer* bSizer74;
	bSizer74 = new wxBoxSizer( wxVERTICAL );

	bSizer74->SetMinSize( wxSize( 72,-1 ) );
	m_staticText60 = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("Z"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText60->Wrap( -1 );
	bSizer74->Add( m_staticText60, 0, wxALIGN_CENTER_HORIZONTAL|wxALL, 5 );

	wxGridBagSizer* gbSizer2;
	gbSizer2 = new wxGridBagSizer( 0, 0 );
	gbSizer2->SetFlexibleDirection( wxBOTH );
	gbSizer2->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

	m_bpButton_z_up = new wxBitmapButton( m_panel_splitter_right, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize( 40,40 ), wxBU_AUTODRAW|0|wxBORDER_NONE );
	gbSizer2->Add( m_bpButton_z_up, wxGBPosition( 0, 0 ), wxGBSpan( 1, 1 ), wxALL, 0 );

	m_bpButton_z_home = new wxBitmapButton( m_panel_splitter_right, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize( 40,40 ), wxBU_AUTODRAW|0|wxBORDER_NONE );
	gbSizer2->Add( m_bpButton_z_home, wxGBPosition( 1, 0 ), wxGBSpan( 1, 1 ), wxALL, 0 );

	m_bpButton_z_down = new wxBitmapButton( m_panel_splitter_right, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize( 40,40 ), wxBU_AUTODRAW|0|wxBORDER_NONE );
	gbSizer2->Add( m_bpButton_z_down, wxGBPosition( 2, 0 ), wxGBSpan( 1, 1 ), wxALL, 0 );


	bSizer74->Add( gbSizer2, 1, wxALIGN_CENTER, 5 );


	bSizer62->Add( bSizer74, 0, wxALL, 5 );


	bSizer73->Add( bSizer62, 0, wxALIGN_CENTER, 5 );

	wxBoxSizer* bSizer_axis_unit;
	bSizer_axis_unit = new wxBoxSizer( wxHORIZONTAL );

	m_button_0_1 = new wxToggleButton( m_panel_splitter_right, wxID_ANY, wxT("0.1"), wxDefaultPosition, wxSize( 50,-1 ), wxBORDER_NONE );
	bSizer_axis_unit->Add( m_button_0_1, 0, wxALIGN_CENTER|wxALL, 5 );

	m_button_1_0 = new wxToggleButton( m_panel_splitter_right, wxID_ANY, wxT("1.0"), wxDefaultPosition, wxSize( 50,-1 ), 0 );
	m_button_1_0->SetValue( true );
	bSizer_axis_unit->Add( m_button_1_0, 0, wxALIGN_CENTER|wxALL, 5 );

	m_button_10_0 = new wxToggleButton( m_panel_splitter_right, wxID_ANY, wxT("10.0"), wxDefaultPosition, wxSize( 50,-1 ), 0 );
	bSizer_axis_unit->Add( m_button_10_0, 0, wxALIGN_CENTER|wxALL, 5 );

	m_button_100_0 = new wxToggleButton( m_panel_splitter_right, wxID_ANY, wxT("100.0"), wxDefaultPosition, wxSize( 50,-1 ), 0 );
	bSizer_axis_unit->Add( m_button_100_0, 0, wxALIGN_CENTER|wxALL, 5 );


	bSizer73->Add( bSizer_axis_unit, 0, wxALIGN_CENTER|wxALL, 0 );


	bSizer_axis_ctrl->Add( bSizer73, 1, wxEXPAND, 5 );


	bSizer_control->Add( bSizer_axis_ctrl, 0, wxALL|wxEXPAND, 5 );

	m_staticline4 = new wxStaticLine( m_panel_splitter_right, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
	bSizer_control->Add( m_staticline4, 0, wxEXPAND | wxALL, 0 );

	wxBoxSizer* bSizer_extruder_ctrl_caption;
	bSizer_extruder_ctrl_caption = new wxBoxSizer( wxVERTICAL );

	m_staticText_extruder_ctrl_caption = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("Extruder Control"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_extruder_ctrl_caption->Wrap( -1 );
	bSizer_extruder_ctrl_caption->Add( m_staticText_extruder_ctrl_caption, 0, wxALL, 5 );


	bSizer_control->Add( bSizer_extruder_ctrl_caption, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer_extruder_ctrl;
	bSizer_extruder_ctrl = new wxBoxSizer( wxVERTICAL );

	wxFlexGridSizer* fgSizer_extruder;
	fgSizer_extruder = new wxFlexGridSizer( 2, 3, 0, 0 );
	fgSizer_extruder->SetFlexibleDirection( wxBOTH );
	fgSizer_extruder->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

	m_comboBox_trays = new wxComboBox( m_panel_splitter_right, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, 0 );
	fgSizer_extruder->Add( m_comboBox_trays, 1, wxALIGN_CENTER|wxALL, 5 );

	m_button_extreder_feed = new wxButton( m_panel_splitter_right, wxID_ANY, wxT("Feed"), wxDefaultPosition, wxDefaultSize, 0 );
	m_button_extreder_feed->SetMinSize( wxSize( 80,-1 ) );

	fgSizer_extruder->Add( m_button_extreder_feed, 0, wxALIGN_CENTER|wxALL, 5 );

	m_button_extruder_back = new wxButton( m_panel_splitter_right, wxID_ANY, wxT("Back"), wxDefaultPosition, wxDefaultSize, 0 );
	m_button_extruder_back->SetMinSize( wxSize( 80,-1 ) );
	m_button_extruder_back->SetMaxSize( wxSize( -1,30 ) );

	fgSizer_extruder->Add( m_button_extruder_back, 1, wxALL|wxEXPAND, 5 );

	wxBoxSizer* bSizer30;
	bSizer30 = new wxBoxSizer( wxHORIZONTAL );

	m_textCtrl_extrude = new wxTextCtrl( m_panel_splitter_right, wxID_ANY, wxT("20"), wxDefaultPosition, wxDefaultSize, 0 );
	m_textCtrl_extrude->SetMinSize( wxSize( 80,-1 ) );

	bSizer30->Add( m_textCtrl_extrude, 1, wxALIGN_CENTER|wxALL, 5 );

	m_staticText_unit_extrude = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("mm"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_unit_extrude->Wrap( -1 );
	bSizer30->Add( m_staticText_unit_extrude, 0, wxALIGN_CENTER|wxALL, 5 );


	fgSizer_extruder->Add( bSizer30, 1, wxEXPAND, 5 );

	m_button_extruder_in = new wxButton( m_panel_splitter_right, wxID_ANY, wxT("Extrude"), wxDefaultPosition, wxSize( 110,-1 ), 0 );
	m_button_extruder_in->SetMinSize( wxSize( 80,-1 ) );

	fgSizer_extruder->Add( m_button_extruder_in, 0, wxALIGN_CENTER|wxALL, 5 );

	m_button_extruder_out = new wxButton( m_panel_splitter_right, wxID_ANY, wxT("Retraction"), wxDefaultPosition, wxSize( 110,-1 ), 0 );
	m_button_extruder_out->SetMinSize( wxSize( 80,-1 ) );

	fgSizer_extruder->Add( m_button_extruder_out, 0, wxALIGN_CENTER|wxALL, 5 );


	bSizer_extruder_ctrl->Add( fgSizer_extruder, 0, wxALIGN_CENTER, 5 );


	bSizer_control->Add( bSizer_extruder_ctrl, 0, wxALL|wxEXPAND, 5 );

	m_staticline5 = new wxStaticLine( m_panel_splitter_right, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
	bSizer_control->Add( m_staticline5, 0, wxEXPAND | wxALL, 0 );

	wxBoxSizer* bSizer_other_ctrl;
	bSizer_other_ctrl = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_other_ctrl_caption;
	bSizer_other_ctrl_caption = new wxBoxSizer( wxVERTICAL );

	m_staticText_other_caption = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("Other Control"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_other_caption->Wrap( -1 );
	bSizer_other_ctrl_caption->Add( m_staticText_other_caption, 0, wxALL, 5 );


	bSizer_other_ctrl->Add( bSizer_other_ctrl_caption, 0, wxEXPAND, 5 );

	wxFlexGridSizer* fgSizer_others;
	fgSizer_others = new wxFlexGridSizer( 0, 2, 0, 0 );
	fgSizer_others->SetFlexibleDirection( wxBOTH );
	fgSizer_others->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

	wxBoxSizer* bSizer79;
	bSizer79 = new wxBoxSizer( wxHORIZONTAL );

	m_bitmap_fan_printing = new wxStaticBitmap( m_panel_splitter_right, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0 );
	bSizer79->Add( m_bitmap_fan_printing, 0, wxALIGN_CENTER|wxALL, 5 );

	m_staticText_fan_printing = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("Printing Fan"), wxDefaultPosition, wxSize( 80,-1 ), 0 );
	m_staticText_fan_printing->Wrap( -1 );
	bSizer79->Add( m_staticText_fan_printing, 0, wxALIGN_CENTER|wxALL, 5 );

	m_bmToggleBtn_printing_fan = new SwitchButton(m_panel_splitter_right);
	m_bmToggleBtn_printing_fan->SetValue( true );
	bSizer79->Add( m_bmToggleBtn_printing_fan, 0, wxALIGN_CENTER|wxALL, 5 );


	fgSizer_others->Add( bSizer79, 1, wxEXPAND, 5 );

	wxBoxSizer* bSizer80;
	bSizer80 = new wxBoxSizer( wxHORIZONTAL );

	m_bitmap_fan_nozzle = new wxStaticBitmap( m_panel_splitter_right, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0 );
	bSizer80->Add( m_bitmap_fan_nozzle, 0, wxALIGN_CENTER|wxALL, 5 );

	m_staticText_fan_nozzle = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("Nozzle Fan"), wxDefaultPosition, wxSize( 80,-1 ), 0 );
	m_staticText_fan_nozzle->Wrap( -1 );
	bSizer80->Add( m_staticText_fan_nozzle, 0, wxALIGN_CENTER|wxALL, 5 );

	m_bmToggleBtn_nozzle_fan = new SwitchButton(m_panel_splitter_right);
	bSizer80->Add( m_bmToggleBtn_nozzle_fan, 0, wxALIGN_CENTER|wxALL, 5 );


	fgSizer_others->Add( bSizer80, 1, wxEXPAND, 5 );

	wxBoxSizer* bSizer81;
	bSizer81 = new wxBoxSizer( wxHORIZONTAL );

	m_bitmap_fan_case = new wxStaticBitmap( m_panel_splitter_right, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0 );
	bSizer81->Add( m_bitmap_fan_case, 0, wxALIGN_CENTER|wxALL, 5 );

	m_staticText66 = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("Case Fan"), wxDefaultPosition, wxSize( 80,-1 ), 0 );
	m_staticText66->Wrap( -1 );
	bSizer81->Add( m_staticText66, 0, wxALIGN_CENTER|wxALL, 5 );

	m_staticText68 = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxSize( -1,-1 ), 0 );
	m_staticText68->Wrap( -1 );
	bSizer81->Add( m_staticText68, 0, wxALIGN_CENTER|wxALL, 5 );


	fgSizer_others->Add( bSizer81, 1, wxEXPAND, 5 );

	wxBoxSizer* bSizer82;
	bSizer82 = new wxBoxSizer( wxHORIZONTAL );

	m_bitmap_fan_big = new wxStaticBitmap( m_panel_splitter_right, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0 );
	bSizer82->Add( m_bitmap_fan_big, 0, wxALIGN_CENTER|wxALL, 5 );

	m_staticText67 = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("Big Fan"), wxDefaultPosition, wxSize( 80,-1 ), 0 );
	m_staticText67->Wrap( -1 );
	bSizer82->Add( m_staticText67, 0, wxALIGN_CENTER|wxALL, 5 );

	m_staticText69 = new wxStaticText( m_panel_splitter_right, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText69->Wrap( -1 );
	bSizer82->Add( m_staticText69, 0, wxALIGN_CENTER|wxALL, 5 );


	fgSizer_others->Add( bSizer82, 1, wxEXPAND, 5 );


	bSizer_other_ctrl->Add( fgSizer_others, 0, wxALIGN_CENTER|wxALL, 5 );


	bSizer_control->Add( bSizer_other_ctrl, 0, wxALL|wxEXPAND, 5 );


	bSizer_control->Add( 0, 0, 0, wxALL|wxEXPAND, 0 );


	bSizer_right_control->Add( bSizer_control, 0, wxALL, 5 );


	bSizer_right_control->Add( 0, 0, 1, wxEXPAND, 5 );


	bSizer651->Add( bSizer_right_control, 0, 0, 5 );


	bSizer651->Add( 5, 0, 0, wxALL, 5 );


	bSizer45->Add( bSizer651, 0, wxALL, 5 );


	bSizer66->Add( bSizer45, 1, wxEXPAND, 5 );


	bSizer66->Add( 0, 1, 0, wxALL, 5 );


	m_panel_splitter_right->SetSizer( bSizer66 );
	m_panel_splitter_right->Layout();
	bSizer66->Fit( m_panel_splitter_right );
	m_splitter->SplitVertically( m_panel_splitter_left, m_panel_splitter_right, 334 );
	bSizer_top->Add( m_splitter, 1, wxALL|wxEXPAND, 0 );


	this->SetSizer( bSizer_top );
	this->Layout();

	// Connect Events
	m_panel_machine_status_title->Connect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorBasePanel::on_status_click ), NULL, this );
	m_staticText_status->Connect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorBasePanel::on_status_click ), NULL, this );
	m_panel_tasklist_title->Connect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorBasePanel::on_tasklist_click ), NULL, this );
	m_staticText_subtask_list_title->Connect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorBasePanel::on_tasklist_click ), NULL, this );
	m_panel_notification->Connect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorBasePanel::on_notification_click ), NULL, this );
	m_staticText_notification->Connect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorBasePanel::on_notification_click ), NULL, this );
	m_button_report->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_subtask_report ), NULL, this );
	m_button_pause_resume->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_subtask_pause_resume ), NULL, this );
	m_button_abort->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_subtask_abort ), NULL, this );
	m_textCtrl_bed->Connect( wxEVT_KILL_FOCUS, wxFocusEventHandler( MonitorBasePanel::on_bed_temp_kill_focus ), NULL, this );
	m_textCtrl_bed->Connect( wxEVT_SET_FOCUS, wxFocusEventHandler( MonitorBasePanel::on_bed_temp_set_focus ), NULL, this );
	m_textCtrl_bed->Connect( wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler( MonitorBasePanel::on_set_bed_temp ), NULL, this );
	m_textCtrl_nozzle->Connect( wxEVT_KILL_FOCUS, wxFocusEventHandler( MonitorBasePanel::on_nozzle_temp_kill_focus ), NULL, this );
	m_textCtrl_nozzle->Connect( wxEVT_SET_FOCUS, wxFocusEventHandler( MonitorBasePanel::on_nozzle_temp_set_focus ), NULL, this );
	m_textCtrl_nozzle->Connect( wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler( MonitorBasePanel::on_set_nozzle_temp ), NULL, this );
	m_button_extreder_feed->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_extruder_feed ), NULL, this );
	m_button_extruder_back->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_extruder_back ), NULL, this );
	m_button_extruder_in->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_extruder_extrude ), NULL, this );
	m_button_extruder_out->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_extruder_retraction ), NULL, this );
	m_bmToggleBtn_printing_fan->Connect( wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_printing_fan_switch ), NULL, this );
	m_bmToggleBtn_nozzle_fan->Connect( wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_nozzle_fan_switch ), NULL, this );
}

MonitorBasePanel::~MonitorBasePanel()
{
	// Disconnect Events
	m_panel_machine_status_title->Disconnect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorBasePanel::on_status_click ), NULL, this );
	m_staticText_status->Disconnect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorBasePanel::on_status_click ), NULL, this );
	m_panel_tasklist_title->Disconnect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorBasePanel::on_tasklist_click ), NULL, this );
	m_staticText_subtask_list_title->Disconnect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorBasePanel::on_tasklist_click ), NULL, this );
	m_panel_notification->Disconnect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorBasePanel::on_notification_click ), NULL, this );
	m_staticText_notification->Disconnect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorBasePanel::on_notification_click ), NULL, this );
	m_button_report->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_subtask_report ), NULL, this );
	m_button_pause_resume->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_subtask_pause_resume ), NULL, this );
	m_button_abort->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_subtask_abort ), NULL, this );
	m_textCtrl_bed->Disconnect( wxEVT_KILL_FOCUS, wxFocusEventHandler( MonitorBasePanel::on_bed_temp_kill_focus ), NULL, this );
	m_textCtrl_bed->Disconnect( wxEVT_SET_FOCUS, wxFocusEventHandler( MonitorBasePanel::on_bed_temp_set_focus ), NULL, this );
	m_textCtrl_bed->Disconnect( wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler( MonitorBasePanel::on_set_bed_temp ), NULL, this );
	m_textCtrl_nozzle->Disconnect( wxEVT_KILL_FOCUS, wxFocusEventHandler( MonitorBasePanel::on_nozzle_temp_kill_focus ), NULL, this );
	m_textCtrl_nozzle->Disconnect( wxEVT_SET_FOCUS, wxFocusEventHandler( MonitorBasePanel::on_nozzle_temp_set_focus ), NULL, this );
	m_textCtrl_nozzle->Disconnect( wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler( MonitorBasePanel::on_set_nozzle_temp ), NULL, this );
	m_button_extreder_feed->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_extruder_feed ), NULL, this );
	m_button_extruder_back->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_extruder_back ), NULL, this );
	m_button_extruder_in->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_extruder_extrude ), NULL, this );
	m_button_extruder_out->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_extruder_retraction ), NULL, this );
	m_bmToggleBtn_printing_fan->Disconnect( wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_printing_fan_switch ), NULL, this );
	m_bmToggleBtn_nozzle_fan->Disconnect( wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler( MonitorBasePanel::on_nozzle_fan_switch ), NULL, this );

}
