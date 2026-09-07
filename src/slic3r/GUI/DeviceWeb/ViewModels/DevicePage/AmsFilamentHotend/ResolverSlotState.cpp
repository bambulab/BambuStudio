#include "ResolverSlotState.hpp"
#include "Schema.hpp"

#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"

#include "slic3r/GUI/DeviceManager.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

namespace Slic3r { namespace GUI {

namespace {

std::string trim_copy(std::string value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string remove_spaces_copy(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch); }), value.end());
    return value;
}

std::string strip_filament_vendor_prefix(std::string shown_name,
                                         const std::string& vendor,
                                         const std::string& fallback_type)
{
    std::string value = trim_copy(std::move(shown_name));
    if (const auto at_pos = value.find(" @"); at_pos != std::string::npos) {
        value = trim_copy(value.substr(0, at_pos));
    }
    if (value.empty()) return fallback_type;

    std::vector<std::string> aliases;
    if (!vendor.empty()) {
        aliases.push_back(vendor);
        aliases.push_back(remove_spaces_copy(vendor));
        if (vendor == "Bambu Lab") aliases.push_back("Bambu");
    }
    for (const auto& alias : aliases) {
        if (alias.empty()) continue;
        if (value == alias) return fallback_type;
        if (value.rfind(alias + " ", 0) == 0) {
            value = trim_copy(value.substr(alias.size() + 1));
            break;
        }
    }

    return value.empty() ? fallback_type : value;
}

} // namespace

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

    return loaded_in_nozzle ? SV::card_state::ready_to_use : SV::card_state::ready_to_load;
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

std::string DevicePageFilamentSlotStateResolver::ResolveFilamentCardText(
    const DevAmsTray* tray,
    const std::string& fallback_type)
{
    if (!tray) return fallback_type;
    if (!tray->sub_brands.empty()) return tray->sub_brands;

    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        const std::string filament_id = tray->get_filament_id();
        if (auto fila_item = preset_bundle->get_filament_by_filament_id(filament_id, "", true); fila_item.has_value()) {
            return strip_filament_vendor_prefix(fila_item->filament_name, fila_item->vendor, fallback_type);
        }
        if (auto fila_item = preset_bundle->get_filament_by_filament_id(filament_id, "", false); fila_item.has_value()) {
            return strip_filament_vendor_prefix(fila_item->filament_name, fila_item->vendor, fallback_type);
        }
    }

    return fallback_type;
}

int DevicePageFilamentSlotStateResolver::ResolveDisplayRemain(const DevAmsTray* tray)
{
    if (!tray) return 100;
    if (tray->remain_g == 0) return 5;
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

bool DevicePageFilamentSlotStateResolver::IsTraySlotLoadedInNozzle(MachineObject* obj_, const DevAmsTray* tray)
{
    if (obj_ && tray) {
        if (auto nozzle_addr_opt = tray->get_nozzle_addr_id(obj_); nozzle_addr_opt.has_value()) {
            auto nozzle_opt = obj_->GetNozzleSystem()->GetNozzleOptByAddrId(nozzle_addr_opt.value());
            if (nozzle_opt && nozzle_opt->GetSlotNowId().has_value()) {
                auto slot_now = nozzle_opt->GetSlotNowId().value();
                if (tray->ams_type != DevAmsType::EXT_SPOOL) {
                    if (std::to_string(slot_now.first) == tray->ams_id && std::to_string(slot_now.second) == tray->id) {
                        return true;
                    }
                } else {
                    if (std::to_string(slot_now.first) == tray->ams_id || std::to_string(slot_now.first) == tray->id) {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

}} // namespace Slic3r::GUI
