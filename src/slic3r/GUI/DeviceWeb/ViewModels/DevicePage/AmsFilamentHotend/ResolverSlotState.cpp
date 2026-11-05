#include "ResolverSlotState.hpp"
#include "Schema.hpp"
#include "BuilderHotendRackProgressStatus.hpp"

#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
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
                                                                  bool tray_reading)
{
    namespace SV = AmsHotendSchema::values;
    if (!tray || !tray->is_exists) {
        return SV::card_state::empty;
    }

    if (tray_reading) {
        return SV::card_state::reading;
    }

    if (tray && tray->WaitReadExternalRFID_TPU(obj_)) {
        return SV::card_state::awaiting_tpu_read;
    }

    if (tray->setting_id.empty() || tray->m_fila_type.empty()) {
        return SV::card_state::unknown;
    }

    bool filament_at_nozzle_inlet = nozzle_opt.has_value() && nozzle_opt->HasFilamentAtNozzleInlet();
    if (filament_changing && current_filament_changing) {
        return obj_ && obj_->is_target_slot_unload() ? SV::card_state::unloading : SV::card_state::loading;
    }

    return filament_at_nozzle_inlet ? SV::card_state::ready_to_use : SV::card_state::ready_to_load;
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

std::string DevicePageFilamentSlotStateResolver::ResolveNozzleState(MachineObject* obj_,
                                                                    const std::optional<DevNozzle>& nozzle_opt,
                                                                    const std::string& line_state)
{
    namespace SV = AmsHotendSchema::values;
    // Lost implies !IsExist(), so it must be checked before the empty-slot case.
    if (nozzle_opt && nozzle_opt->IsToolLostInHolder()) {
        return SV::nozzle_state::lost;
    }

    if (!nozzle_opt || !nozzle_opt->IsExist()) {
        return SV::nozzle_state::none;
    }

    if (nozzle_opt->IsAbnormal()) {
        return SV::nozzle_state::error;
    }

    if (nozzle_opt->IsUnknown()) {
        return SV::nozzle_state::unknown;
    }

    if (nozzle_opt->GetFilamentEngagementStatus() == DevNozzle::FilamentEngagementStatus::Suspect) {
        return SV::nozzle_state::feed_suspect;
    }

    if (line_state == SV::line_state::loaded || line_state == SV::line_state::loading || line_state == SV::line_state::unloading) {
        return SV::nozzle_state::active;
    }

    return SV::nozzle_state::normal;
}

std::string DevicePageHotendSlotStateResolver::ResolveOpState(MachineObject* obj_,
                                                              const std::optional<DevNozzle>& nozzle_opt,
                                                              const std::string& line_state,
                                                              int display_idx)
{
    namespace SV = AmsHotendSchema::values;

    // Rack jobs outrank health.
    if (obj_ && obj_->GetNozzleSystem()) {
        auto* nozzle_sys = obj_->GetNozzleSystem();
        const int reading_flag = nozzle_sys->GetReadingNozzleIdFlag();
        const bool reading_all = reading_flag == 0xff;
        const bool reading_this = nozzle_opt && reading_flag == nozzle_opt->GetNozzlePosId();
        if (reading_all || reading_this) return SV::hotend_op_state::reading;

        if (auto rack = nozzle_sys->GetNozzleRack(); rack && rack->GetJob() != 0) {
            const int target_display_index = DevicePageHotendRackProgressStatusBuilder::TargetDisplayIndex(
                obj_, rack->GetJob(), rack->GetReadingIdx());
            if (target_display_index == display_idx) {
                if (rack->GetJob() == 1 || rack->GetJob() == 3) return SV::hotend_op_state::taking;
                if (rack->GetJob() == 2) return SV::hotend_op_state::placing;
            }
        }
    }

    // Health is shared with the Filament tab; hotend_op_state strings mirror nozzle_state.
    return DevicePageFilamentSlotStateResolver::ResolveNozzleState(obj_, nozzle_opt, line_state);
}

std::string DevicePageHotendSlotStateResolver::ResolveTempTrend(const std::optional<DevNozzle>& nozzle_opt)
{
    namespace SV = AmsHotendSchema::values;
    if (!nozzle_opt) return SV::hotend_temp_trend::steady;

    if (auto temp_status = nozzle_opt->GetRackNozzleTempStatus()) {
        switch (*temp_status) {
        case DevNozzle::NozzleTempStatus::Heating: return SV::hotend_temp_trend::heating;
        case DevNozzle::NozzleTempStatus::Cooling: return SV::hotend_temp_trend::cooling;
        default: return SV::hotend_temp_trend::steady;
        }
    }

    const int target = nozzle_opt->GetTargetTemp();
    const int current = nozzle_opt->GetCurrentTemp();
    if (target > current) return SV::hotend_temp_trend::heating;
    if (current > target && target > 0) return SV::hotend_temp_trend::cooling;
    return SV::hotend_temp_trend::steady;
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
    if (card_state == SV::card_state::awaiting_tpu_read) return _u8L("TPU pending read");
    if (card_state == SV::card_state::unknown) return _u8L("Unknown filament");
    if (card_state == SV::card_state::error) return _u8L("Error");
    return "";
}

bool DevicePageFilamentSlotStateResolver::IsTraySlotLoadedInNozzle(MachineObject* obj_, const DevAmsTray* tray)
{
    if (obj_ && tray) {
        if (auto nozzle_addr_opt = tray->get_nozzle_addr_id(obj_); nozzle_addr_opt.has_value()) {
            auto nozzle_opt = obj_->GetNozzleSystem()->GetNozzleOptByAddrId(nozzle_addr_opt.value());
            if (nozzle_opt && nozzle_opt->GetSlotNowId().has_value() && nozzle_opt->HasFilamentAtNozzleInlet()) {
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
