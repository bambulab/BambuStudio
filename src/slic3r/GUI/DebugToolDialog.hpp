#ifndef slic3r_DebugToolDialog_hpp_
#define slic3r_DebugToolDialog_hpp_

#include <string>
#include <fstream>
#include <queue>
#include <boost/filesystem/path.hpp>

#include <wx/string.h>
#include <wx/event.h>
#include <wx/dialog.h>
#include <wx/timer.h>
#include <wx/panel.h>
#include <wx/notebook.h>
#include <wx/splitter.h>

#include "GUI_Utils.hpp"
#include "MsgDialog.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/PrintResultDialog.hpp"

//#define __CHECK_BIND_USER__

class wxTimer;
class wxTimerEvent;
class wxButton;
class wxTextCtrl;
class wxComboBox;
class wxStaticText;
class wxDataViewListCtrl;
class wxFileDialog;

#define TIMER_ID    1000
#define COMBOBOX_ID 1001

class WXDLLIMPEXP_CORE JsonMsgEvent : public wxEvent
{
public:
    JsonMsgEvent(wxEventType eventType, std::string str) :
        wxEvent(eventType), json_str(str) {}
    virtual wxEvent* Clone() const { return new JsonMsgEvent(*this); }
    std::string getJson() { return json_str; }

private:
    std::string json_str;
};


namespace Slic3r {

    namespace GUI {
        class DebugToolDialog : public wxPanel
        {
        public:
            DebugToolDialog(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(800, 600), long style = wxTAB_TRAVERSAL);

            bool Show(bool show);

            int publish_json(std::string json_str);

            /* log */
            void send_log_evt(std::string info);
            int log_info(std::string str);

            void refresh_device_list();
            void refresh_firmware_list(bool show_error=false);
            void add_firmware(std::string firmware);
            void on_update_list(SimpleEvent& evt);
            void on_update_mybind_list(SimpleEvent& evt);
            void on_select_device(wxCommandEvent& evt);
            void on_select_mybind_device(wxCommandEvent& evt);
            void on_mqtt_failed(wxCommandEvent& evt);
            void on_mqtt_lost(wxCommandEvent& evt);
            void on_mqtt_connected(wxCommandEvent& evt);
            void on_mqtt_disconnected(wxCommandEvent& evt);
            void on_print_end(wxCommandEvent& evt);
            void on_message_arrived(wxCommandEvent& evt);
            void on_message_sent(wxCommandEvent& evt);
            void on_log_info(wxCommandEvent& evt);
            void get_version();

        private:
            enum {
                HEIGHT = 60, WIDTH = 30, SPACING = 5,
                BTN_HEIGHT = 35, BTN_WIDTH = 60,
                LABEL_HEIGHT = 35, LABEL_WIDTH = 40,
                TXT_GCODE_HEIGHT = 80,
                BTN_CTRL_HEIGHT = 30, BTN_CTRL_WIDTH = 50,
                BTN_SEND_HEIGHT = 80, BTN_SEND_WIDTH = 100,
            };

            enum UPGRADE_MODULE { MODULE_RK = 0, MODULE_MC = 1, MODULE_TH = 2, MODULE_AMS = 3, MODULE_OTA = 4, MODULE_MAX };
            enum UPGRADE_MODE { MODE_DAILYBUILD = 0, MODE_RELEASE = 1, MODE_DEBUG = 2, MODE_MAX};
            std::string upgrade_post_url[MODULE_MAX] = { "rk/", "mc/", "th/", "ams/", "ota/"};
            std::string upgrade_module_name[MODULE_MAX] = { "rk1126", "mc", "th", "ams", "ota"};
            std::string upgrade_mode_name[MODE_MAX] = { "dailybuild/", "release/", "debug/"};

            std::string UPGRADE_URL = "http://upgrade.bambooolab.com/";
            std::string CURL_FILE = resources_dir() + "/bbl/curl";

            DeviceManager& dev_manager_;
            std::vector<std::string> machine_list_items;
            std::vector<std::string> mybind_machine_list_items;
            int last_device_selection;
            int last_wlan_device_selection;


            /* GUI Widgets */
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
			wxStaticText* label_gcode_progress1;
			wxButton* btn_run_gcode;
			wxButton* btn_pause;
			wxButton* btn_resume;
			wxButton* btn_abort_print;
			wxPanel* m_panel_info_control;
			wxStaticText* m_staticText_nozzle_temp_title;
			wxStaticText* label_hot_end_temp_val;
			wxStaticText* m_staticText_bed_temp_title;
			wxStaticText* label_bed_end_temp_val;
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
			wxButton* btn_switch_t;
			wxTextCtrl* txt_switch_val;
			wxStaticText* label_ams_flush_temp1;
			wxTextCtrl* txt_ams_flush_temp1;
			wxStaticText* label_ams_flush_temp2;
			wxTextCtrl* txt_ams_flush_temp2;
			wxCheckBox* cbox_ams_auto_home;
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
			wxStaticText* m_staticText_status_title;
			wxStaticText* label_upgrade_status_val;
			wxStaticText* m_staticText_upgrade_module;
			wxStaticText* m_staticText_upgrade_module_value;
			wxStaticText* m_staticText_upgrade_progress;
			wxStaticText* label_upgrade_progress_val;
			wxStaticText* m_staticText_upgrade_info;
			wxStaticText* label_upgrade_message_val;
			wxPanel* m_panel_log;
			wxStaticText* m_staticText_log;
			wxTextCtrl* txt_string_info;


            wxFileDialog*   selectGcodeDialog;
            wxFileDialog*   select3mfDialog;
            bool            gcode_uploading;
            bool            _3mf_uploading;
			std::vector<wxString> upgrade_file_list;
            std::fstream customGcodeCacheFile;
            wxTimer* m_deviceListTimer;
			std::queue<std::string> mqtt_msg_queue;

			void init();
            void init_bind();
            void init_bind_handler();

            int m_sequence_id = 2000;
            int publishGcode(std::string gcode);
            wxString get_machine_display_item(MachineObject* obj);
            std::string switch_ams_gcode(std::string t);
            std::unique_ptr<wxTimer> m_timer;
            void on_timer(wxTimerEvent&);
            std::string _getNewLogFilename();

            bool m_test_alive = false;
            std::string m_curr_dev_id;
            int last_progress;

            /* print summery */
            PrintSummary *summary;
        };
    }
}

#endif
