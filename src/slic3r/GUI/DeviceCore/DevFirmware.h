#pragma once
#include "DevFirmware.h"
#include "slic3r/Utils/json_diff.hpp"

#include <nlohmann/json.hpp>
#include <wx/string.h>
#include "slic3r/Utils/json_diff.hpp"

namespace Slic3r {

//Previous definitions
class MachineObject;

class FirmwareInfo
{
public:
    std::string module_type;    // ota or ams
    std::string version;
    std::string url;
    std::string name;
    std::string description;
};


class DevFirmwareVersionInfo
{
public:
    std::string name;
    wxString    product_name;
    std::string sn;
    std::string hw_ver;
    std::string sw_ver;
    std::string sw_new_ver;
    int         firmware_flag = 0;
    int         id            = -1;// module index reported by firmware, negative when not reported

public:
    bool isValid() const { return !sn.empty(); }

    /* whether the firmware reports the module index explicitly */
    bool hasValidId() const { return id >= 0; }

    /*type check*/
    bool isAirPump() const { return product_name.Contains("Air Pump"); }
    bool isLaszer() const { return product_name.Contains("Laser"); }
    bool isCuttingModule() const { return product_name.Contains("Cutting Module"); }
    bool isRotary() const { return product_name.Contains("Rotary"); }// Rotary Attachment
    bool isExtinguishSystem() const { return product_name.Contains("Extinguishing System"); }// Auto Fire Extinguishing System
    bool isWTM() const { return name.find("wtm") != string::npos; } // nozzle
    bool isHE() const { return name.find("he") != string::npos; } // nozzle
    bool isExhaustFan() const { return product_name.Contains("Exhaust Fan"); }
    /* The air purifier(APS) is always reported with the "aps" module name, the product name is kept as a fallback */
    bool isAirPurifier() const { return name == "aps" || product_name.Contains("Air Purifier"); }
    /* The external AMS buffer is always reported with the "ahbc" module name, the product name is kept as a fallback */
    bool isHmshub() const { return name == "ahbc" || product_name.find("Filament Buffer") != string::npos; }
    bool isFilaTrackSwitch() const { return product_name.find("Filament Track") != string::npos; }
    bool isHotendAms(const std::string& hotend_ams_type_string) const
    {
        return !hotend_ams_type_string.empty() && name.rfind(hotend_ams_type_string, 0) == 0;
    }

};


class DevFirmware
{
public:
    DevFirmware(MachineObject* obj) : m_owner(obj) {}

private:
    MachineObject* m_owner = nullptr;
};

} // namespace Slic3r