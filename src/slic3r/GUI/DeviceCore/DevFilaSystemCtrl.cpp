#include <nlohmann/json.hpp>
#include <boost/log/trivial.hpp>
#include "DevFilaSystem.h"

#include "slic3r/GUI/DeviceManager.hpp"// TODO: remove this include
#include "DevFilamentWeight.h"
#include "DevUtil.h"

using namespace nlohmann;
namespace Slic3r
{

int DevFilaSystem::CtrlAmsReset() const
{
    json jj_command;
    jj_command["print"]["command"] = "ams_reset";
    jj_command["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    return m_owner->publish_json(jj_command);
}

int DevFilaSystem::CtrlSetFilamentWeight(int ams_id, int slot_id, int total, int remain) const
{
    const auto validation = DevFilamentWeight::Validate(total, remain);
    if (!validation.valid) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " invalid filament weight, total=" << total << ", remain=" << remain;
        return -1;
    }

    json jj_command;
    jj_command["print"]["command"] = "set_filament_weight";
    jj_command["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    jj_command["print"]["ams_id"] = ams_id;
    jj_command["print"]["slot_id"] = slot_id;
    jj_command["print"]["total"] = total;
    jj_command["print"]["remain"] = remain;
    jj_command["print"]["src_id"] = 1;
    return m_owner->publish_json(jj_command);
}

int DevFilaSystem::CtrlAmsStartDryingHour(int ams_id,
                                          int zone_id,
                                          std::string filament_type,
                                          int tag_temp,
                                          int tag_duration_hour,
                                          bool rotate_tray,
                                          int cooling_temp,
                                          bool close_power_conflict) const
{
    json jj_command;
    jj_command["print"]["command"] = "ams_filament_drying";
    jj_command["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    jj_command["print"]["ams_id"] = ams_id;
    jj_command["print"]["zone_id"] = zone_id;
    jj_command["print"]["mode"] = DevAms::DryCtrlMode::OnTime;
    jj_command["print"]["filament"] = filament_type,
    jj_command["print"]["temp"] = tag_temp;
    jj_command["print"]["duration"] = tag_duration_hour;
    jj_command["print"]["humidity"] = 0;
    jj_command["print"]["rotate_tray"] = rotate_tray;
    jj_command["print"]["cooling_temp"] = cooling_temp;
    jj_command["print"]["close_power_conflict"] = close_power_conflict;
    return m_owner->publish_json(jj_command);
}

int DevFilaSystem::CtrlAmsStopDrying(int ams_id) const
{
    json jj_command;
    jj_command["print"]["command"] = "ams_filament_drying";
    jj_command["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    jj_command["print"]["ams_id"] = ams_id;
    jj_command["print"]["mode"] = DevAms::DryCtrlMode::Off;
    jj_command["print"]["filament"] = "",
    jj_command["print"]["temp"] = 0;
    jj_command["print"]["duration"] = 0;
    jj_command["print"]["humidity"] = 0;
    jj_command["print"]["rotate_tray"] = false;
    jj_command["print"]["cooling_temp"] = 0;
    jj_command["print"]["close_power_conflict"] = false;
    return m_owner->publish_json(jj_command);
}

}