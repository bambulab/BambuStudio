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
#include <boost/thread.hpp>
#include <boost/log/trivial.hpp>
#include "slic3r/GUI/Event.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/Ssdp.hpp"

namespace pt = boost::property_tree;

#define MAX_CARD_NUMBER     20

namespace Slic3r {

class SsdpDiscovery
{
private:
    bool sdp_quit = false;
    bool keep_sending = false;

    int card_number = 0;

    boost::thread   recv_thread_list[MAX_CARD_NUMBER];
    boost::thread   send_thread_list[MAX_CARD_NUMBER];
    boost::thread   recv_broadcast_thread_list[MAX_CARD_NUMBER];

#if defined(__WINDOWS__)
    int send_msg(int card_no);
    void recv_sdp_msg(int card_no);
    void recv_broadcast_msg(int card_no);
#elif defined(__APPLE__)
    lssdp_ctx lssdp;
    long long last_time;
    long long current_time;
    void ssdp_thread();
#endif
public:
    SsdpDiscovery();
    void start_discover();
    void stop_discover();
    void on_sdp_alive(std::string dev_id, std::string dev_ip);
    void parse_sdp_message(const char *rece_buff, unsigned int recv_size);
    void set_keep_sending(bool sending) { keep_sending = sending; }
};


// LocalClient for communicate with printer

#define MAX_PROTOCAL_MSG_LENGTH     1024

#if defined(__WINDOWS__)
class LocalClient
{
private:
    SOCKET ConnectSocket;

public:
    LocalClient();

    static const int PRO_HEADER_SIZE = 2;
    static const int PRO_LENGTH_SIZE = 2;
    static const int PRO_TAIL_SIZE   = 2;
    static int PRO_EXTRA_SIZE;

    int publish(std::string json_str);
    int connect(std::string server_ip, int port);
    int disconnect();
    int send(const char *buf, unsigned int size);
    int recv(std::string &json_str);
};
#else
//Dummy LocalClient

class LocalClient
{
private:
    int ConnectSocket;

public:
    LocalClient();

    static const int PRO_HEADER_SIZE = 2;
    static const int PRO_LENGTH_SIZE = 2;
    static const int PRO_TAIL_SIZE = 2;
    static int PRO_EXTRA_SIZE;

    int publish(std::string json_str);
    int connect(std::string server_ip, int port);
    int disconnect();
    int send(const char *buf, unsigned int size);
    int recv(std::string &json_str);
};
#endif

class CommuBackend
{
public:
    CommuBackend();
    ~CommuBackend();

    bool is_started() { return m_started; }
    int start();
    int stop();

    void set_ssdp_discovery(bool discovery) {
        if (ssdp)
            ssdp->set_keep_sending(discovery);
    }
    

protected:
    bool m_started = false;

private:
    /* Ssdp */
    SsdpDiscovery* ssdp;
};

}

#endif
