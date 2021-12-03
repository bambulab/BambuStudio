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
#include <wx/radiobut.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/combobox.h>
#include <wx/button.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/panel.h>
#include <wx/statbox.h>
#include <wx/scrolwin.h>
#include <wx/checkbox.h>
#include <wx/dataview.h>
#include <wx/notebook.h>
#include <wx/splitter.h>

#include "BBLStatusBar.hpp"

///////////////////////////////////////////////////////////////////////////

namespace Slic3r
{
	namespace GUI
	{

		///////////////////////////////////////////////////////////////////////////////
		/// Class DebugToolPanel
		///////////////////////////////////////////////////////////////////////////////
		class DebugToolPanel : public wxPanel
		{
			private:

			protected:
				wxRadioButton* radio_btn_lan;
				wxStaticText* m_staticText_lan;
				wxComboBox* cb_device_list;
				wxButton* btn_refresh_device_list;
				wxButton* btn_connect;
				wxButton* btn_disconnect;
				wxButton* btn_bind;
				wxButton* btn_unbind;
				wxRadioButton* radio_btn_wan;
				wxStaticText* m_staticText_wan;
				wxComboBox* cb_my_device_list;
				wxButton* btn_refresh_my_device;
				wxSplitterWindow* m_splitter1;
				wxPanel* m_panel_left;
				wxNotebook* m_notebook1;
				wxPanel* m_panel_guide;
				wxStaticText* m_staticText_guide_title;
				wxTextCtrl* m_textCtrl10;
				wxPanel* m_panel_common;
				wxButton* btn_get_version;
				wxStaticText* m_staticText6;
				wxStaticText* label_force_upgrade_val;
				wxPanel* m_panel_run_3mf;
				wxStaticText* label_3mf_filename;
				wxTextCtrl* txt_3mf_filename;
				wxButton* btn_select_3mf_file;
				wxStaticText* label_upload_progress;
				wxStaticText* label_3mf_progress;
				wxButton* btn_run_3mf;
				wxButton* btn_3mf_pause;
				wxButton* btn_3mf_resume;
				wxButton* btn_3mf_abort_print;
				wxStaticText* m_staticText_run_3mf_tips;
				wxPanel* m_panel_run_gcode;
				wxStaticText* label_gcode_filename;
				wxTextCtrl* txt_gcode_filename;
				wxButton* btn_select_gcode_file;
				wxStaticText* label_upload_progress1;
				wxPanel* m_panel_status;
				wxButton* btn_run_gcode;
				wxButton* btn_pause;
				wxButton* btn_resume;
				wxButton* btn_abort_print;
				wxPanel* m_panel_info_control;
				wxStaticText* m_staticText_nozzle_temp_title;
				wxStaticText* label_hot_end_temp_val;
				wxStaticText* m_staticText_bed_temp_title;
				wxStaticText* label_bed_end_temp_val;
				wxStaticText* m_staticText_pocket_temp;
				wxStaticText* m_staticText_volume_temp_val;
				wxStaticText* m_staticText_progress;
				wxStaticText* label_print_progress_val;
				wxStaticText* m_staticText_wifi_signal;
				wxStaticText* label_wifi_signal_val;
				wxStaticText* m_staticText_th_link;
				wxStaticText* label_wifi_link_th_val;
				wxStaticText* m_staticText_ams_link;
				wxStaticText* label_wifi_link_ams_val;
				wxStaticText* m_staticText_big1_speed_title;
				wxStaticText* m_staticText_big1_speed;
				wxStaticText* m_staticText_big2_speed_title;
				wxStaticText* m_staticText_big2_speed;
				wxStaticText* m_staticText_cooling_speed_title;
				wxStaticText* m_staticText_cooling_speed;
				wxStaticText* m_staticText_heatbreak_speed_title;
				wxStaticText* m_staticText_heatbreak_speed;
				wxStaticText* m_staticText_print_stage;
				wxStaticText* m_staticText_mc_print_stage;
				wxStaticText* m_staticText_print_error_code;
				wxStaticText* m_staticText_mc_print_error_code;
				wxStaticText* m_staticText_gcode_line_number;
				wxStaticText* m_staticText_mc_print_line_number;
				wxPanel* m_panel_settings;
				wxButton* btn_set_hot_bed_temp;
				wxTextCtrl* txt_set_hot_bed_temp;
				wxButton* btn_set_hot_end_temp;
				wxTextCtrl* txt_set_hot_end_temp;
				wxButton* btn_fan_on;
				wxButton* btn_fan_off;
				wxButton* btn_auto_leveling;
				wxButton* btn_xyz_abs_mode;
				wxButton* btn_return_home;
				wxPanel* m_panel__control;
				wxButton* btn_set_x_pos_0_1;
				wxButton* btn_set_x_pos_1_0;
				wxButton* btn_set_x_pos_10_0;
				wxButton* btn_set_x_neg_0_1;
				wxButton* btn_set_x_neg_1_0;
				wxButton* btn_set_x_neg_10_0;
				wxButton* btn_set_y_pos_0_1;
				wxButton* btn_set_y_pos_1_0;
				wxButton* btn_set_y_pos_10_0;
				wxButton* btn_set_y_neg_0_1;
				wxButton* btn_set_y_neg_1_0;
				wxButton* btn_set_y_neg_10_0;
				wxButton* btn_set_z_pos_0_1;
				wxButton* btn_set_z_pos_1_0;
				wxButton* btn_set_z_pos_10_0;
				wxButton* btn_set_z_neg_0_1;
				wxButton* btn_set_z_neg_1_0;
				wxButton* btn_set_z_neg_10_0;
				wxButton* btn_set_e_pos_0_1;
				wxButton* btn_set_e_pos_1_0;
				wxButton* btn_set_e_pos_10_0;
				wxButton* btn_set_e_neg_0_1;
				wxButton* btn_set_e_neg_1_0;
				wxButton* btn_set_e_neg_10_0;
				wxScrolledWindow* m_scrolledWindow_custom;
				wxButton* btn_send_gcode_1;
				wxTextCtrl* txt_custom_gcode1;
				wxButton* btn_send_gcode_2;
				wxTextCtrl* txt_custom_gcode2;
				wxButton* btn_send_gcode_3;
				wxTextCtrl* txt_custom_gcode3;
				wxButton* btn_send_gcode_4;
				wxTextCtrl* txt_custom_gcode4;
				wxButton* btn_send_gcode_5;
				wxTextCtrl* txt_custom_gcode5;
				wxButton* btn_send_gcode_6;
				wxTextCtrl* txt_custom_gcode6;
				wxButton* btn_send_gcode_7;
				wxTextCtrl* txt_custom_gcode7;
				wxPanel* m_panel_upgrade;
				wxStaticText* m_staticText66;
				wxComboBox* cb_upgrade_module;
				wxStaticText* m_staticText67;
				wxComboBox* cb_upgrade_mode;
				wxStaticText* m_staticText57;
				wxComboBox* cb_upgrade_firmware;
				wxButton* btn_refresh_upgrade_list;
				wxButton* btn_upgrade_firmware;
				wxStaticText* m_staticText_new_version_title;
				wxStaticText* m_staticText_new_version;
				wxButton* m_button_upgrade_confirm;
				wxStaticText* m_staticText_consistency;
				wxStaticText* m_staticText_request_consisitency_upgrade;
				wxButton* m_button_consistency_upgrade_confirm;
				wxStaticText* m_staticText_status_title;
				wxStaticText* label_upgrade_status_val;
				wxStaticText* m_staticText_upgrade_module;
				wxStaticText* m_staticText_upgrade_module_value;
				wxStaticText* m_staticText_upgrade_progress;
				wxStaticText* label_upgrade_progress_val;
				wxStaticText* m_staticText_upgrade_info;
				wxStaticText* label_upgrade_message_val;
				wxPanel* m_panel_ams;
				wxButton* btn_switch_t;
				wxTextCtrl* txt_switch_val;
				wxStaticText* label_ams_flush_temp1;
				wxTextCtrl* txt_ams_flush_temp1;
				wxStaticText* label_ams_flush_temp2;
				wxTextCtrl* txt_ams_flush_temp2;
				wxCheckBox* cbox_ams_auto_home;
				wxButton* m_button_ams_pause;
				wxButton* m_button_ams_resume;
				wxDataViewCtrl* m_dataViewCtrl_ams;
				wxPanel* m_panel_log;
				wxStaticText* m_staticText_log;
				wxTextCtrl* txt_string_info;

				std::shared_ptr<BBLStatusBar> m_status_bar;

			public:

				DebugToolPanel( wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( 1379,726 ), long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString );

				~DebugToolPanel();

				void m_splitter1OnIdle( wxIdleEvent& )
				{
					m_splitter1->SetSashPosition( 971 );
					m_splitter1->Disconnect( wxEVT_IDLE, wxIdleEventHandler( DebugToolPanel::m_splitter1OnIdle ), NULL, this );
				}

		};

	} // namespace GUI
} // namespace Slic3r

