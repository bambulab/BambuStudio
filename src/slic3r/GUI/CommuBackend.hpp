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
#include "MQTTAsync.h"


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

private:
    fdal_init_options_t m_options;
    NodeHandle* m_node;
    std::map<std::string, data_event_callback_t> m_cb_map;
    std::map<std::string, SubscriberHandle*> m_sub_map;
    std::map<std::string, PublisherHandle*> m_pub_map;
    static inline int m_sequence_id = 20001;
};

class MqttClient
{
public:
    MqttClient() {}
    ~MqttClient() {}

    int connect();
private:

};

class CommuBackend
{
public:
    enum ConnectionType { DDS_CONNECTION = 0, MQTT_CONNECTION = 1 };

    CommuBackend();
    ~CommuBackend();

    int start();
    int stop();

    int connect(ConnectionType type, std::string device_id);
    int send_async(std::string device_id, JsonMsg msg, DdsClient::JsonMsgHandlerFn fn);
    int publish_json_to_device(std::string device_id, std::string json_str);
    
protected:

private:
    enum { BROADCAST_DOMAIN = 1};

    const std::string QOS_FILE = resources_dir() + "/bbl/default_qos.xml";
    const std::string TOPIC_BROADCAST_ALIVE = "device/alive";

    DdsClient *m_broadcast_client;
    std::map<std::string, DdsClient*> m_dds_client;         /* key: device_id */
    std::map<std::string, MqttClient*> m_mqtt_client;       /* key: device_id */

    int connect_dds_device(std::string device_id);
};

}

#endif