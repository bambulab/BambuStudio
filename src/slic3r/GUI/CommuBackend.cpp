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

#if defined(__WINDOWS__)
SOCKET ssdp_sock_list[MAX_SOCKET_NUM];
SOCKET broadcast_sock_list[MAX_SOCKET_NUM];
#endif

namespace Slic3r {

    void SsdpDiscovery::on_sdp_alive(std::string dev_id, std::string dev_ip)
    {
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

#if defined(__WINDOWS__)
    int SsdpDiscovery::send_msg(int card_no)
    {
        while (!sdp_quit) {
            if (keep_sending)
                bbl_send_ssdp_msg(broadcast_sock_list[card_no]);
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
                if (strncmp(packet.st, SDP_BBL_DEVICE, strlen(SDP_BBL_DEVICE)) == 0) {
                    if (strlen(packet.usn) < strlen(SDP_BBL_DEVICE)) {
                        this->on_sdp_alive(std::string(packet.usn), std::string(packet.location));
                    }
                }
            }
            boost::this_thread::sleep_for(boost::chrono::milliseconds(200));
        }
    }
#elif defined(__APPLE__)

    int show_neighbor_list(lssdp_ctx * lssdp)
    {
        int i = 0;
        lssdp_nbr * nbr;
        SsdpDiscovery* discovery = (SsdpDiscovery*)lssdp->context;
        for (nbr = lssdp->neighbor_list; nbr != NULL; nbr = nbr->next) {
            discovery->on_sdp_alive(nbr->usn, nbr->location);
            BOOST_LOG_TRIVIAL(trace) << "ip = " << nbr->location << "name=" << nbr->usn;
        }
        return 0;
    }

    int show_interface_list_and_rebind_socket(lssdp_ctx * lssdp)
    {
        // 1. show interface list
        BOOST_LOG_TRIVIAL(trace) << "Network Interface List number=" <<  lssdp->interface_num;
        size_t i;
        for (i = 0; i < lssdp->interface_num; i++) {
            BOOST_LOG_TRIVIAL(trace) << "interface " << i + 1 << ": " << lssdp->interface[i].name << ": " << lssdp->interface[i].ip;
        }

        // 2. re-bind SSDP socket
        if (lssdp_socket_create(lssdp) != 0) {
            BOOST_LOG_TRIVIAL(trace) << "SSDP create socket failed";
            return -1;
        }

        return 0;
    }

    void SsdpDiscovery::ssdp_thread()
    {
        sdp_quit = false;

        while(!sdp_quit) {
            fd_set fs;
            FD_ZERO(&fs);
            FD_SET(lssdp.sock, &fs);
            struct timeval tv = {
                .tv_usec = 500 * 1000   // 500 ms
            };
            int ret = select(lssdp.sock + 1, &fs, NULL, NULL, &tv);
            if (ret < 0) {
                BOOST_LOG_TRIVIAL(trace) << "select error, ret=" << ret;
                break;
            }

            if (ret > 0) {
                lssdp_socket_read(&lssdp);
            }
            // get current time
            long long current_time = get_current_time();
            if (current_time < 0) {
                printf("got invalid timestamp %lld\n", current_time);
                break;
            }

            // doing task per 5 seconds
            if (current_time - last_time >= 5000) {
                lssdp_network_interface_update(&lssdp); // 1. update network interface
                lssdp_send_msearch(&lssdp);             // 2. send M-SEARCH
                lssdp_neighbor_check_timeout(&lssdp);   // 3. check neighbor timeout
                last_time = current_time;               // update last_time
            }
        }
    }
#endif


    SsdpDiscovery::SsdpDiscovery()
    {
        
#if defined(__WINDOWS__)
        /* init windows socket */
        bbl_init_socket();
#elif defined(__APPLE__)
        lssdp.context = this;
        lssdp.port = 1990;
        lssdp.neighbor_timeout = 150000;
        strcpy(lssdp.header.search_target, "urn:bambulab-com:device:3dprinter:1");
        strcpy(lssdp.header.unique_service_name ,"slicer_service_name");
        strcpy(lssdp.header.sm_id, "slicer_sm_id");
        strcpy(lssdp.header.device_type, "DEV_TYPE_SLICER");
        strcpy(lssdp.header.location.suffix, ":5678");
        lssdp.neighbor_list_changed_callback = show_neighbor_list;
        lssdp.network_interface_changed_callback = show_interface_list_and_rebind_socket;
        // BBS: fix crash
        lssdp.neighbor_list = NULL;
        lssdp.packet_received_callback = NULL;
#endif
        keep_sending = false;
    }

    void SsdpDiscovery::start_discover()
    {
        sdp_quit = false;
#if defined(__WINDOWS__)
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
#elif defined(__APPLE__)
        lssdp_network_interface_update(&lssdp);
        try {
            boost::thread _thread = Slic3r::create_thread([this]{this->ssdp_thread(); });
        }
        catch (std::exception& e)
        {
            BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::start_discover(), exception=" << e.what();
        }
#endif
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
