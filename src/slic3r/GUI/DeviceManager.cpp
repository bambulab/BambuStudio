#include "libslic3r/libslic3r.h"
#include "DeviceManager.hpp"
#include "libslic3r/Time.hpp"
#include "libslic3r/Thread.hpp"
#include "slic3r/Utils/Http.hpp"

#include <thread>
#include <mutex>


namespace Slic3r {


DeviceInfo::DeviceInfo(std::string deviceId, std::string deviceName, std::string domain_id)
{
    m_deviceName = deviceName;
    m_deviceId = deviceId;
    m_domainId = domain_id;
    connState = false;
    m_productId = "";
    m_bind_status = BIND_UNKOWN;
}

int DeviceInfo::update_bind_status(std::string status)
{
    if (status.compare("FREE") == 0) {
        m_bind_status = BIND_FREE;
    }
    else if (status.compare("SELF") == 0) {
        m_bind_status = BIND_SELF;
    }
    else if (status.compare("OTHER") == 0) {
        m_bind_status = BIND_OHTER;
    }
    else {
        m_bind_status = BIND_UNKOWN;
        return -1;
    }
}

std::string DeviceInfo::get_bind_status_str()
{
    if (m_bind_status == BIND_FREE) {
        return "FREE";
    }
    else if (m_bind_status == BIND_SELF) {
        return "SELF";
    }
    else if (m_bind_status == BIND_OHTER) {
        return "OTHER";
    }
    else {
        return "UNKOWN";
    }
}

DeviceManager::DeviceManager()
{
    try {
        m_device_check_alive = Slic3r::create_thread([this] { this->check_alive(); });
    }
    catch (std::exception& e) {
        ;
    }
}

DeviceManager::~DeviceManager()
{
    if (m_check_alive_quit) return;

    m_check_alive_quit = true;
    m_device_check_alive.try_join_for(boost::chrono::milliseconds(200));
}

bool DeviceManager::isExist(std::string dev_id)
{
    std::map<std::string, DeviceInfo*>::iterator it = m_devicelist.find(dev_id);
    if (it == m_devicelist.end()) {
        return false;
    }
    return true;
}

bool DeviceManager::has_bind_status(std::string dev_id)
{
    std::lock_guard<std::mutex> lock(m_devicelist_mutex);
    std::map<std::string, DeviceInfo*>::iterator it = m_devicelist.find(dev_id);
    if (it == m_devicelist.end()) {
        return false;
    }
    if (it->second->m_bind_status == DeviceInfo::BIND_UNKOWN) {
        return false;
    }
    return true;
}

bool DeviceManager::is_online(std::string dev_id)
{
    if (!isExist(dev_id))
        return false;
    return m_devicelist[dev_id]->connState;
}

int DeviceManager::getDomainId(std::string dev_id)
{
    if (isExist(dev_id))
        return std::stoi(m_devicelist[dev_id]->m_domainId);
    else
        return -1;
}

std::string DeviceManager::getProductId(std::string dev_id)
{
    if (isExist(dev_id))
        return m_devicelist[dev_id]->m_productId;
    else
        return "";
}

std::string DeviceManager::getRequestTopic(std::string dev_id)
{
    std::map<std::string, DeviceInfo*>::iterator it = m_devicelist.find(dev_id);
    if (it == m_devicelist.end())
        return "";

    std::string topic_request = "device/" + it->second->m_productId + "/" + dev_id + "/request";
    return topic_request;
}

std::string DeviceManager::getReportTopic(std::string dev_id)
{
    std::map<std::string, DeviceInfo*>::iterator it = m_devicelist.find(dev_id);
    if (it == m_devicelist.end())
        return "";
    std::string topic_report = "device/" + it->second->m_productId + "/" + dev_id + "/report";
    return topic_report;
}

int DeviceManager::add_new_device(DeviceInfo* device)
{
    std::lock_guard<std::mutex> lock(m_devicelist_mutex);
    std::string dev_id = device->m_deviceId;
    device->connState = true;
    m_devicelist.insert(std::make_pair(dev_id, device));
    return 0;
}

int DeviceManager::update_alive_time(std::string dev_id)
{
    std::lock_guard<std::mutex> lock(m_devicelist_mutex);
    std::map<std::string, DeviceInfo*>::iterator it = m_devicelist.find(dev_id);
    if (it != m_devicelist.end()) {
        DeviceInfo* deviceInfo = it->second;
        deviceInfo->m_last_alive = Slic3r::Utils::get_current_time_utc();
        if (!it->second->connState) {
            it->second->connState = true;
            BOOST_LOG_TRIVIAL(trace) << "device id = " << dev_id << " is online!";
        }
        BOOST_LOG_TRIVIAL(trace) << "update device id = " << dev_id << " alive time!";
        return 0;
    }
    return -1;
}

int DeviceManager::update_bind_status(std::string device_id, std::string status)
{
    std::map<std::string, DeviceInfo*>::iterator it = m_devicelist.find(device_id);
    if (it != m_devicelist.end()) {
        DeviceInfo* deviceInfo = it->second;
        deviceInfo->update_bind_status(status);
        return 0;
    }
    return 0;
}

void DeviceManager::check_alive()
{
    while (!m_check_alive_quit) {
        time_t curr = Slic3r::Utils::get_current_time_utc();
        double seconds;
        std::map<std::string, DeviceInfo*>::iterator it;
        m_devicelist_mutex.lock();
        for (it = m_devicelist.begin(); it != m_devicelist.end(); it++) {
            seconds = difftime(curr, it->second->m_last_alive);
            if (seconds > ALIVE_TIMEOUT) {
                it->second->connState = false;
                BOOST_LOG_TRIVIAL(trace) << "device id = " << it->first << " is offline! difftime=" <<seconds;
            }
        }
        m_devicelist_mutex.unlock();
        BOOST_LOG_TRIVIAL(trace) << "check alive";
        boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));
    }
}

wxArrayString DeviceManager::get_connected_devicelist()
{
    wxArrayString devices;
    std::map<std::string, DeviceInfo*>::iterator it;
    for (it = m_devicelist.begin(); it != m_devicelist.end(); it++) {
        if (it->second->connState) {
            devices.Add(it->second->m_deviceName + "(" + it->first + ")");
        }
    }
    return devices;
}

std::vector<DeviceInfo*> DeviceManager::get_connected_device_info()
{
    std::lock_guard<std::mutex> lock(m_devicelist_mutex);
    std::vector<DeviceInfo*> list;
    std::map<std::string, DeviceInfo*>::iterator it;
    for (it = m_devicelist.begin(); it != m_devicelist.end(); it++) {
        if (it->second->connState) {
            list.emplace_back(it->second);
        }
    }
    return list;
}

std::vector<std::string> DeviceManager::get_connected_device_list()
{
    std::lock_guard<std::mutex> lock(m_devicelist_mutex);
    std::vector<std::string> list;
    std::map<std::string, DeviceInfo*>::iterator it;
    for (it = m_devicelist.begin(); it != m_devicelist.end(); it++) {
        if (it->second->connState) {
            list.emplace_back(it->first);
        }
    }
    return list;
}

std::vector<std::string> DeviceManager::get_bind_self_device_list()
{
    std::lock_guard<std::mutex> lock(m_devicelist_mutex);
    std::vector<std::string> list;
    std::map<std::string, DeviceInfo*>::iterator it;
    for (it = m_devicelist.begin(); it != m_devicelist.end(); it++) {
        if (it->second->is_bind_self()) {
            list.emplace_back(it->first);
        }
    }
    return list;
}

} // namespace Slic3r
