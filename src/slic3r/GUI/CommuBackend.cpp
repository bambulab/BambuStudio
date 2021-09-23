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

#define SDP_BBL_DEVICE      "urn:bambulab-com:device:3dprinter:1"
#define SDP_NT_STR          "NT:"
#define SDP_LOCATION_STR    "LOCATION:"
#define SDP_USN_STR         "USN:"

namespace pt = boost::property_tree;

#if defined(_WIN32)
SOCKET ssdp_sock_list[MAX_SOCKET_NUM];
SOCKET broadcast_sock_list[MAX_SOCKET_NUM];
#else
int ssdp_sock_list[MAX_SOCKET_NUM];
SOCKET broadcast_sock_list[MAX_SOCKET_NUM];
#endif

namespace Slic3r {

    void SsdpDiscovery::on_sdp_alive(std::string dev_id, std::string dev_ip)
    {
        BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::on_sdp_alive, dev_id=" << dev_id << ", dev_ip=" << dev_ip;

        if (dev_ip.empty()) return;

        try {
            // Insert a new device or not
            Slic3r::DeviceManager* device_manager = Slic3r::GUI::wxGetApp().getDeviceManager();
            if (device_manager) {
                //TODO get dev_name, use dev_ip instead
                device_manager->on_machine_alive(dev_ip, dev_id, dev_ip);
            }
            return;
        }
        catch (std::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::on_sdp_alive, exception=" << e.what();
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::on_sdp_alive, exception!";
        }
    }

    int SsdpDiscovery::send_msg(int card_no)
    {
        while (!sdp_quit) {
            int result = bbl_send_ssdp_msg(broadcast_sock_list[card_no]);
            boost::this_thread::sleep_for(boost::chrono::milliseconds(4000));
        }
        return 0;
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
                if (strncmp(packet.st, SDP_BBL_DEVICE, strlen(SDP_BBL_DEVICE)) == 0) {
                    if (strlen(packet.usn) < strlen(SDP_BBL_DEVICE)) {
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

    void SsdpDiscovery::recv_broadcast_msg(int card_no)
    {
        char rece_buff[BUFSIZE];
        int recv_size;
        lssdp_packet packet;
        while (!sdp_quit) {
            memset(&packet, 0, sizeof(packet));
            memset(rece_buff, 0, BUFSIZE);
            bbl_read_from_broadcast(broadcast_sock_list[card_no], rece_buff, &recv_size, BUFSIZE);
            int result = lssdp_packet_parser(rece_buff, recv_size, &packet);
            if (result >= 0) {
                BOOST_LOG_TRIVIAL(trace) << "recv_broadcast_msg, Location=" << packet.location << ", USN=" << packet.usn << ", ST=" << packet.st;
                if (strncmp(packet.st, SDP_BBL_DEVICE, strlen(SDP_BBL_DEVICE)) == 0) {
                    if (strlen(packet.usn) < strlen(SDP_BBL_DEVICE)) {
                        this->on_sdp_alive(std::string(packet.usn), std::string(packet.location));
                    }
                    else {
                        BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::recv_broadcast_msg, invalid device_id!";
                    }
                }
            }
            else {
                BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::recv_broadcast_msg, parser failed!";
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
        int card_numbers = bbl_init_multi_socket(ssdp_sock_list, MAX_SOCKET_NUM);
        card_numbers = bbl_init_broadcast_socket(broadcast_sock_list, MAX_SOCKET_NUM);
        for (int i = 0; i < card_numbers; i++) {
            try {
                boost::thread recv_thread = Slic3r::create_thread([this, i] {this->recv_sdp_msg(i); });
                boost::thread send_thread = Slic3r::create_thread([this, i] {this->send_msg(i); });

                boost::thread recv_multi_thread = Slic3r::create_thread([this, i] {this->recv_broadcast_msg(i); });
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
    {
        ssdp = new SsdpDiscovery();
    }

    int CommuBackend::start()
    {
        ssdp->start_discover();
        return 0;
    }

    int CommuBackend::stop()
    {
        ssdp->stop_discover();
        return 0;
    }    

    CommuBackend::~CommuBackend()
    {
        this->stop();
    }
}
