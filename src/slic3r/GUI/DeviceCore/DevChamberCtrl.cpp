#include "DevChamber.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r {

int DevChamber::CtrlSetChamberTemp(int temp)
{
    if (GUI::wxGetApp().is_monitor_ctrl_blocked(GUI::MonitorCtrlId::ChamberTemp)) return -1;

    json j;
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["print"]["command"]     = "set_ctt";
    j["print"]["ctt_val"]     = temp;

    return m_owner->publish_json(j, 1);
}

} // namespace Slic3r