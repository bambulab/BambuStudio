#ifndef slic3r_DeviceManager_hpp_
#define slic3r_DeviceManager_hpp_

#include <map>
#include <mutex>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <unordered_set>
#include <boost/thread.hpp>
#include <boost/nowide/fstream.hpp>
#include "nlohmann/json.hpp"
#include "libslic3r/ProjectTask.hpp"
#include "slic3r/Utils/json_diff.hpp"
#include "boost/bimap/bimap.hpp"
#include "CameraPopup.hpp"
#include "libslic3r/Calib.hpp"
#include "libslic3r/Utils.hpp"

#include "DeviceCore/DevDefs.h"
#include "DeviceCore/DevConfigUtil.h"
#include "DeviceCore/DevFirmware.h"
#include "DeviceCore/DevUtil.h"
#include "DeviceCore/DevCalib.h"

#include "DeviceErrorDialog.hpp"

#include <wx/object.h>

#define USE_LOCAL_SOCKET_BIND 0

#define DISCONNECT_TIMEOUT      30000.f     // milliseconds
#define PUSHINFO_TIMEOUT        15000.f     // milliseconds
#define TIMEOUT_FOR_STRAT       20000.f     // milliseconds
#define TIMEOUT_FOR_KEEPALIVE   5* 60 * 1000.f     // milliseconds
#define REQUEST_PUSH_MIN_TIME   3000.f     // milliseconds
#define REQUEST_START_MIN_TIME  15000.f     // milliseconds
#define EXTRUSION_OMIT_TIME     20000.f     // milliseconds
#define HOLD_TIMEOUT            10000.f     // milliseconds

#define BED_TEMP_LIMIT          120

#define HOLD_COUNT_MAX          3
#define HOLD_COUNT_CAMERA       6

#define HOLD_TIME_3SEC     3 // 3 seconds
#define HOLD_TIME_6SEC     6 // 6 seconds

#define GET_VERSION_RETRYS      10
#define RETRY_INTERNAL          2000

#define SUBSCRIBE_RETRY_COUNT   5

using namespace nlohmann;
namespace Slic3r {
class Print;

namespace GUI
{
class DeviceErrorDialog; // Previous definitions
class Plater;
}

class NetworkAgent;

enum class CalibSendStatus {
    IDLE = 0,
    SENDING,
    FAILED,
};

// Previous definitions
class DevAms;
class DevAmsTray;
class DevAxis;
class DevBed;
class DevChamber;
class DevConfig;
class DevCtrl;
class DevExtensionTool;
class DevExtderSystem;
class DevFan;
class DevFilaSystem;
class DevPrintOptions;
class DevHMS;
class DevInfo;
class DevLamp;
class DevNozzleSystem;
class DevNozzleRack;
class DeviceManager;
class DevStatus;
class DevStorage;
class DevUpgrade;
struct DevPrintTaskRatingInfo;
struct DevNozzle;


class MachineObject
{
private:
    NetworkAgent *    m_agent{nullptr};
    DeviceManager*    m_manager{ nullptr };
    std::shared_ptr<int> m_token = std::make_shared<int>(1);

    /* properties */
    std::string dev_name;
    std::string dev_ip;
    std::string access_code;
    std::string user_access_code;
    std::shared_ptr<DevInfo>  m_dev_info;

    // type, time stamp, delay
    std::vector<std::tuple<std::string, uint64_t, uint64_t>> message_delay;

    // the latest nozzle mapping
    DevNozzleMappingResult m_auto_nozzle_mapping;

    /*parts*/
    std::shared_ptr<DevAxis>    m_axis;
    std::shared_ptr<DevChamber> m_chamber;
    DevLamp*          m_lamp;
    std::shared_ptr<DevExtensionTool> m_extension_tool;
    DevExtderSystem*  m_extder_system;
    DevNozzleSystem*  m_nozzle_system;
    std::shared_ptr<DevFilaSystem> m_fila_system;
    DevFan*           m_fan;
    DevBed *          m_bed;
    DevStorage*       m_storage;
    DevCalib*         m_calib;

    /*Ctrl*/
    DevCtrl* m_ctrl;

    /*Print Options/Speed*/
    DevPrintOptions* m_print_options;

    /*Upgrade*/
    std::shared_ptr<DevUpgrade> m_upgrade;

    /*Status*/
    DevStatus* m_status;

    /*HMS*/
    DevHMS* m_hms_system;

    /*Config*/
    DevConfig* m_config;

public:
    MachineObject(DeviceManager* manager, NetworkAgent* agent, std::string name, std::string id, std::string ip);
    ~MachineObject();

public:
    enum ActiveState {
        NotActive,
        Active,
        UpdateToDate
    };

    enum PrintOption {
        PRINT_OP_AUTO_RECOVERY = 0,
        PRINT_OP_MAX,
    };

public:

    /* static members and functions */
    static inline int m_sequence_id = STUDIO_START_SEQ_ID;

    /* properties */
    std::string get_dev_name() const { return dev_name; }
    void set_dev_name(std::string val) { dev_name = val; }

    std::string get_dev_ip() const { return dev_ip; }
    void set_dev_ip(std::string ip) { dev_ip = ip;  }

    std::string get_dev_id() const;
    void set_dev_id(std::string val);

    std::string connection_type() const;
    bool        is_lan_mode_printer() const;
    bool        is_cloud_mode_printer() const;

    bool        local_use_ssl_for_mqtt { true };
    bool        local_use_ssl_for_ftp { true };
    std::string get_ftp_folder();

    int         subscribe_counter{3};
    std::string dev_connection_name;    /* lan | eth */

    /* message time*/
    std::chrono::system_clock::time_point last_cloud_msg_time_;
    std::chrono::system_clock::time_point last_lan_msg_time_;

    bool HasRecentCloudMessage();
    bool HasRecentLanMessage();

    /*access code*/
    bool has_access_right() const { return !get_access_code().empty(); }
    std::string get_access_code() const;
    void set_access_code(std::string code, bool only_refresh = true);

    /*user access code*/
    void set_user_access_code(std::string code, bool only_refresh = true);
    void erase_user_access_code();
    std::string get_user_access_code() const;

    void record_user_access_dev_ip();
    void erase_user_access_dev_ip();

    //PRINTER_TYPE printer_type = PRINTER_3DPrinter_UKNOWN;
    std::string printer_type;       /* model_id */
    std::string   get_show_printer_type() const;
    PrinterSeries get_printer_series() const;
    PrinterArch get_printer_arch() const;
    std::string get_printer_ams_type() const;
    wxString get_printer_type_display_str() const;
    std::string get_auto_pa_cali_thumbnail_img_str() const;

    // check printer device series
    std::string get_printer_series_str() const;

    static bool is_series_n(const std::string& series_str);
    static bool is_series_p(const std::string& series_str);
    static bool is_series_x(const std::string& series_str);
    static bool is_series_o(const std::string& series_str);

    bool is_series_n() const;
    bool is_series_p() const;
    bool is_series_x() const;
    bool is_series_o() const;

    void reload_printer_settings();
    std::string get_printer_thumbnail_img_str() const;

    std::string bind_user_name;
    std::string bind_user_id;
    std::string bind_sec_link;
    std::string bind_ssdp_version;
    std::string bind_state;     /* free | occupied */
    bool is_avaliable() { return bind_state == "free"; }

    bool m_is_online;
    bool m_set_ctt_dlg{ false };
    void set_ctt_dlg( wxString text);
    int  parse_msg_count = 0;
    int  keep_alive_count = 0;
    std::chrono::system_clock::time_point   last_update_time;   /* last received print data from machine */
    std::chrono::system_clock::time_point   last_utc_time;   /* last received print data from machine */
    std::chrono::system_clock::time_point   last_keep_alive;    /* last received print data from machine */
    std::chrono::system_clock::time_point   last_push_time;     /* last received print push from machine */
    std::chrono::system_clock::time_point   last_request_push;  /* last received print push from machine */
    std::chrono::system_clock::time_point   last_request_start; /* last received print push from machine */

    bool device_cert_installed = false;

    int m_active_state = 0; // 0 - not active, 1 - active, 2 - update-to-date
    bool is_tunnel_mqtt = false;

    //AmsTray vt_tray;                        // virtual tray
    long  ams_exist_bits = 0;
    long  tray_exist_bits = 0;
    long  tray_is_bbl_bits = 0;
    long  tray_read_done_bits = 0;
    long  tray_reading_bits = 0;
    bool  ams_air_print_status { false };
    bool  ams_support_virtual_tray { true };
    time_t ams_user_setting_start = 0;
    time_t ams_switch_filament_start = 0;
    AmsStatusMain ams_status_main;
    int   ams_status_sub;
    int   ams_version = 0;

    std::chrono::system_clock::time_point last_extrusion_cali_start_time;
    int extrusion_cali_set_tray_id = -1;
    std::chrono::system_clock::time_point extrusion_cali_set_hold_start;
    std::string  extrusion_cali_filament_name;

    bool is_in_extrusion_cali();
    bool is_extrusion_cali_finished();

    /* Networking */
    NetworkAgent *get_agent() const { return m_agent; }

    /* AMS */
    DevAms*     get_curr_Ams();
    DevAmsTray* get_curr_tray();
    DevAmsTray* get_ams_tray(std::string ams_id, std::string tray_id);;

    std::string  get_filament_id(std::string ams_id, std::string tray_id) const;
    std::string  get_filament_type(const std::string& ams_id, const std::string& tray_id) const;
    std::string  get_filament_display_type(const std::string& ams_id, const std::string& tray_id) const;

    // parse amsStatusMain and ams_status_sub
    void _parse_ams_status(int ams_status);

    bool is_target_slot_unload() const;
    bool can_unload_filament();

    void get_ams_colors(std::vector<wxColour>& ams_colors);

    /*extruder*/
    bool is_main_extruder_on_left() const { return false;  } // only means the extruder is on the left hand when extruder id is 0
    bool is_multi_extruders() const;
    int  get_extruder_id_by_ams_id(const std::string& ams_id);

    /* nozzle */
    DevNozzle get_nozzle_by_id_code(int id_code) const;
    DevNozzle get_nozzle_by_sn(const std::string& sn) const;

    // auto nozzle mapping
    DevNozzleMappingResult get_nozzle_mapping_result() const { return m_auto_nozzle_mapping; }
    void set_manual_nozzle_mapping(int fila_id, int nozzle_pos_id) { m_auto_nozzle_mapping.SetManualNozzleMapping(this, fila_id, nozzle_pos_id); };// nozzle_pos_id is O\0x10\0x20\0x30...
    void clear_auto_nozzle_mapping() { m_auto_nozzle_mapping.Clear(); }
    int ctrl_get_auto_nozzle_mapping(Slic3r::GUI::Plater* plater, const std::vector<FilamentInfo>& ams_mapping, int flow_cali_opt, int pa_value);

    /* ams settings*/
    std::optional<bool> IsDetectOnInsertEnabled() const;
    //bool IsDetectOnPowerupEnabled() const { return m_enable_detect_on_powerup; }
    //bool IsDetectRemainEnabled() const { return m_enable_detect_remain; }
    //bool IsAutoRefillEnabled() const { return m_enable_auto_refill; }

    /* E3D has extra nozzle flow type info */
    bool has_extra_flow_type{false};

    [[nodiscard]] bool is_nozzle_flow_type_supported() const { return is_enable_np | has_extra_flow_type; }
    [[nodiscard]] wxString get_nozzle_replace_url() const;

    /*online*/
    bool   online_rfid;
    bool   online_ahb;
    int    online_version = -1;
    int    last_online_version = -1;

    /* temperature */
    float  frame_temp;

    /* signals */
    std::string wifi_signal;
    std::string link_th;
    std::string link_ams;
    bool        network_wired { false };
    std::shared_ptr<DevInfo> GetInfo() const { return m_dev_info; }

    /* parts */
    DevExtderSystem* GetExtderSystem() const { return m_extder_system; }
    std::weak_ptr<DevExtensionTool> GetExtensionTool() const { return m_extension_tool; }

    DevNozzleSystem*               GetNozzleSystem() const { return m_nozzle_system;}
    std::shared_ptr<DevNozzleRack> GetNozzleRack() const;;

    std::shared_ptr<DevFilaSystem>   GetFilaSystem() const { return m_fila_system;}
    bool             HasAms() const;

    std::shared_ptr<DevAxis>    GetAxis() const { return m_axis; }
    std::shared_ptr<DevChamber> GetChamber() const { return m_chamber; }
    DevLamp*        GetLamp()   const { return m_lamp; }
    DevFan*         GetFan()    const { return m_fan; }
    DevBed *        GetBed()    const { return m_bed; };
    DevStorage*     GetStorage()const { return m_storage; }
    DevCalib*       GetCalib()  const { return m_calib; }
    DevStatus*      GetStatus() const { return m_status; }; /* status*/

    DevCtrl*   GetCtrl() const { return m_ctrl; }       /* ctrl*/
    DevHMS*    GetHMS() const { return m_hms_system; }   /* hms*/
    DevConfig* GetConfig() const { return m_config; } /* config*/

    DevPrintOptions*      GetPrintOptions() const { return m_print_options; } /* print options */
    DevPrintingSpeedLevel GetPrintingSpeedLevel() const; /* print speed */

    std::weak_ptr<DevUpgrade> GetUpgrade() const { return m_upgrade;}

    /* upgrade */
    std::string ams_new_version_number;
    std::string ahb_new_version_number;
    int get_version_retry = 0;

    DevFirmwareVersionInfo air_pump_version_info;
    DevFirmwareVersionInfo laser_version_info;
    DevFirmwareVersionInfo cutting_module_version_info;
    DevFirmwareVersionInfo extinguish_version_info;
    DevFirmwareVersionInfo rotary_version_info;
    std::map<std::string, DevFirmwareVersionInfo> module_vers;
    std::vector<FirmwareInfo> firmware_list;

    std::string get_firmware_type_str();
    std::string get_lifecycle_type_str();
    bool is_in_upgrading() const;
    std::string get_ota_version();
    bool check_version_valid();
    // key: ams_id start as 0,1,2,3
    std::map<int, DevFirmwareVersionInfo> get_ams_version();

    void clear_version_info();
    void store_version_info(const DevFirmwareVersionInfo& info);

    /* printing */
    std::string print_type;
    //float   nozzle { 0.0f };        // default is 0.0f as initial value
    bool    is_220V_voltage { false };

    int     mc_print_stage;
    int     mc_print_sub_stage;
    int     mc_print_error_code;
    int     mc_print_line_number;
    int     mc_print_percent;       /* left print progess in percent */
    int     mc_left_time;           /* left time in seconds */
    int     last_mc_print_stage;
    int     m_home_flag = 0;
    int     hw_switch_state;
    bool    is_system_printing();

    int     print_error;
    std::string m_print_error_img_id;
    static std::string get_error_code_str(int error_code);
    std::string get_print_error_str() const { return MachineObject::get_error_code_str(this->print_error); }

    std::unordered_set<GUI::DeviceErrorDialog*> m_command_error_code_dlgs;
    void  add_command_error_code_dlg(int command_err, json action_json=json{});

    int     curr_layer = 0;
    int     total_layers = 0;
    bool    is_support_layer_num { false };

    CalibSendStatus     calib_send_status{CalibSendStatus::IDLE};

    std::vector<int> stage_list_info;
    int stage_curr = 0;
    int stage_remaining_seconds = -1;
    int m_push_count = 0;
    int m_full_msg_count = 0; /*the full message count, there are full or diff messages from network*/
    bool calibration_done { false };

    bool is_filament_at_extruder();

    wxString get_curr_stage();
    int get_curr_stage_idx();
    int get_stage_remaining_seconds() const { return stage_remaining_seconds; }

    bool is_in_calibration();
    bool is_calibration_running();
    bool is_calibration_done();

    void parse_state_changed_event();
    void parse_home_flag(int flag);

    /* printing status */
    std::string print_status;      /* enum string: FINISH, SLICING, RUNNING, PAUSE, INIT, FAILED */
    int queue_number = 0;
    std::string iot_print_status;  /* iot */
    int                printing_speed_mag = 100;
    int get_bed_temperature_limit();
    bool is_filament_installed();

    /* camera */
    bool has_ipcam { false };
    bool camera_recording { false };
    bool camera_recording_when_printing { false };
    bool camera_timelapse { false };
    time_t  camera_recording_ctl_start = 0;
    int  camera_timelapse_hold_count = 0;
    int  camera_resolution_hold_count = 0;
    std::string camera_resolution            = "";
    std::vector<std::string> camera_resolution_supported;
    std::string local_rtsp_url;
    std::string tutk_state;
    enum LiveviewLocal {
        LVL_None,
        LVL_Disable,
        LVL_Local,
        LVL_Rtsps,
        LVL_Rtsp
    } liveview_local{ LVL_None };
    enum LiveviewRemote {
        LVR_None,
        LVR_Tutk,
        LVR_Agora,
        LVR_TutkAgora
    } liveview_remote{ LVR_None };
    enum FileLocal {
        FL_None,
        FL_Local
    } file_local{ FL_None };
    enum FileRemote {
        FR_None,
        FR_Tutk,
        FR_Agora,
        FR_TutkAgora
    } file_remote{ FR_None };


    enum DoorOpenCheckState : int
    {
        DOOR_OPEN_CHECK_DISABLE            = 0,/*do nothing*/
        DOOR_OPEN_CHECK_ENABLE_WARNING     = 1,/*warning*/
        DOOR_OPEN_CHECK_ENABLE_PAUSE_PRINT = 2,/*pause print*/
    };

    bool        file_model_download{false};
    bool        virtual_camera{false};
    bool        m_has_timelapse_kit{false};
    // part skip
    std::vector<int> m_partskip_ids;

    /*target from Studio-SwitchBoard, default to INVALID_NOZZLE_ID if no switching control from PC*/
    int targ_nozzle_id_from_pc = INVALID_EXTRUDER_ID;

    //supported features


    /*PA flow calibration is using in sending print*/
    bool is_support_pa_calibration{false};
    bool is_support_flow_calibration{false};

    bool is_support_send_to_sdcard {false};
    bool is_support_filament_backup{false};
    bool is_support_timelapse{false};
    bool is_support_update_remain{false};
    bool is_support_update_remain_hide_display{ false};
    int  is_support_bed_leveling = 0;/*0: false; 1; on/off 2: auto/on/off*/
    bool is_support_ams_humidity {false};
    bool is_support_1080dpi {false};
    bool is_support_cloud_print_only {false};
    bool is_support_command_ams_switch{false};
    bool is_support_mqtt_alive {false};
    bool is_support_tunnel_mqtt{false};
    bool is_support_motor_noise_cali{false};
    bool is_support_wait_sending_finish{false};
    bool is_support_user_preset{false};
    bool is_support_air_print_detection{false};
    bool is_support_agora{false};
    bool is_support_upgrade_kit{false};
    bool is_support_filament_setting_inprinting{false};
    bool is_support_internal_timelapse { false };// fun[28], support timelapse without SD card
    bool is_support_brtc{false};                 // fun[31], support tcp and upload protocol
    bool is_support_ext_change_assist{false};
    bool is_support_partskip{false};
    bool is_support_refresh_nozzle{false};
    bool is_support_fila_change_abort{false};


    // fun2
    bool is_support_print_with_emmc{false};
    bool is_support_pa_mode{false};
    bool is_support_remote_dry = false;

    bool installed_upgrade_kit{false};
    int  bed_temperature_limit = -1;

    /*nozzle temp range*/
    std::vector<int>    nozzle_temp_range;

    /*temp temp range*/
    std::vector<int>    bed_temp_range;

    /* machine mqtt apis */
    int connect(bool use_openssl = true);
    int disconnect();

    json_diff print_json;

    /* Project Task and Sub Task */
    std::string  project_id_;
    std::string  profile_id_;
    std::string  task_id_;
    std::string  subtask_id_;
    std::string  job_id_;
    std::string  last_subtask_id_;
    BBLSliceInfo* slice_info {nullptr};
    boost::thread* get_slice_info_thread { nullptr };
    boost::thread* get_model_task_thread { nullptr };

    /* job attr */
    int jobState_ = 0;

    /* key: sequence id, value: callback */

    bool is_makeworld_subtask();

    int m_plate_index { -1 };
    std::string m_gcode_file;
    int gcode_file_prepare_percent = 0;
    BBLSubTask* subtask_;
    BBLModelTask *model_task { nullptr };
    DevPrintTaskRatingInfo*  rating_info { nullptr };
    int           request_model_result             = 0;
    bool          get_model_mall_result_need_retry = false;

    std::string obj_subtask_id;     // subtask_id == 0 for sdcard
    std::string subtask_name;
    bool is_sdcard_printing();
    bool is_timelapse();
    bool is_recording_enable();
    bool is_recording();


    int get_liveview_remote();
    int get_file_remote();

    std::string parse_version();
    void parse_version_func();

    /* quick check*/
    bool canEnableTimelapse(wxString& error_message) const;

    /* command commands */
    int command_get_version(bool with_retry = true);
    int command_request_push_all(bool request_now = false);
    int command_pushing(std::string cmd);
    int command_clean_print_error(std::string task_id, int print_error);
    int command_clean_print_error_uiop(int print_error);
    int command_set_printer_nozzle(std::string nozzle_type, float diameter);
    int command_set_printer_nozzle2(int id, std::string nozzle_type, float diameter);
    int command_get_access_code();
    int command_ack_proceed(json& proceed);

    /* control apis */

    int command_task_abort();
    /* cancelled the job_id */
    int command_task_partskip(std::vector<int> part_ids);
    int command_task_cancel(std::string job_id);
    int command_task_pause();
    int command_task_resume();
    int command_hms_idle_ignore(const std::string &error_str, int type);
    int command_hms_resume(const std::string& error_str, const std::string& job_id);
    int command_hms_ignore(const std::string& error_str, const std::string& job_id);
    int command_hms_stop(const std::string &error_str, const std::string &job_id);
    /* buzzer*/
    int command_stop_buzzer();

    /* temp*/
    bool m_support_mqtt_bet_ctrl = false;
    int command_set_bed(int temp);

    int command_set_nozzle(int temp);
    int command_set_nozzle_new(int nozzle_id, int temp);
    int command_refresh_nozzle();
    int check_resume_condition();
    // ams controls
    //int command_ams_switch(int tray_index, int old_temp = 210, int new_temp = 210);
    int command_ams_change_filament(bool load, std::string ams_id, std::string slot_id, int old_temp = 210, int new_temp = 210);
    int command_ams_user_settings(bool start_read_opt, bool tray_read_opt, bool remain_flag = false);
    int command_ams_switch_filament(bool switch_filament);
    int command_ams_air_print_detect(bool air_print_detect);
    int command_ams_calibrate(int ams_id);
    int command_ams_filament_settings(int ams_id, int slot_id, std::string filament_id, std::string setting_id, std::string tray_color, std::string tray_type, int nozzle_temp_min, int nozzle_temp_max);
    int command_ams_select_tray(std::string tray_id);
    int command_ams_refresh_rfid(std::string tray_id);
    int command_ams_refresh_rfid2(int ams_id, int slot_id);
    int command_ams_control(std::string action);
    int command_ams_drying_stop();
    int command_start_extrusion_cali(int tray_index, int nozzle_temp, int bed_temp, float max_volumetric_speed, std::string setting_id = "");
    int command_stop_extrusion_cali();
    int command_extrusion_cali_set(int tray_index, std::string setting_id, std::string name, float k, float n, int bed_temp = -1, int nozzle_temp = -1, float max_volumetric_speed = -1);

    // set printing speed
    int command_set_printing_speed(DevPrintingSpeedLevel lvl);

    // set print option
    int command_set_printing_option(bool auto_recovery);

    int command_extruder_control(int nozzle_id, double val);
    // calibration printer
    bool is_support_command_calibration();
    int command_start_calibration(bool vibration, bool bed_leveling, bool xcam_cali, bool motor_noise, bool nozzle_cali, bool bed_cali, bool clumppos_cali);

    // PA calibration
    int command_start_pa_calibration(const X1CCalibInfos& pa_data, int mode = 0);  // 0: automatic mode; 1: manual mode. default: automatic mode
    int command_set_pa_calibration(const std::vector<PACalibResult>& pa_calib_values, bool is_auto_cali);
    int command_delete_pa_calibration(const PACalibIndexInfo& pa_calib);
    int command_get_pa_calibration_tab(const PACalibExtruderInfo& calib_info);
    int command_get_pa_calibration_result(float nozzle_diameter);
    int commnad_select_pa_calibration(const PACalibIndexInfo& pa_calib_info);

    // flow ratio calibration
    int command_start_flow_ratio_calibration(const X1CCalibInfos& calib_data);
    int command_get_flow_ratio_calibration_result(float nozzle_diameter);

    // camera control
    int command_ipcam_record(bool on_off);
    int command_ipcam_timelapse(bool on_off);
    int command_ipcam_resolution_set(std::string resolution);

    /* common apis */
    inline bool is_local() { return !get_dev_ip().empty(); }
    void set_bind_status(std::string status);
    std::string get_bind_str();
    bool can_print();
    bool can_resume();
    bool can_pause();
    bool can_abort();
    bool is_in_printing();
    bool is_in_printing_pause() const;
    bool is_in_prepare();
    bool is_printing_finished();
    void reset_update_time();
    void reset();
    static bool is_in_printing_status(std::string status);

    void set_print_state(std::string status);

    bool is_connected();
    bool is_connecting();
    void set_online_state(bool on_off);
    bool is_online() { return m_is_online; }
    bool is_info_ready(bool check_version = true) const;
    bool is_security_control_ready() const;
    bool is_camera_busy_off();

    std::vector<std::string> get_resolution_supported();
    std::vector<std::string> get_compatible_machine();

    /* Msg for display MsgFn */
    typedef std::function<void(std::string topic, std::string payload)> MsgFn;
    int publish_json(const json& json_item, int qos = 0, int flag = 0) ;
    int publish_json(const std::string& json_str, int qos = 0, int flag = 0) = delete;
    int cloud_publish_json(std::string json_str, int qos = 0, int flag = 0);
    int local_publish_json(std::string json_str, int qos = 0, int flag = 0);
    int parse_json(std::string tunnel, std::string payload, bool key_filed_only = false);
    int publish_gcode(std::string gcode_str);
    void update_device_cert_state(bool ready);

    static std::string setting_id_to_type(std::string setting_id, std::string tray_type);
    BBLSubTask* get_subtask();
    BBLModelTask* get_modeltask();
    void set_modeltask(BBLModelTask* task);

    void update_model_task();

    void free_slice_info();
    void update_slice_info(std::string project_id, std::string profile_id, std::string subtask_id, int plate_idx);

    bool m_firmware_valid { false };
    bool m_firmware_thread_started { false };
    std::optional<bool> m_firmware_support_print_tpu_left;
    void get_firmware_info();
    bool is_firmware_info_valid();

    /*for more extruder*/
    bool                        is_enable_np{ false };
    bool                        is_enable_ams_np{ false };

    /*vi slot data*/
    std::vector<DevAmsTray> vt_slot;
    DevAmsTray parse_vt_tray(json vtray);

    /*get ams slot info*/
    bool    contains_tray(const std::string &ams_id, const std::string &tray_id) const;
    std::optional<DevAmsTray> get_tray(const std::string &ams_id, const std::string &tray_id) const;/*use contains_tray() check first*/

    /*for parse new info*/
    bool check_enable_np(const json& print) const;
    void parse_new_info(json print);
    int  get_flag_bits(std::string str, int start, int count = 1) const;
    uint32_t get_flag_bits_no_border(std::string str, int start_idx, int count = 1) const;
    int get_flag_bits(int num, int start, int count = 1, int base = 10) const;

    /* Device Filament Check */
    struct FilamentData
    {
        std::set<std::string>                      checked_filament;
        std::string                                printer_preset_name;
        std::map<std::string, std::pair<int, int>> filament_list; // filament_id, pair<min temp, max temp>
    };
    std::map<std::string, FilamentData> m_nozzle_filament_data;
    void update_filament_list();
    void update_printer_preset_name();
    void check_ams_filament_valid();



    /* xcam door open check*/
    bool               support_door_open_check() const { return is_support_door_open_check;};
    DoorOpenCheckState get_door_open_check_state() const { return xcam_door_open_check;};
    void               command_set_door_open_check(DoorOpenCheckState state);

    /* xcam save remove print file to local*/
    bool get_save_remote_print_file_to_storage() const { return xcam__save_remote_print_file_to_storage; };
    void command_set_save_remote_print_file_to_storage(bool save);

private:

    /* xcam door open check*/
    bool is_support_door_open_check = false;
    DoorOpenCheckState xcam_door_open_check  = DoorOpenCheckState::DOOR_OPEN_CHECK_DISABLE;
    time_t xcam_door_open_check_start_time   = 0;

    /* xcam save remove print file to local*/
    bool xcam__save_remote_print_file_to_storage      = false;
    time_t xcam__save_remote_print_file_to_storage_start_time = 0;
};


// change the opacity
void change_the_opacity(wxColour& colour);
wxString get_stage_string(int stage);

}; // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
