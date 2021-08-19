#ifndef slic3r_DebugToolDialog_hpp_
#define slic3r_DebugToolDialog_hpp_

#include <string>
#include <fstream>
#include <boost/filesystem/path.hpp>

#include <wx/string.h>
#include <wx/event.h>
#include <wx/dialog.h>
#include <wx/timer.h>

#include "GUI_Utils.hpp"
#include "MsgDialog.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/DeviceManager.hpp"


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

    struct PrintHostJob;

    namespace GUI {

        wxDECLARE_EVENT(EVT_DEVICE_REPORT_MSG, SimpleEvent);

        class DebugToolDialog : public GUI::DPIDialog
        {
        public:
            DebugToolDialog(wxWindow *parent);

            virtual bool Show(bool show = true) override;

            int publish_json(std::string json_str);
            int publish_json_to_device(std::string dev_id, std::string json_str);
            int handle_report_print_msg(std::string topic, std::string json_str);
            int handle_device_report_msg(std::string json_str);
            int handle_alive_msg(std::string dev_id);
            int append_output_string_info(std::string str);
            int handle_offline_event(std::string dev_id);

            void refresh_device_list();
            void refresh_firmware_list(bool show_error=false);
            void add_firmware(std::string firmware);
            void on_device_report_msg(SimpleEvent& evt);
            void on_select_device(wxCommandEvent& evt);
            void on_dropdown_devicelist(wxCommandEvent& evt);
            void on_select_host(wxCommandEvent& evt);
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
            std::string iot_host_item[2] = { "http://iot.qa.bbl", "http://192.168.0.10:9000" };
            std::string mqtt_host_item[2] = { "47.100.225.51:1883", "192.168.0.10:1883" };

            wxButton* btn_select_device;
            wxButton* btn_refresh_upgrade_list;
            wxButton* btn_disconnect_via_name;
            wxButton* btn_upgrade_firmware;
            wxButton* btn_run_gcode;
            wxButton* btn_abort_print;
            wxStaticText* label_progress;
            wxButton* btn_select_gcode_file;
            wxButton* btn_clear_output_string;
            wxButton* btn_save_file;
            wxButton* btn_bind;
            wxButton* btn_unbind;
            wxButton* btn_login;
            wxButton* btn_logout;
            wxButton* btn_register;

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

            wxButton* btn_return_home;
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
            wxButton* btn_get_version;

            wxButton* btn_send_gcode_1;
            wxButton* btn_send_gcode_2;
            wxButton* btn_send_gcode_3;
            wxButton* btn_send_gcode_4;
            wxButton* btn_send_gcode_5;
            wxButton* btn_send_gcode_6;
            wxButton* btn_send_gcode_7;

            wxTextCtrl* txt_printer_name;
            wxTextCtrl* txt_gcode_filename;
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
            wxTextCtrl* txt_user;
            wxTextCtrl* txt_password;

            wxStaticText* label_upgrade_filename;
            wxStaticText* label_gcode_filename;
            wxStaticText* label_output_string;
            wxStaticText* label_device_list;
            wxStaticText* label_device_status;

            wxStaticText* label_pos_x;
            wxStaticText* label_pos_x_val;
            wxStaticText* label_pos_y;
            wxStaticText* label_pos_y_val;
            wxStaticText* label_pos_z;
            wxStaticText* label_pos_z_val;
            wxStaticText* label_pos_e;
            wxStaticText* label_pos_e_val;
            wxStaticText* label_hot_end_temp;
            wxStaticText* label_hot_end_temp_val;
            wxStaticText* label_bed_end_temp;
            wxStaticText* label_bed_end_temp_val;
            wxStaticText* label_print_progress;
            wxStaticText* label_print_progress_val;
            wxStaticText* label_wifi_signal;
            wxStaticText* label_wifi_signal_val;


            wxComboBox* cb_upgrade_module;
            wxArrayString module_items;
            wxComboBox* cb_device_list;
            wxComboBox* cb_publish_mode;
            wxComboBox* cb_upgrade_firmware;
            wxComboBox* cb_upgrade_mode;
            wxComboBox* cb_select_host;

            std::vector<wxString> upgrade_file_list;
            wxFileDialog* selectGcodeDialog;
            FILE* logFile;
            std::fstream customGcodeCacheFile;
            wxTimer* m_deviceListTimer;

            wxBoxSizer* top_sizer;
            wxGridSizer* pos_btns_sizer;
            wxBoxSizer* user_sizer;
            wxBoxSizer* conn_device_sizer;
            wxBoxSizer* upgrade_sizer;
            wxBoxSizer* run_gcode_sizer;
            wxFlexGridSizer* custom_gcode_sizer;

            /* GUI init control */
            void init_account();
            void init_device();
            void init_upgrade();
            void init_gcode_run_file();
            void init_gcode_control();
            void init_gcode_custom();
            void init_push_info();

            int m_sequence_id = 2000;
            int publishGcode(std::string gcode);
            int callSystem(std::string cmd, std::string& output);
            int set_current_device_id();
            int get_current_device_id(std::string &dev_id);
            std::string get_device_list_item(DeviceInfo* info);

            std::unique_ptr<wxTimer> m_timer;
            void on_timer(wxTimerEvent&);
            std::string _getNewLogFilename();
            std::vector<std::string> log_lines;
            std::mutex log_mutex;

            bool m_test_alive = false;
            std::string m_curr_dev_id;
        };
    }
}

#endif
