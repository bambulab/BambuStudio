#ifndef DEVICEPAGEHOTENDRACKPROGRESSSTATUSBUILDER_HPP
#define DEVICEPAGEHOTENDRACKPROGRESSSTATUSBUILDER_HPP

#include <nlohmann/json.hpp>

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI {

class DevicePageHotendRackProgressStatusBuilder
{
public:
    static nlohmann::json Build(MachineObject* machine_obj);
    static int TargetDisplayIndex(MachineObject* machine_obj, int job, int reading_idx);
};

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEHOTENDRACKPROGRESSSTATUSBUILDER_HPP
