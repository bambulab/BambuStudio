#include "SsdpDiscovery.hpp"

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
#include "Ssdp.hpp"

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

namespace BBL {

    void SsdpDiscovery::on_sdp_alive(std::string dev_id, std::string dev_ip)
    {
        if (dev_ip.empty()) return;

        try {
            // Insert a new device or not
            //TODO
            //Slic3r::DeviceManager* device_manager = Slic3r::GUI::wxGetApp().getDeviceManager();
            //if (device_manager) {
            //    //TODO get dev_name, use dev_ip instead
            //    device_manager->on_machine_alive(dev_ip, dev_id, dev_ip);
            //}
            return;
        }
        catch (std::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::on_sdp_alive, exception=" << e.what();
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::on_sdp_alive, exception!";
        }
    }

    static void on_parse_sdp_message(const char* rece_buff, unsigned int recv_size)
    {
        lssdp_packet packet;
        memset(&packet, 0, sizeof(packet));
        int result = lssdp_packet_parser(rece_buff, recv_size, &packet);
        if (result >= 0) {
            if (strncmp(packet.st, SDP_BBL_DEVICE, strlen(SDP_BBL_DEVICE)) == 0) {
                try {
                    // set printer name to local ip by default
                    std::string printer_name = packet.printer_name;
                    std::string printer_type = packet.printer_type;
                    std::string printer_signal = packet.printer_signal;
                    std::string printer_ip = std::string(packet.location);
                    std::string printer_dev_id = std::string(packet.usn);
                    //TODO
                    /*Slic3r::DeviceManager* device_manager = Slic3r::GUI::wxGetApp().getDeviceManager();
                    if (device_manager) {
                        device_manager->on_machine_alive(printer_name, printer_dev_id, printer_ip, printer_type, printer_signal);
                    }*/
                }
                catch (std::exception& e) {
                    BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::parse_sdp_message, exception=" << e.what();
                }
                catch (...) {
                    BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::parse_sdp_message, exception!";
                }
            }
            else {
                BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery: mismatch packet.st = " << packet.st;
            }
        }
        else {
            BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery: lssdp_packet_parser error = " << result;
        }
    }

    void SsdpDiscovery::parse_sdp_message(const char *rece_buff, unsigned int recv_size)
    {
        lssdp_packet packet;
        memset(&packet, 0, sizeof(packet));
        int result = lssdp_packet_parser(rece_buff, recv_size, &packet);
        if (result >= 0) {
            if (strncmp(packet.st, SDP_BBL_DEVICE, strlen(SDP_BBL_DEVICE)) == 0) {
                try {
                    // set printer name to local ip by default
                    std::string printer_name = packet.printer_name;
                    std::string printer_type = packet.printer_type;
                    std::string printer_signal = packet.printer_signal;
                    std::string printer_ip = std::string(packet.location);
                    std::string printer_dev_id = std::string(packet.usn);
                    if (alive_fn) {
                        alive_fn(printer_name, printer_dev_id, printer_ip, printer_type, printer_signal);
                    }
                }
                catch (std::exception& e) {
                    BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::parse_sdp_message, exception=" << e.what();
                }
                catch (...) {
                    BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::parse_sdp_message, exception!";
                }
            }
            else {
                BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery: mismatch packet.st = " << packet.st;
            }
        }
        else {
            BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery: lssdp_packet_parser error = " << result;
        }
    }

#if defined(__WINDOWS__)
    int SsdpDiscovery::send_msg(int card_no)
    {
        int count = 0;
        while (!sdp_quit) {
            try {
                if (keep_sending && (count % 100) == 0)
                    bbl_send_ssdp_msg(broadcast_sock_list[card_no]);
                count++;
                boost::this_thread::sleep_for(boost::chrono::milliseconds(50));
            } catch(...) {
                ;
            }
        }
        return 0;
    }

    void SsdpDiscovery::recv_sdp_msg(int card_no)
    {
        char buff[BUFSIZE];
        int size;
        while (!sdp_quit) {
            try {
                memset(buff, 0, BUFSIZE);
                bbl_read_from_ssdp(ssdp_sock_list[card_no], buff, &size, BUFSIZE);
                parse_sdp_message(buff, size);
                boost::this_thread::sleep_for(boost::chrono::milliseconds(200));
            }
            catch(...) {
                ;
            }
        }
    }

    void SsdpDiscovery::recv_broadcast_msg(int card_no)
    {
        char buff[BUFSIZE];
        int size;
        lssdp_packet packet;
        while (!sdp_quit) {
            try {
                memset(&packet, 0, sizeof(packet));
                memset(buff, 0, BUFSIZE);
                bbl_read_from_broadcast(broadcast_sock_list[card_no], buff, &size, BUFSIZE);
                parse_sdp_message(buff, size);
                boost::this_thread::sleep_for(boost::chrono::milliseconds(200));
            }
            catch(...) {
                ;
            }
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

    static int on_packet_received(struct lssdp_ctx* lssdp, const char* packet, size_t packet_len)
    {
        BOOST_LOG_TRIVIAL(trace) << "ssdp_mac: on_packet_received, packet = " << packet << ", size = " << packet_len;
        on_parse_sdp_message(packet, packet_len);
        return 0;
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
        lssdp.packet_received_callback = on_packet_received;
#endif
        keep_sending = false;
    }

    void SsdpDiscovery::start()
    {
        if (!m_started) {
            m_started = true;
        }

        sdp_quit = false;
#if defined(__WINDOWS__)
        /* create thread to recv ssdp message */
        card_number  = bbl_init_multi_socket(ssdp_sock_list, MAX_SOCKET_NUM);
        card_number = bbl_init_broadcast_socket(broadcast_sock_list, MAX_SOCKET_NUM);
        for (int i = 0; i < card_number; i++) {
            try {
                recv_thread_list[i] = boost::thread([this, i] { this->recv_sdp_msg(i); });
                send_thread_list[i] = boost::thread([this, i] { this->send_msg(i); });
                recv_broadcast_thread_list[i] = boost::thread([this, i] { this->recv_broadcast_msg(i); });
                BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::start_discover(), create thread " << i;
            }
            catch (std::exception& e)
            {
                BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::start_discover(), exception=" << e.what();
            }
        }
#elif defined(__APPLE__)
        lssdp_network_interface_update(&lssdp);
        try {
            boost::thread _thread = boost::thread([this]{this->ssdp_thread(); });
        }
        catch (std::exception& e)
        {
            BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::start_discover(), exception=" << e.what();
        }
#endif
    }

    void SsdpDiscovery::stop()
    {
        if (m_started)
            m_started = false;
        sdp_quit = true;
#if defined(__WINDOWS__)
        for (int i = 0; i < card_number; i++) {
            closesocket(ssdp_sock_list[i]);
            closesocket(broadcast_sock_list[i]);
            recv_thread_list[i].join();
            send_thread_list[i].join();
            recv_broadcast_thread_list[i].join();
            BOOST_LOG_TRIVIAL(trace) << "SsdpDiscovery::stop_discover(), join thread " << i;
        }
#endif
        return;
    }


    // share same api on mac and windows
    int LocalClient::PRO_EXTRA_SIZE = LocalClient::PRO_HEADER_SIZE + LocalClient::PRO_LENGTH_SIZE + LocalClient::PRO_TAIL_SIZE;

    LocalClient::LocalClient()
    {
        ;
    }

    int LocalClient::publish(std::string json_str)
    {
        // do not publish a msg larger than 1k
        char buf[MAX_PROTOCAL_MSG_LENGTH];
        buf[0] = 0xA5;
        buf[1] = 0xA5;
        short msg_len = LocalClient::PRO_EXTRA_SIZE + json_str.length();
        memcpy(&buf[LocalClient::PRO_HEADER_SIZE], &msg_len, LocalClient::PRO_HEADER_SIZE);
        memcpy(&buf[LocalClient::PRO_HEADER_SIZE + LocalClient::PRO_LENGTH_SIZE], json_str.c_str(), json_str.length());
        buf[json_str.length() + LocalClient::PRO_HEADER_SIZE + LocalClient::PRO_LENGTH_SIZE + 0] = 0xA7;
        buf[json_str.length() + LocalClient::PRO_HEADER_SIZE + LocalClient::PRO_LENGTH_SIZE + 1] = 0xA7;

        return send(buf, msg_len);
    }

    int LocalClient::recv(std::string& json_str)
    {
        int iResult = 0;
        char buf[1024];
        int    size = 1024;
        iResult = ::recv(ConnectSocket, buf, size, 0);
        if (iResult > 0) {
            BOOST_LOG_TRIVIAL(trace) << "login_bind: recv bytes = " << iResult;
            unsigned int idx = 0;
            while (iResult - idx > LocalClient::PRO_EXTRA_SIZE) {
                if ((unsigned char)buf[idx] == 0xA5 && (unsigned char)buf[idx + 1] == 0xA5) {
                    short msg_len = 0;
                    memcpy(&msg_len, &buf[idx + LocalClient::PRO_HEADER_SIZE], LocalClient::PRO_LENGTH_SIZE);
                    BOOST_LOG_TRIVIAL(trace) << "login_bind: parse msg_len = " << msg_len;
                    if (msg_len >= LocalClient::PRO_EXTRA_SIZE && msg_len <= iResult - idx) {
                        if ((unsigned char)buf[idx + msg_len - 1] == 0xA7 && (unsigned char)buf[idx + msg_len - 2] == 0xA7) {
                            json_str = std::string((char*)&buf[idx + LocalClient::PRO_HEADER_SIZE + LocalClient::PRO_LENGTH_SIZE], msg_len - LocalClient::PRO_EXTRA_SIZE);
                            return 0;
                        }
                        else {
                            BOOST_LOG_TRIVIAL(trace) << "login_bind: invalid proto format";
                        }
                    }
                    else {
                        BOOST_LOG_TRIVIAL(trace) << "login_bind: invalid msg_len = " << msg_len;
                    }
                }
                else {
                    BOOST_LOG_TRIVIAL(trace) << "login_bind: header is not match, buf 0/1 = " << (int)buf[idx] << " " << (int)buf[idx + 1];
                }

                idx += 2;
            }
            return -1;
        }
        else if (iResult == 0) {
            return 0;
        }
        else {
            BOOST_LOG_TRIVIAL(trace) << "login_bind: recv result = " << iResult;
            return -1;
        }
        return 1;
    }

#if defined(__WINDOWS__)
    int LocalClient::connect(std::string server_ip, int port)
    {
        // init win socket
        WORD sockVersion = MAKEWORD(2, 2);
        WSADATA wsaData;
        if (WSAStartup(sockVersion, &wsaData) != 0)
            return -1;

        struct addrinfo *result = NULL, *ptr = NULL, hints;

        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        int iResult = getaddrinfo(server_ip.c_str(), std::to_string(port).c_str(), &hints, &result);
        if (iResult != 0) {
            BOOST_LOG_TRIVIAL(trace) << "login_bind: getaddrinfo failed!";
            WSACleanup();
            return -1;
        }

        ptr = result;
        ConnectSocket = INVALID_SOCKET;
        ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (ConnectSocket == INVALID_SOCKET) {
            BOOST_LOG_TRIVIAL(trace) << "login_bind: socket failed!";
            WSACleanup();
            return -2;
        }

        int recvTimeout = 2 * 1000;
        setsockopt(ConnectSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&recvTimeout, sizeof(int));
        
        iResult = ::connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            BOOST_LOG_TRIVIAL(trace) << "login_bind: connect failed!";
            closesocket(ConnectSocket);
            ConnectSocket = INVALID_SOCKET;
        }

        freeaddrinfo(result);

        if (ConnectSocket == INVALID_SOCKET) {
            WSACleanup();
            return -1;
        }

        return 0;
    }

    int LocalClient::disconnect()
    {
        int iResult = shutdown(ConnectSocket, SD_SEND);
        if (iResult == SOCKET_ERROR) {
            closesocket(ConnectSocket);
            WSACleanup();
            return -1;
        }
        return 0;
    }

    int LocalClient::send(const char *buf, unsigned int size)
    {
        int iResult = ::send(ConnectSocket, buf, size, 0);
        if (iResult == SOCKET_ERROR) {
            printf("send failed: %d\n", WSAGetLastError());
            closesocket(ConnectSocket);
            WSACleanup();
            return 1;
        }

        return 0;
    }
#else

    int LocalClient::connect(std::string server_ip, int port)
    {
        ConnectSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (ConnectSocket < 0) {
            BOOST_LOG_TRIVIAL(trace) << "login_bind: create socket failed!";
            return -1;
        }
        struct sockaddr_in server_addr;

        bzero((char*)&server_addr, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr(server_ip.c_str());
        server_addr.sin_port = htons(port);

        /*int recvTimeout = 2 * 1000;
        setsockopt(ConnectSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvTimeout, sizeof(int));*/

        if (::connect(ConnectSocket, (struct sockaddr*)&server_addr, sizeof(server_addr))) {
            BOOST_LOG_TRIVIAL(trace) << "login_bind: connect failed!";
            close(ConnectSocket);
            ConnectSocket = -1;
        }
        return 0;
    }
    int LocalClient::disconnect()
    {
        close(ConnectSocket);
        return 0;
    }
    int LocalClient::send(const char* buf, unsigned int size)
    {
        return ::send(ConnectSocket, buf, size, 0);
    }
#endif
}
