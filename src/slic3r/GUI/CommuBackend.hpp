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
public:
    SsdpDiscovery();
    void start_discover();
    void stop_discover();
    int send_msg(int card_no);
    void on_sdp_alive(std::string dev_id, std::string dev_ip);
    void recv_sdp_msg(int card_no);
    void recv_broadcast_msg(int card_no);
};

class CommuBackend
{
public:
    CommuBackend();
    ~CommuBackend();

    int start();
    int stop();

protected:

private:
    /* Ssdp */
    SsdpDiscovery* ssdp;
};

}

#endif
