#ifndef slic3r_CommuBackend_hpp_
#define slic3r_CommuBackend_hpp_

#include <string>
#include <vector>
#include <ctime>
#include <memory>
#include <wx/string.h>
#include <wx/event.h>
#include <boost/optional.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/log/trivial.hpp>
#include "slic3r/GUI/Event.hpp"
#include "libslic3r/Utils.hpp"
#include "mqtt/async_client.h"
#include "slic3r/GUI/Ssdp.hpp"

namespace Slic3r {


typedef std::function<void(std::string name)> SuccessFn;
typedef std::function<void(std::string name)> FailedFn;
typedef std::function<void(std::string name)> LostFn;

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

    void reconnect();

    void connected(const std::string& cause) override;

    void on_failure(const mqtt::token& tok) override;

    void on_success(const mqtt::token& tok) override;

    void connection_lost(const std::string& cause) override;

    void message_arrived(mqtt::const_message_ptr msg) override;
public:
    cloud_conn_callback(mqtt::async_client& cli, mqtt::connect_options& connOpts)
        : nretry_(0), cli_(cli), connOpts_(connOpts) {}

    void add_topics(std::string topic) { sub_topics.push_back(topic); }
};

class client_conn_callback : public virtual mqtt::callback, public virtual mqtt::iaction_listener
{
public:
    
private:
    int nretry_;
    mqtt::async_client& cli_;
    mqtt::connect_options& connOpts_;
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

    void delivery_complete(mqtt::delivery_token_ptr tok) override;
    
public:
    client_conn_callback(mqtt::async_client& cli, mqtt::connect_options& connOpts)
        : nretry_(0), cli_(cli), connOpts_(connOpts) {}

    void add_topics(std::string topic) { sub_topics.push_back(topic); }
    void set_connect_fns(SuccessFn cFn, FailedFn fFn, LostFn lfn);
};

class SsdpDiscovery
{
private:
    bool sdp_quit = false;
public:
    SsdpDiscovery();
    void start_discover();
    void stop_discover();
    int send_msg(int card_no);
    void on_sdp_alive(std::string dev_id, std::string dev_ip);
    void recv_sdp_msg(int card_no);
    void recv_broadcast_msg(int card_no);
};

class CommuBackend
{
public:
    typedef std::function<void(std::string loginfo)> LogFn;
    typedef std::function<void(std::string topic, std::string payload)> MsgFn;

    CommuBackend();
    ~CommuBackend();

    int start();
    int stop();

    /* cloud mqtt apis */
    int connect_to_cloud(std::string user_id);
    int disconnect_to_cloud();
    void handle_cloud_msg(std::string topic, std::string payload);
    int publish_json_to_cloud(std::string device_id, std::string json_str);

    /* client mqtt apis*/
    int connect_to_client(std::string host, std::string user_id, std::string device_id, SuccessFn cFn, FailedFn fFn, LostFn lFn);
    int disconnect_to_client();
    void handle_client_msg(std::string topic, std::string payload);
    int publish_json_to_client(std::string device_id, std::string json_str);

    void set_log_fn(LogFn fn) { m_log_fn = std::move(fn); }
    void set_msg_send_fn(MsgFn fn) { m_msg_send_fn = std::move(fn); }
    void set_msg_recv_fn(MsgFn fn) { m_msg_recv_fn = std::move(fn); }

    std::string get_request_topic(std::string device_id);
    //std::string get_report_topic(std::string device_id);
    std::string get_connect_topic(std::string device_id);

    static std::string get_report_topic(std::string device_id);
protected:

private:
    std::string MQTT_SERVER_ADDRESS = "emqx.bambooolab.com:1883";
    const int MQTT_QOS = 0;
    const int MQTT_TIMEOUT = 10;
    mqtt::string_ref mqtt_cloud_user;
    mqtt::binary_ref mqtt_cloud_pwd;

    /* client for mqtt connect to local */
    mqtt::async_client* m_mqtt_cli;
    client_conn_callback* m_mqtt_cli_cb;
    mqtt::connect_options conn_cli_opt;

    /* client form mqtt connect to cloud*/
    mqtt::async_client* m_mqtt_cloud;
    cloud_conn_callback* m_mqtt_cloud_cb;
    mqtt::connect_options conn_cloud_opt;
    std::string m_mqtt_uuid;

    /* Log for display LogFn */
    LogFn m_log_fn;

    /* Msg for display MsgFn */
    MsgFn m_msg_send_fn;
    MsgFn m_msg_recv_fn;

    /* Ssdp */
    SsdpDiscovery* ssdp;
};

}

#endif
