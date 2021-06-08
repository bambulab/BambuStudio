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

    int DdsClient::wait_for_client(std::string topic)
    {
        std::map<std::string, PublisherHandle*>::iterator it = m_pub_map.find(topic);
        int result = wait_for_subscriber(it->second, 1000);
        if (result == 1) {
            BOOST_LOG_TRIVIAL(trace) << "wait for client OK! topic=" << topic;
        }
        else {
            BOOST_LOG_TRIVIAL(trace) << "wait for client Failed! topic=" << topic;
        }
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
        BOOST_LOG_TRIVIAL(trace) << "publish topic = " << topic << ", msg = " << msg.json();
        return publish(it->second, &device_msg);
    }

    void conn_callback::connected(const std::string& cause)
    {
        BOOST_LOG_TRIVIAL(trace) << "connected";
        Slic3r::CommuBackend* backend = Slic3r::GUI::wxGetApp().getCommuBackend();
        Slic3r::DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();
        std::vector<std::string> list = manager->get_connected_device_list();
        std::vector<std::string>::iterator it;
        for (it = list.begin(); it != list.end(); it++) {
            std::string topic = backend->get_report_topic(*it);
            backend->subscribe_device_topic(*it);
        }
    }

    void conn_callback::on_success(const mqtt::token& tok)
    {
        BOOST_LOG_TRIVIAL(trace) << "on_success";
    }

    void conn_callback::message_arrived(mqtt::const_message_ptr msg)
    {
        BOOST_LOG_TRIVIAL(trace) << "message arrived";
        std::string topic = msg->get_topic();
        std::string payload = msg->get_payload_str();
    }

    void on_alive_msg(void* message, message_info_t* message_info, void* priv) {
        topic_device_json* msg = static_cast<topic_device_json*>(message);
        std::istringstream is(msg->json());
        BOOST_LOG_TRIVIAL(trace) << "on_alive_msg json = " << msg->json();

        pt::ptree root;
        try {
            // Parse Json, TODO Use JsonMsg
            read_json(is, root);
            boost::optional<std::string> dev_name = root.get_optional<std::string>("dev_name");
            boost::optional<std::string> prod_id = root.get_optional<std::string>("prod_id");
            boost::optional<std::string> dev_id = root.get_optional<std::string>("dev_id");
            boost::optional<std::string> domain_id = root.get_optional<std::string>("domain_id");
            boost::optional<std::string> ip_addr = root.get_optional<std::string>("ip_addr");
            if (!dev_id.has_value() || !domain_id.has_value() || !dev_name.has_value() || !prod_id.has_value() || !ip_addr.has_value()) {
                BOOST_LOG_TRIVIAL(trace) << "on_alive_msg parse json failed! json = " << msg->json();
                return;
            }

            // Insert a new device or not
            Slic3r::DeviceManager* device_manager = Slic3r::GUI::wxGetApp().getDeviceManager();
            if (!device_manager->isExist(dev_id.value())) {  // Insert a new device
                Slic3r::DeviceInfo* newDevice = new Slic3r::DeviceInfo(dev_id.value(), dev_name.value(), domain_id.value());
                newDevice->m_productId = prod_id.value();
                newDevice->m_ipAddr = ip_addr.value();
                device_manager->add_new_device(newDevice);
            }
            //TODO register update device alive timer
            device_manager->update_alive_time(dev_id.value());
            Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
            if (account_manager->is_user_login()) {
                if (!device_manager->has_bind_status(dev_id.value())) {
                    account_manager->query_bind_status(dev_id.value());
                }
            }
            
            return;
        }
        catch (std::exception &e) {

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
        BOOST_LOG_TRIVIAL(trace) << "received device report msg = " << msg->json();
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

        m_mqtt_client = NULL;
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

    int CommuBackend::connect_mqtt_server(std::string user_id)
    {
        try {
            m_mqtt_client = new mqtt::async_client(MQTT_SERVER_ADDRESS, user_id);
            auto conn_opt = mqtt::connect_options_builder().clean_session().finalize();
            m_mqtt_cb = new conn_callback(*m_mqtt_client, conn_opt);
            m_mqtt_client->set_callback(*m_mqtt_cb);
            mqtt::token_ptr token = m_mqtt_client->connect(conn_opt, nullptr, *m_mqtt_cb);
            //token->wait();
        }
        catch (mqtt::exception& e) {
            m_mqtt_client = NULL;
            return -1;
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

            client->start();
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

        /*
        if (m_mqtt_client) {
            if (m_mqtt_client->is_connected()) {
                std::string topic = manager->getRequestTopic(device_id);
                mqtt::message_ptr pubmsg = mqtt::make_message(topic, msg.json());
                pubmsg->set_qos(0);
                mqtt::delivery_token_ptr token = m_mqtt_client->publish(pubmsg);
            }
        }
        */

        
        std::map<std::string, DdsClient*>::iterator it = m_dds_client.find(device_id);
        if (it == m_dds_client.end()) {
            connect_dds_device(device_id);
            it = m_dds_client.find(device_id);
            if (it == m_dds_client.end()) {
                return -1;
            }
            std::string topic = manager->getRequestTopic(device_id);
            it->second->wait_for_client(topic);
        }
        return send_async(device_id, msg, NULL);
        
        
        return 0;
    }

    int CommuBackend::subscribe_device_topic(std::string device_id) {
        try {
            std::string topic = get_report_topic(device_id);
            action_listener* sub_listener = new action_listener("Subscription");
            BOOST_LOG_TRIVIAL(trace) << "subscribe topic = " << topic;
            m_mqtt_client->subscribe(topic, 0, nullptr, *sub_listener);
            return 0;
        }
        catch (mqtt::exception& e) {
            return -1;
        }
    }

    CommuBackend::~CommuBackend()
    {
    }

    std::string CommuBackend::get_request_topic(std::string dev_id)
    {
        Slic3r::DeviceManager* device_manager = Slic3r::GUI::wxGetApp().getDeviceManager();
        std::string prod_id = device_manager->getProductId(dev_id);
            return (boost::format("device/%1%/%2%/request") % prod_id % dev_id).str();
    }

    std::string CommuBackend::get_report_topic(std::string dev_id)
    {
        Slic3r::DeviceManager* device_manager = Slic3r::GUI::wxGetApp().getDeviceManager();
        std::string prod_id = device_manager->getProductId(dev_id);
        return (boost::format("device/%1%/%2%/report") % prod_id % dev_id).str();
    }
}