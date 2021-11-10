#ifndef slic3r_AccountManager_hpp_
#define slic3r_AccountManager_hpp_

#include <map>
#include <vector>
#include <string>
#include <memory>
#include <boost/thread.hpp>
#include "mqtt/async_client.h"
#include "ProjectTask.hpp"
#include "slic3r/Utils/Http.hpp"

#define MY_MODEL_PUBLISH_URL_FORMAT     "https://portal-dev.bambu-lab.com/my/models/%s/publish?project_id=%s"
#define MY_COLLECTIONS_URL              "https://portal-dev.bambu-lab.com/my/collections"
#define MY_PROJECT_LIST_URL             "https://portal-dev.bambu-lab.com/"
#define MODEL_STORE_URL                 "https://portal-dev.bambu-lab.com/designs"

namespace Slic3r {

typedef std::function<void(std::string name)> SuccessFn;
typedef std::function<void(std::string name)> FailedFn;
typedef std::function<void(std::string name)> LostFn;

class AccountManager;

class action_listener : public virtual mqtt::iaction_listener
{
private:
    std::string name_;
    void* context_;

    void on_failure(const mqtt::token& tok) override {
        BOOST_LOG_TRIVIAL(trace) << "subscribe failed";
    }
    void on_success(const mqtt::token& tok) override {
        BOOST_LOG_TRIVIAL(trace) << "subscribe success";
    }
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
    SuccessFn  successFn;
    FailedFn failedFn;
    LostFn lostFn;

    void connected(const std::string& cause) override;

    void on_failure(const mqtt::token& tok) override;

    void on_success(const mqtt::token& tok) override;

    void connection_lost(const std::string& cause) override;

    void message_arrived(mqtt::const_message_ptr msg) override;
public:
    cloud_conn_callback(mqtt::async_client& cli, mqtt::connect_options& connOpts, void* context)
        : nretry_(0), cli_(cli), connOpts_(connOpts), context_(context) {}

    void add_topics(std::string topic) { sub_topics.push_back(topic); }
    void set_connect_fns(SuccessFn sFn, FailedFn fFn, LostFn lFn);
};

class AccountInfo {
public:
    enum LoginStatus
    {
        STATUS_LOGIN,
        STATUS_LOGOUT,
    };

    AccountInfo(std::string account, std::string user_id, AccountInfo::LoginStatus status = STATUS_LOGOUT);

    std::string user_id() { return m_user_id; }
    void set_token(std::string token) { m_token = token; }
    void set_login_status(AccountInfo::LoginStatus status) { m_login_status = status; }
    std::string get_token() { return m_token; }
    std::string get_account() { return m_account; }
    std::string get_user_id() { return m_user_id; }
    LoginStatus login_status() { return m_login_status; }
    int save_to_json(std::string filename);
    static AccountInfo* load_from_json(std::string filename);

    /* user project */
    std::vector<BBLProject*> project_list;
    /* send a project task to machine */
    //int send_print_task(MachineObject* obj, int project_id, int plate_idx = 0);


    friend class boost::serialization::access;
    std::string m_account;
    std::string m_name;
    std::string m_password;
    std::string m_user_id;
    std::string m_avatar;
    std::string m_token;
    LoginStatus m_login_status;
};

class MachineObject;

class AccountManager
{
private:
    //std::unique_ptr<AccountInfo> m_curr_user;
    AccountInfo* m_curr_user;

    boost::filesystem::path m_user_info_path;
    const std::string account_json = "UserInfo.json";
    std::string host = "https://api-qa.bambu-lab.com";
    std::string MSG_SUCCESS = "success";


    /* login, register */
    std::string _get_login_request(std::string account, std::string password);
    std::string _get_register_request(std::string account, std::string password);
    std::string _get_login_url();
    std::string _get_user_profile_url(std::string account);
    std::string _get_register_url();

    /* bind */
    std::string _get_bind_request(std::string device_id);
    std::string _get_unbind_request(std::string device_id);
    std::string _get_bind_url(std::string device_id);
    std::string _get_bind_list_url();
    std::string _get_bind_list_request();
    std::string _get_query_bind_request(std::string device_id);
    std::string _get_query_url(std::string device_id);
    std::string _get_qeury_bind_list_url(std::vector<std::string> device_id_list);


    std::string _get_device_json(std::string device_id);
    std::string json_request_body_post_project(BBLProject* project);
    std::string json_request_body_post_profile(BBLProfile* profile);
    std::string json_request_body_post_task(BBLTask* task);
    std::string json_request_body_post_task(BBLSubTask* task);
    std::string json_request_poll_3mf_gather(BBLSubTask* task);
    std::string json_request_poll_3mf_gather_model_only();

    /* project */
    BBLProject* default_project;
    std::string _get_project_url();

    /* check valid of user or pwd */
    bool _check_valid(std::string user, std::string password);
    /* common error code handler */
    void _handle_error_code(int status, std::string error, std::string body);

    /* mqtt cloud client */
    mqtt::string_ref mqtt_user = "bbl_mqtt";
    mqtt::binary_ref mqtt_pwd = "emqx@204";
    mqtt::async_client* mqtt_cli;
    cloud_conn_callback* mqtt_cb;
    mqtt::connect_options mqtt_opt;
    std::string mqtt_uuid;
    int mqtt_uuid_bytes;
public:
    std::string MQTT_HOST = "emqx.bambooolab.com:1883";
    const int MQTT_QOS = 0;

    typedef std::function<void(int progress)> ProgressFn;
    typedef std::function<void(int retcode, std::string info)> LoginFn;
    typedef std::function<void(std::string body)> CompletedFn;
    typedef std::function<void(int retcode, std::string error, std::string body)> ErrorFn;
    typedef std::function<void(int result, std::string info)> ResultFn;

    AccountManager();
    ~AccountManager();

    // Check user last login status
    int load_user_info();
    int save_user_info();

    /* mqtt */
    std::map<std::string, MachineObject*> mqtt_topics;

    /* mqtt apis */
    mqtt::async_client* get_client() { return mqtt_cli; }
    int connect_mqtt(bool sync = false);
    int disconnect_mqtt();
    void add_subscribe(MachineObject* obj);
    void del_subscribe(MachineObject* obj);
    void update_subscription();

    /* user login register apis */
    bool is_user_login();
    int user_login(std::string account, std::string password, LoginFn fn);
    int user_get_profile(std::string account, LoginFn fn);
    int user_logout();
    int user_register(std::string account, std::string passoword);
    int request_user_unbind(std::string device_id, ResultFn fn);
    void clean_user_data();

    /* myBindList */
    std::mutex listMutex;
    std::map<std::string, MachineObject*>  myBindMachineList;   /* dev_id -> MachineObject* */
    std::string default_machine;                                /* default bind machine dev_id */
    /* create bind machine or update machine properties */
    void update_my_bind_list(std::string body);
    MachineObject* get_default_machine();
    MachineObject* find_machine(std::string dev_id);
    std::vector<MachineObject*> get_select_machine_list();

    /* project struct */
    std::map<std::string, BBLProject*> myProjectList;

    /* bind apis */
    int query_bind_status(std::vector<std::string> device_list, CompletedFn fn, ErrorFn errFn);
    int request_bind(std::string device_id, ResultFn fn);
    int request_unbind(std::string device_id, ResultFn fn);
    int request_bind_list(ResultFn fn = nullptr);

    /* project apis */
    BBLProject* get_default_project() { return default_project; }
    void set_default_project(BBLProject* project) { default_project = project; }
    // request a project id, project_name -> project_id, sync
    int request_project_id(BBLProject* project, ResultFn resFn = nullptr);
    // request a profile id, profile_name -> profile_id, sync
    int request_profile_id(BBLProfile* profile, ResultFn resFn = nullptr);
    // request a task id, project_id, profile_id -> task_id, sync
    int request_task_id(BBLTask* task, ResultFn resFn = nullptr);
    // request a sub task id, project_id, profile_id -> subtask_id, sync
    int request_subtask_id(BBLSubTask* task, ResultFn resFn = nullptr);
    // upload 3mf for project and profile, aync
    int upload_3mf(BBLProfile* profile, ResultFn resFn = nullptr, Http::ProgressFn proFn = nullptr, bool sync = false);
    // poll_3mf for project model only, sync
    int poll_3mf(BBLProject* project);
    int poll_3mf(BBLProject* project, std::string profile_id, Http::ErrorFn errFn = nullptr);
    // poll_3mf for project and profile, sync
    int poll_3mf(BBLProfile* profile);
    // poll_3mf for task, sync
    int poll_3mf(BBLSubTask* task);
    // get task info
    void get_task(BBLTask* &task);
    void get_subtask(BBLSubTask* &subtask);
    void get_profile(BBLProject*& project, BBLProfile*& profile);

    // create a project 
    void get_project_info(BBLProject* project);
    void get_profile_info(BBLProject* &project, BBLProfile* &profile);
    void create_task(BBLProject* project, BBLTask* task, ResultFn resFn);
    void post_task(BBLSubTask* task, ResultFn resFn, ProgressFn proFn);

    bool can_publish();

    /* submit */
    int submit_print_result(std::string device_id, std::string json_str, ResultFn fn);

    /* common apis */
    AccountInfo* get_curr_user() { return m_curr_user; }
    AccountInfo* user() { return m_curr_user; }
    std::string get_user_name();
    std::string get_token_str();

    void set_host(std::string host_url);
    void set_user_info_path(boost::filesystem::path path) { m_user_info_path = path; }
    std::string get_user_id() {
        if (m_curr_user) {
            return m_curr_user->user_id();
        }
        return "";
    }

    /* handle webpage command */
    std::string handle_web_request(std::string cmd);

    void request_model_download(std::string model_id);
    void request_project_download(std::string project_id);
    void request_open_project(std::string project_id);
};

} // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
