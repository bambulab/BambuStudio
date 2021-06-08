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


class DeviceInfo {
public:
    enum BindStatus {
        BIND_UNKOWN = 0,
        BIND_FREE = 1,
        BIND_SELF = 2,
        BIND_OHTER = 3,
        BIND_ERROR = 4};
    DeviceInfo(std::string deviceId, std::string deviceName, std::string domainId);

    int update_bind_status(std::string status);
    bool is_bind_self() { return m_bind_status == BindStatus::BIND_SELF; }

    std::string m_deviceName;
    std::string m_deviceId;
    std::string m_productId;
    std::string m_domainId;
    std::string m_ipAddr;
    std::string get_bind_status_str();
    time_t m_last_alive;
    bool connState;
    BindStatus m_bind_status;
};

class DeviceManager
{
private:
    std::map<std::string, DeviceInfo*> m_devicelist;
	std::mutex m_devicelist_mutex;
    bool m_check_alive_quit = false;
    const double ALIVE_TIMEOUT = 6.0;
    boost::thread m_device_check_alive;

public:
	DeviceManager();
	~DeviceManager();

    bool isExist(std::string dev_id);
    bool has_bind_status(std::string dev_id);
    bool is_online(std::string dev_id);
    int getDomainId(std::string dev_id);
    std::string getProductId(std::string dev_id);
    std::string getRequestTopic(std::string dev_id);
    std::string getReportTopic(std::string dev_id);
    int add_new_device(DeviceInfo* device);
    int update_alive_time(std::string dev_id);
    int update_bind_status(std::string device_id, std::string status);
    wxArrayString get_connected_devicelist();
    std::vector<DeviceInfo*> get_connected_device_info();
    std::vector<std::string> get_connected_device_list();
    std::vector<std::string> get_bind_self_device_list();
    void check_alive();
};

} // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
