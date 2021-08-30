#ifndef slic3r_DeviceManager_hpp_
#define slic3r_DeviceManager_hpp_

#include <map>
#include <vector>
#include <string>
#include <memory>
#include <boost/thread.hpp>

namespace Slic3r {

class DeviceChangedEvent : public wxEvent
{
public:
    DeviceChangedEvent(wxEventType type, std::string str) : wxEvent(0, type)
    {
        m_dev_id = str;
    }

    virtual wxEvent* Clone() const
    {
        return new DeviceChangedEvent(*this);
    }
    std::string getDeviceId() { return m_dev_id; }
private:
    std::string m_dev_id;
};


enum CONNECTION_FLAGS { DDS_CONNECTION = 1, MQTT_CONNECTION = 2};

class DeviceInfo {
public:
    enum BindStatus {
        BIND_UNKOWN = 0,
        BIND_FREE = 1,
        BIND_SELF = 2,
        BIND_OHTER = 3,
        BIND_ERROR = 4};
    DeviceInfo(std::string dev_id, std::string dev_name, int conn_flag);
    void set_ip_addr(std::string ip) { m_ip_addr = ip; }
    int set_bind_status(std::string status);
    void set_mqtt_conn_status(bool status) { m_mqtt_conn_status; }
    bool is_bind_self() { return m_bind_status == BindStatus::BIND_SELF; }
    bool is_bind_free() { return m_bind_status == BindStatus::BIND_FREE; }
    std::string get_dev_id() { return m_dev_id; }
    std::string get_dev_ip() { return m_ip_addr; }

    std::string m_dev_name;
    std::string m_dev_id;
    std::string m_ip_addr;
    std::string get_bind_status_str();
    BindStatus m_bind_status;
    std::string m_bind_status_str;

    int m_conn_flag;

    /* DDS attribute */
    time_t m_last_alive;
    bool m_dds_conn_status;
    std::string m_domain_id;
    
    /* MQTT attribute */
    bool m_mqtt_conn_status;
};

class DeviceManager
{
private:
    std::map<std::string, DeviceInfo*> m_devicelist;
	std::mutex m_devicelist_mutex;
    bool m_check_alive_quit = false;
    const double ALIVE_TIMEOUT = 30.0;
    boost::thread m_device_check_alive;

public:
    DeviceManager();
    ~DeviceManager();

    bool isExist(std::string dev_id);
    bool has_bind_status(std::string dev_id);
    bool is_bind_self(std::string dev_id);
    bool is_dds_online(std::string dev_id);
    int get_domain_id(std::string dev_id);
    std::string get_ip(std::string dev_id);
    std::string getRequestTopic(std::string dev_id);
    std::string getReportTopic(std::string dev_id);
    int add_new_device(DeviceInfo* device);
    int update_alive_time(std::string dev_id);
    int update_ip_address(std::string dev_id, std::string dev_ip);
    int update_bind_status(std::string device_id, std::string status);
    wxArrayString get_connected_devicelist();
    std::vector<DeviceInfo*> get_connected_device_info();
    std::vector<std::string> get_connected_device_list();
    std::vector<std::string> get_bind_self_device_list();
    std::vector<std::string> get_free_and_self_device_list();
    void check_alive();
};

} // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
