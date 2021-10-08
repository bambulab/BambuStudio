#ifndef slic3r_DebugToolDialog_hpp_
#define slic3r_DebugToolDialog_hpp_

#include <string>
#include <fstream>
#include <boost/filesystem/path.hpp>

#include <wx/string.h>
#include <wx/event.h>
#include <wx/dialog.h>
#include <wx/timer.h>
#include <wx/panel.h>
#include <wx/notebook.h>

#include "GUI_Utils.hpp"
#include "MsgDialog.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/PrintResultDialog.hpp"


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
        class DebugToolDialog : public GUI::DPIDialog
        {
        public:
            DebugToolDialog(wxWindow *parent);

            virtual bool Show(bool show = true) override;

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
        protected:
            void on_dpi_changed(const wxRect& suggested_rect) override;

            void on_show(wxShowEvent& event);
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

            /* GUI widgets */
            wxNotebook* nb_main;

            /* switch host servers */
            wxComboBox* cb_server_host;

            /* Connections widgets */
            wxButton*       btn_refresh_device_list;
            wxButton*       btn_connect;
            wxButton*       btn_disconnect;
            wxComboBox*     cb_device_list;
            wxComboBox*     cb_my_device_list;
            wxRadioButton* radio_btn_lan;
            wxRadioButton* radio_btn_wan;


            /* Upgrade widgets */
            wxButton* btn_refresh_upgrade_list;
            wxButton* btn_upgrade_firmware;
            std::vector<wxString> upgrade_file_list;

            /* Gcode widgets*/
            wxButton*       btn_run_gcode;
            wxTextCtrl*     txt_gcode_filename;
            wxStaticText*   label_gcode_progress;
            wxButton*       btn_select_gcode_file;
            wxFileDialog*   selectGcodeDialog;
            bool            gcode_uploading;

            wxButton*       btn_upload_3mf;
            wxTextCtrl*     txt_3mf_filename;
            wxTextCtrl*     txt_3mf_plate_idx;
            wxTextCtrl*     txt_wlan_gcode_filename;
            wxButton*       btn_run_3mf;
            wxButton*       btn_abort_3mf;
            wxStaticText*   label_3mf_progress;
            wxButton*       btn_select_3mf_file;
            wxFileDialog*   select3mfDialog;
            wxStaticText*   label_upload_gcode_progress_val;

            /* display plate and send task */
            wxTextCtrl*     txt_plate_idx;
            wxComboBox*     cb_profiles;


            /* machine control */
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

            wxButton* btn_auto_leveling;
            wxButton* btn_xyz_abs_mode;
            wxButton* btn_fan_on;
            wxButton* btn_fan_off;
            wxButton* btn_set_hot_bed_temp;
            wxButton* btn_set_hot_end_temp;
            wxButton* btn_start_temp_push;
            wxButton* btn_stop_temp_push;
            wxButton* btn_get_curr_temp;
            wxButton* btn_get_curr_pos;
            wxButton* btn_switch_t;
            wxTextCtrl* txt_switch_val;

            wxTextCtrl* txt_printer_name;
            wxTextCtrl* txt_set_hot_bed_temp;
            wxTextCtrl* txt_set_hot_end_temp;
            wxTextCtrl* txt_string_info;
            wxTextCtrl* txt_custom_gcode1;
            wxTextCtrl* txt_custom_gcode2;
            wxTextCtrl* txt_custom_gcode3;
            wxTextCtrl* txt_custom_gcode4;
            wxTextCtrl* txt_custom_gcode5;
            wxTextCtrl* txt_custom_gcode6;
            wxTextCtrl* txt_custom_gcode7;
            wxTextCtrl* txt_ams_flush_temp1;
            wxTextCtrl* txt_ams_flush_temp2;
            wxCheckBox* cbox_ams_auto_home;

            wxStaticText* label_pos_x_val;
            wxStaticText* label_pos_y_val;
            wxStaticText* label_pos_z_val;
            wxStaticText* label_pos_e_val;
            wxStaticText* label_hot_end_temp_val;
            wxStaticText* label_bed_end_temp_val;
            wxStaticText* label_print_progress_val;
            wxStaticText* label_wifi_signal_val;
            wxStaticText* label_wifi_link_th_val;
            wxStaticText* label_wifi_link_ams_val;
            wxStaticText* label_ams_flush_temp1;
            wxStaticText* label_ams_flush_temp2;

            wxStaticText* label_upgrade_status_val;
            wxStaticText* label_upgrade_progress_val;
            wxStaticText* label_upgrade_module_val;
            wxStaticText* label_upgrade_message_val;
            wxStaticText* label_force_upgrade_val;


            wxComboBox* cb_upgrade_module;
            wxArrayString module_items;
            wxComboBox* cb_publish_mode;
            wxComboBox* cb_upgrade_firmware;
            wxComboBox* cb_upgrade_mode;
            wxComboBox* cb_select_host;

            wxCheckBox* chk_enable_direct;
            wxStaticText* label_device_ip;
            wxTextCtrl* txt_device_ip;
            wxStaticText* label_domain_id;
            wxTextCtrl* txt_domain_id;
            wxStaticText* label_client_id;
            wxTextCtrl* txt_client_id;
            wxButton* btn_mqtt_connect;
            
            std::fstream customGcodeCacheFile;
            wxTimer* m_deviceListTimer;

            wxBoxSizer* top_sizer;
            wxGridSizer* pos_btns_sizer;
            wxBoxSizer* conn_sizer;


            /* GUI init control */
            void init_connection_widgets();
            void init_common(wxWindow* parent);
            void init_upgrade(wxWindow* parent);
            void init_gcode_run_file(wxWindow* parent);
            void init_custom_ctrl(wxWindow* parent);
            void init_task_widgets(wxWindow* parent);
            void init_gcode_control(wxWindow* parent, wxBoxSizer* sizer);
            void init_gcode_custom(wxWindow* parent, wxBoxSizer* sizer);
            void init_log_panel(wxWindow* parent);
            void init_layout();
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
