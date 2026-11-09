#pragma once
#include <nlohmann/json.hpp>
#include "slic3r/Utils/json_diff.hpp"

namespace Slic3r {

class MachineObject;

// Air purifier(APS) gear values, source: device.aps "gear" field
enum AIR_PURIFIER_GEAR : int {
    AIR_PURIFIER_GEAR_OFF      = 0,  // off
    AIR_PURIFIER_GEAR_SILENT   = 1,  // low speed
    AIR_PURIFIER_GEAR_STANDARD = 2,  // mid speed
    AIR_PURIFIER_GEAR_MAX      = 3,  // high speed
    AIR_PURIFIER_GEAR_AUTO     = 15,
};

// Air purifier(APS) module, decoupled from DevFan.
// Data source: aux bit[37] (in place) + print.device.aps (type/pm/gear/rpm).
class DevAirPurifier
{
public:
    static std::shared_ptr<DevAirPurifier> Create(MachineObject *obj) { return std::shared_ptr<DevAirPurifier>(new DevAirPurifier(obj)); }

public: // getter
    bool IsExist() const { return m_exist; }
    int  GetType() const { return m_type; }
    int  GetPm()   const { return m_pm; }
    int  GetGear() const { return m_gear; }
    int  GetRpm()  const { return m_rpm; }

public:
    // parse aux bit[37] (in place) + device.aps (pm/gear/rpm/type)
    void ParseAps(const json &print_json);

    // control: software-initiated gear switch, part_control module = aps
    int CtrlSetGear(int gear);

protected:
    DevAirPurifier(MachineObject *obj) : m_owner(obj) {}

private:
    bool m_exist = false;
    int  m_type  = 0;
    int  m_pm    = 0;
    int  m_gear  = 0;
    int  m_rpm   = 0;

    MachineObject *m_owner = nullptr;
};

} // namespace Slic3r
