#include "ResolverSlotState.hpp"
#include "Schema.hpp"

#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/I18N.hpp"

#include "slic3r/GUI/DeviceManager.hpp"

namespace Slic3r { namespace GUI {

std::string DevicePageFilamentSlotStateResolver::ResolveCardState(MachineObject* obj_,
                                                                  const DevAmsTray* tray,
                                                                  std::optional<DevNozzle> nozzle_opt,
                                                                  bool filament_changing,
                                                                  bool current_filament_changing,
                                                                  bool tray_reading,
                                                                  bool loaded_in_nozzle)
{
    namespace SV = AmsHotendSchema::values;
    if (!tray || !tray->is_exists) {
        return SV::card_state::empty;
    }

    if (tray_reading) {
        return SV::card_state::reading;
    }

    if (tray->setting_id.empty() || tray->m_fila_type.empty()) {
        return SV::card_state::unknown;
    }

    if (filament_changing && current_filament_changing) {
        return loaded_in_nozzle ? SV::card_state::unloading : SV::card_state::loading;//TODO
    }

    if (nozzle_opt && nozzle_opt->GetSlotNowId().has_value()) {
        auto slot_now = nozzle_opt->GetSlotNowId().value();
        if (tray->ams_type != DevAmsType::EXT_SPOOL) {
            if (std::to_string(slot_now.first) == tray->ams_id && std::to_string(slot_now.second) == tray->id) {
                return SV::card_state::ready_to_use;
            }
        } else {
            if (std::to_string(slot_now.first) == tray->ams_id) {
                return SV::card_state::ready_to_use;
            }
        }
    }

    return SV::card_state::ready_to_load;
}

std::string DevicePageFilamentSlotStateResolver::ResolveLineState(const std::string& card_state,
                                                                  bool has_hotend_slot,
                                                                  bool filament_changing,
                                                                  bool current_filament_changing,
                                                                  bool loaded_in_nozzle)
{
    namespace SV = AmsHotendSchema::values;
    if (!has_hotend_slot) return SV::line_state::no_hotend;
    if (filament_changing && current_filament_changing) return loaded_in_nozzle ? SV::line_state::unloading : SV::line_state::loading;
    if (card_state == SV::card_state::loading) return SV::line_state::loading;
    if (card_state == SV::card_state::unloading) return SV::line_state::unloading;
    if (card_state == SV::card_state::ready_to_use) return SV::line_state::loaded;
    if (card_state == SV::card_state::unknown) return loaded_in_nozzle ? SV::line_state::loaded : SV::line_state::no_filament;
    return SV::line_state::no_filament;
}

std::string DevicePageFilamentSlotStateResolver::ResolveNozzleState(const std::string& card_state,
                                                                    const std::string& line_state,
                                                                    bool has_tool,
                                                                    bool is_abnormal,
                                                                    bool is_lost)
{
    namespace SV = AmsHotendSchema::values;
    if (is_lost) return SV::nozzle_state::lost;
    if (!has_tool) return SV::nozzle_state::none;
    if (is_abnormal || card_state == SV::card_state::error) return SV::nozzle_state::error;
    if (line_state == SV::line_state::loaded || line_state == SV::line_state::loading || line_state == SV::line_state::unloading) {
        return SV::nozzle_state::active;
    }
    return SV::nozzle_state::normal;
}

int DevicePageFilamentSlotStateResolver::ResolveDisplayRemain(const DevAmsTray* tray)
{
    if (!tray) return 100;
    if (tray->remain_g > 0 && tray->remain < 5) return 5;
    if (tray->remain >= 5 && tray->remain <= 100) return tray->remain;
    return 100;
}

std::string DevicePageFilamentSlotStateResolver::ResolveStatusText(const std::string& card_state)
{
    namespace SV = AmsHotendSchema::values;
    if (card_state == SV::card_state::reading) return _u8L("Reading");
    if (card_state == SV::card_state::loading || card_state == SV::card_state::unloading) return _u8L("Loading/Unloading");
    if (card_state == SV::card_state::ready_to_load) return _u8L("Ready to load");
    if (card_state == SV::card_state::ready_to_use) return _u8L("Loaded");
    if (card_state == SV::card_state::empty) return "--";
    if (card_state == SV::card_state::unknown) return _u8L("Unknown filament");
    if (card_state == SV::card_state::error) return _u8L("Error");
    return "";
}

}} // namespace Slic3r::GUI
