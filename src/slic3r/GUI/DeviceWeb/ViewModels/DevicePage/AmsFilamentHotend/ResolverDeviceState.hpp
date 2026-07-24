#ifndef DEVICEPAGEDEVICESTATERESOLVER_HPP
#define DEVICEPAGEDEVICESTATERESOLVER_HPP

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI {

class DevicePageDeviceStateResolver
{
public:
    static bool IsBusy(MachineObject* machine_obj);
    static bool BlockIfBusy(MachineObject* machine_obj);
};

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEDEVICESTATERESOLVER_HPP
