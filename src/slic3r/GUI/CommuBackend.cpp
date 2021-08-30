#include "CommuBackend.hpp"

#include <algorithm>
#include <sstream>
#include <exception>
#include <boost/format.hpp>
#include <boost/bind.hpp>
#include <boost/log/trivial.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <wx/event.h>

#include "libslic3r/Time.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "libslic3r/Utils.hpp"
#include "Ssdp.hpp"

#include "libslic3r/Thread.hpp"

#include <thread>
#include <mutex>
#include <curl/curl.h>

#define SDP_BBL_DEVICE      "bambulab-com:device:3dpprinter:1"
#define SDP_NT_STR          "NT:"
#define SDP_LOCATION_STR    "LOCATION:"
#define SDP_USN_STR         "USN:"

namespace pt = boost::property_tree;

#if defined(_WIN32)
SOCKET ssdp_sock_list[MAX_SOCKET_NUM];
#else
int ssdp_sock_list[MAX_SOCKET_NUM];
#endif

namespace Slic3r {

    /* mqtt cloud connection callbacks */
    void cloud_conn_callback::reconnect()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        try {
            BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::reconnect()  connecting...";
            cli_.connect(connOpts_, nullptr, *this);
        }
        catch (const mqtt::exception& exc) {
            BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::reconnect() exception:" << exc.get_message();
        }
        catch (std::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::reconnect() exception:" << e.what();
        }
    }

    void cloud_conn_callback::connected(const std::string& cause)
    {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::connected!";
        /* subscribe binded device */
        /* TODO subscribe cloud online device topics */
        /* subscribe device reqeust and report */
        /* subscribe connected */
        /* update sub_topics */
        BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::connected!";
        /* subscribe current device reqeust and report */
        try {
            for (int i = 0; i < sub_topics.size(); i++) {
                action_listener* sub_listener = new action_listener("Subscriber_" + sub_topics[i]);
                cli_.subscribe(sub_topics[i], 0, nullptr, *sub_listener);
            }
        }
        catch (mqtt::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::connected, exception=" << e.what();
        }
    }

    void cloud_conn_callback::on_failure(const mqtt::token& tok)
    {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::on_failure, Connection(mqtt) failed! retry=" << nretry_;
        /* TODO mqtt connect failed tips */
        ++nretry_;
        reconnect();
    }

    void cloud_conn_callback::on_success(const mqtt::token& tok)
    {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::on_success, Connection(mqtt) OK!";
        /* mqtt connect on success tips, same as connected */
    }  

    void cloud_conn_callback::connection_lost(const std::string& cause) {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::connection_lost!, cause =" << cause;
    }

    void cloud_conn_callback::message_arrived(mqtt::const_message_ptr msg)
    {
        /* handle message in CommuBackend */
        Slic3r::CommuBackend* backend = Slic3r::GUI::wxGetApp().getCommuBackend();
        if (backend)
            backend->handle_cloud_msg(msg->get_topic(), msg->get_payload_str());
    }

    /* mqtt client connection callbacks */
    void client_conn_callback::reconnect()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        try {
            BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::reconnect()  connecting...";
            cli_.connect(connOpts_, nullptr, *this);
        }
        catch (const mqtt::exception& exc) {
            BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::reconnect() exception:" << exc.get_message();
        }
        catch (std::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::reconnect() exception:" << e.what();
        }
    }

    void client_conn_callback::connected(const std::string& cause)
    {
        BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::connected!";
        /* subscribe current device reqeust and report */
        try {
            if (succussFn) {
                succussFn(cli_.get_client_id());
            }
            
            for (int i = 0; i < sub_topics.size(); i++) {
                action_listener* sub_listener = new action_listener("Subscriber_" + sub_topics[i]);
                cli_.subscribe(sub_topics[i], 0, nullptr, *sub_listener);
            }
            
        } catch (mqtt::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::connected, exception=" << e.what();
        }
    }

    void client_conn_callback::on_failure(const mqtt::token& tok)
    {
        BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::on_failure, Connection(mqtt) failed! retry=" << nretry_;
        /* mqtt connect failed tips */
        if (failedFn) {
            failedFn(cli_.get_client_id());
        }
        ++nretry_;
        reconnect();
    }

    void client_conn_callback::on_success(const mqtt::token& tok)
    {
        //BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::on_success, Connection(mqtt) OK!";
        /* TODO mqtt connect on success tips, update UI */
    }

    void client_conn_callback::connection_lost(const std::string& cause) {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::connection_lost!, cause =" << cause;
        if (lostFn) {
            lostFn(cli_.get_client_id());
        }
    }

    void client_conn_callback::message_arrived(mqtt::const_message_ptr msg)
    {
        /* handle message in CommuBackend */
        Slic3r::CommuBackend* backend = Slic3r::GUI::wxGetApp().getCommuBackend();
        if (backend) {
            backend->handle_client_msg(msg->get_topic(), msg->get_payload_str());
        }
    }

    void client_conn_callback::delivery_complete(mqtt::delivery_token_ptr tok)
    {
        BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::delivery_complete.";
    }

    void client_conn_callback::set_connect_fns(SuccessFn sFn, FailedFn fFn, LostFn lFn)
    {
        succussFn = sFn;
        failedFn = fFn;
        lostFn = lFn;
    }

    void SsdpDiscovery::on_sdp_alive(std::string dev_id, std::string dev_ip)
    {
        BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::on_sdp_alive, dev_id=" << dev_id << ", dev_ip=" << dev_ip;

        if (dev_ip.empty()) return;

        try {
            // Insert a new device or not
            Slic3r::DeviceManager* device_manager = Slic3r::GUI::wxGetApp().getDeviceManager();
            if (device_manager) {
                if (!device_manager->isExist(dev_id)) {  // Insert a new device
                    Slic3r::DeviceInfo* new_device = new Slic3r::DeviceInfo(dev_id, dev_ip, MQTT_CONNECTION);
                    new_device->set_ip_addr(dev_ip);
                    device_manager->add_new_device(new_device);
                }
                device_manager->update_alive_time(dev_id);
            }
            return;
        }
        catch (std::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::on_sdp_alive, exception=" << e.what();
        }
    }

    void SsdpDiscovery::recv_sdp_msg(int card_no)
    {
        char rece_buff[BUFSIZE];
        int recv_size;
        lssdp_packet packet;
        while (!sdp_quit) {
            memset(&packet, 0, sizeof(packet));
            memset(rece_buff, 0, BUFSIZE);
            bbl_read_from_ssdp(ssdp_sock_list[card_no], rece_buff, &recv_size, BUFSIZE);
            int result = lssdp_packet_parser(rece_buff, recv_size, &packet);
            if (result >= 0) {
                BOOST_LOG_TRIVIAL(trace) << "Location=" << packet.location << ", USN=" << packet.usn << ", ST=" << packet.st;
                if (strncmp(packet.st, "urn:bambulab-com:device:3dprinter:1", 20) == 0) {
                    if (strlen(packet.usn) < 20) {
                        this->on_sdp_alive(std::string(packet.usn), std::string(packet.location));
                    }
                    else {
                        BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::recv_sdp_msg, invalid device_id!";
                    }
                }
            }
            else {
                BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::recv_sdp_msg, parser failed!";
            }
            boost::this_thread::sleep_for(boost::chrono::milliseconds(200));
        }
    }

    SsdpDiscovery::SsdpDiscovery()
    {
        /* init windows socket */
        bbl_init_socket();
    }

    void SsdpDiscovery::start_discover()
    {
        /* create thread to recv ssdp message */
        int card_numbers = bbl_get_network_card_list(ssdp_sock_list, MAX_SOCKET_NUM);
        for (int i = 0; i < card_numbers; i++) {
            try {
                boost::thread recv_thread = Slic3r::create_thread([this, i] {this->recv_sdp_msg(i); });
            }
            catch (std::exception& e)
            {
                BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::start_discover(), exception=" << e.what();
            }
        }
    }

    void SsdpDiscovery::stop_discover()
    {
        sdp_quit = true;
        return;
    }


    CommuBackend::CommuBackend()
        :conn_cloud_opt(mqtt::connect_options_builder().clean_session().finalize()),
        mqtt_cloud_user("bbl_mqtt"),
        mqtt_cloud_pwd("emqx@204"),
        m_mqtt_cloud(nullptr),
        conn_cli_opt(mqtt::connect_options_builder().clean_session(true).finalize()),
        m_mqtt_cli(nullptr),
        m_mqtt_cli_cb(nullptr)
    {
        /* init mqtt connect options */
        conn_cloud_opt.set_user_name(mqtt_cloud_user);
        conn_cloud_opt.set_password(mqtt_cloud_pwd);

        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        m_mqtt_uuid = to_string(uuid);
        ssdp = new SsdpDiscovery();
        assert(ssdp);
    }

    int CommuBackend::start()
    {
        ssdp->start_discover();
        return 0;
    }

    int CommuBackend::stop()
    {
        ssdp->stop_discover();
        if (m_mqtt_cli) {
            try {
                m_mqtt_cli->disconnect();
            }
            catch (mqtt::exception& e) {
                return 0;
            }
        }

        if (m_mqtt_cloud) {
            try {
                m_mqtt_cloud->disconnect();
            }
            catch (mqtt::exception& e) {
                return 0;
            }
        }
        return 0;
    }

    int CommuBackend::connect_to_cloud(std::string user_id)
    {
        try {
            std::string client_id = (boost::format("%1%:%2%") % user_id % m_mqtt_uuid).str();
            m_mqtt_cloud = new mqtt::async_client(MQTT_SERVER_ADDRESS, client_id);
            m_mqtt_cloud_cb = new cloud_conn_callback(*m_mqtt_cloud, conn_cloud_opt);
            /* TODO add topics */
            m_mqtt_cloud->set_callback(*m_mqtt_cloud_cb);
            m_mqtt_cloud->connect(conn_cloud_opt, nullptr, *m_mqtt_cloud_cb);
            return 0;
        }
        catch (mqtt::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "connect_to_cloud exception:" << e.what();
            return -1;
        }
        return 0;
    }

    int CommuBackend::disconnect_to_cloud()
    {
        if (m_mqtt_cloud) {
            m_mqtt_cloud->disconnect();
        }
        return 0;
    }

    int CommuBackend::connect_to_client(std::string host, std::string user_id, std::string device_id, SuccessFn cFn, FailedFn fFn, LostFn lFn)
    {
        try {
            std::string client_id = user_id;
            m_mqtt_cli = new mqtt::async_client(host, client_id);
            m_mqtt_cli_cb = new client_conn_callback(*m_mqtt_cli, conn_cli_opt);
            m_mqtt_cli_cb->set_connect_fns(cFn, fFn, lFn);
            /* add topics */
            m_mqtt_cli_cb->add_topics(get_report_topic(device_id));
            m_mqtt_cli->set_callback(*m_mqtt_cli_cb);
            m_mqtt_cli->connect(conn_cli_opt, nullptr, *m_mqtt_cli_cb);
            return 0;
        }
        catch (mqtt::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "connect_to_client exception:" << e.what();
            return -1;
        }
        return 0;
    }


    int CommuBackend::disconnect_to_client()
    {
        if (m_mqtt_cli) {
            m_mqtt_cli->disconnect();
        }
        return 0;
    }

    void CommuBackend::handle_cloud_msg(std::string topic, std::string payload)
    {
        if (m_msg_recv_fn) {
            m_msg_recv_fn(topic, payload);
        }
        return;
    }

    int CommuBackend::publish_json_to_client(std::string device_id, std::string json_str)
    {
        BOOST_LOG_TRIVIAL(trace) << "CommuBackend::publish_json_to_client start";
        if (m_mqtt_cli) {
            // !!!blocking in is_connected() !!!
            //if (m_mqtt_cli->is_connected()) {
                std::string topic = get_request_topic(device_id);
                json_str += '\0';
                mqtt::message_ptr pubmsg = mqtt::make_message(topic, json_str.c_str(), json_str.size());
                pubmsg->set_qos(0);
                BOOST_LOG_TRIVIAL(trace) << "CommuBackend::publish_json_to_client start 1";
                mqtt::delivery_token_ptr token = m_mqtt_cli->publish(pubmsg);
                BOOST_LOG_TRIVIAL(trace) << "CommuBackend::publish_json_to_client start 2";
                if (m_msg_send_fn) {
                    m_msg_send_fn(topic, json_str);
                }
                BOOST_LOG_TRIVIAL(trace) << "CommuBackend::publish_json_to_client finished, mqtt publish topic = " << topic << ", msg = " << json_str;
                return 0;
            /*}
            else {
                BOOST_LOG_TRIVIAL(trace) << "CommuBackend::publish_json_to_client finished";
                return -1;
            }*/
        }
        else {
            BOOST_LOG_TRIVIAL(trace) << "CommuBackend::publish_json_to_client finished";
            return -1;
        }
    }

    int CommuBackend::publish_json_to_cloud(std::string device_id, std::string json_str)
    {
        if (m_mqtt_cloud) {
            if (m_mqtt_cloud->is_connected()) {
                std::string topic = get_request_topic(device_id);
                mqtt::message_ptr pubmsg = mqtt::make_message(topic, json_str);
                pubmsg->set_qos(0);
                mqtt::delivery_token_ptr token = m_mqtt_cloud->publish(pubmsg);
                if (m_msg_send_fn) {
                    m_msg_send_fn(topic, json_str);
                }
                BOOST_LOG_TRIVIAL(trace) << "CommuBackend::publish_json_to_cloud, mqtt publish topic = " << topic << ", msg = " << json_str;
                return 0;
            }
        }
        return 0;
    }

    void CommuBackend::handle_client_msg(std::string topic, std::string payload)
    {
        if (m_msg_recv_fn) {
            m_msg_recv_fn(topic, payload);
        }
        return;
    }

    CommuBackend::~CommuBackend()
    {
        this->stop();
    }

    std::string CommuBackend::get_request_topic(std::string device_id)
    {
        return (boost::format("device/%1%/request") % device_id).str();
    }

    std::string CommuBackend::get_report_topic(std::string device_id)
    {
        return (boost::format("device/%1%/report") % device_id).str();
    }

    std::string CommuBackend::get_connect_topic(std::string device_id)
    {
        return (boost::format("device/%1%/connect") % device_id).str();
    }
}
