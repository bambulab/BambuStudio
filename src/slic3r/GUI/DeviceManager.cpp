#include "libslic3r/libslic3r.h"
#include "DeviceManager.hpp"
#include "libslic3r/Time.hpp"
#include "libslic3r/Thread.hpp"
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
    std::string dev_id = device->m_deviceId;
    device->connState = true;
    m_devicelist_mutex.lock();
    m_devicelist.insert(std::make_pair(dev_id, device));
    m_devicelist_mutex.unlock();
    return 0;
}

int DeviceManager::update_alive_time(std::string dev_id)
{
    std::map<std::string, DeviceInfo*>::iterator it = m_devicelist.find(dev_id);
    if (it != m_devicelist.end()) {
        DeviceInfo* deviceInfo = it->second;
        deviceInfo->m_last_alive = Slic3r::Utils::get_current_time_utc();
        if (!it->second->connState) {
            //TODO emit online event
            it->second->connState = true;
        }
        return 0;
    }
    return -1;
}

void DeviceManager::check_alive()
{
    while (!m_check_alive_quit) {
        time_t curr = Slic3r::Utils::get_current_time_utc();
        double seconds;
        std::map<std::string, DeviceInfo*>::iterator it;
        m_devicelist_mutex.lock();
        for (it = m_devicelist.begin(); it != m_devicelist.end();it++) {
            seconds = difftime(curr, it->second->m_last_alive);
            if (seconds > ALIVE_TIMEOUT) {
                it->second->connState = false;
            }
        }
        m_devicelist_mutex.unlock();
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

} // namespace Slic3r