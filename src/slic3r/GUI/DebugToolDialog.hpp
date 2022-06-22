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
#include "slic3r/GUI/DebugToolPanel.h"
#include "slic3r/GUI/Search.hpp"
#include "slic3r/GUI/AmsWidgets.hpp"
#include "Jobs/Job.hpp"

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



namespace Slic3r {
namespace GUI {

class GcodePrintJob : public Job
{
private:
    std::string         m_gcode_file_str;
    MachineObject*      m_obj;

protected:

    void prepare() override;

    void on_exception(const std::exception_ptr &) override;
public:
    GcodePrintJob(std::shared_ptr<ProgressIndicator> pri, std::string gcode_file_str, MachineObject* obj)
        : Job{std::move(pri)},
        m_obj(obj),
        m_gcode_file_str(gcode_file_str)
    {}

    int status_range() const override
    {
        return 100;
    }

    void process() override;
    void finalize() override;
};

class DeviceSearchListModel : public wxDataViewVirtualListModel
{
    std::vector<wxString>   m_values;

public:
    enum {
        colDeviceInfo,
        colMax
    };
    DeviceSearchListModel(wxWindow* parent);

    void Clear();
    void update_info(std::vector<wxString> list);

    unsigned int GetColumnCount() const override { return colMax; }
    wxString GetColumnType(unsigned int col) const override;
    void GetValueByRow(wxVariant& variant, unsigned int row, unsigned int col) const override;
    bool GetAttrByRow(unsigned int row, unsigned int col, wxDataViewItemAttr& attr) const override { return true; }
    bool SetValueByRow(const wxVariant& variant, unsigned int row, unsigned int col) override { return false; }


};


class DeviceSearchDialog : public wxDialog
{
private:
    wxString default_string;
protected:
	wxTextCtrl* m_textCtrl_search_line;
	wxDataViewCtrl* m_dataViewCtrl;

    DeviceSearchListModel*    search_list_model   { nullptr };

    bool     prevent_list_events {false};

	// Virtual event handlers, override them in your derived class
    void OnInputText(wxCommandEvent& event);
    void OnActivate(wxDataViewEvent& event);
    void OnSelect(wxDataViewEvent& event);
    void update_list();
    void ProcessSelection(wxDataViewItem selection);


public:

	DeviceSearchDialog( wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = wxEmptyString, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( 538,351 ), long style = wxDEFAULT_DIALOG_STYLE );

	~DeviceSearchDialog();

};





        class DebugToolDialog : public DebugToolPanel
        {
        public:
            DebugToolDialog(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(800, 600), long style = wxTAB_TRAVERSAL);

            ~DebugToolDialog();

            bool Show(bool show);

            int publish_json(std::string json_str);

            /* log */
            void send_log_evt(std::string info);
            int log_info(std::string str);

            void refresh_device_list();
            void refresh_firmware_list(bool show_error=false);
            std::string get_curr_module_name();
            void on_update_list(SimpleEvent& evt);
            void on_update_mybind_list(SimpleEvent& evt);
            void on_select_device(wxCommandEvent& evt);
            void on_select_mybind_device(wxCommandEvent& evt);
            void on_mqtt_failed(wxCommandEvent& evt);
            void on_mqtt_lost(wxCommandEvent& evt);
            void on_mqtt_connected(wxCommandEvent& evt);
            void on_mqtt_disconnected(wxCommandEvent& evt);
            void on_message_arrived(wxCommandEvent& evt);
            void on_message_sent(wxCommandEvent& evt);
            void on_log_info(wxCommandEvent& evt);
            void on_local_connected(int state, std::string dev_id, std::string msg);
            void get_version();
            void message_arrived(std::string dev_id, std::string msg);

            wxArrayString device_list_items;
            void jump_to_printer(wxString selected);

        private:

            enum UPGRADE_MODULE { MODULE_RK = 0, MODULE_MC = 1, MODULE_TH = 2, MODULE_AMS = 3, MODULE_OTA = 4, MODULE_MAX };
            enum UPGRADE_MODE { MODE_DAILYBUILD = 0, MODE_RELEASE = 1, MODE_DEBUG = 2, MODE_WIP = 3, MODE_MAX};
            std::string upgrade_post_url[MODULE_MAX] = { "rk", "mc", "th", "ams", "ota"};
            std::string upgrade_module_name[MODULE_MAX] = { "rk1126", "mc", "th", "ams", "ota"};
            std::string upgrade_mode_name[MODE_MAX] = { "dailybuild", "release", "debug", "wip" };
            int last_upgrade_module_sel;
            int last_upgrade_mode_sel;
            int last_upgrade_version_sel;

            std::string UPGRADE_URL = "http://192.168.0.12:8000/api/devices_upgrade_firmware";

            DeviceManager& dev_manager_;
            std::vector<std::string> machine_list_items;
            std::vector<std::string> mybind_machine_list_items;
            int last_device_selection;
            int last_wlan_device_selection;

            wxBitmap        m_search_img;
            DeviceSearchDialog* search_dialog;
            wxFileDialog*   selectGcodeDialog;
            wxFileDialog*   select3mfDialog;
            wxObjectDataPtr<TrayListModel> tray_model;
            bool            gcode_uploading;
            bool            _3mf_uploading;
			std::vector<wxString> upgrade_file_list;

            struct UpgradeItem {
                std::string name;
                std::string version;
                std::string url;
            };
            std::vector<UpgradeItem> upgrade_img_list;

            std::fstream customGcodeCacheFile;
            wxTimer* m_deviceListTimer;
			std::queue<std::string> mqtt_msg_queue;
            std::queue<std::string> mqtt_msg_queue_cloud;

			void init();
            void init_model();
            void init_bind();
            void init_bind_handler();

            int m_sequence_id = 2000;
            int publishGcode(std::string gcode);
            wxString get_machine_display_item(MachineObject* obj);
            std::string switch_ams_gcode(std::string t);
            std::unique_ptr<wxTimer> m_timer;
            void on_timer(wxTimerEvent&);
            std::string _getNewLogFilename();

            std::string m_curr_dev_id;
            int last_progress;
        };
    }
}

#endif
