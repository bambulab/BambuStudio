#ifndef DEVICEPAGEFILAMENTMANUALPULLSTATUSBUILDER_HPP
#define DEVICEPAGEFILAMENTMANUALPULLSTATUSBUILDER_HPP

#include <nlohmann/json.hpp>

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI {

class DevicePageFilamentManualPullStatusBuilder
{
public:
    static nlohmann::json Build(MachineObject* machine_obj);
};

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEFILAMENTMANUALPULLSTATUSBUILDER_HPP
