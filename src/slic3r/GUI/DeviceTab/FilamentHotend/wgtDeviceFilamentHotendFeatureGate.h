#pragma once

namespace Slic3r {
class MachineObject;
}

namespace Slic3r::GUI
{

struct wgtDeviceFilamentHotendAmsActionState
{
    bool show_ams_setting{ false };
    bool show_auto_refill{ false };
};

class wgtDeviceFilamentHotendFeatureGate
{
public:
    wgtDeviceFilamentHotendAmsActionState ResolveAmsActionState(const Slic3r::MachineObject* machine_obj) const;
};

} // namespace Slic3r::GUI
