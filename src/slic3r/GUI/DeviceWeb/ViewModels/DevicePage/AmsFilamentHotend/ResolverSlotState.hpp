#ifndef DEVICEPAGEFILAMENTSLOTSTATERESOLVER_HPP
#define DEVICEPAGEFILAMENTSLOTSTATERESOLVER_HPP

#include <string>

namespace Slic3r {
class DevAmsTray;
}

namespace Slic3r { namespace GUI {

class DevicePageFilamentSlotStateResolver
{
public:
    static std::string ResolveCardState(const DevAmsTray* tray,
                                        bool filament_changing,
                                        bool listed_as_changing,
                                        bool loaded_in_extruder,
                                        bool nozzle_has_filament,
                                        bool tray_reading);
    static std::string ResolveLineState(const std::string& card_state,
                                        bool has_hotend_slot,
                                        bool filament_changing,
                                        bool listed_as_changing,
                                        bool loaded_in_nozzle);
    static std::string ResolveNozzleState(const std::string& card_state,
                                          const std::string& line_state,
                                          bool has_tool,
                                          bool is_abnormal);
    static int ResolveDisplayRemain(const DevAmsTray* tray);
    static std::string ResolveStatusText(const std::string& card_state);
};

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEFILAMENTSLOTSTATERESOLVER_HPP
