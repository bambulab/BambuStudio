#include "CommuBackend.hpp"

#include <algorithm>
#include <sstream>
#include <exception>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <wx/progdlg.h>
#include <wx/event.h>

#include "topic_device.h"
#include "topic_devicePubSubTypes.h"
#include "libslic3r/Time.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "DebugToolDialog.hpp"

#include <curl/curl.h>

namespace pt = boost::property_tree;


namespace Slic3r {

    void JsonMsg::set_sequence_id(int seq_id)
    {
        m_sequence_id = seq_id;
        //TODO put seqence_id to child
    }

    DdsClient::DdsClient(int domain)
    {
        memset(&m_options, 0, sizeof(m_options));
        m_options.qos_file_path = DDS_QOS_FILE.c_str();

        m_node = create_node(domain, &m_options);
        assert(m_node != NULL);
    }

    int DdsClient::add_pub_topic(std::string topic, data_callback callback)
    {
        topic_device_jsonPubSubType* pubSubType = new topic_device_jsonPubSubType();
        PublisherHandle* pub = create_publisher(m_node, NULL, pubSubType, topic.c_str(), NULL);
        m_pub_map.insert(std::make_pair(topic, pub));
        return 0;
    }

    int DdsClient::add_sub_topic(std::string topic, data_callback callback)
    {
        data_event_callback_t *cb = new data_event_callback_t();
        cb->callback = callback;
        topic_device_jsonPubSubType *pubSubType = new topic_device_jsonPubSubType();
        SubscriberHandle *sub = create_subscriber(m_node, cb, NULL, pubSubType, topic.c_str(), NULL);
        if (!sub) {
            return -1;
        }
        m_sub_map.insert(std::make_pair(topic, sub));
        return 0;
    }

    int DdsClient::start()
    {
        execute_node(m_node);
        return 0;
    }

    int DdsClient::stop()
    {
        finish_node(m_node);
        return 0;
    }

    int DdsClient::publish_msg(std::string topic, JsonMsg msg)
    {
        std::map<std::string, PublisherHandle*>::iterator it = m_pub_map.find(topic);
        if (it == m_pub_map.end()) {
            return -1;
        }
        msg.set_sequence_id(DdsClient::m_sequence_id++);
        topic_device_json device_msg;
        device_msg.json(msg.json());
        return publish(it->second, &device_msg);
    }

    int MqttClient::connect()
    {
        return 0;
    }

    void on_alive_msg(void* message, message_info_t* message_info, void* priv) {
        topic_device_json* msg = static_cast<topic_device_json*>(message);
        std::istringstream is(msg->json());
        pt::ptree root;
        try {
            // Parse Json, TODO Use JsonMsg
            read_json(is, root);
            boost::optional<std::string> dev_name = root.get_optional<std::string>("dev_name");
            boost::optional<std::string> prod_id = root.get_optional<std::string>("prod_id");
            boost::optional<std::string> dev_id = root.get_optional<std::string>("dev_id");
            boost::optional<std::string> domain_id = root.get_optional<std::string>("domain_id");
            boost::optional<std::string> ip_addr = root.get_optional<std::string>("ip_addr");
            if (dev_id.value_or("0").compare("0") == 0) {
                return;
            }

            // Insert a new device or not
            Slic3r::DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();
            if (!manager->isExist(dev_id.value_or("0"))) {  // Insert a new device
                Slic3r::DeviceInfo* newDevice = new Slic3r::DeviceInfo(dev_id.value_or("0"), dev_name.value_or(""), domain_id.value_or("0"));
                newDevice->m_productId = prod_id.value_or("0");
                newDevice->m_ipAddr = ip_addr.value_or("192.168.0.1");
                manager->add_new_device(newDevice);
            }
            //TODO register update device alive timer
            manager->update_alive_time(dev_id.value_or("0"));
            return;
        }
        catch (std::exception&) {
        }
    }

    void on_device_request_msg(void* message, message_info_t* message_info, void* priv)
    {
        ;
    }

    /* device/[device_id]/[prod_id]/report */
    void on_device_report_msg(void* message, message_info_t* message_info, void* priv)
    {
        topic_device_json* msg = static_cast<topic_device_json*>(message);
        Slic3r::GUI::MainFrame* frame = Slic3r::GUI::wxGetApp().mainframe;
        if (!frame) return;
        GUI::DebugToolDialog* dlg = frame->m_debug_tool_dlg;
        if (dlg) {
            dlg->handle_report_print_msg(msg->json());
        }
    }

    CommuBackend::CommuBackend()
    {
        m_broadcast_client = new DdsClient(BROADCAST_DOMAIN);
        
        m_broadcast_client->add_sub_topic(TOPIC_BROADCAST_ALIVE, on_alive_msg);
    } 

    int CommuBackend::start()
    {
        return m_broadcast_client->start();
    }

    int CommuBackend::stop()
    {
        std::map<std::string, DdsClient*>::iterator it;
        for (it = m_dds_client.begin(); it != m_dds_client.end(); it++) {
            it->second->stop();
        }
        return m_broadcast_client->stop();
    }

    int CommuBackend::connect(ConnectionType type, std::string device_id)
    {
        if (type == DDS_CONNECTION) {
            return connect_dds_device(device_id);
        }
        else if (type == MQTT_CONNECTION) {
            ; //TODO
        }
        return 0;
    }

    int CommuBackend::connect_dds_device(std::string device_id)
    {
        try {
            Slic3r::DeviceManager *manager = Slic3r::GUI::wxGetApp().getDeviceManager();
            if (!manager->isExist(device_id))
                return -1;

            std::map<std::string, DdsClient*>::iterator it = m_dds_client.find(device_id);
            if (it != m_dds_client.end()) {
                return 0;
            }

            int domain_id = manager->getDomainId(device_id);

            DdsClient *client = new DdsClient(domain_id);
            m_dds_client.insert(std::make_pair(device_id, client));
            std::string pub_topic = manager->getRequestTopic(device_id);
            std::string sub_topic = manager->getReportTopic(device_id);

            client->add_pub_topic(pub_topic, on_device_request_msg);
            client->add_sub_topic(sub_topic, on_device_report_msg);

            return client->start();
        }
        catch (std::exception& e)
        {
            return -1;
        }
    }

    int CommuBackend::send_async(std::string device_id, JsonMsg msg, DdsClient::JsonMsgHandlerFn fn)
    {
        std::map<std::string, DdsClient*>::iterator it = m_dds_client.find(device_id);
        if (it == m_dds_client.end()) return -1;

        Slic3r::DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();
        std::string topic = manager->getRequestTopic(device_id);
        return it->second->publish_msg(topic, msg);
    }

    int CommuBackend::publish_json_to_device(std::string device_id, std::string json_str)
    {
        JsonMsg msg(json_str);

        Slic3r::DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();
        if (!manager->isExist(device_id)) {
            return -1;
        }

        std::map<std::string, DdsClient*>::iterator it = m_dds_client.find(device_id);
        if (it == m_dds_client.end()) {
            connect_dds_device(device_id);
            if (m_dds_client.find(device_id) == m_dds_client.end()) {
                return -1;
            }
        }

        return send_async(device_id, msg, NULL);
    }

    CommuBackend::~CommuBackend()
    {
    }
}