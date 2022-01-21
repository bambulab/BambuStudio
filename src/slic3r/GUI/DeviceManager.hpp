#ifndef slic3r_DeviceManager_hpp_
#define slic3r_DeviceManager_hpp_

#include <map>
#include <vector>
#include <string>
#include <memory>
#include <boost/thread.hpp>
#include "mqtt/async_client.h"
#include "CommuBackend.hpp"
#include "AccountManager.hpp"
#include "libslic3r/ProjectTask.hpp"

namespace Slic3r {

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


class AmsTray {
public:
    AmsTray(std::string tray_id) {
        is_bbl = false;
        id = tray_id;
    }

    std::string     id;
    std::string     last_color;
    wxColour        wx_color;
    std::string     sn;
    bool            is_bbl;
    std::string     meterial;
    std::string     saturability;
    std::string     smooth;
    std::string     time;
    std::string     transmittance;
    std::string     weight;
    std::string     manufacturer;
    double          diameter;

    void update_color_from_str(std::string color);
};


class Ams {
public:
    Ams(std::string ams_id) {
        id = ams_id;
    }
    std::string                     id;
    std::map<std::string, AmsTray*> trayList;
};

   
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

    enum MachineBindStatus {
        MACHINE_BIND_UNKOWN = 0,
        MACHINE_BIND_FREE = 1,
        MACHINE_BIND_SELF = 2,
        MACHINE_BIND_OHTER = 3,
        MACHINE_BIND_ERROR = 4
    };

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

    static inline int m_sequence_id = 20000;

    MachineObject(AccountManager& acc, std::string name, std::string id, std::string ip);
    /* properties */
    std::string dev_name;
    std::string dev_ip;
    std::string dev_id;

    MachineBindStatus dev_bind_status;
    std::string bind_user_name;
    std::string bind_user_id;
    bool is_alive;      /* local alive */
    time_t last_alive;
    bool is_online;     /* wan online */

    /* Ams Properties */
    std::map<std::string, Ams*> amsList;
    int     ams_exist_bits;
    int     tray_exist_bits;
    int     tray_is_bbl_bits;
    bool    is_ams_need_update;

    /* temperature */
    float  nozzle_temp;
    float  nozzle_temp_target;
    float  bed_temp;
    float  bed_temp_target;
    float  chamber_temp;
    float  frame_temp;

    /* cooling */
    int     heatbreak_fan_speed;
    int     cooling_fan_speed;
    int     big_fan1_speed;
    int     big_fan2_speed;

    /* signals */
    std::string wifi_signal;

    /* lights */
    LIGHT_EFFECT chamber_light;
    LIGHT_EFFECT work_light;

    /* upgrade */
    bool upgrade_force_upgrade;
    bool upgrade_new_version;
    bool upgrade_consistency_request;
    std::string upgrade_progress;
    std::string upgrade_message;
    std::string upgrade_status;

    /* printing */
    int     mc_print_stage;
    int     mc_print_error_code;
    int     mc_print_line_number;
    int     mc_print_percent;       /* left print progess in percent */
    int     mc_left_time;           /* left time in seconds */

    std::string print_status;   /* enum string: FINISH, RUNNING, PAUSE, INIT, FAILED */

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

    /* Msg for display MsgFn */
    MsgFn msg_send_fn;
    MsgFn msg_recv_fn;

    /* Project Task and Sub Task */
    BBLProject* project_;
    BBLProfile* profile_;
    BBLTask* task_;
    BBLSubTask* subtask_;
    BBLSubTask* temptask_;
    
    /* command commands */
    int command_get_version();

    /* control apis */
    int command_xyz_abs();
    int command_auto_leveling();
    int command_go_home();
    int command_fan_on();
    int command_fan_off();
    int command_task_abort();
    int command_task_pause();
    int command_task_resume();
    int command_set_bed(int temp);
    int command_set_nozzle(int temp);

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
    inline LIGHT_EFFECT light_effect_parse(std::string& effect_str) {
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

    // axis string is X, Y, Z, E
    int command_axis_control(std::string axis, double unit = 1.0f, double value = 1.0f, int speed = 3000);

    // device bind and unbind
    int command_bind();
    int command_unbind();

    /* machine mqtt apis */
    void set_callbacks(SuccessFn sFn, FailedFn fFn, LostFn lFn);
    int connect();
    int disconnect();
    int reconnect();
    bool is_connected();
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
    void on_machine_alive(std::string dev_name, std::string dev_id, std::string dev_ip);
    /* disconnect all machine connections */
    void disconnect_all();
    void query_bind_status(AccountManager::CompletedFn cFn, AccountManager::ErrorFn errFn);

    MachineObject* get_default();   /* return default machine */
    std::map<std::string, MachineObject*> get_all_machine_list();
    std::map<std::string, MachineObject*> get_free_machine_list();
    std::map<std::string, MachineObject*> get_user_machine_list();


    void check_alive();
};

} // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
