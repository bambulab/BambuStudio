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
                                        bool tray_reading,
                                        bool loaded_in_nozzle);
    static std::string ResolveLineState(const std::string& card_state,
                                        bool has_hotend_slot,
                                        bool filament_changing,
                                        bool current_filament_changing,
                                        bool loaded_in_nozzle);
    static std::string ResolveNozzleState(const std::string& card_state,
                                          const std::string& line_state,
                                          bool has_tool,
                                          bool is_abnormal,
                                          bool is_lost);
    static int ResolveDisplayRemain(const DevAmsTray* tray);
    static std::string ResolveStatusText(const std::string& card_state);
};

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEFILAMENTSLOTSTATERESOLVER_HPP
