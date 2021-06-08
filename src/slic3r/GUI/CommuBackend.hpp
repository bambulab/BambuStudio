#ifndef slic3r_CommuBackend_hpp_
#define slic3r_CommuBackend_hpp_

#include <string>
#include <vector>
#include <ctime>
#include <wx/string.h>
#include <wx/event.h>
#include <boost/optional.hpp>
#include "fdal/fdal_wrapper.hpp"
#include "fdal/types.h"
#include "topic_device.h"
#include "topic_devicePubSubTypes.h"
#include "slic3r/GUI/Event.hpp"
#include "libslic3r/Utils.hpp"
#include "mqtt/async_client.h"

namespace pt = boost::property_tree;

namespace Slic3r {

class JsonMsg
{
public:
    JsonMsg() {}
    JsonMsg(std::string json) { json_str = json; }
    ~JsonMsg() {}

    std::string json() {return json_str;}
    void set_sequence_id(int seq_id);
private:
    pt::ptree m_root;
    int m_sequence_id;
    std::string json_str;
};

class DdsClient
{
public:
    typedef std::function<void(JsonMsg)> JsonMsgHandlerFn;
    const std::string DDS_QOS_FILE = resources_dir() + "/bbl/default_qos.xml";
    DdsClient(int domain);

    int add_pub_topic(std::string topic, data_callback callback);
    int add_sub_topic(std::string topic, data_callback callback);

    int start();
    int stop();

    int publish_msg(std::string topic, JsonMsg msg);
    int wait_for_client(std::string topic);

private:
    fdal_init_options_t m_options;
    NodeHandle* m_node;
    std::map<std::string, data_event_callback_t> m_cb_map;
    std::map<std::string, SubscriberHandle*> m_sub_map;
    std::map<std::string, PublisherHandle*> m_pub_map;
    static inline int m_sequence_id = 20001;
};

class action_listener : public virtual mqtt::iaction_listener
{
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

class conn_callback : public virtual mqtt::callback, public virtual mqtt::iaction_listener
{
    int nretry_;
    mqtt::async_client& cli_;
    mqtt::connect_options& connOpts_;

    void reconnect() {
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        try {
            cli_.connect(connOpts_, nullptr, *this);
        }
        catch (const mqtt::exception& exc) {
            ;
        }
    }

    void connected(const std::string& cause) override;

    void on_failure(const mqtt::token& tok) override {
        BOOST_LOG_TRIVIAL(trace) << "on_failure";
    }

    void on_success(const mqtt::token& tok) override;

    void connection_lost(const std::string& cause) override {
        BOOST_LOG_TRIVIAL(trace) << "connection_lost";
    }

    void message_arrived(mqtt::const_message_ptr msg) override;
public:
    conn_callback(mqtt::async_client& cli, mqtt::connect_options& connOpts)
        : nretry_(0), cli_(cli), connOpts_(connOpts) {}
};


class CommuBackend
{
public:
    enum ConnectionType { DDS_CONNECTION = 0, MQTT_CONNECTION = 1 };

    CommuBackend();
    ~CommuBackend();

    int start();
    int stop();

    int connect_mqtt_server(std::string user_id);
    int connect_dds_device(std::string device_id);
    int send_async(std::string device_id, JsonMsg msg, DdsClient::JsonMsgHandlerFn fn);
    int publish_json_to_device(std::string device_id, std::string json_str);
    mqtt::async_client* get_mqtt() { return m_mqtt_client; }
    int subscribe_device_topic(std::string device_id);

    static std::string get_request_topic(std::string dev_id);
    static std::string get_report_topic(std::string dev_id);
protected:

private:
    enum { BROADCAST_DOMAIN = 1};

    const std::string QOS_FILE = resources_dir() + "/bbl/default_qos.xml";
    const std::string TOPIC_BROADCAST_ALIVE = "device/alive";

    const std::string MQTT_SERVER_ADDRESS = "192.168.0.10:1883";
    const int MQTT_QOS = 0;
    const int MQTT_TIMEOUT = 10;

    DdsClient *m_broadcast_client;
    std::map<std::string, DdsClient*> m_dds_client;         /* key: device_id */
    mqtt::async_client* m_mqtt_client;
    conn_callback* m_mqtt_cb;
};

}

#endif