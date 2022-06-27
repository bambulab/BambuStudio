#ifndef __BAMBU_NETWORK_DEFINE_HPP__
#define __BAMBU_NETWORK_DEFINE_HPP__

#include <string>
#include <functional>
#include <map>

namespace BBL {

#define BAMBU_NETWORK_SUCCESS   0
#define BAMBU_NETWORK_ERR_INVALID_HANDLE            -1
#define BAMBU_NETWORK_ERR_CONNECT_FAILED            -2
#define BAMBU_NETWORK_ERR_DISCONNECT_FAILED         -3
#define BAMBU_NETWORK_ERR_SEND_MSG_FAILED           -4
#define BAMBU_NETWORK_ERR_BIND_FAILED               -5
#define BAMBU_NETWORK_ERR_UNBIND_FAILED             -6
#define BAMBU_NETWORK_ERR_PRINT_FAILED              -7
#define BAMBU_NETWORK_ERR_LOCAL_PRINT_FAILED        -8
#define BAMBU_NETWORK_ERR_REQUEST_SETTING_FAILED    -9
#define BAMBU_NETWORK_ERR_PUT_SETTING_FAILED        -10
#define BAMBU_NETWORK_ERR_GET_SETTING_LIST_FAILED   -11
#define BAMBU_NETWORK_ERR_DEL_SETTING_FAILED        -12
#define BAMBU_NETWORK_ERR_GET_USER_PRINTINFO_FAILED -13
#define BAMBU_NETWORK_ERR_GET_PRINTER_FIRMWARE_FAILED -14
#define BAMBU_NETWORK_ERR_QUERY_BIND_INFO_FAILED    -15
#define BAMBU_NETWORK_ERR_MODIFY_PRINTER_NAME_FAILED  -16

#define BAMBU_NETWORK_LIBRARY               "bambu_networking"
#define BAMBU_NETWORK_AGENT_NAME            "bambu_network_agent"
#define BAMBU_NETWORK_AGENT_VERSION         "1.0.0.1"


//iot preset type strings
#define IOT_PRINTER_TYPE_STRING     "printer"
#define IOT_FILAMENT_STRING         "filament"
#define IOT_PRINT_TYPE_STRING       "print"

#define IOT_JSON_KEY_VERSION            "version"
#define IOT_JSON_KEY_NAME               "name"
#define IOT_JSON_KEY_TYPE               "type"
#define IOT_JSON_KEY_UPDATE_TIME        "updated_time"
#define IOT_JSON_KEY_BASE_ID            "base_id"
#define IOT_JSON_KEY_SETTING_ID         "setting_id"
#define IOT_JSON_KEY_FILAMENT_ID        "filament_id"
#define IOT_JSON_KEY_USER_ID            "user_id"


// user callbacks
typedef std::function<void(int online_login, bool login)> OnUserLoginFn;
// printer callbacks
typedef std::function<void(std::string topic_str)>  OnPrinterConnectedFn;
typedef std::function<void(int status, std::string dev_id, std::string msg)> OnLocalConnectedFn;
typedef std::function<void()>                       OnServerConnectedFn;
typedef std::function<void(std::string dev_id, std::string msg)> OnMessageFn;
// http callbacks
typedef std::function<void(unsigned http_code, std::string http_body)> OnHttpErrorFn;
typedef std::function<std::string()>                GetCountryCodeFn;
// print callbacks
typedef std::function<void(int status, int code, std::string msg)> OnUpdateStatusFn;
typedef std::function<bool()>                       WasCancelledFn;
// local callbacks
typedef std::function<void(std::string dev_info_json_str)> OnMsgArrivedFn;

typedef std::function<void(int progress)> ProgressFn;
typedef std::function<void(int retcode, std::string info)> LoginFn;
typedef std::function<void(int result, std::string info)> ResultFn;
typedef std::function<bool()> CancelFn;

enum SendingPrintJobStage {
    PrintingStageCreate = 0,
    PrintingStageUpload = 1,
    PrintingStageWaiting = 2,
    PrintingStageSending = 3,
    PrintingStageRecord  = 4,
    PrintingStageFinished = 5,
};

enum BindJobStage {
    LoginStageConnect = 0,
    LoginStageLogin = 1,
    LoginStageWaitForLogin = 2,
    LoginStageGetIdentify = 3,
    LoginStageWaitAuth = 4,
    LoginStageFinished = 5,
};

enum ConnectStatus {
    ConnectStatusOk = 0,
    ConnectStatusFailed = 1,
    ConnectStatusLost = 2,
};

/* print job*/
struct PrintParams {
    /* basic info */
    std::string     dev_id;
    std::string     task_name;
    std::string     project_name;
    std::string     preset_name;
    std::string     filename;
    std::string     config_filename;
    int             plate_index;
    std::string     ams_mapping;
    std::string     connection_type;

    /* access options */
    std::string     dev_ip;
    std::string     username;
    std::string     password;

    /*user options */
    bool            task_bed_leveling;      /* bed leveling of task */
    bool            task_flow_cali;         /* flow calibration of task */
    bool            task_vibration_cali;    /* vibration calibration of task */
    bool            task_layer_inspect;     /* first layer inspection of task */
    bool            task_record_timelapse;  /* record timelapse of task */
};

void* bambu_network_create_agent();
int bambu_network_destroy_agent(void *agent);
int bambu_network_init_log(void *agent);
int bambu_network_set_config_dir(void *agent, std::string config_dir);
int bambu_network_set_cert_file(void *agent, std::string folder, std::string filename);
int bambu_network_set_country_code(void *agent, std::string country_code);
int bambu_network_start(void *agent);
int bambu_network_set_on_ssdp_msg_fn(void *agent, OnMsgArrivedFn fn);
int bambu_network_set_on_user_login_fn(void *agent, OnUserLoginFn fn);
int bambu_network_set_on_printer_connected_fn(void *agent, OnPrinterConnectedFn fn);
int bambu_network_set_on_server_connected_fn(void *agent, OnServerConnectedFn fn);
int bambu_network_set_on_http_error_fn(void *agent, OnHttpErrorFn fn);
int bambu_network_set_get_country_code_fn(void *agent, GetCountryCodeFn fn);
int bambu_network_set_on_message_fn(void *agent, OnMessageFn fn);
int bambu_network_set_on_local_connect_fn(void *agent, OnLocalConnectedFn fn);
int bambu_network_set_on_local_message_fn(void *agent, OnMessageFn fn);
int bambu_network_connect_server(void *agent);
bool bambu_network_is_server_connected(void *agent);
int bambu_network_refresh_connection(void *agent);
int bambu_network_start_subscribe(void *agent, std::string module);
int bambu_network_stop_subscribe(void *agent, std::string module);
int bambu_network_send_message(void *agent, std::string dev_id, std::string json_str, int qos);
int bambu_network_connect_printer(void *agent, std::string dev_id, std::string dev_ip, std::string username, std::string password);
int bambu_network_disconnect_printer(void *agent);
int bambu_network_send_message_to_printer(void *agent, std::string dev_id, std::string json_str, int qos);
bool bambu_network_start_discovery(void *agent, bool start, bool sending);
int bambu_network_change_user(void *agent, std::string user_info);
bool bambu_network_is_user_login(void *agent);
int bambu_network_user_logout(void *agent);
std::string bambu_network_get_user_id(void *agent);
std::string bambu_network_get_user_name(void *agent);
std::string bambu_network_get_user_avatar(void *agent);
std::string bambu_network_get_user_nickanme(void *agent);
std::string bambu_network_build_login_cmd(void *agent);
std::string bambu_network_build_logout_cmd(void *agent);
std::string bambu_network_build_login_info(void *agent);
int bambu_network_bind(void *agent, std::string dev_ip, std::string timezone, OnUpdateStatusFn update_fn);
int bambu_network_unbind(void *agent, std::string dev_id);
std::string bambu_network_get_bambulab_host(void *agent);
std::string bambu_network_get_user_selected_machine(void *agent);
int bambu_network_set_user_selected_machine(void *agent, std::string dev_id);
int bambu_network_start_print(void *agent, PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn);
int bambu_network_start_local_print_with_record(void *agent, PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn);
int bambu_network_start_local_print(void *agent, PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn);
int bambu_network_get_user_presets(void *agent, std::map<std::string, std::map<std::string, std::string>>* user_presets);
std::string bambu_network_request_setting_id(void *agent, std::string name, std::map<std::string, std::string>* values_map, unsigned int* http_code);
int bambu_network_put_setting(void *agent, std::string setting_id, std::string name, std::map<std::string, std::string>* values_map, unsigned int* http_code);
int bambu_network_get_setting_list(void *agent, std::string bundle_version, ProgressFn pro_fn);
int bambu_network_delete_setting(void *agent, std::string setting_id);
std::string bambu_network_get_studio_info_url(void *agent);
int bambu_network_set_extra_http_header(void *agent, std::map<std::string, std::string> extra_headers);
int bambu_network_check_user_task_report(void *agent, int* task_id, bool* printable);
int bambu_network_get_user_print_info(void *agent, unsigned int* http_code, std::string* http_body);
int bambu_network_get_printer_firmware(void *agent, std::string dev_id, unsigned* http_code, std::string* http_body);
int bambu_network_get_task_plate_index(void *agent, std::string task_id, int* plate_index);
int bambu_network_get_slice_info(void *agent, std::string project_id, std::string profile_id, int plate_index, std::string* slice_json);
int bambu_network_query_bind_status(void *agent, std::vector<std::string> query_list, unsigned int* http_code, std::string* http_body);
int bambu_network_modify_printer_name(void *agent, std::string dev_id, std::string dev_name);
int bambu_network_get_camera_url(void *agent, std::string dev_id, std::function<void(std::string)> callback);


}

#endif
