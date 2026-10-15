#ifndef DEVICEPAGEFILAMENTCHANGESTATUSBUILDER_HPP
#define DEVICEPAGEFILAMENTCHANGESTATUSBUILDER_HPP

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI {

class DevicePageFilamentChangeStatusBuilder
{
public:
    static nlohmann::json Build(MachineObject* machine_obj);

    static bool IsActive(MachineObject* machine_obj);
};

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEFILAMENTCHANGESTATUSBUILDER_HPP
