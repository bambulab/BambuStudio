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
    SuccessFn  succussFn;
    FailedFn failedFn;
    LostFn lostFn;

    void reconnect();

    void connected(const std::string& cause) override;

    void on_failure(const mqtt::token& tok) override;

    void on_success(const mqtt::token& tok) override;

    void connection_lost(const std::string& cause) override;

    void message_arrived(mqtt::const_message_ptr msg) override;
public:
    machine_conn_callback(mqtt::async_client& cli, mqtt::connect_options& connOpts, void* context)
        : nretry_(0), cli_(cli), connOpts_(connOpts), context_(context) {}

    void add_topics(std::string topic) { sub_topics.push_back(topic); }
    void set_connect_fns(SuccessFn sFn, FailedFn fFn, LostFn lFn);
};

   
class MachineObject
{
private:
    AccountManager& acc_;
    CommuBackend& backend_;

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
        CONNECTION_LAN = 0,
        CONNECTION_WAN = 1,
    };

    enum CONNECTION_STATE {
        STATE_DISCONNECTED = 0,
        STATE_CONNECTING = 1,
        STATE_CONNECTED = 2,
    };

    static inline int m_sequence_id = 20000;

    MachineObject(AccountManager& acc, CommuBackend& backend, std::string name, std::string id, std::string ip);
    /* properties */
    std::string dev_name;
    std::string dev_ip;
    std::string dev_id;

    MachineBindStatus dev_bind_status;
    std::string owner;
    bool is_alive;      /* local alive */
    time_t last_alive;
    bool is_online;     /* wan online */

    /* mqtt connections */
    CONNECTION_TYPE conn_type;
    CONNECTION_STATE conn_state;
    std::string mqtt_uuid;
    int mqtt_uuid_bytes;
    mqtt::async_client* mqtt_cli;
    mqtt::connect_options mqtt_opt;
    machine_conn_callback* mqtt_cb;

    /* Msg for display MsgFn */
    MsgFn msg_send_fn;
    MsgFn msg_recv_fn;

    /* cloud mqtt cli */
    //mqtt::async_client& mqtt_cloud;

    /* machine mqtt apis */
    int connect(SuccessFn sFn, FailedFn fFn, LostFn lFn);
    int disconnect();
    bool is_connected();
    void set_msg_send_fn(MsgFn fn) { msg_send_fn = std::move(fn); }
    void set_msg_recv_fn(MsgFn fn) { msg_recv_fn = std::move(fn); }
    int publish_json(std::string json_str, ResultFn resFn = nullptr);

    int send_print_task(BBLTask* task);
    int send_wan_print_task(BBLTask* task);

    int send_print_subtask(BBLSubTask* task, UploadedFn cFn, UploadProgressFn proFn, ErrorFn errFn);
    int send_lan_print_subtask(BBLSubTask* task, UploadedFn cFn, UploadProgressFn proFn, ErrorFn errFn);
    int send_wan_print_subtask(BBLSubTask* task, UploadedFn cFn, UploadProgressFn proFn, ErrorFn errFn);

    /* iot operation apis */
    void request_bind(ResultFn fn, bool force_bind = false);
    void request_unbind(ResultFn fn);

    /* common apis */
    void set_bind_status(std::string status);
    void set_connect_state(CONNECTION_STATE state);
    std::string get_bind_str();
    
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
    std::map<std::string, MachineObject*>  myBindMachineList;   /* dev_id -> MachineObject* */
    std::map<std::string, MachineObject*>  allMachineList;      /* dev_id -> MachineObject* */
    std::string default_machine;    /* dev_id */

    /* create amchine or update machine properties */
    void on_machine_alive(std::string dev_name, std::string dev_id, std::string dev_ip);
    /* disconnect all machine connections */
    void disconnect_all();
    void query_bind_status(AccountManager::CompletedFn cFn, AccountManager::ErrorFn errFn);

    MachineObject* get_default();   /* return default machine */
    std::map<std::string ,MachineObject*> get_all_machine_list();

    void check_alive();
};

} // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
