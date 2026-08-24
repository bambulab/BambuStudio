#include "ViewModelDataBuilder.hpp"

#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevCalib.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSwitch.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/Utils/CalibUtils.hpp"

#include <optional>
#include <utility>

namespace Slic3r { namespace GUI { namespace AmsControlWebData {

namespace SchemaFormat = AmsControlWebSchema::format;
namespace SchemaValues = AmsControlWebSchema::values;

namespace {

std::string normalize_hex_for_web(const std::string& raw)
{
    if (raw.empty()) return "#D9D9D9";
    std::string hex = raw;
    if (hex[0] != '#') hex = "#" + hex;
    if (hex.size() > 9) hex = hex.substr(0, 9);
    return hex;
}

// Mirror the enumerator name so the Web side keeps no copy of the enum table.
std::string ams_type_name_for_web(DevAmsType ams_type)
{
    switch (ams_type) {
    case DevAmsType::EXT_SPOOL:      return "EXT_SPOOL";
    case DevAmsType::AMS:            return "AMS";
    case DevAmsType::AMS_LITE:       return "AMS_LITE";
    case DevAmsType::N3F:            return "N3F";
    case DevAmsType::N3S:            return "N3S";
    // AMS_LITE_MIXED is unreachable here: DevAms::GetAmsType() folds it into
    // AMS_LITE. Unit::is_ams_lite_mixed carries it instead.
    case DevAmsType::AMS_LITE_MIXED: return "AMS_LITE";
    }
    return "UNKNOWN";
}

// DevAmsTray::get_slot_name() only exists on branches carrying the newer device
// layer, so the same labelling rules are reproduced locally. owner_ams_id is
// only consulted for AMS trays; external spools do not belong to a unit.
std::string slot_name_for_tray(MachineObject* machine_obj, const DevAmsTray& tray, const std::string& owner_ams_id)
{
    if (!machine_obj) return "";

    int tray_index = 0;
    try {
        tray_index = std::stoi(tray.id);
    } catch (const std::exception&) {
        return "";
    }

    if (tray.ams_type != DevAmsType::EXT_SPOOL) {
        int ams_count = 0;
        for (const auto& ams_item : machine_obj->GetFilaSystem()->GetAmsList()) {
            if (ams_item.first == owner_ams_id) break;
            ++ams_count;
        }

        const char val_char = static_cast<char>('A' + ams_count);
        if (tray.ams_type == DevAmsType::N3S) return std::string("HT-") + val_char;
        return val_char + std::to_string(tray_index);
    }

    const int total_extruder_count = machine_obj->GetExtderSystem()->GetTotalExtderCount();
    const int total_external_count = static_cast<int>(machine_obj->vt_slot.size());
    if (total_external_count > total_extruder_count)
        return "Ext-" + std::to_string(256 - tray_index);

    if (total_external_count == 2) {
        if (tray.id == VIRTUAL_AMS_MAIN_ID_STR) return "Ext-R";
        if (tray.id == VIRTUAL_AMS_DEPUTY_ID_STR) return "Ext-L";
    }

    return "Ext";
}

SchemaFormat::Tray build_tray(MachineObject* machine_obj,
                              const DevAmsTray* tray,
                              const std::string& ams_id,
                              const std::string& slot_id,
                              const std::string& tray_label,
                              bool is_exists)
{
    SchemaFormat::Tray out;
    out.ams_id     = ams_id;
    out.slot_id    = slot_id;
    out.tray_label = tray_label;
    out.is_exists  = is_exists;
    out.reading    = machine_obj && tray && tray->is_reading(machine_obj->tray_reading_bits);
    out.info_ready = tray && tray->is_tray_info_ready();
    if (!tray) return out;

    out.fila_type  = tray->get_display_filament_type();
    out.sub_brands = tray->sub_brands;
    out.setting_id = tray->setting_id;
    out.tag_uid    = tray->tag_uid;
    out.color      = normalize_hex_for_web(tray->color);
    out.color_type = static_cast<int>(tray->ctype);
    out.remain     = tray->remain;
    out.remain_g   = tray->remain_g;
    out.is_bbl     = tray->is_bbl;
    out.binded_extruder_ids.assign(tray->binded_extruder_set.begin(), tray->binded_extruder_set.end());
    if (tray->current_extruder_id.has_value())
        out.current_extruder_id = *tray->current_extruder_id;
    if (tray->binded_switcher_pos.has_value()) {
        out.switcher_port = *tray->binded_switcher_pos == DevFilaSwitch::POS_IN_A
            ? SchemaValues::switcher_port::a
            : SchemaValues::switcher_port::b;
    }
    out.colors     = tray->cols;
    if (out.colors.empty() && !tray->color.empty())
        out.colors.push_back(tray->color);
    for (auto& color : out.colors)
        color = normalize_hex_for_web(color);
    if (out.colors.empty())
        out.colors.push_back(out.color);

    out.cali_idx = tray->cali_idx;
    if (tray->is_tray_info_ready() && machine_obj && machine_obj->GetCalib() &&
        machine_obj->GetCalib()->IsVersionInited()) {
        CalibUtils::get_pa_k_n_value_by_cali_idx(machine_obj, tray->cali_idx, out.k, out.n);
    } else {
        out.k = tray->k;
        out.n = tray->n;
    }
    return out;
}

SchemaFormat::FilaSwitchData build_fila_switch(MachineObject* machine_obj)
{
    SchemaFormat::FilaSwitchData out;

    auto fila_switch = machine_obj ? machine_obj->GetFilaSwitch() : nullptr;
    if (!fila_switch) return out;

    out.installed = fila_switch->IsInstalled();
    out.ready     = fila_switch->IsReady();

    const auto cali_status = fila_switch->GetCaliStatus();
    out.cali_status        = static_cast<int>(cali_status);
    out.cali_status_name   = cali_status == DevFilaSwitch::CaliStatus::CALI_STEPING
        ? SchemaValues::fila_switch_cali_status::stepping
        : SchemaValues::fila_switch_cali_status::idle;

    auto fill_port = [](SchemaFormat::FilaSwitchPort& port,
                        const std::optional<bool>& has_filament,
                        const std::optional<DevAmsSlotId>& slot) {
        if (has_filament.has_value())
            port.has_filament = *has_filament ? 1 : 0;
        if (slot.has_value()) {
            port.ams_id  = std::to_string(slot->first);
            port.slot_id = std::to_string(slot->second);
        }
    };
    fill_port(out.in_a, fila_switch->IsInA_HasFilament(), fila_switch->GetInA_SlotId());
    fill_port(out.in_b, fila_switch->IsInB_HasFilament(), fila_switch->GetInB_SlotId());

    if (const auto extruder_id = fila_switch->GetOutA_ExtruderId())
        out.out_a_extruder_id = *extruder_id;
    if (const auto extruder_id = fila_switch->GetOutB_ExtruderId())
        out.out_b_extruder_id = *extruder_id;

    return out;
}

} // namespace

bool IsSlotLoaded(MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id)
{
    if (!machine_obj || !machine_obj->GetExtderSystem() || ams_id.empty() || slot_id.empty())
        return false;
    for (const auto& ext : machine_obj->GetExtderSystem()->GetExtruders()) {
        if (ext.GetSlotNow().ams_id == ams_id && ext.GetSlotNow().slot_id == slot_id && ext.HasFilamentInExt())
            return true;
    }
    return false;
}

LoadedSlot Build(MachineObject* machine_obj, SchemaFormat::AmsListData& data)
{
    LoadedSlot loaded;

    auto fila_system = machine_obj ? machine_obj->GetFilaSystem() : nullptr;
    if (!fila_system) return loaded;

    data.selected_dev_id       = machine_obj->get_dev_id();
    data.detect_remain_enabled = fila_system->IsDetectRemainEnabled();

    for (const auto& [ams_id, ams] : fila_system->GetAmsList()) {
        if (!ams || !ams->IsExist() || ams->GetBindedExtruderSet().empty())
            continue;

        SchemaFormat::Unit unit;
        unit.ams_id           = ams->GetAmsId();
        unit.ams_type         = static_cast<int>(ams->GetAmsType());
        unit.ams_type_name    = ams_type_name_for_web(ams->GetAmsType());
        unit.is_ams_lite_mixed = ams->IsAmsLiteMixed();
        unit.humidity_level   = ams->SupportHumidityLevel() ? ams->GetHumidityLevel() : -1;
        unit.humidity_percent = ams->SupportHumidityPercent() ? ams->GetHumidityPercent() : -1;
        if (ams->SupportHumidityPercent())
            unit.humidity_display_type = SchemaValues::humidity_display_type::percent;
        else if (ams->SupportHumidityLevel())
            unit.humidity_display_type = SchemaValues::humidity_display_type::level;
        else
            unit.humidity_display_type = SchemaValues::humidity_display_type::none;

        const auto binded_extruders = ams->GetBindedExtruderSet();
        unit.binded_extruder_ids.assign(binded_extruders.begin(), binded_extruders.end());
        if (const auto switcher_pos = ams->GetSwitcherPos()) {
            unit.switcher_port = *switcher_pos == DevFilaSwitch::POS_IN_A
                ? SchemaValues::switcher_port::a
                : SchemaValues::switcher_port::b;
        }

        const int slot_count = ams->GetSlotCount();
        for (int i = 0; i < slot_count; ++i) {
            const std::string slot_id = std::to_string(i);
            const DevAmsTray* tray = ams->GetTray(slot_id);
            if (!tray)
                continue;

            if (IsSlotLoaded(machine_obj, unit.ams_id, slot_id) && loaded.ams_id.empty()) {
                loaded.ams_id  = unit.ams_id;
                loaded.slot_id = slot_id;
            }

            unit.trays.push_back(build_tray(
                machine_obj, tray, unit.ams_id, slot_id,
                slot_name_for_tray(machine_obj, *tray, unit.ams_id), tray->is_exists));
        }

        data.ams_units.push_back(std::move(unit));
    }

    // vt_slot is ordered main (255) first, deputy (254) second, which matches the
    // order the panel draws the external spools in.
    for (const DevAmsTray& tray : machine_obj->vt_slot) {
        const std::string ams_id = tray.id;
        const std::string slot_id = "0";
        std::string tray_label = slot_name_for_tray(machine_obj, tray, ams_id);
        if (tray_label == "Ext-L" || tray_label == "Ext-R")
            tray_label = "Ext";

        if (IsSlotLoaded(machine_obj, ams_id, slot_id) && loaded.ams_id.empty()) {
            loaded.ams_id  = ams_id;
            loaded.slot_id = slot_id;
        }

        const bool exists = tray.is_exists || tray.is_tray_info_ready();
        data.ext_slots.push_back(build_tray(
            machine_obj, &tray, ams_id, slot_id, tray_label, exists));
    }

    data.fila_switch = build_fila_switch(machine_obj);

    return loaded;
}

const SchemaFormat::Tray* FindTray(const SchemaFormat::AmsListData& data,
                                   const std::string& ams_id,
                                   const std::string& slot_id)
{
    for (const auto& unit : data.ams_units) {
        for (const auto& tray : unit.trays) {
            if (tray.ams_id == ams_id && tray.slot_id == slot_id) return &tray;
        }
    }
    for (const auto& tray : data.ext_slots) {
        if (tray.ams_id == ams_id && tray.slot_id == slot_id) return &tray;
    }
    return nullptr;
}

}}} // namespace Slic3r::GUI::AmsControlWebData
