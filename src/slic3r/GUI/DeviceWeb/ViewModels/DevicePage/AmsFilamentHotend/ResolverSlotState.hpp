#ifndef DEVICEPAGEFILAMENTSLOTSTATERESOLVER_HPP
#define DEVICEPAGEFILAMENTSLOTSTATERESOLVER_HPP

#include <string>

namespace Slic3r {
class DevAmsTray;
class MachineObject;
struct DevNozzle;
}

namespace Slic3r { namespace GUI {

class DevicePageFilamentSlotStateResolver
{
public:
    static std::string ResolveCardState(MachineObject* obj_,
                                        const DevAmsTray* tray,
                                        std::optional<DevNozzle> nozzle_opt,
                                        bool filament_changing,
                                        bool current_filament_changing,
                                        bool tray_reading);
    static std::string ResolveLineState(const std::string& card_state,
                                        bool has_hotend_slot,
                                        bool filament_changing,
                                        bool current_filament_changing,
                                        bool loaded_in_nozzle);
    static std::string ResolveNozzleState(MachineObject* obj_,
                                          const std::optional<DevNozzle>& nozzle_opt,
                                          const std::string& line_state);
    static std::string ResolveFilamentCardText(const DevAmsTray* tray,
                                               const std::string& fallback_type);
    static int ResolveDisplayRemain(const DevAmsTray* tray);
    static std::string ResolveStatusText(const std::string& card_state);

    static bool IsTraySlotLoadedInNozzle(MachineObject* obj_, const DevAmsTray* tray);
};

// Hotend tab counterpart of DevicePageFilamentSlotStateResolver.
class DevicePageHotendSlotStateResolver
{
public:
    static std::string ResolveOpState(MachineObject* obj_,
                                      const std::optional<DevNozzle>& nozzle_opt,
                                      const std::string& line_state,
                                      int display_idx);
    static std::string ResolveTempTrend(const std::optional<DevNozzle>& nozzle_opt);
};

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEFILAMENTSLOTSTATERESOLVER_HPP
