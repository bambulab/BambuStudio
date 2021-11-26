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
#include "slic3r/GUI/DebugToolPanel.h"

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
        class DebugToolDialog : public DebugToolPanel
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
