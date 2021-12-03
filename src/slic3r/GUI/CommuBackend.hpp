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
#include <boost/log/trivial.hpp>
#include "slic3r/GUI/Event.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/Ssdp.hpp"

namespace pt = boost::property_tree;

namespace Slic3r {

class SsdpDiscovery
{
private:
    bool sdp_quit = false;
    bool keep_sending = false;

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
    void set_keep_sending(bool sending) { keep_sending = sending; }
};

class CommuBackend
{
public:
    CommuBackend();
    ~CommuBackend();

    int start();
    int stop();

    void set_ssdp_discovery(bool discovery) {
        if (ssdp)
            ssdp->set_keep_sending(discovery);
    }
    

protected:

private:
    /* Ssdp */
    SsdpDiscovery* ssdp;
};

}

#endif
