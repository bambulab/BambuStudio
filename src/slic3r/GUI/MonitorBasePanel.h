///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 3.10.1-0-g8feb16b3)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#pragma once

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/statbmp.h>
#include <wx/scrolwin.h>
#include <wx/statline.h>
#include <wx/textctrl.h>
#include <wx/simplebook.h>
#include <wx/bmpbuttn.h>
#include <wx/button.h>
#include <wx/gauge.h>
#include <wx/gbsizer.h>
#include <wx/tglbtn.h>
#include <wx/combobox.h>
#include <wx/splitter.h>

///////////////////////////////////////////////////////////////////////////

namespace Slic3r
{
	namespace GUI
	{

		///////////////////////////////////////////////////////////////////////////////
		/// Class MonitorBasePanel
		///////////////////////////////////////////////////////////////////////////////
		class MonitorBasePanel : public wxPanel
		{
			private:

			protected:
				wxSplitterWindow* m_splitter;
				wxPanel* m_panel_splitter_left;
				wxPanel* m_panel_machine_status_title;
				wxStaticText* m_staticText_status;
				wxPanel* m_panel_machine_status_content;
				wxStaticText* m_staticText_machine_status;
				wxStaticText* m_staticText_machine_name;
				wxStaticText* m_staticText_wifi_signal;
				wxStaticBitmap* m_bitmap_signal;
				wxStaticText* m_staticText_printing_title;
				wxStaticText* m_staticText_printing_val;
				wxStaticText* m_staticText_capacity_title;
				wxStaticText* m_staticText_capacity_val;
				wxStaticText* m_staticText_filament_left_title;
				wxScrolledWindow* m_scrolledWindow_ams;
				wxStaticLine* m_staticline1;
				wxPanel* m_panel_tasklist_title;
				wxStaticText* m_staticText_subtask_list_title;
				wxPanel* m_panel_tasklist_content;
				wxScrolledWindow* m_scrolledWindow_tasklist;
				wxStaticLine* m_staticline8;
				wxPanel* m_panel_notification;
				wxStaticText* m_staticText_notification;
				wxPanel* m_panel_notification_content;
				wxTextCtrl* m_textCtrl_notification;
				wxPanel* m_panel_splitter_right;
				wxPanel* m_panel29;
				wxStaticText* m_staticText_live;
				wxStaticText* m_staticText_timelapse;
				wxSimplebook* m_simplebook_middle;
				wxPanel* m_panel_monitor;
				wxPanel* m_panel_live;
				wxStaticBitmap* m_bitmap_live_default;
				wxPanel* m_panel_timelapse;
				wxPanel* m_panel_printing_content;
				wxStaticText* m_staticText_task_caption;
				wxBitmapButton* m_bpButton_open_project;
				wxStaticBitmap* m_bitmap_thumbnail;
				wxStaticText* m_staticText_subtask_value;
				wxStaticText* m_staticText_subtask_progress;
				wxGauge* m_gauge_progress;
				wxStaticText* m_staticText_progress_duration;
				wxStaticText* m_staticText_progress_left;
				wxButton* m_button_report;
				wxButton* m_button_pause_resume;
				wxButton* m_button_abort;
				wxStaticLine* m_staticline6;
				wxPanel* m_panel27;
				wxStaticText* m_staticText_temp_caption;
				wxStaticBitmap* m_bitmap_bed;
				wxStaticBitmap* m_bitmap_nozzle;
				wxStaticBitmap* m_bitmap_pocket;
				wxStaticText* m_staticText_current;
				wxStaticText* m_staticText_bed_current;
				wxStaticText* m_staticText_nozzle_current;
				wxStaticText* m_staticText_pocket_current;
				wxStaticText* m_staticText_current_unit;
				wxStaticText* m_staticText_txt_target;
				wxTextCtrl* m_textCtrl_bed;
				wxTextCtrl* m_textCtrl_nozzle;
				wxStaticText* m_staticText_target_unit;
				wxStaticLine* m_staticline3;
				wxStaticText* m_staticText_ctrl_caption;
				wxStaticText* m_staticText57;
				wxBitmapButton* m_bpButton_y_up;
				wxBitmapButton* m_bpButton_y_down;
				wxBitmapButton* m_bpButton_x_left;
				wxBitmapButton* m_bpButton_xy_home;
				wxBitmapButton* m_bpButton_x_right;
				wxStaticText* m_staticText60;
				wxBitmapButton* m_bpButton_z_up;
				wxBitmapButton* m_bpButton_z_home;
				wxBitmapButton* m_bpButton_z_down;
				wxToggleButton* m_button_0_1;
				wxToggleButton* m_button_1_0;
				wxToggleButton* m_button_10_0;
				wxToggleButton* m_button_100_0;
				wxStaticLine* m_staticline4;
				wxStaticText* m_staticText_extruder_ctrl_caption;
				wxComboBox* m_comboBox_trays;
				wxButton* m_button_extreder_feed;
				wxButton* m_button_extruder_back;
				wxTextCtrl* m_textCtrl_extrude;
				wxStaticText* m_staticText_unit_extrude;
				wxButton* m_button_extruder_in;
				wxButton* m_button_extruder_out;
				wxStaticLine* m_staticline5;
				wxStaticText* m_staticText_other_caption;
				wxStaticBitmap* m_bitmap_fan_printing;
				wxStaticText* m_staticText_fan_printing;
				wxBitmapToggleButton* m_bmToggleBtn_printing_fan;
				wxStaticBitmap* m_bitmap_fan_nozzle;
				wxStaticText* m_staticText_fan_nozzle;
				wxBitmapToggleButton* m_bmToggleBtn_nozzle_fan;
				wxStaticBitmap* m_bitmap_fan_case;
				wxStaticText* m_staticText66;
				wxStaticText* m_staticText68;
				wxStaticBitmap* m_bitmap_fan_big;
				wxStaticText* m_staticText67;
				wxStaticText* m_staticText69;

				// Virtual event handlers, override them in your derived class
				virtual void on_status_click( wxMouseEvent& event ) { event.Skip(); }
				virtual void on_tasklist_click( wxMouseEvent& event ) { event.Skip(); }
				virtual void on_notification_click( wxMouseEvent& event ) { event.Skip(); }
				virtual void on_subtask_report( wxCommandEvent& event ) { event.Skip(); }
				virtual void on_subtask_pause_resume( wxCommandEvent& event ) { event.Skip(); }
				virtual void on_subtask_abort( wxCommandEvent& event ) { event.Skip(); }
				virtual void on_bed_temp_kill_focus( wxFocusEvent& event ) { event.Skip(); }
				virtual void on_bed_temp_set_focus( wxFocusEvent& event ) { event.Skip(); }
				virtual void on_set_bed_temp( wxCommandEvent& event ) { event.Skip(); }
				virtual void on_nozzle_temp_kill_focus( wxFocusEvent& event ) { event.Skip(); }
				virtual void on_nozzle_temp_set_focus( wxFocusEvent& event ) { event.Skip(); }
				virtual void on_set_nozzle_temp( wxCommandEvent& event ) { event.Skip(); }
				virtual void on_extruder_feed( wxCommandEvent& event ) { event.Skip(); }
				virtual void on_extruder_back( wxCommandEvent& event ) { event.Skip(); }
				virtual void on_extruder_extrude( wxCommandEvent& event ) { event.Skip(); }
				virtual void on_extruder_retraction( wxCommandEvent& event ) { event.Skip(); }
				virtual void on_printing_fan_switch( wxCommandEvent& event ) { event.Skip(); }
				virtual void on_nozzle_fan_switch( wxCommandEvent& event ) { event.Skip(); }


			public:

				MonitorBasePanel( wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( 1440,900 ), long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString );

				~MonitorBasePanel();

				void m_splitterOnIdle( wxIdleEvent& )
				{
					m_splitter->SetSashPosition( 334 );
					m_splitter->Disconnect( wxEVT_IDLE, wxIdleEventHandler( MonitorBasePanel::m_splitterOnIdle ), NULL, this );
				}

		};

	} // namespace GUI
} // namespace Slic3r

