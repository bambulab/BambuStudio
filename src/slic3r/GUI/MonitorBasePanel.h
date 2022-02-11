///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 3.10.1-0-g8feb16b3)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#pragma once

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/panel.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/statbmp.h>
#include <wx/gauge.h>
#include <wx/button.h>
#include <wx/bmpbuttn.h>
#include <wx/statline.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>
#include <wx/gbsizer.h>
#include <wx/splitter.h>
#include <wx/mediactrl.h>
#include "Widgets/Button.hpp"
#include "Widgets/SwitchButton.hpp"
#include "Widgets/AxisCtrlButton.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/StaticLine.hpp"
#include "wxMediaCtrl2.h"

///////////////////////////////////////////////////////////////////////////
class wxMediaCtrl2;

namespace Slic3r
{
	namespace GUI
	{

		class MediaPlayCtrl;
		class MediaFilePanel;

		///////////////////////////////////////////////////////////////////////////////
		/// Class MonitorBasePanel
		///////////////////////////////////////////////////////////////////////////////
		class MonitorBasePanel : public wxPanel
		{
		private:

		protected:
			wxSplitterWindow* m_splitter;
			wxPanel* m_panel_splitter_left;
			wxPanel* m_panel_printer;
			wxStaticBitmap* m_bitmap_printer;
			wxStaticBitmap* m_bitmap_arrow1;
			wxStaticText* m_staticText_machine_name;
			wxStaticText* m_staticText_capacity_val;
			StaticLine* m_staticline1;
			wxPanel* m_panel_status_tab;
			wxStaticText* m_staticText_status;
			wxStaticBitmap* m_bitmap_signal;
			wxStaticBitmap* m_bitmap_arrow2;
			StaticLine* m_staticline2;
			wxPanel* m_panel_time_lapse_tab;
			wxStaticText* m_staticText_time_lapse;
			wxStaticBitmap* m_bitmap_arrow3;
			StaticLine* m_staticline3;
			wxPanel* m_panel_video_tab;
			wxStaticText* m_staticText_video_monitoring;
			wxStaticBitmap* m_bitmap_arrow4;
			StaticLine* m_staticline4;
			wxPanel* m_panel_task_list_tab;
			wxStaticText* m_staticText_subtask_list;
			wxStaticBitmap* m_bitmap_arrow5;
			StaticLine* m_staticline5;
			wxPanel* m_panel_splitter_right;

			// Virtual event handlers, override them in your derived class
			virtual void m_splitterOnSplitterSashPosChanging(wxSplitterEvent& event) { event.Veto(); }
			virtual void on_printer_clicked(wxMouseEvent& event) { event.Skip(); }
			virtual void on_status(wxMouseEvent& event) { event.Skip(); }
			virtual void on_timelapse(wxMouseEvent& event) { event.Skip(); }
			virtual void on_video(wxMouseEvent& event) { event.Skip(); }
			virtual void on_tasklist(wxMouseEvent& event) { event.Skip(); }

		public:

			MonitorBasePanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(1440, 900), long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString);

			~MonitorBasePanel();

			void m_splitterOnIdle(wxIdleEvent&)
			{
				m_splitter->SetSashPosition(182);
				m_splitter->Disconnect(wxEVT_IDLE, wxIdleEventHandler(MonitorBasePanel::m_splitterOnIdle), NULL, this);
			}

		};

		///////////////////////////////////////////////////////////////////////////////
		/// Class StatusBasePanel
		///////////////////////////////////////////////////////////////////////////////
		class StatusBasePanel : public wxPanel
		{
		private:

		protected:
			MediaFilePanel* m_media_file_panel = NULL;
			wxPanel* m_panel_separotor_top;
			wxPanel* m_panel_separotor_left;
			wxPanel* m_panel_monitoring_title;
			wxStaticText* m_staticText_monitoring;
			wxMediaCtrl2* m_media_ctrl;
			MediaPlayCtrl* m_media_play_ctrl;
			wxPanel* m_panel_separotor1;
			wxPanel* m_panel_printing_title;
			wxStaticText* m_staticText_printing;
			wxStaticBitmap* m_bitmap_thumbnail;
			wxStaticText* m_staticText_subtask_value;
			wxStaticText* m_staticText_subtask_progress;
			wxGauge* m_gauge_progress;
			wxStaticText* m_staticText_progress_duration;
			wxStaticText* m_staticText_progress_left;
			Button* m_button_report;
			Button* m_button_pause_resume;
			Button* m_button_abort;
			wxPanel* m_panel_separotor2;
			wxPanel* m_panel_calibration_titile;
			wxStaticText* m_staticText_calbration;
			wxPanel* m_panel_separator_middle;
			wxPanel* m_panel_control_title;
			wxStaticText* m_staticText_control;
			wxStaticBitmap* m_bitmap_lamp;
			wxStaticText* m_staticText_lamp;
			SwitchButton* m_bmToggleBtn_lamp;
			wxStaticText* m_staticText_temp_caption;
			StaticLine* m_staticline1;
			wxStaticBitmap* m_bitmap_bed;
			wxStaticBitmap* m_bitmap_nozzle;
			wxStaticBitmap* m_bitmap_pocket;
			wxStaticText* m_staticText_current;
			wxStaticText* m_staticText_bed_current;
			wxStaticText* m_staticText_nozzle_current;
			wxStaticText* m_staticText_pocket_current;
			wxStaticText* m_staticText_txt_target;
			TextInput* m_textCtrl_bed;
			TextInput* m_textCtrl_nozzle;
			wxStaticBitmap* m_bitmap_fan_nozzle;
			wxStaticText* m_staticText_fan_nozzle;
			wxStaticBitmap* m_bitmap_nozzle_fan_note;
			SwitchButton* m_bmToggleBtn_nozzle_fan;
			wxStaticBitmap* m_bitmap_fan_printing;
			wxStaticText* m_staticText_fan_printing;
			wxStaticBitmap* m_bitmap_printing_fan_note;
			SwitchButton* m_bmToggleBtn_printing_fan;
			wxStaticBitmap* m_bitmap_fan_big;
			wxStaticText* m_staticText_big_fan;
			wxStaticBitmap* m_bitmap_big_fan_note;
			wxStaticText* m_staticText_big_fan_status;
			wxStaticBitmap* m_bitmap_fan_case;
			wxStaticText* m_staticText_case_fan;
			wxStaticBitmap* m_bitmap_case_fan_note;
			wxStaticText* m_staticText_case_fan_status;
			wxStaticText* m_staticText_ctrl_caption;
			StaticLine* m_staticline2;
			Button* m_bpButton_home_x;
			AxisCtrlButton* m_bpButton_xy;
			Button* m_bpButton_home_y;
			Button* m_bpButton_home_z;
			Button* m_bpButton_home;
			wxStaticText* m_staticText_z;
			Button* m_bpButton_z_10;
			Button* m_bpButton_z_1;
			Button* m_bpButton_z_0_1;
			Button* m_bpButton_z_down_0_1;
			Button* m_bpButton_z_down_1;
			Button* m_bpButton_z_down_10;
			wxStaticText* m_staticText_z_tip;
			wxStaticText* m_staticText_e;
			wxPanel* m_panel_e_ctrl;
			Button* m_bpButton_e_10;
			Button* m_bpButton_e_1;
			Button* m_bpButton_e_down_1;
			Button* m_bpButton_e_down_10;
			wxStaticText* m_staticText_extruder_ctrl_caption;
			StaticLine* m_staticline3;
			Button* m_bpButton_extruder_1;
			StaticLine* m_staticline4;
			Button* m_bpButton_extruder_2;
			StaticLine* m_staticline5;
			Button* m_bpButton_extruder_3;
			StaticLine* m_staticline6;
			Button* m_bpButton_extruder_4;
			wxStaticText* m_staticText_select_space;
			Button* m_button_extruder_feed;
			Button* m_button_extruder_back;
			wxPanel* m_panel_separator_right;
			wxPanel* m_panel_separotor_bottom;

			// Virtual event handlers, override them in your derived class
			virtual void on_subtask_report(wxCommandEvent& event) { event.Skip(); }
			virtual void on_subtask_pause_resume(wxCommandEvent& event) { event.Skip(); }
			virtual void on_subtask_abort(wxCommandEvent& event) { event.Skip(); }
			virtual void on_lamp_switch(wxCommandEvent& event) { event.Skip(); }
			virtual void on_bed_temp_kill_focus(wxFocusEvent& event) { event.Skip(); }
			virtual void on_bed_temp_set_focus(wxFocusEvent& event) { event.Skip(); }
			virtual void on_set_bed_temp(wxCommandEvent& event) { event.Skip(); }
			virtual void on_nozzle_temp_kill_focus(wxFocusEvent& event) { event.Skip(); }
			virtual void on_nozzle_temp_set_focus(wxFocusEvent& event) { event.Skip(); }
			virtual void on_set_nozzle_temp(wxCommandEvent& event) { event.Skip(); }
			virtual void on_nozzle_fan_switch(wxCommandEvent& event) { event.Skip(); }
			virtual void on_printing_fan_switch(wxCommandEvent& event) { event.Skip(); }
			virtual void on_axis_ctrl_x_home(wxCommandEvent& event) { event.Skip(); }
			virtual void on_axis_ctrl_xy(wxCommandEvent& event) { event.Skip(); }
			virtual void on_axis_ctrl_y_home(wxCommandEvent& event) { event.Skip(); }
			virtual void on_axis_ctrl_z_home(wxCommandEvent& event) { event.Skip(); }
			virtual void on_axis_ctrl_home(wxCommandEvent& event) { event.Skip(); }
			virtual void on_axis_ctrl_z_up_10(wxCommandEvent& event) { event.Skip(); }
			virtual void on_axis_ctrl_z_up_1(wxCommandEvent& event) { event.Skip(); }
			virtual void on_axis_ctrl_z_up_0_1(wxCommandEvent& event) { event.Skip(); }
			virtual void on_axis_ctrl_z_down_0_1(wxCommandEvent& event) { event.Skip(); }
			virtual void on_axis_ctrl_z_down_1(wxCommandEvent& event) { event.Skip(); }
			virtual void on_axis_ctrl_z_down_10(wxCommandEvent& event) { event.Skip(); }
			virtual void on_axis_ctrl_e_up_10(wxCommandEvent& event) { event.Skip(); }
			virtual void on_axis_ctrl_e_up_1(wxCommandEvent& event) { event.Skip(); }
			virtual void on_axis_ctrl_e_down_1(wxCommandEvent& event) { event.Skip(); }
			virtual void on_axis_ctrl_e_down_10(wxCommandEvent& event) { event.Skip(); }
			virtual void on_select_space_1(wxCommandEvent& event) { event.Skip(); }
			virtual void on_select_space_2(wxCommandEvent& event) { event.Skip(); }
			virtual void on_select_space_3(wxCommandEvent& event) { event.Skip(); }
			virtual void on_select_space_4(wxCommandEvent& event) { event.Skip(); }
			virtual void on_extruder_feed(wxCommandEvent& event) { event.Skip(); }
			virtual void on_extruder_back(wxCommandEvent& event) { event.Skip(); }


		public:

			StatusBasePanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(1258, 834), long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString);

			~StatusBasePanel();

		};
		///////////////////////////////////////////////////////////////////////////////
		/// Class VideoMonitoringBasePanel
		///////////////////////////////////////////////////////////////////////////////
		class VideoMonitoringBasePanel : public wxPanel
		{
		private:

		protected:

		public:

			VideoMonitoringBasePanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(1258, 834), long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString);

			~VideoMonitoringBasePanel();

		};

		///////////////////////////////////////////////////////////////////////////////
		/// Class TaskListBasePanel
		///////////////////////////////////////////////////////////////////////////////
		class TaskListBasePanel : public wxPanel
		{
		private:

		protected:
			wxPanel* m_panel_model_name_caption;
			wxStaticText* m_staticText_model_name;
			wxPanel* m_panel_model_name_content;
			wxStaticBitmap* m_bitmap_task;
			wxStaticText* m_staticText_task_desc;
			wxStaticText* m_staticText_ceation_time_title;
			wxStaticText* m_staticText_creation_time;
			wxPanel* m_panel_plater_caption;
			wxStaticText* m_staticText_plater;
			wxPanel* m_panel_plater_content;
			wxFlexGridSizer* fgSizer_subtask;

		public:

			TaskListBasePanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(1258, 834), long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString);

			~TaskListBasePanel();

		};

	} // namespace GUI
} // namespace Slic3r

