#pragma once
#include <nlohmann/json.hpp>
#include "slic3r/Utils/json_diff.hpp"
#include <wx/string.h>

#include "DevDefs.h"
#include "slic3r/GUI/DeviceManager.hpp"

namespace Slic3r {

class MachineObject;

/* some static info of machine*/ /*TODO*/
class DevInfo
{
    friend class DeviceManager;

public:
    static std::shared_ptr<DevInfo> Create(MachineObject *obj) { return std::shared_ptr<DevInfo>(new DevInfo(obj)); }

public:
    void SetDevId(const std::string &id) { m_dev_id = id; }
    std::string GetDevId() const { return m_dev_id; }

    // std::string GetDevIP() const { return m_dev_ip; }
    // std::string GetDevName() const { return m_dev_name; }
    // std::string GetPrinterTypeStr() const { return m_printer_type_str; }
    // std::string GetPrinterSignal() const { return m_printer_signal; }
    // std::string GetConnectType() const { return m_connect_type; }
    // std::string GetBindState() const { return m_bind_state; }

    // connection type
    std::string ConnectionType() const { return m_dev_connection_type; }
    bool        IsLanMode() const { return m_dev_connection_type == "lan"; }
    bool        IsCloudMode() const { return m_dev_connection_type == "cloud"; }

    // device mode
    bool                         IsFdmMode() const { return (m_device_mode & DEVICE_MODE_FDM) == DEVICE_MODE_FDM; }
    DEV_RESERVED_FOR_FUTURE(
    bool IsLaserMode() const { return (m_device_mode & DEVICE_MODE_LASER) == DEVICE_MODE_LASER; }
    bool IsCutMode() const { return (m_device_mode & DEVICE_MODE_CUT) = DEVICE_MODE_CUT; }
    );

    // Passer
    void ParseInfo(const nlohmann::json &print_jj);

protected:
    DevInfo(MachineObject *obj) : m_owner(obj) { m_device_mode = DEVICE_MODE_FDM; };

    // setters
    void SetConnectionType(const std::string &type);

private:
    MachineObject *m_owner = nullptr;

    std::string m_dev_id;
    // std::string m_dev_name;
    // std::string m_dev_ip;
    // std::string m_printer_type_str;
    // std::string m_printer_signal;
    // std::string m_connect_type;
    // std::string m_bind_state;

    std::string m_dev_connection_type;

    enum DeviceMode : unsigned int {
        DEVICE_MODE_UNKNOWN = 0x00000000,
        DEVICE_MODE_FDM     = 0x00000001,
        DEVICE_MODE_LASER   = 0x00000010,
        DEVICE_MODE_CUT     = 0x00000100,
    } m_device_mode;
};

} // namespace Slic3r