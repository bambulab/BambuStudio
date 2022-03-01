///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 3.10.1-0-g8feb16b3)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#include "MonitorBasePanel.h"
#include "Printer/PrinterFileSystem.h"
#include "Widgets/Label.hpp"

///////////////////////////////////////////////////////////////////////////
using namespace Slic3r::GUI;

MonitorBasePanel::MonitorBasePanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name) : wxPanel(parent, id, pos, size, style, name)
{
	this->SetMinSize(wxSize(600, 400));

	wxBoxSizer* bSizer_top;
	bSizer_top = new wxBoxSizer(wxVERTICAL);

	m_splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_3D | wxSP_BORDER);
	m_splitter->SetSashGravity(0);
	m_splitter->SetSashSize(0);
	m_splitter->Connect(wxEVT_IDLE, wxIdleEventHandler(MonitorBasePanel::m_splitterOnIdle), NULL, this);
	m_splitter->SetMinimumPaneSize(182);

	m_panel_splitter_left = new wxPanel(m_splitter, wxID_ANY, wxDefaultPosition, wxSize(182, -1), wxTAB_TRAVERSAL);
	m_panel_splitter_left->SetBackgroundColour(wxColour(255, 255, 255));

	wxBoxSizer* bSizer_left_top;
	bSizer_left_top = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* bSizerleft;
	bSizerleft = new wxBoxSizer(wxVERTICAL);

	bSizerleft->SetMinSize(wxSize(182, 833));
	m_panel_printer = new wxPanel(m_panel_splitter_left, wxID_ANY, wxDefaultPosition, wxSize(182, 87), wxTAB_TRAVERSAL);
	m_panel_printer->SetBackgroundColour(wxColour(255, 255, 255));

	wxBoxSizer* bSizer_printer_top;
	bSizer_printer_top = new wxBoxSizer(wxVERTICAL);

	bSizer_printer_top->AddStretchSpacer();

	wxBoxSizer* bSizer_printer;
	bSizer_printer = new wxBoxSizer(wxHORIZONTAL);

	bSizer_printer->SetMinSize(wxSize(-1, 36));

	bSizer_printer->Add(23, 0, 0, wxEXPAND, 0);

	m_bitmap_printer = new wxStaticBitmap(m_panel_printer, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(-1, -1), 0);

	bSizer_printer->Add(m_bitmap_printer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 0);


	bSizer_printer->Add(3, 0, 0, wxEXPAND, 0);

	m_bitmap_arrow1 = new wxStaticBitmap(m_panel_printer, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(-1, -1), 0);

	bSizer_printer->Add(m_bitmap_arrow1, 0, wxALL | wxALIGN_CENTER_VERTICAL, 0);


	bSizer_printer->Add(8, 0, 0, 0, 0);

	wxBoxSizer* bSizer_printer_info;
	bSizer_printer_info = new wxBoxSizer(wxVERTICAL);

	bSizer_printer_info->SetMinSize(wxSize(-1, 27));

	bSizer_printer_info->Add(0, 14, 0, wxEXPAND, 0);

	m_staticText_machine_name = new wxStaticText(m_panel_printer, wxID_ANY, wxT("BBL-Printer001"), wxDefaultPosition, wxSize(-1, -1), wxST_ELLIPSIZE_END | wxST_ELLIPSIZE_MIDDLE | wxST_ELLIPSIZE_START);
	m_staticText_machine_name->Wrap(-1);
	m_staticText_machine_name->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("@HarmonyOS Sans SC")));
	m_staticText_machine_name->SetMinSize(wxSize(100, -1));

	bSizer_printer_info->Add(m_staticText_machine_name, 0, wxALIGN_CENTER_VERTICAL | wxALL, 0);

	m_staticText_capacity_val = new wxStaticText(m_panel_printer, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxSize(-1, -1), 0);
	m_staticText_capacity_val->Wrap(-1);
	m_staticText_capacity_val->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxEmptyString));

	bSizer_printer_info->Add(m_staticText_capacity_val, 0, wxALL | wxALIGN_CENTER_VERTICAL, 0);


	bSizer_printer->Add(bSizer_printer_info, 1, wxEXPAND, 0);


	bSizer_printer_top->Add(bSizer_printer, 0, wxEXPAND, 0);

	bSizer_printer_top->AddStretchSpacer();

	m_panel_printer->SetSizer(bSizer_printer_top);
	m_panel_printer->Layout();
	bSizer_printer_top->Fit(m_panel_printer);
	bSizerleft->Add(m_panel_printer, 0, wxALL | wxEXPAND, 0);

	m_staticline1 = new StaticLine(m_panel_splitter_left);
	m_staticline1->SetLineColour(wxColour(0xEEEEEE));
	bSizerleft->Add(m_staticline1, 0, wxEXPAND | wxRIGHT | wxLEFT, 14);

	m_panel_status_tab = new wxPanel(m_panel_splitter_left, wxID_ANY, wxDefaultPosition, wxSize(182, 52), wxTAB_TRAVERSAL);
	m_panel_status_tab->SetBackgroundColour(wxColour(255, 255, 255));

	wxBoxSizer* bSizer_status_caption;
	bSizer_status_caption = new wxBoxSizer(wxHORIZONTAL);

	bSizer_status_caption->Add(28, 0, 0, 0, 0);

	m_staticText_status = new wxStaticText(m_panel_status_tab, wxID_ANY, wxT("Status"), wxDefaultPosition, wxSize(-1, -1), wxST_ELLIPSIZE_END);
	m_staticText_status->Wrap(-1);
	m_staticText_status->SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("@HarmonyOS Sans SC")));
	m_staticText_status->SetMinSize(wxSize(65, -1));

	bSizer_status_caption->Add(m_staticText_status, 0, wxALIGN_CENTER_VERTICAL | wxBOTTOM | wxLEFT, 0);

	m_bitmap_signal = new wxStaticBitmap(m_panel_status_tab, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(-1, -1), 0);

	bSizer_status_caption->Add(m_bitmap_signal, 0, wxALL | wxALIGN_CENTER_VERTICAL, 0);


	bSizer_status_caption->AddStretchSpacer();

	m_bitmap_arrow2 = new wxStaticBitmap(m_panel_status_tab, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(-1, -1), 0);

	bSizer_status_caption->Add(m_bitmap_arrow2, 0, wxALIGN_CENTER_VERTICAL, 26);

	bSizer_status_caption->Add(16, 0, 0, wxEXPAND, 0);

	m_panel_status_tab->SetSizer(bSizer_status_caption);
	m_panel_status_tab->Layout();
	bSizer_status_caption->Fit(m_panel_status_tab);
	bSizerleft->Add(m_panel_status_tab, 0, wxALL | wxEXPAND, 0);

	m_staticline2 = new StaticLine(m_panel_splitter_left);
	m_staticline2->SetLineColour(wxColour(0xEEEEEE));
	bSizerleft->Add(m_staticline2, 0, wxEXPAND | wxRIGHT | wxLEFT, 14);

	m_panel_time_lapse_tab = new wxPanel(m_panel_splitter_left, wxID_ANY, wxDefaultPosition, wxSize(182, 52), wxTAB_TRAVERSAL);
	m_panel_time_lapse_tab->SetBackgroundColour(wxColour(255, 255, 255));

	wxBoxSizer* bSizer_time_lapse_caption;
	bSizer_time_lapse_caption = new wxBoxSizer(wxHORIZONTAL);

	bSizer_time_lapse_caption->Add(28, 0, 0, wxALL, 0);

	m_staticText_time_lapse = new wxStaticText(m_panel_time_lapse_tab, wxID_ANY, wxT("Time Lapse"), wxDefaultPosition, wxSize(-1, -1), wxALIGN_LEFT | wxST_ELLIPSIZE_END);
	m_staticText_time_lapse->Wrap(-1);
	m_staticText_time_lapse->SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("@HarmonyOS Sans SC")));
	m_staticText_time_lapse->SetMinSize(wxSize(122, -1));

	bSizer_time_lapse_caption->Add(m_staticText_time_lapse, 0, wxALIGN_CENTER_VERTICAL | wxALL, 0);


	bSizer_time_lapse_caption->AddStretchSpacer();

	m_bitmap_arrow3 = new wxStaticBitmap(m_panel_time_lapse_tab, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(-1, -1), 0);

	bSizer_time_lapse_caption->Add(m_bitmap_arrow3, 0, wxALIGN_CENTER_VERTICAL, 26);

	bSizer_time_lapse_caption->Add(16, 0, 0, wxEXPAND, 0);

	m_panel_time_lapse_tab->SetSizer(bSizer_time_lapse_caption);
	m_panel_time_lapse_tab->Layout();
	bSizer_time_lapse_caption->Fit(m_panel_time_lapse_tab);
	bSizerleft->Add(m_panel_time_lapse_tab, 0, wxALL | wxEXPAND, 0);

	m_staticline3 = new StaticLine(m_panel_splitter_left);
	m_staticline3->SetLineColour(wxColour(0xEEEEEE));
	bSizerleft->Add(m_staticline3, 0, wxEXPAND | wxRIGHT | wxLEFT, 14);

	m_panel_video_tab = new wxPanel(m_panel_splitter_left, wxID_ANY, wxDefaultPosition, wxSize(182, 52), wxTAB_TRAVERSAL);
	m_panel_video_tab->SetBackgroundColour(wxColour(255, 255, 255));

	wxBoxSizer* bSizer_video_monitoring_caption;
	bSizer_video_monitoring_caption = new wxBoxSizer(wxHORIZONTAL);

	bSizer_video_monitoring_caption->Add(28, 0, 0, wxALL, 0);

	m_staticText_video_monitoring = new wxStaticText(m_panel_video_tab, wxID_ANY, wxT("Video"), wxDefaultPosition, wxSize(-1, -1), wxALIGN_LEFT | wxST_ELLIPSIZE_END);
	m_staticText_video_monitoring->Wrap(-1);
	m_staticText_video_monitoring->SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("@HarmonyOS Sans SC")));
	m_staticText_video_monitoring->SetMinSize(wxSize(122, -1));

	bSizer_video_monitoring_caption->Add(m_staticText_video_monitoring, 0, wxALIGN_CENTER_VERTICAL | wxALL, 0);


	bSizer_video_monitoring_caption->AddStretchSpacer();

	m_bitmap_arrow4 = new wxStaticBitmap(m_panel_video_tab, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(-1, -1), 0);

	bSizer_video_monitoring_caption->Add(m_bitmap_arrow4, 0, wxALIGN_CENTER_VERTICAL, 26);

	bSizer_video_monitoring_caption->Add(16, 0, 0, wxEXPAND, 0);

	m_panel_video_tab->SetSizer(bSizer_video_monitoring_caption);
	m_panel_video_tab->Layout();
	bSizer_video_monitoring_caption->Fit(m_panel_video_tab);
	bSizerleft->Add(m_panel_video_tab, 0, wxALL | wxEXPAND, 0);

	m_staticline4 = new StaticLine(m_panel_splitter_left);
	m_staticline4->SetLineColour(wxColour(0xEEEEEE));

	bSizerleft->Add(m_staticline4, 0, wxEXPAND | wxRIGHT | wxLEFT, 14);

	m_panel_task_list_tab = new wxPanel(m_panel_splitter_left, wxID_ANY, wxDefaultPosition, wxSize(182, 52), wxTAB_TRAVERSAL);
	m_panel_task_list_tab->SetBackgroundColour(wxColour(255, 255, 255));

	wxBoxSizer* bSizer_tasklist_caption;
	bSizer_tasklist_caption = new wxBoxSizer(wxHORIZONTAL);

	bSizer_tasklist_caption->Add(28, 0, 0, wxALL, 0);

	m_staticText_subtask_list = new wxStaticText(m_panel_task_list_tab, wxID_ANY, wxT("Task List"), wxDefaultPosition, wxSize(-1, -1), wxALIGN_LEFT | wxST_ELLIPSIZE_END);
	m_staticText_subtask_list->Wrap(-1);
	m_staticText_subtask_list->SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("@HarmonyOS Sans SC")));
	m_staticText_subtask_list->SetMinSize(wxSize(122, -1));

	bSizer_tasklist_caption->Add(m_staticText_subtask_list, 0, wxALIGN_CENTER_VERTICAL | wxALL, 0);


	bSizer_tasklist_caption->AddStretchSpacer();

	m_bitmap_arrow5 = new wxStaticBitmap(m_panel_task_list_tab, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(-1, -1), 0);

	bSizer_tasklist_caption->Add(m_bitmap_arrow5, 0, wxALIGN_CENTER_VERTICAL, 26);

	bSizer_tasklist_caption->Add(16, 0, 0, wxEXPAND, 0);

	m_panel_task_list_tab->SetSizer(bSizer_tasklist_caption);
	m_panel_task_list_tab->Layout();
	bSizer_tasklist_caption->Fit(m_panel_task_list_tab);
	bSizerleft->Add(m_panel_task_list_tab, 0, wxALL | wxEXPAND, 0);

	m_staticline5 = new StaticLine(m_panel_splitter_left);
	m_staticline5->SetLineColour(wxColour(0xEEEEEE));
	bSizerleft->Add(m_staticline5, 0, wxEXPAND | wxRIGHT | wxLEFT, 14);


	bSizer_left_top->Add(bSizerleft, 0, wxEXPAND, 0);


	m_panel_splitter_left->SetSizer(bSizer_left_top);
	m_panel_splitter_left->Layout();
	bSizer_left_top->Fit(m_panel_splitter_left);
	m_panel_splitter_right = new wxPanel(m_splitter, wxID_ANY, wxDefaultPosition, wxSize(1258, 900), wxTAB_TRAVERSAL);
	m_panel_splitter_right->SetBackgroundColour(wxColour(255, 255, 255));

	m_splitter->SplitVertically(m_panel_splitter_left, m_panel_splitter_right, 182);
	bSizer_top->Add(m_splitter, 1, wxALL | wxEXPAND, 0);


	this->SetSizerAndFit(bSizer_top);
	this->Layout();

	// Connect Events

	//make splitter immovable
	m_splitter->Connect(wxEVT_COMMAND_SPLITTER_SASH_POS_CHANGING, wxSplitterEventHandler(MonitorBasePanel::m_splitterOnSplitterSashPosChanging), NULL, this);
}

MonitorBasePanel::~MonitorBasePanel()
{
	m_splitter->Disconnect(wxEVT_COMMAND_SPLITTER_SASH_POS_CHANGING, wxSplitterEventHandler(MonitorBasePanel::m_splitterOnSplitterSashPosChanging), NULL, this);
}

StatusBasePanel::StatusBasePanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name) : wxPanel(parent, id, pos, size, style, name)
{
	this->SetBackgroundColour(wxColour(255, 255, 255));

	wxBoxSizer* bSizer_status;
	bSizer_status = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* bSizer_separator_top;
	bSizer_separator_top = new wxBoxSizer(wxVERTICAL);

	bSizer_separator_top->SetMinSize(wxSize(-1, 15));
	m_panel_separotor_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
	m_panel_separotor_top->SetBackgroundColour(wxColour(238, 238, 238));
	m_panel_separotor_top->SetMinSize(wxSize(-1, 15));

	bSizer_separator_top->Add(m_panel_separotor_top, 1, wxEXPAND | wxALL, 0);


	bSizer_status->Add(bSizer_separator_top, 0, wxEXPAND, 0);

	wxBoxSizer* bSizer_status_below;
	bSizer_status_below = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* bSizer_separator_left;
	bSizer_separator_left = new wxBoxSizer(wxVERTICAL);

	bSizer_separator_left->SetMinSize(wxSize(12, -1));
	m_panel_separotor_left = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
	m_panel_separotor_left->SetBackgroundColour(wxColour(238, 238, 238));
	m_panel_separotor_left->SetMinSize(wxSize(12, -1));

	bSizer_separator_left->Add(m_panel_separotor_left, 1, wxALL | wxEXPAND, 0);


	bSizer_status_below->Add(bSizer_separator_left, 12, wxEXPAND, 0);

	wxBoxSizer* bSizer_left;
	bSizer_left = new wxBoxSizer(wxVERTICAL);

	m_panel_monitoring_title = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 48), wxTAB_TRAVERSAL);
	m_panel_monitoring_title->SetBackgroundColour(wxColour(248, 248, 248));

	wxBoxSizer* bSizer_monitoring_title;
	bSizer_monitoring_title = new wxBoxSizer(wxHORIZONTAL);

	m_staticText_monitoring = new wxStaticText(m_panel_monitoring_title, wxID_ANY, wxT("Monitoring"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
	m_staticText_monitoring->Wrap(-1);
	m_staticText_monitoring->SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxT("HarmonyOS Sans SC")));

	bSizer_monitoring_title->Add(m_staticText_monitoring, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 23);


	m_panel_monitoring_title->SetSizer(bSizer_monitoring_title);
	m_panel_monitoring_title->Layout();
	bSizer_monitoring_title->Fit(m_panel_monitoring_title);
	bSizer_left->Add(m_panel_monitoring_title, 0, wxEXPAND | wxALL, 0);

	wxBoxSizer* bSizer_monitoring;
	bSizer_monitoring = new wxBoxSizer(wxVERTICAL);

	bSizer_monitoring->SetMinSize(wxSize(-1, -1));

#ifdef __WXMAC__
	m_media_ctrl = new wxMediaCtrl2(this, wxSize(16, 9));
#else
	m_media_ctrl = new wxMediaCtrl2();
	m_media_ctrl->Create(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(16, 9), wxMEDIACTRLPLAYERCONTROLS_NONE);
#endif
	//m_media_ctrl->SetMinSize(wxSize(528, 297));
	m_media_play_ctrl = new MediaPlayCtrl(this, m_media_ctrl);

	bSizer_monitoring->Add(m_media_ctrl, 1, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxRIGHT | wxLEFT | wxSHAPED, 15);
	bSizer_monitoring->Add(m_media_play_ctrl, 0, wxALL | wxEXPAND, 5);

	bSizer_left->Add(bSizer_monitoring, 1, wxEXPAND | wxALL, 0);

	wxBoxSizer* bSizer_separator1;
	bSizer_separator1 = new wxBoxSizer(wxVERTICAL);

	bSizer_separator1->SetMinSize(wxSize(-1, 15));
	m_panel_separotor1 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
	m_panel_separotor1->SetBackgroundColour(wxColour(238, 238, 238));
	m_panel_separotor1->SetMinSize(wxSize(-1, 15));

	bSizer_separator1->Add(m_panel_separotor1, 1, wxEXPAND | wxALL, 0);


	bSizer_left->Add(bSizer_separator1, 0, wxEXPAND, 0);

	m_panel_printing_title = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 48), wxTAB_TRAVERSAL);
	m_panel_printing_title->SetBackgroundColour(wxColour(248, 248, 248));

	wxBoxSizer* bSizer_printing_title;
	bSizer_printing_title = new wxBoxSizer(wxHORIZONTAL);

	m_staticText_printing = new wxStaticText(m_panel_printing_title, wxID_ANY, wxT("Printing Progress"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
	m_staticText_printing->Wrap(-1);
	m_staticText_printing->SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxT("HarmonyOS Sans SC")));

	bSizer_printing_title->Add(m_staticText_printing, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 23);


	m_panel_printing_title->SetSizer(bSizer_printing_title);
	m_panel_printing_title->Layout();
	bSizer_printing_title->Fit(m_panel_printing_title);
	bSizer_left->Add(m_panel_printing_title, 0, wxEXPAND | wxALL, 0);

	wxBoxSizer* bSizer_printing;
	bSizer_printing = new wxBoxSizer(wxHORIZONTAL);

	bSizer_printing->SetMinSize(wxSize(566, 210));
	m_bitmap_thumbnail = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(-1, -1), 0);

	bSizer_printing->Add(m_bitmap_thumbnail, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxLEFT, 15);


	bSizer_printing->Add(12, 0, 0, wxEXPAND, 0);

	wxBoxSizer* bSizer_task_internal;
	bSizer_task_internal = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* bSizer_subtask_info;
	bSizer_subtask_info = new wxBoxSizer(wxVERTICAL);


	bSizer_subtask_info->Add(0, 55, 0, wxEXPAND, 0);

	wxBoxSizer* bSizer63;
	bSizer63 = new wxBoxSizer(wxHORIZONTAL);

	m_staticText_subtask_value = new wxStaticText(this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT | wxST_ELLIPSIZE_END);
	m_staticText_subtask_value->Wrap(-1);
	m_staticText_subtask_value->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxT("HarmonyOS Sans SC")));
	m_staticText_subtask_value->SetForegroundColour(wxColour(44, 44, 46));
	m_staticText_subtask_value->SetMinSize(wxSize(270, -1));

	bSizer63->Add(m_staticText_subtask_value, 1, wxALIGN_CENTER_VERTICAL | wxBOTTOM | wxEXPAND, 10);

	m_staticText_subtask_progress = new wxStaticText(this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_subtask_progress->Wrap(-1);
	m_staticText_subtask_progress->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxT("HarmonyOS Sans SC")));

	bSizer63->Add(m_staticText_subtask_progress, 0, wxBOTTOM, 10);


	bSizer_subtask_info->Add(bSizer63, 0, wxEXPAND, 0);

	wxBoxSizer* bSizer64;
	bSizer64 = new wxBoxSizer(wxVERTICAL);

	m_gauge_progress = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize, wxGA_HORIZONTAL);
	m_gauge_progress->SetValue(0);
	bSizer64->Add(m_gauge_progress, 0, wxALL | wxEXPAND, 0);


	bSizer_subtask_info->Add(bSizer64, 0, wxEXPAND, 0);

	wxBoxSizer* bSizer65;
	bSizer65 = new wxBoxSizer(wxHORIZONTAL);

	m_staticText_progress_duration = new wxStaticText(this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_progress_duration->Wrap(-1);
	m_staticText_progress_duration->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
	m_staticText_progress_duration->SetForegroundColour(wxColour(146, 146, 146));

	bSizer65->Add(m_staticText_progress_duration, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND | wxTOP, 10);

	m_staticText_progress_left = new wxStaticText(this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_progress_left->Wrap(-1);
	m_staticText_progress_left->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
	m_staticText_progress_left->SetForegroundColour(wxColour(146, 146, 146));

	bSizer65->Add(m_staticText_progress_left, 0, wxTOP, 10);


	bSizer_subtask_info->Add(bSizer65, 0, wxEXPAND, 0);


	bSizer_subtask_info->Add(0, 15, 0, wxEXPAND, 0);

	wxBoxSizer* bSizer_task_btn;
	bSizer_task_btn = new wxBoxSizer(wxHORIZONTAL);


	bSizer_task_btn->Add(0, 0, 1, wxEXPAND, 0);

	m_button_report = new Button(this, wxT("Report"));
	StateColor report_bg(
		std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Disabled),
		std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
		std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
		std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Enabled),
		std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal)
	);
	m_button_report->SetBackgroundColor(report_bg);

	StateColor report_bd(
		std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled),
		std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled)
	);
	m_button_report->SetBorderColor(report_bd);

	StateColor report_text(
		std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled),
		std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled)
	);
	m_button_report->SetTextColor(report_text);

	m_button_report->SetFont(Label::Body_10);

	bSizer_task_btn->Add(m_button_report, 0, wxALIGN_CENTER | wxALL, 0);


	bSizer_task_btn->Add(20, 0, 0, wxEXPAND, 0);

	m_button_pause_resume = new Button(this, wxT("Pause"));

	StateColor pause_resume_bg (
		std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Disabled),
		std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
		std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
		std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Enabled),
		std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal)
		);
	m_button_pause_resume->SetBackgroundColor(pause_resume_bg);

	StateColor pause_resume_bd(
		std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled),
		std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Enabled)
	);
	m_button_pause_resume->SetBorderColor(pause_resume_bd);

	StateColor pause_resume_text(
		std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled),
		std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Enabled)
	);
	m_button_pause_resume->SetTextColor(pause_resume_text);

	m_button_pause_resume->SetFont(Label::Body_10);

	bSizer_task_btn->Add(m_button_pause_resume, 0, wxALIGN_CENTER | wxALL, 0);


	bSizer_task_btn->Add(20, 0, 0, wxEXPAND, 0);

	m_button_abort = new Button(this, wxT("Abort"));

	StateColor abort_bg(
		std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Disabled),
		std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
		std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
		std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Enabled),
		std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal)
	);
	m_button_abort->SetBackgroundColor(abort_bg);

	StateColor abort_bd(
		std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled),
		std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled)
	);
	m_button_abort->SetBorderColor(abort_bd);

	StateColor abort_text(
		std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled),
		std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled)
	);
	m_button_abort->SetTextColor(abort_text);

	m_button_abort->SetFont(Label::Body_10);

	bSizer_task_btn->Add(m_button_abort, 0, wxALIGN_CENTER | wxALL, 0);


	bSizer_subtask_info->Add(bSizer_task_btn, 0, wxALIGN_BOTTOM | wxALIGN_CENTER | wxEXPAND, 0);


	bSizer_task_internal->Add(bSizer_subtask_info, 1, wxALL | wxEXPAND, 0);


	bSizer_task_internal->Add(33, 0, 0, wxEXPAND, 0);


	bSizer_printing->Add(bSizer_task_internal, 1, wxALL | wxEXPAND, 0);


	bSizer_left->Add(bSizer_printing, 0, wxALL | wxEXPAND, 0);

	wxBoxSizer* bSizer_separator2;
	bSizer_separator2 = new wxBoxSizer(wxVERTICAL);

	bSizer_separator2->SetMinSize(wxSize(-1, 15));
	m_panel_separotor2 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
	m_panel_separotor2->SetBackgroundColour(wxColour(238, 238, 238));
	m_panel_separotor2->SetMinSize(wxSize(-1, 15));

	bSizer_separator2->Add(m_panel_separotor2, 1, wxALL | wxEXPAND, 0);


	bSizer_left->Add(bSizer_separator2, 0, wxEXPAND, 0);

	m_panel_calibration_title = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 48), wxTAB_TRAVERSAL);
	m_panel_calibration_title->SetBackgroundColour(wxColour(248, 248, 248));

	wxBoxSizer* bSizer_calibration_title;
	bSizer_calibration_title = new wxBoxSizer(wxHORIZONTAL);

	m_staticText_calbration = new wxStaticText(m_panel_calibration_title, wxID_ANY, wxT("Machine Calibration"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
	m_staticText_calbration->Wrap(-1);
	m_staticText_calbration->SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxT("HarmonyOS Sans SC")));

	bSizer_calibration_title->Add(m_staticText_calbration, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 23);


	m_panel_calibration_title->SetSizer(bSizer_calibration_title);
	m_panel_calibration_title->Layout();
	bSizer_calibration_title->Fit(m_panel_calibration_title);
	bSizer_left->Add(m_panel_calibration_title, 0, wxEXPAND | wxALL, 0);

	wxBoxSizer* bSizer_calibration;
	bSizer_calibration = new wxBoxSizer(wxHORIZONTAL);

	bSizer_calibration->SetMinSize(wxSize(566, -1));

	bSizer_calibration->Add(0, 55, 0, wxEXPAND, 0);

	bSizer_left->Add(bSizer_calibration, 0, wxEXPAND | wxALL, 0);


	bSizer_status_below->Add(bSizer_left, 566, wxALL | wxEXPAND, 0);

	wxBoxSizer* bSizer_separator_middle;
	bSizer_separator_middle = new wxBoxSizer(wxVERTICAL);

	bSizer_separator_middle->SetMinSize(wxSize(12, -1));
	m_panel_separator_middle = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL);
	m_panel_separator_middle->SetBackgroundColour(wxColour(238, 238, 238));
	m_panel_separator_middle->SetMinSize(wxSize(12, -1));

	bSizer_separator_middle->Add(m_panel_separator_middle, 1, wxEXPAND, 0);


	bSizer_status_below->Add(bSizer_separator_middle, 12, wxEXPAND, 0);

	wxBoxSizer* bSizer_right;
	bSizer_right = new wxBoxSizer(wxVERTICAL);

	bSizer_right->SetMinSize(wxSize(-1, 834));
	m_panel_control_title = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 48), wxTAB_TRAVERSAL);
	m_panel_control_title->SetBackgroundColour(wxColour(248, 248, 248));

	wxBoxSizer* bSizer_control_title;
	bSizer_control_title = new wxBoxSizer(wxHORIZONTAL);

	m_staticText_control = new wxStaticText(m_panel_control_title, wxID_ANY, wxT("Machine Control"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
	m_staticText_control->Wrap(-1);
	m_staticText_control->SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxT("HarmonyOS Sans SC")));
	m_staticText_control->SetMinSize(wxSize(80, -1));

	bSizer_control_title->Add(m_staticText_control, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 23);

	m_bitmap_lamp = new wxStaticBitmap(m_panel_control_title, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(-1, -1), 0);
	bSizer_control_title->Add(m_bitmap_lamp, 0, wxALL | wxALIGN_CENTER_VERTICAL, 0);

	m_staticText_lamp = new wxStaticText(m_panel_control_title, wxID_ANY, wxT("Lamp"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_lamp->Wrap(-1);
	m_staticText_lamp->SetFont(wxFont(13, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));

	bSizer_control_title->Add(m_staticText_lamp, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxLEFT, 7);

	m_bmToggleBtn_lamp = new SwitchButton(m_panel_control_title);
	m_bmToggleBtn_lamp->SetMinSize(wxSize(45, -1));
	bSizer_control_title->Add(m_bmToggleBtn_lamp, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 28);


	m_panel_control_title->SetSizer(bSizer_control_title);
	m_panel_control_title->Layout();
	bSizer_control_title->Fit(m_panel_control_title);
	bSizer_right->Add(m_panel_control_title, 0, wxALL | wxEXPAND, 0);


	bSizer_right->Add(0, 20, 0, wxEXPAND, 0);

	wxBoxSizer* bSizer_control;
	bSizer_control = new wxBoxSizer(wxVERTICAL);

	bSizer_control->SetMinSize(wxSize(654, -1));
	wxBoxSizer* bSizer_temp_caption;
	bSizer_temp_caption = new wxBoxSizer(wxHORIZONTAL);

	m_staticText_temp_caption = new wxStaticText(this, wxID_ANY, wxT("Temp Control"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_temp_caption->Wrap(-1);
	m_staticText_temp_caption->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxT("HarmonyOS Sans SC")));

	bSizer_temp_caption->Add(m_staticText_temp_caption, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 42);

	m_staticline1 = new StaticLine(this);
	m_staticline1->SetLineColour(wxColour(0xCECECE));
	bSizer_temp_caption->Add(m_staticline1, 1, wxRIGHT | wxLEFT | wxALIGN_CENTER_VERTICAL, 11);
	bSizer_temp_caption->Add(52, 0, 0, wxEXPAND, 0);


	bSizer_control->Add(bSizer_temp_caption, 0, wxEXPAND, 0);


	bSizer_control->Add(0, 25, 0, wxEXPAND, 0);

	wxBoxSizer* bSizer_temp_ctrl;
	bSizer_temp_ctrl = new wxBoxSizer(wxHORIZONTAL);

	bSizer_temp_ctrl->SetMinSize(wxSize(-1, 160));
	wxFlexGridSizer* fgSizer_temp;
	fgSizer_temp = new wxFlexGridSizer(3, 4, 18, 0);
	fgSizer_temp->SetFlexibleDirection(wxBOTH);
	fgSizer_temp->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_NONE);


	fgSizer_temp->Add(0, 42, 0, wxEXPAND, 0);

	m_bitmap_bed = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
	fgSizer_temp->Add(m_bitmap_bed, 0, wxALIGN_CENTER | wxALL, 0);

	m_bitmap_nozzle = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
	fgSizer_temp->Add(m_bitmap_nozzle, 0, wxALIGN_CENTER | wxALL, 0);

	m_bitmap_pocket = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
	fgSizer_temp->Add(m_bitmap_pocket, 0, wxALIGN_CENTER | wxALL | wxALIGN_CENTER_VERTICAL, 0);

	m_staticText_current = new wxStaticText(this, wxID_ANY, wxT("Current"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
	m_staticText_current->Wrap(-1);
	m_staticText_current->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
	m_staticText_current->SetForegroundColour(wxColour(90, 90, 90));
	m_staticText_current->SetMinSize(wxSize(75, -1));

	fgSizer_temp->Add(m_staticText_current, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 0);

	m_staticText_bed_current = new wxStaticText(this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxSize(-1, -1), wxALIGN_CENTER_HORIZONTAL);
	m_staticText_bed_current->Wrap(-1);
	m_staticText_bed_current->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
	m_staticText_bed_current->SetMinSize(wxSize(75, -1));

	fgSizer_temp->Add(m_staticText_bed_current, 1, wxALIGN_CENTER | wxALL, 0);

	m_staticText_nozzle_current = new wxStaticText(this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxSize(-1, -1), wxALIGN_CENTER_HORIZONTAL);
	m_staticText_nozzle_current->Wrap(-1);
	m_staticText_nozzle_current->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
	m_staticText_nozzle_current->SetMinSize(wxSize(75, -1));

	fgSizer_temp->Add(m_staticText_nozzle_current, 1, wxALIGN_CENTER | wxALL, 0);

	m_staticText_pocket_current = new wxStaticText(this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxSize(-1, -1), wxALIGN_CENTER_HORIZONTAL);
	m_staticText_pocket_current->Wrap(-1);
	m_staticText_pocket_current->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
	m_staticText_pocket_current->SetMinSize(wxSize(75, -1));

	fgSizer_temp->Add(m_staticText_pocket_current, 0, wxALIGN_CENTER | wxALL, 0);

	m_staticText_txt_target = new wxStaticText(this, wxID_ANY, wxT("Target"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
	m_staticText_txt_target->Wrap(-1);
	m_staticText_txt_target->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
	m_staticText_txt_target->SetForegroundColour(wxColour(90, 90, 90));
	m_staticText_txt_target->SetMinSize(wxSize(75, -1));

	fgSizer_temp->Add(m_staticText_txt_target, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 0);

	StateColor target_temp_text_ctrl_bd(
		std::pair<wxColour, int>((0, 174, 66), StateColor::Focused),
		std::pair<wxColour, int>((0, 174, 66), StateColor::Hovered),
		std::pair<wxColour, int>(parent->GetBackgroundColour(), StateColor::Normal)
	);
	m_textCtrl_bed = new TextInput(this, wxT("0"), wxT("C"));
	m_textCtrl_bed->SetBorderColor(target_temp_text_ctrl_bd);
	m_textCtrl_bed->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
	m_textCtrl_bed->SetMinSize(wxSize(75, -1));

	fgSizer_temp->Add(m_textCtrl_bed, 0, wxALIGN_CENTER | wxALL, 0);

	m_textCtrl_nozzle = new TextInput(this, wxT("0"), wxT("C"));
	m_textCtrl_nozzle->SetBorderColor(target_temp_text_ctrl_bd);
	m_textCtrl_nozzle->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
	m_textCtrl_nozzle->SetMinSize(wxSize(75, -1));

	fgSizer_temp->Add(m_textCtrl_nozzle, 0, wxALIGN_CENTER | wxALL, 0);


	bSizer_temp_ctrl->Add(fgSizer_temp, 0, wxALIGN_CENTER | wxEXPAND | wxALIGN_CENTER_VERTICAL | wxLEFT, 50);


	bSizer_temp_ctrl->Add(40, 0, 0, wxEXPAND, 0);

	wxBoxSizer* bSizer_fan;
	bSizer_fan = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* bSizer_nozzle_fan;
	bSizer_nozzle_fan = new wxBoxSizer(wxHORIZONTAL);

	m_bitmap_fan_nozzle = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
	bSizer_nozzle_fan->Add(m_bitmap_fan_nozzle, 0, wxALIGN_CENTER | wxALL | wxALIGN_CENTER_VERTICAL, 5);

	m_staticText_fan_nozzle = new wxStaticText(this, wxID_ANY, wxT("Nozzle Fan"), wxDefaultPosition, wxSize(-1, -1), 0);
	m_staticText_fan_nozzle->Wrap(-1);
	m_staticText_fan_nozzle->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
	m_staticText_fan_nozzle->SetMinSize(wxSize(85, -1));

	bSizer_nozzle_fan->Add(m_staticText_fan_nozzle, 0, wxALIGN_CENTER | wxALL, 5);

	m_bitmap_nozzle_fan_note = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
	bSizer_nozzle_fan->Add(m_bitmap_nozzle_fan_note, 0, wxALIGN_CENTER_VERTICAL, 5);


	bSizer_nozzle_fan->Add(25, 0, 0, wxEXPAND, 0);

	m_bmToggleBtn_nozzle_fan = new SwitchButton(this);
	m_bmToggleBtn_nozzle_fan->SetMinSize(wxSize(45, -1));

	bSizer_nozzle_fan->Add(m_bmToggleBtn_nozzle_fan, 0, wxALIGN_CENTER | wxALL, 5);


	bSizer_fan->Add(bSizer_nozzle_fan, 1, wxEXPAND, 5);

	wxBoxSizer* bSizer_printing_fan;
	bSizer_printing_fan = new wxBoxSizer(wxHORIZONTAL);

	m_bitmap_fan_printing = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
	bSizer_printing_fan->Add(m_bitmap_fan_printing, 0, wxALIGN_CENTER | wxALL, 5);

	m_staticText_fan_printing = new wxStaticText(this, wxID_ANY, wxT("Printing Fan"), wxDefaultPosition, wxSize(-1, -1), 0);
	m_staticText_fan_printing->Wrap(-1);
	m_staticText_fan_printing->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
	m_staticText_fan_printing->SetMinSize(wxSize(85, -1));

	bSizer_printing_fan->Add(m_staticText_fan_printing, 0, wxALIGN_CENTER | wxALL, 5);

	m_bitmap_printing_fan_note = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
	bSizer_printing_fan->Add(m_bitmap_printing_fan_note, 0, wxALIGN_CENTER_VERTICAL, 5);


	bSizer_printing_fan->Add(25, 0, 0, wxEXPAND, 0);

	m_bmToggleBtn_printing_fan = new SwitchButton(this);
	m_bmToggleBtn_printing_fan->SetValue(true);
	m_bmToggleBtn_printing_fan->SetMinSize(wxSize(45, -1));

	bSizer_printing_fan->Add(m_bmToggleBtn_printing_fan, 0, wxALIGN_CENTER | wxALL, 5);


	bSizer_fan->Add(bSizer_printing_fan, 1, wxEXPAND, 5);

	wxBoxSizer* bSizer_big_fan;
	bSizer_big_fan = new wxBoxSizer(wxHORIZONTAL);

	m_bitmap_fan_big = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
	bSizer_big_fan->Add(m_bitmap_fan_big, 0, wxALIGN_CENTER | wxALL, 5);

	m_staticText_big_fan = new wxStaticText(this, wxID_ANY, wxT("Big Fan"), wxDefaultPosition, wxSize(-1, -1), 0);
	m_staticText_big_fan->Wrap(-1);
	m_staticText_big_fan->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
	m_staticText_big_fan->SetMinSize(wxSize(85, -1));

	bSizer_big_fan->Add(m_staticText_big_fan, 0, wxALIGN_CENTER | wxALL, 5);

	m_bitmap_big_fan_note = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
	bSizer_big_fan->Add(m_bitmap_big_fan_note, 0, wxALIGN_CENTER_VERTICAL, 5);


	bSizer_big_fan->Add(35, 0, 0, wxEXPAND, 0);

	m_staticText_big_fan_status = new wxStaticText(this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_big_fan_status->Wrap(-1);
	m_staticText_big_fan_status->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));

	bSizer_big_fan->Add(m_staticText_big_fan_status, 0, wxALIGN_CENTER | wxALL, 5);


	bSizer_fan->Add(bSizer_big_fan, 1, wxEXPAND, 5);

	wxBoxSizer* bSizer_case_fan;
	bSizer_case_fan = new wxBoxSizer(wxHORIZONTAL);

	m_bitmap_fan_case = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
	bSizer_case_fan->Add(m_bitmap_fan_case, 0, wxALIGN_CENTER | wxALL, 5);

	m_staticText_case_fan = new wxStaticText(this, wxID_ANY, wxT("Case Fan"), wxDefaultPosition, wxSize(-1, -1), 0);
	m_staticText_case_fan->Wrap(-1);
	m_staticText_case_fan->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
	m_staticText_case_fan->SetMinSize(wxSize(85, -1));

	bSizer_case_fan->Add(m_staticText_case_fan, 0, wxALIGN_CENTER | wxALL, 5);

	m_bitmap_case_fan_note = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
	bSizer_case_fan->Add(m_bitmap_case_fan_note, 0, wxALIGN_CENTER_VERTICAL, 5);


	bSizer_case_fan->Add(35, 0, 0, wxEXPAND, 0);

	m_staticText_case_fan_status = new wxStaticText(this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxSize(-1, -1), 0);
	m_staticText_case_fan_status->Wrap(-1);
	m_staticText_case_fan_status->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));

	bSizer_case_fan->Add(m_staticText_case_fan_status, 0, wxALIGN_CENTER | wxALL, 5);


	bSizer_fan->Add(bSizer_case_fan, 1, wxEXPAND, 5);


	bSizer_temp_ctrl->Add(bSizer_fan, 0, wxEXPAND | wxALIGN_CENTER_VERTICAL, 0);


	bSizer_control->Add(bSizer_temp_ctrl, 0, wxTOP | wxBOTTOM, 0);

	wxBoxSizer* bSizer_axis_ctrl_caption;
	bSizer_axis_ctrl_caption = new wxBoxSizer(wxHORIZONTAL);

	m_staticText_ctrl_caption = new wxStaticText(this, wxID_ANY, wxT("Axis Control"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_ctrl_caption->Wrap(-1);
	m_staticText_ctrl_caption->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxT("HarmonyOS Sans SC")));

	bSizer_axis_ctrl_caption->Add(m_staticText_ctrl_caption, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 42);

	m_staticline2 = new StaticLine(this);
	m_staticline2->SetLineColour(wxColour(0xCECECE));
	bSizer_axis_ctrl_caption->Add(m_staticline2, 1, wxRIGHT | wxLEFT | wxALIGN_CENTER_VERTICAL, 11);
	bSizer_axis_ctrl_caption->Add(52, 0, 0, wxEXPAND, 0);


	bSizer_control->Add(bSizer_axis_ctrl_caption, 0, wxEXPAND, 0);


	bSizer_control->Add(0, 30, 0, wxEXPAND, 0);

	wxBoxSizer* bSizer_axis_ctrl;
	bSizer_axis_ctrl = new wxBoxSizer(wxHORIZONTAL);

	bSizer_axis_ctrl->SetMinSize(wxSize(-1, 340));

	wxGridBagSizer* gbSizer_control;
	gbSizer_control = new wxGridBagSizer(0, 0);
	gbSizer_control->SetFlexibleDirection(wxBOTH);
	gbSizer_control->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

	StateColor axis_ctrl_home_bg(
		std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
		std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
		std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal)
	);
	StateColor axis_ctrl_home_bd(
		std::pair<wxColour, int>(wxColour(237, 237, 237), StateColor::Normal)
	);
	m_bpButton_home_x = new Button(this, wxT(""), "axis_x_home", 0, 28);
	m_bpButton_home_x->SetBackgroundColor(axis_ctrl_home_bg);
	m_bpButton_home_x->SetBorderColor(axis_ctrl_home_bd);
	gbSizer_control->Add(m_bpButton_home_x, wxGBPosition(1, 0), wxGBSpan(2, 2), 0, 0);

	m_bpButton_xy = new AxisCtrlButton(this);
	gbSizer_control->Add(m_bpButton_xy, wxGBPosition(2, 2), wxGBSpan(7, 7), 0, 0);

	m_bpButton_home_y = new Button(this, wxT(""), "axis_y_home", 0, 28);
	m_bpButton_home_y->SetBackgroundColor(axis_ctrl_home_bg);
	m_bpButton_home_y->SetBorderColor(axis_ctrl_home_bd);
	gbSizer_control->Add(m_bpButton_home_y, wxGBPosition(8, 0), wxGBSpan(2, 2), 0, 0);

	m_bpButton_home_z = new Button(this, wxT(""), "axis_z_home", 0, 28);
	m_bpButton_home_z->SetBackgroundColor(axis_ctrl_home_bg);
	m_bpButton_home_z->SetBorderColor(axis_ctrl_home_bd);
	gbSizer_control->Add(m_bpButton_home_z, wxGBPosition(1, 10), wxGBSpan(2, 2), 0, 0);

	m_bpButton_home = new Button(this, wxT(""), "axis_home", 0, 28);
	m_bpButton_home->SetBackgroundColor(axis_ctrl_home_bg);
	m_bpButton_home->SetBorderColor(axis_ctrl_home_bd);
	gbSizer_control->Add(m_bpButton_home, wxGBPosition(8, 9), wxGBSpan(2, 2), wxRIGHT, 16);


	bSizer_axis_ctrl->Add(gbSizer_control, 0, wxALIGN_CENTER | wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL | wxLEFT, 46);

	wxBoxSizer* bSizer_z_ctrl;
	bSizer_z_ctrl = new wxBoxSizer(wxVERTICAL);

	m_staticText_z = new wxStaticText(this, wxID_ANY, wxT("Z"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_z->Wrap(-1);
	m_staticText_z->SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxEmptyString));

	bSizer_z_ctrl->Add(m_staticText_z, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);

	StateColor z_10_ctrl_bg(
		std::pair<wxColour, int>(wxColour(172, 172, 172), StateColor::Pressed),
		std::pair<wxColour, int>(wxColour(196, 196, 196), StateColor::Normal)
	);
	StateColor z_10_ctrl_bd(
		std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Hovered),
		std::pair<wxColour, int>(wxColour(196, 196, 196), StateColor::Normal)
	);
	m_bpButton_z_10 = new Button(this, wxT("+10"));
	m_bpButton_z_10->SetBackgroundColor(z_10_ctrl_bg);
	m_bpButton_z_10->SetBorderColor(z_10_ctrl_bd);

	bSizer_z_ctrl->Add(m_bpButton_z_10, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);

	StateColor z_1_ctrl_bg(
		std::pair<wxColour, int>(wxColour(172, 172, 172), StateColor::Pressed),
		std::pair<wxColour, int>(wxColour(209, 209, 209), StateColor::Normal)
	);
	StateColor z_1_ctrl_bd(
		std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Hovered),
		std::pair<wxColour, int>(wxColour(209, 209, 209), StateColor::Normal)
	);
	m_bpButton_z_1 = new Button(this, wxT("+1"));
	m_bpButton_z_1->SetBackgroundColor(z_1_ctrl_bg);
	m_bpButton_z_1->SetBorderColor(z_1_ctrl_bd);

	bSizer_z_ctrl->Add(m_bpButton_z_1, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);

	StateColor z_0_1_ctrl_bg(
		std::pair<wxColour, int>(wxColour(172, 172, 172), StateColor::Pressed),
		std::pair<wxColour, int>(wxColour(229, 229, 229), StateColor::Normal)
	);
	StateColor z_0_1_ctrl_bd(
		std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Hovered),
		std::pair<wxColour, int>(wxColour(229, 229, 229), StateColor::Normal)
	);
	m_bpButton_z_0_1 = new Button(this, wxT("+0.1"));
	m_bpButton_z_0_1->SetBackgroundColor(z_0_1_ctrl_bg);
	m_bpButton_z_0_1->SetBorderColor(z_0_1_ctrl_bd);

	bSizer_z_ctrl->Add(m_bpButton_z_0_1, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);


	bSizer_z_ctrl->Add(0, 7, 0, wxEXPAND, 0);

	StateColor z__down_0_1_ctrl_bg(
		std::pair<wxColour, int>(wxColour(172, 172, 172), StateColor::Pressed),
		std::pair<wxColour, int>(wxColour(229, 229, 229), StateColor::Normal)
	);
	StateColor z_down_0_1_ctrl_bd(
		std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Hovered),
		std::pair<wxColour, int>(wxColour(229, 229, 229), StateColor::Normal)
	);
	m_bpButton_z_down_0_1 = new Button(this, wxT("-0.1"));
	m_bpButton_z_down_0_1->SetBackgroundColor(z__down_0_1_ctrl_bg);
	m_bpButton_z_down_0_1->SetBorderColor(z_down_0_1_ctrl_bd);

	bSizer_z_ctrl->Add(m_bpButton_z_down_0_1, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);

	StateColor z_down_1_ctrl_bg(
		std::pair<wxColour, int>(wxColour(172, 172, 172), StateColor::Pressed),
		std::pair<wxColour, int>(wxColour(209, 209, 209), StateColor::Normal)
	);
	StateColor z_down_1_ctrl_bd(
		std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Hovered),
		std::pair<wxColour, int>(wxColour(209, 209, 209), StateColor::Normal)
	);
	m_bpButton_z_down_1 = new Button(this, wxT("-1"));
	m_bpButton_z_down_1->SetBackgroundColor(z_down_1_ctrl_bg);
	m_bpButton_z_down_1->SetBorderColor(z_down_1_ctrl_bd);

	bSizer_z_ctrl->Add(m_bpButton_z_down_1, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);

	StateColor z_down_10_ctrl_bg(
		std::pair<wxColour, int>(wxColour(172, 172, 172), StateColor::Pressed),
		std::pair<wxColour, int>(wxColour(196, 196, 196), StateColor::Normal)
	);
	StateColor z_down_10_ctrl_bd(
		std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Hovered),
		std::pair<wxColour, int>(wxColour(196, 196, 196), StateColor::Normal)
	);
	m_bpButton_z_down_10 = new Button(this, wxT("-10"));
	m_bpButton_z_down_10->SetBackgroundColor(z_down_10_ctrl_bg);
	m_bpButton_z_down_10->SetBorderColor(z_down_10_ctrl_bd);

	bSizer_z_ctrl->Add(m_bpButton_z_down_10, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);

	bSizer_z_ctrl->Add(0, 8, 0, wxEXPAND, 0);

	m_staticText_z_tip = new wxStaticText(this, wxID_ANY, wxT("(relative distance \nbetween nozzle\n and bed)"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_z_tip->Wrap(-1);
	m_staticText_z_tip->SetFont(wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("@SimSun-ExtB")));
	bSizer_z_ctrl->Add(m_staticText_z_tip, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);


	bSizer_axis_ctrl->Add(bSizer_z_ctrl, 0, wxALL, 0);

	bSizer_axis_ctrl->Add(50, 0, 0, wxEXPAND, 0);

	wxBoxSizer* bSizer_e_ctrl;
	bSizer_e_ctrl = new wxBoxSizer(wxVERTICAL);

	m_staticText_e = new wxStaticText(this, wxID_ANY, wxT("E"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_e->Wrap(-1);
	m_staticText_e->SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxEmptyString));

	bSizer_e_ctrl->Add(m_staticText_e, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);

	m_panel_e_ctrl = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
	m_panel_e_ctrl->SetBackgroundColour(wxColour(246, 246, 246));

	wxBoxSizer* bSizer_e_ctrl_internal;
	bSizer_e_ctrl_internal = new wxBoxSizer(wxVERTICAL);


	bSizer_e_ctrl_internal->Add(0, 10, 0, wxEXPAND, 0);

	StateColor e_ctrl_bg(
		std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
		std::pair<wxColour, int>(parent->GetBackgroundColour(), StateColor::Normal)
	);
	StateColor e_ctrl_bd(
		std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Hovered),
		std::pair<wxColour, int>(parent->GetBackgroundColour(), StateColor::Normal)
	);
	m_bpButton_e_10 = new Button(m_panel_e_ctrl, wxT("+10"));
	m_bpButton_e_10->SetBackgroundColor(e_ctrl_bg);
	m_bpButton_e_10->SetBorderColor(e_ctrl_bd);

	bSizer_e_ctrl_internal->Add(m_bpButton_e_10, 0, wxALIGN_CENTER_HORIZONTAL | wxRIGHT | wxLEFT, 12);


	bSizer_e_ctrl_internal->Add(0, 5, 0, wxEXPAND, 0);

	m_bpButton_e_1 = new Button(m_panel_e_ctrl, wxT("+1"));
	m_bpButton_e_1->SetBackgroundColor(e_ctrl_bg);
	m_bpButton_e_1->SetBorderColor(e_ctrl_bd);

	bSizer_e_ctrl_internal->Add(m_bpButton_e_1, 0, wxALIGN_CENTER_HORIZONTAL | wxRIGHT | wxLEFT, 12);


	bSizer_e_ctrl_internal->Add(0, 42, 0, wxEXPAND, 0);

	m_bpButton_e_down_1 = new Button(m_panel_e_ctrl, wxT("-1"));
	m_bpButton_e_down_1->SetBackgroundColor(e_ctrl_bg);
	m_bpButton_e_down_1->SetBorderColor(e_ctrl_bd);

	bSizer_e_ctrl_internal->Add(m_bpButton_e_down_1, 0, wxALIGN_CENTER_HORIZONTAL | wxRIGHT | wxLEFT, 12);


	bSizer_e_ctrl_internal->Add(0, 5, 0, wxEXPAND, 0);

	m_bpButton_e_down_10 = new Button(m_panel_e_ctrl, wxT("-10"));
	m_bpButton_e_down_10->SetBackgroundColor(e_ctrl_bg);
	m_bpButton_e_down_10->SetBorderColor(e_ctrl_bd);

	bSizer_e_ctrl_internal->Add(m_bpButton_e_down_10, 0, wxALIGN_CENTER_HORIZONTAL | wxRIGHT | wxLEFT, 12);


	bSizer_e_ctrl_internal->Add(0, 10, 0, wxEXPAND, 0);


	m_panel_e_ctrl->SetSizer(bSizer_e_ctrl_internal);
	m_panel_e_ctrl->Layout();
	bSizer_e_ctrl_internal->Fit(m_panel_e_ctrl);
	bSizer_e_ctrl->Add(m_panel_e_ctrl, 1, wxEXPAND | wxALL, 0);


	bSizer_axis_ctrl->Add(bSizer_e_ctrl, 0, wxALL, 0);


	bSizer_control->Add(bSizer_axis_ctrl, 0, 0, 0);

	wxBoxSizer* bSizer_extruder_ctrl_caption;
	bSizer_extruder_ctrl_caption = new wxBoxSizer(wxHORIZONTAL);

	m_staticText_extruder_ctrl_caption = new wxStaticText(this, wxID_ANY, wxT("Extruder Control"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_extruder_ctrl_caption->Wrap(-1);
	m_staticText_extruder_ctrl_caption->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxT("HarmonyOS Sans SC")));

	bSizer_extruder_ctrl_caption->Add(m_staticText_extruder_ctrl_caption, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 42);

	m_staticline3 = new StaticLine(this);
	m_staticline3->SetLineColour(wxColour(0xCECECE));
	bSizer_extruder_ctrl_caption->Add(m_staticline3, 1, wxRIGHT | wxLEFT | wxALIGN_CENTER_VERTICAL, 11);
	bSizer_extruder_ctrl_caption->Add(52, 0, 0, wxEXPAND, 0);


	bSizer_control->Add(bSizer_extruder_ctrl_caption, 0, wxEXPAND, 0);


	bSizer_control->Add(0, 35, 0, wxEXPAND, 0);

	wxBoxSizer* bSizer_extruder_ctrl;
	bSizer_extruder_ctrl = new wxBoxSizer(wxHORIZONTAL);

	bSizer_extruder_ctrl->SetMinSize(wxSize(-1, 131));
	wxBoxSizer* bSizer_material;
	bSizer_material = new wxBoxSizer(wxHORIZONTAL);

	StateColor extruder_material_bg(
		std::pair<wxColour, int>(wxColour(237, 250, 242), StateColor::Pressed),
		std::pair<wxColour, int>(wxColour(246, 246, 246), StateColor::Normal)
	);
	StateColor extruder_material_bd(
		std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Hovered),
		std::pair<wxColour, int>(wxColour(246, 246, 246), StateColor::Normal)
	);
	m_bpButton_extruder_1 = new Button(this, wxT(""), "extruder_material", 0, 26);
	m_bpButton_extruder_1->SetBackgroundColor(extruder_material_bg);
	m_bpButton_extruder_1->SetBorderColor(extruder_material_bd);

	bSizer_material->Add(m_bpButton_extruder_1, 0, wxALL, 0);

	m_staticline4 = new StaticLine(this);
	m_staticline4->SetLineColour(wxColour(0xCECECE));
	bSizer_material->Add(m_staticline4, 0, wxALIGN_CENTER_VERTICAL | wxEXPAND | wxALL, 0);

	m_bpButton_extruder_2 = new Button(this, wxT(""), "extruder_material", 0, 26);
	m_bpButton_extruder_2->SetBackgroundColor(extruder_material_bg);
	m_bpButton_extruder_2->SetBorderColor(extruder_material_bd);

	bSizer_material->Add(m_bpButton_extruder_2, 0, wxALL, 0);

	m_staticline5 = new StaticLine(this);
	m_staticline5->SetLineColour(wxColour(0xCECECE));
	bSizer_material->Add(m_staticline5, 0, wxEXPAND | wxALL, 0);

	m_bpButton_extruder_3 = new Button(this, wxT(""), "extruder_material", 0, 26);
	m_bpButton_extruder_3->SetBackgroundColor(extruder_material_bg);
	m_bpButton_extruder_3->SetBorderColor(extruder_material_bd);

	bSizer_material->Add(m_bpButton_extruder_3, 0, wxALL, 0);

	m_staticline6 = new StaticLine(this);
	m_staticline6->SetLineColour(wxColour(0xCECECE));
	bSizer_material->Add(m_staticline6, 0, wxEXPAND | wxALL, 0);

	m_bpButton_extruder_4 = new Button(this, wxT(""), "extruder_material", 0, 26);
	m_bpButton_extruder_4->SetBackgroundColor(extruder_material_bg);
	m_bpButton_extruder_4->SetBorderColor(extruder_material_bd);

	bSizer_material->Add(m_bpButton_extruder_4, 0, wxALL, 0);


	bSizer_extruder_ctrl->Add(bSizer_material, 0, wxLEFT, 50);


	bSizer_extruder_ctrl->Add(24, 0, 0, wxEXPAND, 0);

	wxBoxSizer* bSizer_extruder;
	bSizer_extruder = new wxBoxSizer(wxVERTICAL);

	m_staticText_select_space = new wxStaticText(this, wxID_ANY, wxT("Specify the corresponding space:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_select_space->Wrap(-1);
	bSizer_extruder->Add(m_staticText_select_space, 0, wxALL, 5);

	wxBoxSizer* bSizer_feed_back;
	bSizer_feed_back = new wxBoxSizer(wxHORIZONTAL);

	StateColor extruder_bg(
		std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
		std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
		std::pair<wxColour, int>(parent->GetBackgroundColour(), StateColor::Normal)
	);

	StateColor extruder_bd(
		std::pair<wxColour, int>(wxColour(107, 107, 107), StateColor::Disabled),
		std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled)
	);
	m_button_extruder_feed = new Button(this, wxT("Feed"));
	m_button_extruder_feed->SetBackgroundColor(extruder_bg);
	m_button_extruder_feed->SetBorderColor(extruder_bd);
	m_button_extruder_feed->SetFont(Label::Body_10);

	bSizer_feed_back->Add(m_button_extruder_feed, 0, wxALL, 5);


	bSizer_feed_back->Add(13, 0, 0, 0, 0);

	m_button_extruder_back = new Button(this, wxT("Back"));
	m_button_extruder_back->SetBackgroundColor(extruder_bg);
	m_button_extruder_back->SetBorderColor(extruder_bd);
	m_button_extruder_back->SetFont(Label::Body_10);

	bSizer_feed_back->Add(m_button_extruder_back, 0, wxALL, 5);


	bSizer_extruder->Add(bSizer_feed_back, 0, wxEXPAND, 0);


	bSizer_extruder_ctrl->Add(bSizer_extruder, 0, 0, 0);


	bSizer_control->Add(bSizer_extruder_ctrl, 0, wxEXPAND, 0);


	bSizer_right->Add(bSizer_control, 1, wxEXPAND | wxALL, 0);


	bSizer_right->Add(0, 20, 0, wxEXPAND, 0);


	bSizer_status_below->Add(bSizer_right, 654, wxALL | wxEXPAND, 0);

	wxBoxSizer* bSizer_separator_right;
	bSizer_separator_right = new wxBoxSizer(wxVERTICAL);

	bSizer_separator_right->SetMinSize(wxSize(12, -1));
	m_panel_separator_right = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL);
	m_panel_separator_right->SetBackgroundColour(wxColour(238, 238, 238));
	m_panel_separator_right->SetMinSize(wxSize(12, -1));

	bSizer_separator_right->Add(m_panel_separator_right, 1, wxEXPAND | wxALL, 0);


	bSizer_status_below->Add(bSizer_separator_right, 12, wxEXPAND, 0);


	bSizer_status->Add(bSizer_status_below, 1, wxALL | wxEXPAND, 0);

	wxBoxSizer* bSizer_separator_bottom;
	bSizer_separator_bottom = new wxBoxSizer(wxVERTICAL);

	bSizer_separator_bottom->SetMinSize(wxSize(-1, 15));
	m_panel_separotor_bottom = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
	m_panel_separotor_bottom->SetBackgroundColour(wxColour(238, 238, 238));
	m_panel_separotor_bottom->SetMinSize(wxSize(-1, 15));

	bSizer_separator_bottom->Add(m_panel_separotor_bottom, 1, wxEXPAND | wxALL, 0);


	bSizer_status->Add(bSizer_separator_bottom, 0, wxEXPAND, 5);


	this->SetSizerAndFit(bSizer_status);
	this->Layout();
	// Connect Events
}

StatusBasePanel::~StatusBasePanel()
{
}

VideoMonitoringBasePanel::VideoMonitoringBasePanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name) : wxPanel(parent, id, pos, size, style, name)
{
}

VideoMonitoringBasePanel::~VideoMonitoringBasePanel()
{
}

///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 3.10.1-0-g8feb16b3)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#include "MonitorBasePanel.h"

///////////////////////////////////////////////////////////////////////////
using namespace Slic3r::GUI;

TaskListBasePanel::TaskListBasePanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name) : wxPanel(parent, id, pos, size, style, name)
{
	this->SetBackgroundColour(wxColour(238, 238, 238));
	this->SetMinSize(wxSize(600, 400));

	wxFlexGridSizer* fgSizer_tasklist_top;
	fgSizer_tasklist_top = new wxFlexGridSizer(3, 1, 24, 0);
	fgSizer_tasklist_top->SetFlexibleDirection(wxBOTH);
	fgSizer_tasklist_top->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);


	fgSizer_tasklist_top->Add(0, 6, 0, wxEXPAND, 0);

	wxBoxSizer* bSizer_model_name;
	bSizer_model_name = new wxBoxSizer(wxVERTICAL);

	bSizer_model_name->SetMinSize(wxSize(496, 245));
	m_panel_model_name_caption = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(496, 48), wxTAB_TRAVERSAL);
	m_panel_model_name_caption->SetBackgroundColour(wxColour(248, 248, 248));

	wxBoxSizer* bSizer_model_name_caption;
	bSizer_model_name_caption = new wxBoxSizer(wxHORIZONTAL);

	bSizer_model_name_caption->Add(23, 0, 0, wxEXPAND, 0);

	m_staticText_model_name = new wxStaticText(m_panel_model_name_caption, wxID_ANY, wxT("Model Name"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_model_name->Wrap(-1);
	m_staticText_model_name->SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxT("HarmonyOS Sans SC")));

	bSizer_model_name_caption->Add(m_staticText_model_name, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);


	m_panel_model_name_caption->SetSizer(bSizer_model_name_caption);
	m_panel_model_name_caption->Layout();
	bSizer_model_name_caption->Fit(m_panel_model_name_caption);
	bSizer_model_name->Add(m_panel_model_name_caption, 0, wxALL | wxEXPAND, 0);

	m_panel_model_name_content = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(496, 197), wxTAB_TRAVERSAL);
	m_panel_model_name_content->SetBackgroundColour(wxColour(255, 255, 255));

	wxBoxSizer* bSizer_model_name_content;
	bSizer_model_name_content = new wxBoxSizer(wxVERTICAL);


	bSizer_model_name_content->Add(0, 30, 0, wxEXPAND, 0);

	wxBoxSizer* bSizer11;
	bSizer11 = new wxBoxSizer(wxHORIZONTAL);

	m_bitmap_task = new wxStaticBitmap(m_panel_model_name_content, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);

	bSizer11->Add(m_bitmap_task, 0, wxALL, 5);

	wxBoxSizer* bSizer12;
	bSizer12 = new wxBoxSizer(wxVERTICAL);

	m_staticText_task_desc = new wxStaticText(m_panel_model_name_content, wxID_ANY, wxT("Robort expose task dao movie with smart part \ndesigned for new year\n"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_task_desc->Wrap(-1);
	bSizer12->Add(m_staticText_task_desc, 0, wxALL, 5);

	wxBoxSizer* bSizer13;
	bSizer13 = new wxBoxSizer(wxHORIZONTAL);

	m_staticText_ceation_time_title = new wxStaticText(m_panel_model_name_content, wxID_ANY, wxT("CreationTime:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_ceation_time_title->Wrap(-1);
	bSizer13->Add(m_staticText_ceation_time_title, 0, wxALL, 5);

	m_staticText_creation_time = new wxStaticText(m_panel_model_name_content, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_creation_time->Wrap(-1);
	bSizer13->Add(m_staticText_creation_time, 0, wxALL, 5);


	bSizer12->Add(bSizer13, 1, wxEXPAND, 5);


	bSizer11->Add(bSizer12, 1, 0, 5);


	bSizer_model_name_content->Add(bSizer11, 1, wxLEFT | wxRIGHT | wxEXPAND, 46);


	m_panel_model_name_content->SetSizer(bSizer_model_name_content);
	m_panel_model_name_content->Layout();
	bSizer_model_name_content->Fit(m_panel_model_name_content);
	bSizer_model_name->Add(m_panel_model_name_content, 1, wxALL | wxEXPAND, 0);


	fgSizer_tasklist_top->Add(bSizer_model_name, 0, wxLEFT, 38);

	wxBoxSizer* bSizer_plater;
	bSizer_plater = new wxBoxSizer(wxVERTICAL);

	bSizer_plater->SetMinSize(wxSize(496, -1));
	m_panel_plater_caption = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(496, 48), wxTAB_TRAVERSAL);
	m_panel_plater_caption->SetBackgroundColour(wxColour(248, 248, 248));

	wxBoxSizer* bSizer_plater_caption;
	bSizer_plater_caption = new wxBoxSizer(wxHORIZONTAL);

	bSizer_plater_caption->Add(23, 0, 0, wxEXPAND, 0);

	m_staticText_plater = new wxStaticText(m_panel_plater_caption, wxID_ANY, wxT("Plater"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText_plater->Wrap(-1);
	m_staticText_plater->SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxT("HarmonyOS Sans SC")));

	bSizer_plater_caption->Add(m_staticText_plater, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);


	m_panel_plater_caption->SetSizer(bSizer_plater_caption);
	m_panel_plater_caption->Layout();
	bSizer_plater_caption->Fit(m_panel_plater_caption);
	bSizer_plater->Add(m_panel_plater_caption, 0, wxEXPAND | wxALL, 0);

	m_panel_plater_content = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(496, 439), wxTAB_TRAVERSAL);
	m_panel_plater_content->SetBackgroundColour(wxColour(255, 255, 255));

	wxBoxSizer* bSizer_tasklist;
	bSizer_tasklist = new wxBoxSizer(wxVERTICAL);

	fgSizer_subtask = new wxFlexGridSizer(100, 1, 10, 0);
	fgSizer_subtask->SetFlexibleDirection(wxBOTH);
	fgSizer_subtask->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

	bSizer_tasklist->Add(fgSizer_subtask, 0, wxALIGN_CENTER_HORIZONTAL, 0);

	m_panel_plater_content->SetSizer(bSizer_tasklist);
	m_panel_plater_content->Layout();
	bSizer_tasklist->Fit(m_panel_plater_content);
	bSizer_plater->Add(m_panel_plater_content, 1, wxEXPAND | wxALL, 0);


	fgSizer_tasklist_top->Add(bSizer_plater, 0, wxLEFT, 38);


	this->SetSizer(fgSizer_tasklist_top);
	this->Layout();
}

TaskListBasePanel::~TaskListBasePanel()
{
}


