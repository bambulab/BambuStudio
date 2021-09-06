#ifndef slic3r_AccountManager_hpp_
#define slic3r_AccountManager_hpp_

#include <map>
#include <vector>
#include <string>
#include <memory>
#include <boost/thread.hpp>
#include "mqtt/async_client.h"

#include "ProjectTask.hpp"

namespace Slic3r {


class action_listener : public virtual mqtt::iaction_listener
{
private:
    std::string name_;

    void on_failure(const mqtt::token& tok) override {
        BOOST_LOG_TRIVIAL(trace) << "on_failure";
    }
    void on_success(const mqtt::token& tok) override {
        BOOST_LOG_TRIVIAL(trace) << "on_success";
    }
public:
    action_listener(const std::string& name) : name_(name) {}
};

class cloud_conn_callback : public virtual mqtt::callback, public virtual mqtt::iaction_listener
{
private:
    int nretry_;
    mqtt::async_client& cli_;
    mqtt::connect_options& connOpts_;
    std::vector<std::string> sub_topics;
    void* context_;

    void reconnect();

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

class AccountInfo {
public:
    typedef std::function<void(std::string topic, std::string payload)> MsgFn;
    std::string MQTT_HOST = "emqx.bambooolab.com:1883";
    const int MQTT_QOS = 0;

    enum LoginStatus
    {
        STATUS_LOGIN,
        STATUS_LOGOUT,
    };

    AccountInfo(std::string account, std::string user_id, AccountInfo::LoginStatus status = STATUS_LOGOUT);

    std::string user_id() { return m_user_id; }
    void set_token(std::string token) { m_token = token; }
    void set_login_status(LoginStatus status) { m_login_status = status; }
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

    /* mqtt apis */
    int connect_mqtt();
    int disconnect_mqtt();
    void add_topics(std::string topic);
    void remove_topics(std::string topic);
    void set_msg_send_fn(MsgFn fn) { msg_send_fn = std::move(fn); }
    void set_msg_recv_fn(MsgFn fn) { msg_recv_fn = std::move(fn); }

    /* Msg for display MsgFn */
    MsgFn msg_send_fn;
    MsgFn msg_recv_fn;

    friend class boost::serialization::access;
    std::string m_account;
    std::string m_password;
    std::string m_user_id;
    std::string m_token;
    LoginStatus m_login_status;

private:
    /* mqtt cloud client */
    mqtt::string_ref mqtt_user = "bbl_mqtt";
    mqtt::binary_ref mqtt_pwd = "emqx@204";
    mqtt::async_client* mqtt_cli;
    cloud_conn_callback* mqtt_cb;
    mqtt::connect_options mqtt_opt;
    std::string mqtt_uuid;
    int mqtt_uuid_bytes;
};


class AccountManager
{
private:
    AccountInfo* m_curr_user;
    boost::filesystem::path m_user_info_path;
    const std::string account_json = "UserInfo.json";
    std::string host = "http://iot.dev.bbl";
    std::string MSG_SUCCESS = "success";


    /* login, register */
    std::string _get_login_request(std::string account, std::string password);
    std::string _get_register_request(std::string account, std::string password);
    std::string _get_login_url();
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

    /* project */
    BBLProject* default_project;
    std::string _get_project_url();

    /* check valid of user or pwd */
    bool _check_valid(std::string user, std::string password);
    /* common error code handler */
    void _handle_error_code(int status, std::string error, std::string body);

public:
    typedef std::function<void(int progress)> ProgressFn;
    typedef std::function<void(int retcode, std::string info)> LoginFn;
    typedef std::function<void(std::string body)> CompletedFn;
    typedef std::function<void(int retcode, std::string error, std::string body)> ErrorFn;
    typedef std::function<void(int result, std::string info)> ResultFn;

    AccountManager();
    ~AccountManager() {}

    // Check user last login status
    int load_user_info();
    int save_user_info();

    /* user login register apis */
    bool is_user_login();
    int user_login(std::string account, std::string password, LoginFn fn);
    int user_logout();
    int user_register(std::string account, std::string passoword);
    int request_user_unbind(std::string device_id, ResultFn fn);


    /* bind apis */
    int query_bind_status(std::vector<std::string> device_list, CompletedFn fn, ErrorFn errFn);
    int request_bind(std::string device_id, ResultFn fn);
    int request_unbind(std::string device_id, ResultFn fn);
    int request_bind_list(std::string user_id);

    /* project apis */
    BBLProject* get_default_project() { return default_project; }
    // create a project 
    BBLProject* create_project(BBLProject::ProjectType type, std::wstring file, ResultFn resFn, ProgressFn proFn);
    void get_project_info(BBLProject* project);
    void get_profile_info(BBLProject* project, BBLProfile* profile);
    void create_task(BBLProject* project, BBLTask* task, ResultFn resFn);


    /* submit */
    int submit_print_result(std::string device_id, std::string json_str, ResultFn fn);

    /* common apis */
    AccountInfo* get_curr_user() { return m_curr_user; }
    AccountInfo* user() { return m_curr_user; }
    std::string get_user_name();
    std::string get_token_str();

    void set_host(std::string host_url);
    void set_user_info_path(boost::filesystem::path path) { m_user_info_path = path; }
    std::string get_user_id() { return m_curr_user->user_id(); }  
};

} // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
