#ifndef __BBL_AccountManager_hpp__
#define __BBL_AccountManager_hpp__

#include <map>
#include <vector>
#include <string>
#include <memory>
#include "mqtt/async_client.h"
#include "SsdpDiscovery.hpp"
#include "NetworkProjectTask.hpp"
#include "slic3r/Utils/Http.hpp"
#include "nlohmann/json.hpp"

using namespace nlohmann;

//#define BBL_CHECK_USER_REPORT

#define MY_MODEL_PUBLISH_URL_FORMAT     "/my/models/%1%/publish?project_id=%2%&profile_id=%3%&design_id="
#define MY_PROFILE_PUBLISH_URL_FORMAT   "/my/profiles/%1%/publish?project_id=%2%&design_id=%3%"
#define MODEL_STORE_URL                 "/designs"

#define POLL_3MF_TIMEOUT                180
#define POLL_3MF_INTERVAL               1
#define POLL_NOTIFICATION_TIMEOUT_MAX   600
#define POLL_NOTIFICATION_TIMEOUT       120
#define POLL_NOTIFICATION_INTERVAL      2

#define MSG_SUCCESS                     "success"

#define RET_ERR_CANCEL                  -2
#define RET_ERR_TIMEOUT                 -3
#define RET_MD5_CHECK_FAILED            -4
#define RET_ERR_OVERSIZE                -5

#define AGENT_CONFIG_FILE               "BambuNetworkEngine.conf"
#define TOKEN_MIN_EXPIRES_IN            30

#define LOCAL_COMMU_PORT        3000
#define DEBUG_COMMU_PORT        5000

namespace pt = boost::property_tree;


namespace BBL {

enum SendingPrintJobStage {
    PrintingStageCreate     = 0,
    PrintingStageUpload     = 1,
    PrintingStageWaiting    = 2,
    PrintingStageSending    = 3,
    PrintingStageFinished   = 4,
};

enum BindJobStage {
    LoginStageConnect           = 0,
    LoginStageLogin             = 1,
    LoginStageWaitForLogin      = 2,
    LoginStageGetIdentify       = 3,
    LoginStageWaitAuth          = 4,
    LoginStageFinished          = 5,
};

enum ConnectStatus {
    ConnectStatusOk = 0,
    ConnectStatusFailed = 1,
    ConnectStatusLost = 2,
};

class RegionServer
{
public:
    RegionServer() {}
    std::string iot_server_host;
    std::string api_servier_host;
    std::string mqtt_server_host;
    std::string tutk_server_host;
    std::string wifi_code;
    std::string base_domain;
    std::string environment;
};

class AccountManager;

class action_listener : public virtual mqtt::iaction_listener
{
private:
    std::string name_;
    void* context_;

    void on_failure(const mqtt::token& tok) override {
        for (int i = 0; i < tok.get_topics()->size(); i++) {
            BOOST_LOG_TRIVIAL(trace) << "subscribe topic:" << (*tok.get_topics())[i].c_str() << " failed";
        }
        BOOST_LOG_TRIVIAL(trace) << "subscribe return code: " << tok.get_return_code();
        BOOST_LOG_TRIVIAL(trace) << "subscribe reason code: " << tok.get_reason_code();
    }
    void on_success(const mqtt::token& tok) override;
public:
    action_listener(const std::string& name, void* context) : name_(name), context_(context) {}
};

class cloud_conn_callback : public virtual mqtt::callback, public virtual mqtt::iaction_listener
{
private:
    int nretry_;
    mqtt::async_client& cli_;
    mqtt::connect_options& connOpts_;
    std::vector<std::string> sub_topics;
    void* context_;

    void connected(const std::string& cause) override;

    void on_failure(const mqtt::token& tok) override;

    void on_success(const mqtt::token& tok) override;

    void connection_lost(const std::string& cause) override;

    void message_arrived(mqtt::const_message_ptr msg) override;
public:
    cloud_conn_callback(mqtt::async_client& cli, mqtt::connect_options& connOpts, void* context)
        : nretry_(0), cli_(cli), connOpts_(connOpts), context_(context) {}

    void add_topics(std::string topic) { sub_topics.push_back(topic); }
};



class sub_action_listener : public virtual mqtt::iaction_listener
{
private:
    std::string name_;

    void on_failure(const mqtt::token& tok) override;
    void on_success(const mqtt::token& tok) override;
        
public:
    sub_action_listener(const std::string& name) : name_(name) {}
};

class local_conn_callback : public virtual mqtt::callback, public virtual mqtt::iaction_listener
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
    local_conn_callback(mqtt::async_client& cli, mqtt::connect_options& connOpts, void* context)
        : nretry_(0), cli_(cli), connOpts_(connOpts), context_(context) {}

    void add_topics(std::string topic) { sub_topics.push_back(topic); }
};



#define  VERSION_LEN    4
class VersionInfo
{
public:
    std::string version_str;
    std::string version_name;
    std::string description;
    std::string url;
    bool        force_upgrade { false };
    int      ver_items[VERSION_LEN];  // AA.BB.CC.DD
    VersionInfo() {
        for (int i = 0; i < VERSION_LEN; i++) {
            ver_items[i] = 0;
        }
        force_upgrade = false;
    }

    void parse_version_str(std::string str) {
        version_str = str;
        std::vector<std::string> items;
        boost::split(items, str, boost::is_any_of("."));
        if (items.size() == VERSION_LEN) {
            try{
                for (int i = 0; i < VERSION_LEN; i++) {
                    ver_items[i] = stoi(items[i]);
                }
            }
            catch (...) {
                ;
            }
        }
    }
    static std::string convert_full_version(std::string short_version);
    static std::string convert_short_version(std::string full_version);
    static std::string get_full_version() {
        return convert_full_version(SLIC3R_VERSION);
    }

    /* return > 0, need update */
    int compare(std::string ver_str) {
        if (version_str.empty()) return -1;

        int      ver_target[VERSION_LEN];
        std::vector<std::string> items;
        boost::split(items, ver_str, boost::is_any_of("."));
        if (items.size() == VERSION_LEN) {
            try{
                for (int i = 0; i < VERSION_LEN; i++) {
                    ver_target[i] = stoi(items[i]);
                    if (ver_target[i] < ver_items[i]) {
                        return 1;
                    }
                    else if (ver_target[i] == ver_items[i]) {
                        continue;
                    }
                    else {
                        return -1;
                    }
                }
            }
            catch (...) {
                return -1;
            }
        }
        return -1;
    }
};

class AccountInfo {
public:
    enum LoginStatus
    {
        STATUS_LOGIN,
        STATUS_LOGOUT,
    };

    AccountInfo(std::string account, std::string user_id, AccountInfo::LoginStatus status = STATUS_LOGOUT);
    AccountInfo(std::string account, std::string user_id, std::string strToken, std::string strName, std::string strAvatar, AccountInfo::LoginStatus status,std::string strAutotestToken="");
    AccountInfo(std::string account, std::string user_id, std::string strName, std::string strAvatar, AccountInfo::LoginStatus status, std::string strRefreshToken, long long refreshExpiresIn, std::string strToken = "", long long expiresIn = 0, std::string strAutotestToken = "");

    std::string user_id() { return m_user_id; }
    void set_token(std::string token) { m_token = token; }
    void set_expires_in(long long expires_in) { m_expires_in = expires_in; }
    void set_refresh_token(std::string refresh_token) { m_refresh_token = refresh_token; }
    void set_refresh_expires_in(long long refresh_expires_in) { m_refresh_expires_in = refresh_expires_in; }
    void set_login_status(AccountInfo::LoginStatus status) { m_login_status = status; }
    std::string get_token() { return m_token; }
    long long get_expires_in() { return m_expires_in; }
    std::string get_refresh_token() { return m_refresh_token; }
    long long get_refresh_expires_in() { return m_refresh_expires_in; }
    std::string get_account() { return m_account; }
    std::string get_user_id() { return m_user_id; }
    LoginStatus login_status() { return m_login_status; }
    int save_to_json(json& config_json);
    static AccountInfo* load_from_json(json& config_json);
    bool is_valid() { return !m_user_id.empty() && !m_token.empty() && !m_account.empty(); }

    std::string m_account;
    std::string m_name;
    std::string m_password;
    std::string m_user_id;
    std::string m_avatar;
    std::string m_token;
    long long m_expires_in;
    std::string m_refresh_token;
    long long m_refresh_expires_in;
    LoginStatus m_login_status;

    std::string m_autotest_token;
};

class AccountManager
{
private:
    AccountInfo* m_curr_user { nullptr };
    std::string host = "";

    /* studio */
    std::string _get_slicer_info_url();

    /* bind */
    std::string _get_bind_url(std::string device_id);
    std::string _get_bind_list_url();
    std::string _get_qeury_bind_list_url(std::vector<std::string> device_id_list);


    std::string json_request_body_post_project(BBLProject* project);
    std::string json_request_body_post_profile(BBLProfile* profile);
    std::string json_request_body_post_subtask(BBLProject* project, BBLProfile* profile, BBLSubTask* task);
    std::string json_request_body_post_setting(std::string name, bool is_system, std::map<std::string, std::string>& values_map);
    std::string json_request_body_put_setting(std::string name, std::map<std::string, std::string>& values_map);
    std::string json_request_poll_3mf_gather(BBLSubTask* task);
    std::string json_request_poll_3mf_gather_model_only();

    std::string _get_project_url();

    /* check valid of user or pwd */
    bool _check_valid(std::string user, std::string password);

    /* mqtt cloud client */
    static inline int m_sequence_id = 50000;

    mqtt::async_client* mqtt_cli{ nullptr };
    cloud_conn_callback* mqtt_cb{ nullptr };
    mqtt::connect_options mqtt_opt;
    mqtt::ssl_options mqtt_ssl_opt;
    std::string mqtt_uuid;
    bool m_is_subscribing { false };
    std::map<std::string, bool> subscribe_module;
    void set_product_mqtt_opt();
    void set_engineering_mqtt_opt();

    /* mqtt local client */
    mqtt::async_client* mqtt_local_cli;
    mqtt::connect_options mqtt_local_opt;
    local_conn_callback* mqtt_local_cb;


    int mqtt_uuid_bytes;
public:
    std::string MQTT_HOST = "ssl://47.100.225.51:8883";
    const int MQTT_QOS = 0;
    bool m_is_connecting{ false };
    RegionServer user_region_server;

    std::string get_emqx_server_host();
    std::string get_official_server_host();

    typedef std::function<void(int progress)> ProgressFn;
    typedef std::function<void(int retcode, std::string info)> LoginFn;
    typedef std::function<void(int result, std::string info)> ResultFn;
    typedef std::function<bool()> CancelFn;

    //define callbacks
    typedef std::function<void(int online_login)>       OnUserLoginFn;
    typedef std::function<void(std::string topic_str)>  OnPrinterConnectedFn;
    typedef std::function<void()>                       OnServerConnectedFn;
    typedef std::function<void(unsigned http_code, std::string http_body)> OnHttpErrorFn;
    typedef std::function<std::string()>                GetCountryCodeFn;
    typedef std::function<void(std::string dev_id, std::string msg)> OnMessageFn;
    typedef std::function<void(int status, int code, std::string msg)> OnUpdateStatusFn;
    typedef std::function<bool()>                       WasCancelledFn;

    typedef std::function<void(int status, std::string dev_id, std::string msg)> OnLOcalConnectFn;

    // ballbacks
    OnUserLoginFn           on_user_login_fn;
    OnPrinterConnectedFn    on_printer_connected_fn;
    OnServerConnectedFn     on_server_connected_fn;
    OnHttpErrorFn           on_http_error_fn;
    GetCountryCodeFn        get_country_code_fn;
    OnMessageFn             on_message_fn;
    OnLOcalConnectFn        on_local_connect_fn;
    OnMessageFn             on_local_message_fn;

    void set_on_user_login_fn(OnUserLoginFn fn) { on_user_login_fn  = fn; }
    void set_on_printer_connected_fn(OnPrinterConnectedFn fn) { on_printer_connected_fn = fn; }
    void set_on_server_connected_fn(OnServerConnectedFn fn) { on_server_connected_fn = fn; }
    void set_on_http_error_fn(OnHttpErrorFn fn) { on_http_error_fn = fn; }
    void set_get_country_code_fn(GetCountryCodeFn fn) { get_country_code_fn  = fn; }
    void set_on_message_fn(OnMessageFn fn) { on_message_fn  = fn; }
    void set_on_local_connect_fn(OnLOcalConnectFn fn) { on_local_connect_fn = fn; }
    void set_on_local_message_fn(OnMessageFn fn) { on_local_message_fn = fn; }

    /* bambu stdio agent config */
    json config_json;
    std::string config_dir;
    std::string cert_dir;
    std::string cert_name;
    void set_config_dir(std::string dir) {
        BOOST_LOG_TRIVIAL(trace) << "Agent: set_config_dir = " << dir;
        config_dir = dir;
    }

    void set_cert_dir_name(std::string dir, std::string name) {
        BOOST_LOG_TRIVIAL(trace) << "Agent: set_cert_dir_name,dir=" << dir << ",name=" << name;
        cert_dir = dir;
        cert_name = name;
    }

    int save_config();
    int load_config();
    std::string get_config(std::string section, std::string key);


    AccountManager();
    ~AccountManager();

    void init_log();

    // Check user last login status
    int load_user_info();
    int save_user_info();

    /* cloud mqtt connections apis */
    mqtt::async_client* get_client() { return mqtt_cli; }
    bool is_mqtt_connected();
    int connect_mqtt(bool sync = false);
    int disconnect_mqtt();
    void check_mqtt_connection();
    void add_subscribe(std::string dev_id);
    void del_subscribe(std::string dev_id);

    int local_send_message(std::string dev_id, std::string json_str, int qos = 0);

    void set_monitor_machine(std::string dev_id);

    /* local mqtt connections apis */
    int local_connect_mqtt(std::string dev_id, std::string dev_ip);
    int local_disconnect_mqtt();


    //control subscribe default machine
    void start_subscribe(std::string module = "");
    void stop_subscribe(std::string module = "");

    /* user login register apis */
    bool is_user_login();
    int user_logout();
    int request_user_unbind(std::string device_id, ResultFn fn);
    void clean_user_data();
    void user_check_report(int* query_task_id, bool* printable);

    // GET /api/user/notification
    /* return: -1 : failed, 1 : success, -2: cancelled, -3: timeout */
    int get_notification(BBLProfile *profile, unsigned int &http_code, std::string &http_body, CancelFn cancel_fn = nullptr, int timeout = POLL_NOTIFICATION_TIMEOUT);
    int calc_get_notification_timeout(boost::filesystem::path &file);

    // PUT /api/user/notification
    int put_notification(BBLProfile* profile, std::string upload_filename, unsigned int &http_code, std::string &http_body);

    /* myBindList */
    std::string default_machine;                                /* default bind machine dev_id */
    std::string get_default_machine() { return default_machine; }
    void set_default_machine(std::string dev_id);

    /* project struct */
    std::map<std::string, BBLProject*> myProjectList;
    std::map<std::string, std::map<std::string, std::string>> m_system_presets;      // key is setting_id
    std::map<std::string, std::map<std::string, std::string>> m_my_presets;      // key is setting_id
    std::vector<std::string> need_delete_presets;   // store setting ids of preset

    /* bind apis */
    int query_bind_status(std::vector<std::string> device_list, unsigned int &http_code, std::string &http_body);
    int request_unbind(std::string device_id, ResultFn fn);
    int request_bind_list(ResultFn fn = nullptr);

    /* device apis */
    // PATCH /user/device/info
    int modify_device_name(std::string dev_id, std::string dev_name, unsigned int &http_code, std::string &http_body);

    /* print apis */
    int get_print_info(unsigned int& http_code, std::string& http_body);

    // POST /api/user/project
    int request_project_profile_id(BBLProject *project, BBLProfile *profile, unsigned int &http_code, std::string &http_body);

    // POST /api/user/project
    int request_project_id(BBLProject* project, unsigned int &http_code, std::string &http_body);

    // POST /api/user/project/{project_id}
    int request_profile_id(BBLProfile* profile, unsigned int &http_code, std::string &http_body);

    // POST /api/user/refreshtoken
    int request_refreshtoken(std::string refresh_token);

    // PUT alibaba oss
    int upload_3mf_to_oss(BBLProfile* profile, unsigned int &http_code, std::string & http_body, Http::ProgressFn proFn = nullptr);

    // upload_3mf_to_oss + put_notification + get_notification
    //int upload_3mf(BBLProfile* profile, unsigned int &http_code, std::string &http_body, Http::ProgressFn proFn = nullptr);

    // GET /api/user/profile/{profile_id}
    int get_profile_3mf(BBLProfile* profile, unsigned int &http_code, std::string http_body);

    // GET /design-service/model/{model_id}
    int get_design_info(std::string model_id, std::string &design_id, unsigned int &http_code, std::string &http_body);

    // get task info
    void get_task(BBLTask* &task);
    void get_subtask(BBLSubTask* &subtask);
    void get_plate_index(std::string subtask_id, int &plate_index);
    void get_profile(BBLProject*& project, BBLProfile*& profile);
    void get_slice_info(std::string project_id, std::string profile_id, int plate_index, std::string &slice_info_json);

    static void get_machine_last_report_url(std::string dev_id, std::string& last_url);

    // create a project
    void get_project_info(BBLProject* project);
    void get_profile_info(BBLProject* &project, BBLProfile* &profile);

    // POST /my/task
    int post_task(BBLProject* project, BBLProfile* profile, BBLSubTask *task, unsigned int &http_code, std::string &http_body);

    // Get /my/ticket
    int get_ticket(std::string ticket, unsigned int &http_code, std::string &http_body);

    // POST /my/ticket
    int post_ticket(std::string ticket, unsigned int &http_code, std::string &http_body);

    // GET /my/tasks
    int get_tasks(std::string dev_id, unsigned limit, unsigned int &http_code, std::string &http_body);

    int load_servers_from_region(std::string country_code);
    int update_country_code(std::string country_code);

    bool can_publish();

    /* preset settings api */
    int get_setting_list(std::string bundle_version, Http::ErrorFn errFn = nullptr);
    void get_setting(std::string name, std::map<std::string, std::string>& values_map, std::function<void(void)> callback = {});
    std::string request_setting_id(std::string name, std::map<std::string, std::string>& values_map, unsigned int& http_code);
    int put_setting(std::string setting_id, std::string name, std::map<std::string, std::string>& values_map, unsigned int& http_code);
    int del_setting(std::string setting_id, unsigned int& http_code);

    void parse_setting(pt::ptree node, std::string type, std::string attr);
    void _parse_preset_internal(std::map<std::string, std::map<std::string, std::string>>& setting_maps, pt::ptree node, std::string type, bool is_system);

    /* camera */
    void get_camera_url(std::string const &              device,
                        std::function<void(std::string)> callback);
    std::string get_tutk_region();

    int get_machine_version(std::string dev_id, unsigned &http_code, std::string &http_body);

    /* slicer resources apis */
    std::string get_slicer_info_url() { return _get_slicer_info_url(); }

    /* common apis */
    AccountInfo* get_curr_user() { return m_curr_user; }
    AccountInfo* user() { return m_curr_user; }
    void        set_curr_user(AccountInfo *user_info) {
        if (m_curr_user)
            delete m_curr_user;
        m_curr_user = user_info;
        save_user_info();
    }

    std::string get_user_name();
    std::string get_nick_name();
    std::string get_token_str(bool only_token = false);

    void set_host(std::string host_url);
    std::string get_host() { return host; }
    std::string get_user_id() {
        if (m_curr_user) {
            return m_curr_user->user_id();
        }
        return "";
    }

    /* build webpage command */
    std::string build_login_cmd();
    std::string build_logout_cmd();

    /* bind job */
    int start_bind(std::string dev_ip, OnUpdateStatusFn update_fn);
    std::string build_login_request();
    int _parse_login_report(std::string json_str, std::string fail_reason);

    /* print job*/
    struct PrintParams {
        std::string     dev_id;
        std::string     task_name;
        std::string     project_name;
        std::string     preset_name;
        std::string     filename;
        int             plate_index;

        /*user options */
        bool            task_bed_leveling;      /* bed leveling of task */
        bool            task_flow_cali;         /* flow calibration of task */
        bool            task_vibration_cali;    /* vibration calibration of task */
        bool            task_layer_inspect;     /* first layer inspection of task */
        bool            task_record_timelapse;  /* record timelapse of task */

    };
    int start_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn);
    
};

} // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
