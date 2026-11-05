#include "DevAirPurifier.hpp"
#include "DevUtil.h"

#include "slic3r/GUI/DeviceManager.hpp"
#include "libslic3r/Log.hpp"

namespace Slic3r {

void DevAirPurifier::ParseAps(const json &print_json)
{
    // aux bit[37]: APS(air purifier) in place
    if (print_json.contains("aux") && print_json["aux"].is_string()) {
        const std::string aux = print_json["aux"].get<std::string>();
        if (!aux.empty()) { m_exist = (DevUtil::get_flag_bits(aux, 37, 1) == 1); }
    }

    // device.aps: real-time data
    if (print_json.contains("device") && print_json["device"].contains("aps")) {
        const json &aps = print_json["device"]["aps"];
        if (aps.contains("type") && aps["type"].is_number()) { m_type = aps["type"].get<int>(); }
        if (aps.contains("pm")   && aps["pm"].is_number())   { m_pm   = aps["pm"].get<int>(); }
        if (aps.contains("gear") && aps["gear"].is_number()) { m_gear = aps["gear"].get<int>(); }
        if (aps.contains("rpm")  && aps["rpm"].is_number())  { m_rpm  = aps["rpm"].get<int>(); }
    }
}

// Software-initiated air purifier gear switch: part_control, module = aps
int DevAirPurifier::CtrlSetGear(int gear)
{
    BOOST_LOG_TRIVIAL(info) << "set air purifier gear = " << gear;
    json j;
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["print"]["command"]     = "part_control";
    j["print"]["action"]      = std::to_string(gear); // "0" "1" "2" "3" "15"
    j["print"]["module"]      = "aps";

    return m_owner->publish_json(j);
}

} // namespace Slic3r
