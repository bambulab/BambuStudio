#ifndef slic3r_DeviceManager_hpp_
#define slic3r_DeviceManager_hpp_

#include <map>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <boost/thread.hpp>
#include "mqtt/async_client.h"
#include "CommuBackend.hpp"
#include "AccountManager.hpp"
#include "libslic3r/ProjectTask.hpp"
#include "slic3r/Utils/json_diff.hpp"

#define USE_LOCAL_SOCKET_BIND 0

#define DISCONNECT_TIMEOUT      10000.f     // milliseconds

#define LOCAL_COMMU_PORT        3000
#define DEBUG_COMMU_PORT        5000

#define FILAMENT_MAX_TEMP       300
#define FILAMENT_DEF_TEMP       220
#define FILAMENT_MIN_TEMP       120

inline int correct_filament_temperature(int filament_temp)
{
    int temp = std::min(filament_temp, FILAMENT_MAX_TEMP);
    temp     = std::max(temp, FILAMENT_MIN_TEMP);
    return temp;
}

namespace Slic3r {

enum PRINTER_TYPE {
    PRINTER_3DPrinter_UKNOWN,
    PRINTER_3DPrinter_X1_Carbon,    // BL-P001
    PRINTER_3DPrinter_X1,           // BL-P002
    PRINTER_3DPrinter_P1,           // BL-P003
};

enum PRINTING_STAGE {
    PRINTING_STAGE_PRINTING = 0,
    PRINTING_STAGE_BED_LEVELING,
    PRINTING_STAGE_HEADBED,
    PRINTING_STAGE_XY_MECH_MODE,
    PRINTING_STAGE_CHANGE_MATERIAL,
    PRINTING_STAGE_M400_PAUSE,
    PRINTING_STAGE_FILAMENT_RUNOUT_PAUSE,
    PRINTING_STAGE_HOTEND_HEATING,
    PRINTING_STAGE_EXTRUDER_SCAN,
    PRINTING_STAGE_BED_SCAN,
    PRINTING_STAGE_FIRST_LAYER_SCAN,
    PRINTING_STAGE_SURFACE_TYPE_IDENT,
    PRINTING_STAGE_SCANNER_PARAM_CALI,
    PRINTING_STAGE_TOOHEAD_HOMING,
    PRINTING_STAGE_NOZZLE_TIP_CLEANING,
    PRINTING_STAGE_COUNT
};


enum PrintingSpeedLevel {
    SPEED_LEVEL_INVALID = 0,
    SPEED_LEVEL_SILENCE = 1,
    SPEED_LEVEL_NORMAL = 2,
    SPEED_LEVEL_RAPID = 3,
    SPEED_LEVEL_RAMPAGE = 4,
    SPEED_LEVEL_COUNT
};

class AccountManager;

class sub_action_listener : public virtual mqtt::iaction_listener
{
private:
    std::string name_;

    void on_failure(const mqtt::token& tok) override {
        ;
    }
    void on_success(const mqtt::token& tok) override {
        ;
    }
public:
    sub_action_listener(const std::string& name) : name_(name) {}
};

class machine_conn_callback : public virtual mqtt::callback, public virtual mqtt::iaction_listener
{
private:
    int nretry_;
    mqtt::async_client& cli_;
    mqtt::connect_options& connOpts_;
    void* context_;
    std::vector<std::string> sub_topics;

    void connected(const std::string& cause) override;

    void on_failure(const mqtt::token& tok) override;

    void on_success(const mqtt::token& tok) override;

    void connection_lost(const std::string& cause) override;

    void message_arrived(mqtt::const_message_ptr msg) override;
public:
    machine_conn_callback(mqtt::async_client& cli, mqtt::connect_options& connOpts, void* context)
        : nretry_(0), cli_(cli), connOpts_(connOpts), context_(context) {}

    void add_topics(std::string topic) { sub_topics.push_back(topic); }
};

enum AmsRfidState {
    AMS_RFID_INIT,
    AMS_RFID_LOADING,
    AMS_REID_DONE,
};

enum AmsStep {
    AMS_STEP_INIT,
    AMS_STEP_HEAT_EXTRUDER,
    AMS_STEP_LOADING,
    AMS_STEP_COMPLETED,
};

enum AmsRoadPosition {
    AMS_ROAD_POSITION_TRAY,     // filament at tray
    AMS_ROAD_POSITION_TUBE,     // filament at tube
    AMS_ROAD_POSITION_HOTEND,   // filament at hotend
};

enum AmsStatusMain {
    AMS_STATUS_MAIN_IDLE                = 0x00,
    AMS_STATUS_MAIN_FILAMENT_CHANGE     = 0x01,
    AMS_STATUS_MAIN_RFID_IDENTIFYING    = 0x02,
    AMS_STATUS_MAIN_ASSIST              = 0x03,
    AMS_STATUS_MAIN_CALIBRATION         = 0x04,
    AMS_STATUS_MAIN_SELF_CHECK          = 0x10,
    AMS_STATUS_MAIN_DEBUG               = 0x20,
    AMS_STATUS_MAIN_UNKNOWN             = 0xFF,
};

class AmsTray {
public:
    AmsTray(std::string tray_id) {
        is_bbl          = false;
        id              = tray_id;
        road_position   = AMS_ROAD_POSITION_TRAY;
        step_state      = AMS_STEP_INIT;
        rfid_state      = AMS_RFID_INIT;
    }

    static int hex_digit_to_int(const char c)
    {
        return (c >= '0' && c <= '9') ? int(c - '0') : (c >= 'A' && c <= 'F') ? int(c - 'A') + 10 : (c >= 'a' && c <= 'f') ? int(c - 'a') + 10 : -1;
    }

    static wxColour decode_color(const std::string &color)
    {
        std::array<int, 3> ret = {0, 0, 0};
        const char *       c   = color.data();
        if (color.size() == 8) {
            for (size_t j = 0; j < 3; ++j) {
                int digit1 = hex_digit_to_int(*c++);
                int digit2 = hex_digit_to_int(*c++);
                if (digit1 == -1 || digit2 == -1) break;
                ret[j] = float(digit1 * 16 + digit2);
            }
        }
        return wxColour(ret[0], ret[1], ret[2]);
    }

    std::string     id;
    std::string     tag_uid;    // tag_uid
    std::string     setting_id; // tray_info_idx
    std::string     type;
    std::string     sub_brands;
    std::string     color;
    std::string     weight;
    std::string     diameter;
    std::string     temp;
    std::string     time;
    std::string     bed_temp_type;
    std::string     bed_temp;
    std::string     hot_end_temp_max;
    std::string     hot_end_temp_limit; // hot_endtemp_limit, nozzle temperature
    std::string     xcam_info;
    std::string     uuid;

    wxColour        wx_color;
    bool            is_bbl;
    bool            is_exists = false;

    AmsRoadPosition road_position;
    AmsStep         step_state;
    AmsRfidState    rfid_state;

    void update_color_from_str(std::string color);
};


class Ams {
public:
    Ams(std::string ams_id) {
        id = ams_id;
    }
    std::string   id;
    bool          startup_read_opt{true};
    bool          tray_read_opt{false};
    bool          is_exists{false};
    std::map<std::string, AmsTray*> trayList;
};

enum PrinterFirmwareType {
    FIRMWARE_TYPE_ENGINEER = 0,
    FIRMWARE_TYPE_PRODUCTION,
    FIRMEARE_TYPE_UKNOWN,
};


class FirmwareInfo
{
public:
    std::string module_type;    // ota or ams
    std::string version;
    std::string url;
    std::string name;
    std::string description;
};

enum ModuleID {
    MODULE_UKNOWN       = 0x00,
    MODULE_01           = 0x01,
    MODULE_02           = 0x02,
    MODULE_MC           = 0x03,
    MODULE_04           = 0x04,
    MODULE_MAINBOARD    = 0x05,
    MODULE_06           = 0x06,
    MODULE_AMS          = 0x07,
    MODULE_TH           = 0x08,
    MODULE_09           = 0x09,
    MODULE_10           = 0x0A,
    MODULE_11           = 0x0B,
    MODULE_XCAM         = 0x0C,
    MODULE_13           = 0x0D,
    MODULE_14           = 0x0E,
    MODULE_15           = 0x0F,
    MODULE_MAX          = 0x10
};

enum HMSMessageLevel {
    HMS_UNKNOWN = 0,
    HMS_FATAL   = 1,
    HMS_SERIOUS = 2,
    HMS_COMMON  = 3,
    HMS_INFO    = 4,
    HMS_MSG_LEVEL_MAX,
};

class HMSItem
{
public:
    ModuleID        module_id;
    unsigned        module_num;
    unsigned        part_id;
    unsigned        reserved;
    HMSMessageLevel msg_level = HMS_UNKNOWN;
    int             msg_code = 0;
    bool parse_hms_info(unsigned attr, unsigned code);
    static wxString get_module_name(ModuleID module_id);
    static wxString get_hms_msg_level_str(HMSMessageLevel level);
};


#define UpgradeNoError          0
#define UpgradeDownloadFailed   -1
#define UpgradeVerfifyFailed    -2
#define UpgradeFlashFailed      -3
#define UpgradePrinting         -4


   
class MachineObject
{
private:
    AccountManager& acc_;

    bool check_valid_ip();
public:
    typedef std::function<void(std::string topic, std::string payload)> MsgFn;
    typedef std::function<void()> UploadedFn;
    typedef std::function<void(int progress)> UploadProgressFn;
    typedef std::function<void(std::string error)> ErrorFn;
    typedef std::function<void(std::string body)> CompletedFn;
    typedef std::function<void(int result, std::string info)> ResultFn;

    enum CONNECTION_TYPE {
        CONNECTION_DEFAULT = 0,
        CONNECTION_LAN = 1,
        CONNECTION_WAN = 2,
    };

    enum CONNECTION_STATE {
        STATE_DISCONNECTED = 0,
        STATE_CONNECTING = 1,
        STATE_CONNECTED = 2,
    };

    enum LIGHT_EFFECT {
        LIGHT_EFFECT_ON,
        LIGHT_EFFECT_OFF,
        LIGHT_EFFECT_FLASHING,
        LIGHT_EFFECT_UNKOWN,
    };

    enum FanType {
        COOLING_FAN = 1,
        BIG_COOLING_FAN = 2,
        CHAMBER_FAN = 3,
    };

    enum UpgradingDisplayState {
        UpgradingUnavaliable = 0,
        UpgradingAvaliable = 1,
        UpgradingInProgress = 2,
        UpgradingFinished = 3
    };

    class ModuleVersionInfo
    {
    public:
        std::string name;
        std::string sn;
        std::string hw_ver;
        std::string sw_ver;
    };

    static inline int m_sequence_id = 20000;
    static PRINTER_TYPE parse_printer_type(std::string type_str);
    static PRINTER_TYPE parse_iot_printer_type(std::string type_str);
    static PRINTER_TYPE parse_preset_printer_type(std::string type_str);
    static std::string get_preset_printer_model_name(PRINTER_TYPE printer_type);

    std::string get_printer_type_string();
    wxString get_printer_type_display_str();

    MachineObject(AccountManager& acc, std::string name, std::string id, std::string ip);
    /* properties */
    std::string dev_name;
    std::string dev_ip;
    std::string dev_id;
    PRINTER_TYPE printer_type = PRINTER_3DPrinter_UKNOWN;
    std::string product_name;       // set by iot service, get /user/print

    std::string bind_user_name;
    std::string bind_user_id;
    bool is_alive;          /* local alive */
    time_t last_alive;
    bool m_is_online;
    std::chrono::system_clock::time_point   last_update_time;   /* last received print data from machine */

    /* Ams Properties */
    std::map<std::string, Ams*> amsList;    // key: ams[id], start with 0
    long  ams_exist_bits;
    long  tray_exist_bits;
    long  tray_is_bbl_bits; // valid bits
    long  tray_read_done_bits;
    AmsStatusMain ams_status_main;
    int   ams_status_sub;
    int   ams_version;

    std::string m_ams_id;           // local ams  : "0" ~ "3"
    std::string m_tray_id;          // local tray id : "0" ~ "3"
    std::string m_tray_now;         // tray_now : "0" ~ "15" or "255"
    std::string m_tray_tar;         // tray_tar : "0" ~ "15" or "255"
    void _parse_tray_now(std::string tray_now);
    bool    is_ams_need_update;

    inline bool is_ams_unload() { return m_tray_tar.compare("255") == 0; }
    Ams*     get_curr_Ams();
    AmsTray* get_curr_tray();
    AmsTray *get_ams_tray(std::string ams_id, std::string tray_id);
    // parse amsStatusMain and ams_status_sub
    void _parse_ams_status(int ams_status);
    static bool is_bbl_filament(std::string tag_uid);
    

    /* temperature */
    float  nozzle_temp;
    float  nozzle_temp_target;
    float  bed_temp;
    float  bed_temp_target;
    float  chamber_temp;
    float  frame_temp;

    /* cooling */
    int     heatbreak_fan_speed = 0;
    int     cooling_fan_speed = 0;
    int     big_fan1_speed = 0;
    int     big_fan2_speed = 0;

    /* signals */
    std::string wifi_signal;

    /* lights */
    LIGHT_EFFECT chamber_light;
    LIGHT_EFFECT work_light;

    /* upgrade */
    bool upgrade_force_upgrade { false };
    bool upgrade_new_version { false };
    bool upgrade_consistency_request;
    int upgrade_display_state = 0;          // 0 : upgrade unavailable, 1: upgrade idle, 2: upgrading, 3: upgrade_finished
    PrinterFirmwareType       firmware_type; // engineer|production
    std::string upgrade_progress;
    std::string upgrade_message;
    std::string upgrade_status;
    std::string upgrade_module;
    std::string ams_new_version_number;
    std::string ota_new_version_number;
    std::map<std::string, ModuleVersionInfo> module_vers;
    int upgrade_err_code = 0;
    std::vector<FirmwareInfo> firmware_list;

    std::string get_firmware_type_str();
    bool is_in_upgrading();
    bool is_upgrading_avalable();
    int get_upgrade_percent();
    std::string get_ota_version();
    wxString get_upgrade_result_str(int upgrade_err_code);
    // key: ams_id start as 0,1,2,3
    std::map<int, ModuleVersionInfo> get_ams_version();

    /* printing */
    int     mc_print_stage;
    int     mc_print_sub_stage;
    int     mc_print_error_code;
    int     mc_print_line_number;
    int     mc_print_percent;       /* left print progess in percent */
    int     mc_left_time;           /* left time in seconds */

    std::vector<int> stage_list_info;
    int stage_curr = 0;

    wxString get_curr_stage();

    /* iot printing status */
    std::string iot_printing_taskname;
    std::string iot_task_id;
    std::string iot_profile_id;
    std::string iot_project_id;
    std::string iot_task_status;
    std::string subtask_name;

    std::string print_status;   /* enum string: FINISH, RUNNING, PAUSE, INIT, FAILED */
    PrintingSpeedLevel printing_speed_lvl;
    int                printing_speed_mag;
    PrintingSpeedLevel _parse_printing_speed_lvl(int lvl);

    /* HMS */
    std::vector<HMSItem>    hms_list;

    /* mqtt connections */
    CONNECTION_TYPE conn_type;
    CONNECTION_STATE conn_state;
    std::string mqtt_uuid;
    int mqtt_uuid_bytes;
    mqtt::async_client* mqtt_cli;
    mqtt::connect_options mqtt_opt;
    machine_conn_callback* mqtt_cb;
    SuccessFn  successFn;
    FailedFn failedFn;
    LostFn lostFn;
    json_diff print_json;


    /* local communicate */
    std::string login_ticket;
    LocalClient *local_client { nullptr };  // client for bind progress
    LocalClient *debug_client { nullptr };  // client for debug connections


    /* Msg for display MsgFn */
    MsgFn msg_send_fn;
    MsgFn msg_recv_fn;

    /* Project Task and Sub Task */
    BBLProject* project_;
    BBLProfile* profile_;
    BBLTask* task_;
    BBLSubTask* subtask_;
    BBLSubTask* temptask_;
    std::string obj_subtask_id;     // subtask_id == 0 for sdcard
    bool is_sdcard_printing();
    
    /* command commands */
    int command_get_version();
    int command_request_push_all();

    /* command upgrade */
    int command_upgrade_confirm();
    int command_upgrade_firmware(FirmwareInfo info);

    /* control apis */
    int command_xyz_abs();
    int command_auto_leveling();
    int command_go_home();
    int command_control_fan(FanType fan_type, bool on_off);
    int command_task_abort();
    int command_task_pause();
    int command_task_resume();
    int command_set_bed(int temp);
    int command_set_nozzle(int temp);

    int command_ams_switch(std::string tray_id, int old_temp = 210, int new_temp = 210);
    int command_ams_user_settings(int ams_id, bool start_read_opt, bool tray_read_opt);
    int command_ams_calibrate(int ams_id);
    int command_ams_filament_settings(int ams_id, int tray_id, std::string setting_id, std::string tray_color, int bed_temp);
    int command_ams_select_tray(std::string tray_id);
    int command_ams_refresh_rfid(std::string tray_id);

    inline std::string light_effect_str(LIGHT_EFFECT effect) {
        switch (effect)
        {
        case LIGHT_EFFECT::LIGHT_EFFECT_ON:
            return "on";
        case LIGHT_EFFECT_OFF:
            return "off";
        case LIGHT_EFFECT_FLASHING:
            return "flashing";
        default:
            return "unknown";
        }
    }
    inline LIGHT_EFFECT light_effect_parse(std::string effect_str) {
        if (effect_str.compare("on") == 0)
            return LIGHT_EFFECT_ON;
        else if (effect_str.compare("off") == 0)
            return LIGHT_EFFECT_OFF;
        else if (effect_str.compare("flashing") == 0)
            return LIGHT_EFFECT_FLASHING;
        else
            return LIGHT_EFFECT_UNKOWN;
    }

    int command_set_chamber_light(LIGHT_EFFECT effect, int on_time = 500, int off_time = 500, int loops = 1, int interval = 1000);
    int command_set_work_light(LIGHT_EFFECT effect, int on_time = 500, int off_time = 500, int loops = 1, int interval = 1000);

    // set printing speed
    int command_set_printing_speed(PrintingSpeedLevel lvl);

    // axis string is X, Y, Z, E
    int command_axis_control(std::string axis, double unit = 1.0f, double value = 1.0f, int speed = 3000);

    // device bind and unbind
    int command_bind();
    int command_unbind();

    // new bind progress
    int command_new_bind();
    std::string build_login_request();
    int _parse_login_report(std::string json_str, std::string fail_reason);

    /* machine local client apis */
    int local_connect();
    int local_disconnect();

    /* machine mqtt apis */
    void set_callbacks(SuccessFn sFn, FailedFn fFn, LostFn lFn);
    int connect();
    int disconnect();
    int reconnect();

    bool is_connected();
    void set_online_state(bool on_off);
    bool is_online() { return m_is_online; }


    void set_msg_send_fn(MsgFn fn) { msg_send_fn = std::move(fn); }
    void set_msg_recv_fn(MsgFn fn) { msg_recv_fn = std::move(fn); }
    int publish_json(std::string json_str, ResultFn resFn = nullptr, int qos = 0);
    int parse_json(std::string topic, std::string payload);
    int publish_gcode(std::string gcode_str);

    int send_print_task(BBLTask* task);
    int send_wan_print_task(BBLTask* task);

    int send_print_subtask(BBLSubTask* task, UploadedFn cFn = nullptr, UploadProgressFn proFn = nullptr, ErrorFn errFn = nullptr);
    int send_lan_print_subtask(BBLSubTask* task, UploadedFn cFn = nullptr, UploadProgressFn proFn = nullptr, ErrorFn errFn = nullptr);
    int send_wan_print_subtask(BBLSubTask* task, UploadedFn cFn = nullptr, UploadProgressFn proFn = nullptr, ErrorFn errFn = nullptr);
    BBLSubTask* get_subtask();
    BBLSliceInfo* get_slice_info(std::string plate_idx);
    void update_subtask(std::string subtask_id);
    void update_task(std::string task_id);
    void update_profile(std::string project_id, std::string profile_id);

    /* iot operation apis */
    void request_bind(ResultFn fn, bool force_bind = false);
    void request_unbind(ResultFn fn);

    bool get_firmware_info();

    /* common apis */
    inline bool is_local() { return !dev_ip.empty(); }
    void set_bind_status(std::string status);
    void set_connect_state(CONNECTION_STATE state);
    std::string get_bind_str();
    bool can_print();
    bool can_resume();
    bool can_pause();
    bool can_abort();
    bool is_printing_finished();
    void reset();

    void set_print_state(std::string status);
    
    /* static apis */
    static std::string build_report_topic(std::string dev_id);
};

class DeviceManager
{
private:
    std::mutex m_devicelist_mutex;
    bool m_check_alive_quit = false;
    const double ALIVE_TIMEOUT = 30.0;
    boost::thread m_device_check_alive;
    AccountManager& acc_;
    CommuBackend& backend_;

public:
    DeviceManager(AccountManager& acc, CommuBackend& backend);
    ~DeviceManager();

    std::mutex listMutex;
    std::map<std::string, MachineObject*> localMachineList;     /* dev_id -> MachineObject* */
    std::string default_machine;    /* dev_id */

    /* create machine or update machine properties */
    void on_machine_alive(std::string dev_name, std::string dev_id, std::string dev_ip, std::string printer_type_str = "", std::string printer_signal = "");
    /* disconnect all machine connections */
    void disconnect_all();
    void query_bind_status(AccountManager::CompletedFn cFn, AccountManager::ErrorFn errFn);

    MachineObject* get_default();   /* return default machine */
    std::map<std::string, MachineObject*> get_all_machine_list();
    std::map<std::string, MachineObject*> get_user_machine_list();


    void check_alive();
};

} // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
