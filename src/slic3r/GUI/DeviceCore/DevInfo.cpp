#include "DevInfo.h"
#include "DevUtil.h"
#include "slic3r/GUI/DeviceManager.hpp"

namespace Slic3r
{

// begin of class MachineObject
std::string MachineObject::connection_type() const {
    return m_dev_info->ConnectionType();
}

bool MachineObject::is_lan_mode_printer() const {
    return m_dev_info->IsLanMode();
}

bool MachineObject::is_cloud_mode_printer() const {
    return m_dev_info->IsCloudMode();
}
// end of class MachineObject

// begin pf class DevInfo
void DevInfo::SetConnectionType(const std::string &type) {
    if (m_dev_connection_type != type) {
        m_dev_connection_type = type;
    }
}

void DevInfo::ParseInfo(const nlohmann::json &print_jj) {

    if (print_jj.contains("device")) {
        const auto& device_jj = print_jj["device"];
        DevJsonValParser::ParseVal(device_jj, "connection_type", m_dev_connection_type);
        DevJsonValParser::ParseVal(device_jj, "type", m_device_mode);
    }
}
// end of class DevInfo

}