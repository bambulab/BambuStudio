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
        cb->priv = new std::string(topic);
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
        BOOST_LOG_TRIVIAL(trace) << "dds publish topic = " << topic << ", msg = " << msg.json();
        return publish(it->second, &device_msg);
    }

    void conn_callback::reconnect()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        try {
            cli_.connect(connOpts_, nullptr, *this);
        }
        catch (const mqtt::exception& exc) {
            BOOST_LOG_TRIVIAL(trace) << exc.get_message();
        }
        catch (std::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << e.what();
        }
    }

    void conn_callback::connected(const std::string& cause)
    {
        BOOST_LOG_TRIVIAL(trace) << "connected";
        /* subscribe binded device */
        Slic3r::CommuBackend* backend = Slic3r::GUI::wxGetApp().getCommuBackend();
        Slic3r::DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();
        std::vector<std::string> list = manager->get_free_and_self_device_list();
        std::vector<std::string>::iterator it;
        for (it = list.begin(); it != list.end(); it++) {
            std::string topic = backend->get_report_topic(*it);
            backend->subscribe_device_topic(*it);
        }
    }

    void conn_callback::on_failure(const mqtt::token& tok)
    {
        BOOST_LOG_TRIVIAL(trace) << "Connection(mqtt) attempt failed! retry=" << nretry_;
        if (nretry_ == 0) {
            Slic3r::GUI::wxGetApp().show_message_box("Connection(mqtt) attempt failed!");
        }
        ++nretry_;
        reconnect();
    }

    void conn_callback::on_success(const mqtt::token& tok)
    {
        BOOST_LOG_TRIVIAL(trace) << "Connection(mqtt) connect ok!";
    }  

    void conn_callback::connection_lost(const std::string& cause) {
        BOOST_LOG_TRIVIAL(trace) << "Connection(mqtt) lost";
        if (!cause.empty()) {
            BOOST_LOG_TRIVIAL(trace) << cause;
        }
        Slic3r::GUI::wxGetApp().show_message_box("Mqtt Connection Lost! Reconnecting...");
        BOOST_LOG_TRIVIAL(trace) << "Reconnecting... ";
        nretry_ = 0;
        reconnect();
    }

    void conn_callback::message_arrived(mqtt::const_message_ptr msg)
    {
        std::string topic = msg->get_topic();
        std::string payload = msg->get_payload_str();

        Slic3r::CommuBackend* backend = Slic3r::GUI::wxGetApp().getCommuBackend();
        // backend->print_info("received mqtt msg = " + payload);
        backend->on_report_msg(topic, payload);
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
            if (!dev_id.has_value() || !domain_id.has_value() || !dev_name.has_value() || !ip_addr.has_value()) {
                BOOST_LOG_TRIVIAL(trace) << "on_alive_msg parse json failed! json = " << msg->json();
                return;
            }

            // Insert a new device or not
            Slic3r::DeviceManager* device_manager = Slic3r::GUI::wxGetApp().getDeviceManager();
            if (!device_manager->isExist(dev_id.value())) {  // Insert a new device
                Slic3r::DeviceInfo* new_device = new Slic3r::DeviceInfo(dev_id.value(), dev_name.value(), DDS_CONNECTION);
                new_device->set_domain_id(domain_id.value());
                new_device->set_ip_addr(ip_addr.value());
                device_manager->add_new_device(new_device);
            }
            device_manager->update_alive_time(dev_id.value());
            Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
            if (account_manager->is_user_login()) {
                account_manager->query_bind_status(dev_id.value());
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
        if (priv) {
            SubscriberHandle* sub = (SubscriberHandle*)priv;
            Slic3r::CommuBackend* backend = Slic3r::GUI::wxGetApp().getCommuBackend();
            std::string* topic = (std::string*)priv;
            backend->on_report_msg(*topic, msg->json());
        }
    }

    CommuBackend::CommuBackend()
        :conn_opt(mqtt::connect_options_builder().clean_session().finalize()),
         MQTT_USERNAME("bbl_mqtt"),
         MQTT_PASSWORD("emqx@204")
    {
        m_broadcast_client = new DdsClient(BROADCAST_DOMAIN);
        m_broadcast_client->add_sub_topic(TOPIC_BROADCAST_ALIVE, on_alive_msg);
        conn_opt.set_user_name(MQTT_USERNAME);
        conn_opt.set_password(MQTT_PASSWORD);
        m_mqtt_client = nullptr;
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
            if (m_mqtt_client) {
                return 0;
            }

            m_mqtt_client = new mqtt::async_client(MQTT_SERVER_ADDRESS, user_id);
            m_mqtt_cb = new conn_callback(*m_mqtt_client, conn_opt);
            m_mqtt_client->set_callback(*m_mqtt_cb);
            mqtt::token_ptr token = m_mqtt_client->connect(conn_opt, nullptr, *m_mqtt_cb);
            return 0;
        }
        catch (mqtt::exception& e) {
            m_mqtt_client = NULL;
            return -1;
        }
        return 0;
    }

    int CommuBackend::disconnect_mqtt_server()
    {
        if (m_mqtt_client) {
            m_mqtt_client->disconnect();
            m_mqtt_client = NULL;
            return 0;
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

            int domain_id = manager->get_domain_id(device_id);
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

#ifdef USE_MQTT_CONTROL
        AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
        if (!account_manager->is_user_login()) {
            Slic3r::GUI::wxGetApp().show_message_box("Please login first!");
            return -1;
        }  
#endif
        if (manager->is_bind_self(device_id))
        {
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
            std::string t = manager->getRequestTopic(device_id);
            print_info("send dds topic=" + t + ", msg = " + msg.json() + " to device " + device_id + " ok!\n");
            send_async(device_id, msg, NULL);
        }
        else {
            Slic3r::GUI::wxGetApp().show_message_box("Please Bind First!");
            return -1;
        }

#ifdef USE_MQTT_CONTROL
        if (m_mqtt_client) {
            if (m_mqtt_client->is_connected()) {
                std::string topic = manager->getRequestTopic(device_id);
                mqtt::message_ptr pubmsg = mqtt::make_message(topic, msg.json());
                pubmsg->set_qos(0);
                mqtt::delivery_token_ptr token = m_mqtt_client->publish(pubmsg);
                BOOST_LOG_TRIVIAL(trace) << "mqtt publish topic = " << topic << ", msg = " << msg.json();
                print_info("send dds topic=" + topic + ", msg = " + msg.json() + " to device " + device_id + " ok!\n");
                return 0;
            }
            else {
                Slic3r::GUI::wxGetApp().show_message_box("MQTT is not connected!");
                return -1;
            }
        }
#endif
        return 0;
    }

    void CommuBackend::on_report_msg(std::string &topic, std::string &payload) {

        BOOST_LOG_TRIVIAL(trace) << "message arrived, topic=" << topic << ", payload=" << payload;

        Slic3r::GUI::MainFrame* frame = Slic3r::GUI::wxGetApp().mainframe;
        if (!frame) return;
        GUI::DebugToolDialog* dlg = frame->m_debug_tool_dlg;
        if (dlg) {
            dlg->handle_report_print_msg(topic, payload);
            dlg->handle_device_report_msg(payload);
        }
    }

    void CommuBackend::print_info(std::string info)
    {
        Slic3r::GUI::MainFrame* frame = Slic3r::GUI::wxGetApp().mainframe;
        if (!frame) return;
        GUI::DebugToolDialog* dlg = frame->m_debug_tool_dlg;
        if (dlg) {
            dlg->append_output_string_info(info);
        }
    }

    int CommuBackend::subscribe_device_topic(std::string device_id) {
        try {
            std::string topic = get_report_topic(device_id);
            action_listener* sub_listener = new action_listener("DeviceReport(Subscriber)");
            BOOST_LOG_TRIVIAL(trace) << "subscribe topic = " << topic;
            /* do not sub now */
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
        return (boost::format("device/%1%/request") % dev_id).str();
    }

    std::string CommuBackend::get_report_topic(std::string dev_id)
    {
        return (boost::format("device/%1%/report") % dev_id).str();
    }
}