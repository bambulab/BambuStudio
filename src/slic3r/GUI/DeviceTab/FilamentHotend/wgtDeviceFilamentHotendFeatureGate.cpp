#include "wgtDeviceFilamentHotendFeatureGate.h"

#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"

namespace Slic3r::GUI
{

wgtDeviceFilamentHotendAmsActionState wgtDeviceFilamentHotendFeatureGate::ResolveAmsActionState(const Slic3r::MachineObject* machine_obj) const
{
    wgtDeviceFilamentHotendAmsActionState state{};
    if (!machine_obj) {
        return state;
    }

    auto fila_system = machine_obj->GetFilaSystem();
    if (!fila_system) {
        return state;
    }

    const bool has_connected_ams = !fila_system->GetAmsList().empty() && machine_obj->ams_exist_bits != 0;
    state.show_ams_setting = has_connected_ams;
    state.show_auto_refill = has_connected_ams && machine_obj->is_support_filament_backup && machine_obj->GetFilaSystem()->IsAutoRefillEnabled();
    return state;
}

} // namespace Slic3r::GUI
