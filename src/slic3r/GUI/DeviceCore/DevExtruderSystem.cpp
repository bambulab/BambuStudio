#include <nlohmann/json.hpp>
#include "DevExtruderSystem.h"
#include "DevNozzleRack.h"
#include "DevNozzleSystem.h"
#include "DevConfigUtil.h"

// TODO: remove this include
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "DevUtil.h"
#include "DevFilaSystem.h"
#include "libslic3r/Log.hpp"

using namespace nlohmann;

namespace Slic3r
{
    DevExtrusionDirection decode_extrusion_direction(unsigned int info)
    {
        switch ((info >> 11) & 0x3u) {
        case 0: return DevExtrusionDirection::Idle;
        case 1: return DevExtrusionDirection::Extruding;
        case 2: return DevExtrusionDirection::Retracting;
        default: return DevExtrusionDirection::Unknown;
        }
    }

    DevRackExtruderVisualState resolve_rack_extruder_visual_state(MachineObject* obj, const DevExtder& extruder)
    {
        if (!obj || !obj->GetNozzleSystem()) {
            return DevRackExtruderVisualState::Empty;
        }

        auto* nozzle_system = obj->GetNozzleSystem();
        const auto rack = nozzle_system->GetNozzleRack();

        if (rack && rack->IsPicking() && obj->GetExtderSystem() && obj->GetExtderSystem()->GetTotalExtderCount() == 1) {
            const auto src_id = nozzle_system->GetReplaceNozzleSrc();
            if (src_id && *src_id >= 0x10) {
                const auto source = nozzle_system->GetNozzleOptByAddrId(*src_id - 0x10);
                if (source && source->IsOnRack() && source->GetNozzlePosId() == *src_id) {
                    return DevRackExtruderVisualState::Picking;
                }
            }
        }

        if (rack && rack->IsPlacing()) {
            const auto tar_id = nozzle_system->GetReplaceNozzleTar();
            if (tar_id && *tar_id >= 0x10) {
                const auto target = nozzle_system->GetNozzleOptByAddrId(*tar_id - 0x10);
                if (target && !target->IsOnRack() &&
                    target->GetNozzlePosId() == *tar_id &&
                    target->GetNozzleId() == extruder.GetExtId()) {
                    return DevRackExtruderVisualState::Placing;
                }
            }
        }

        const bool has_nozzle = extruder.HasNozzleInstalled() && !nozzle_system->GetExtNozzle(extruder.GetExtId()).IsEmpty();
        if (!has_nozzle) {
            return DevRackExtruderVisualState::Empty;
        }

        const auto direction = extruder.GetExtrusionDirection();
        if (direction == DevExtrusionDirection::Extruding) return DevRackExtruderVisualState::ClampedExtruding;
        if (direction == DevExtrusionDirection::Retracting) return DevRackExtruderVisualState::ClampedRetracting;
        if (!extruder.HasFilamentInExt()) return DevRackExtruderVisualState::ClampedNoFilament;
        return DevRackExtruderVisualState::Clamped;
    }

    std::string_view rack_extruder_visual_state_key(DevRackExtruderVisualState state)
    {
        switch (state) {
        case DevRackExtruderVisualState::Empty:              return "empty";
        case DevRackExtruderVisualState::Clamped:            return "clamped";
        case DevRackExtruderVisualState::ClampedRetracting:  return "clamped_retracting";
        case DevRackExtruderVisualState::ClampedExtruding:   return "clamped_extruding";
        case DevRackExtruderVisualState::Picking:            return "picking";
        case DevRackExtruderVisualState::Placing:            return "placing";
        case DevRackExtruderVisualState::ClampedNoFilament:  return "clamped_no_filament";
        }
        return "clamped";
    }

    int DevExtder::to_physical_extruder_id(int total_ext_count, int logical_extruder_id)
    {
        if (total_ext_count == 2) {
            return logical_extruder_id == 1 ? 0 : 1;
        } else if (total_ext_count == 1) {
            return 0;
        } else {
            return logical_extruder_id - 1;
        }
    }

    int DevExtder::to_logical_extruder_id(int total_ext_count, int physical_extruder_id)
    {
        if (total_ext_count == 2) {
            return physical_extruder_id == 1 ? 0 : 1;
        } else if (total_ext_count == 1) {
            return 0;
        } else {
            return physical_extruder_id + 1;
        }
    }

    wxString DevExtder::GetDisplayLoc() const
    {
        if (system->GetTotalExtderCount() == 2)
        {
            return _L(DevPrinterConfigUtil::get_toolhead_display_name(
                system->Owner()->printer_type, m_ext_id,
                ToolHeadComponent::Extruder, ToolHeadNameCase::LowerCase, true));  // "right" / "main"
        }

        return wxEmptyString;
    }

    wxString DevExtder::GetDisplayName() const
    {
        if (system->GetTotalExtderCount() == 2)
        {
            std::string name = DevPrinterConfigUtil::get_toolhead_display_name(
                system->Owner()->printer_type, m_ext_id,
                ToolHeadComponent::Extruder, ToolHeadNameCase::LowerCase);
            return _L(name);  // "right extruder" / "main extruder"
        }

        return _L("extruder");
    }

    NozzleType DevExtder::GetNozzleType() const
    {
        return system->Owner()->GetNozzleSystem()->GetExtNozzle(m_current_nozzle_id).m_nozzle_type;
    }

    NozzleFlowType DevExtder::GetNozzleFlowType() const
    {
        return system->Owner()->GetNozzleSystem()->GetExtNozzle(m_current_nozzle_id).m_nozzle_flow;
    }

    float DevExtder::GetNozzleDiameter() const
    {
        return system->Owner()->GetNozzleSystem()->GetExtNozzle(m_current_nozzle_id).m_diameter;
    }

    NozzleDiameterType DevExtder::GetNozzleDiameterType() const
    {
        return system->Owner()->GetNozzleSystem()->GetExtNozzle(m_current_nozzle_id).GetNozzleDiameterType();
    }

    std::unordered_map<int, bool> DevExtder::GetBackupStatus(unsigned int fila_back_group)
    {
        std::unordered_map<int, bool> trayid_group;
        for (int i = 0; i < 16; i++)
        {
            if (fila_back_group & (1 << i))
            {
                trayid_group[i] = true;
            }
        }

        for (int j = 16; j <= 23; j++)/* single ams is from 128*/
        {
            if (fila_back_group & (1 << j)) {
                trayid_group[128 + j - 16] = true;
            }
        }

        for (int i = 24; i <= 27; i++) /* ams lite for N9*/
        {
            if (fila_back_group & (1 << i)) { trayid_group[i] = true; }
        }

        return trayid_group;
    }

    bool  DevExtder::IsBowdenExtuder() const
    {
        Preset * preset = GUI::get_printer_preset(system->Owner());

        if (!preset) return false;

        auto exrtuder_type_opt = dynamic_cast<const ConfigOptionEnumsGeneric *>(preset->config.option("extruder_type"));

        if (!exrtuder_type_opt) return false;

        // reverse the order of preset extuder id to match the real extruder id
        ExtruderType extruder_type = (ExtruderType)exrtuder_type_opt->values[exrtuder_type_opt->values.size() - 1 - GetExtId()];

        return extruder_type == ExtruderType::etBowden;
    }

    std::vector<std::vector<DevAmsSlotId>> DevExtder::GetBackupSlots() const
    {
        if (!system || !system->Owner() || !system->Owner()->GetFilaSystem()) {
            return {};
        }
        std::vector<std::vector<DevAmsSlotId>> backup_groups;
        const auto groups = system->Owner()->GetFilaSystem()->GetAutoRefillMgr().GetGroups(m_ext_id);
        backup_groups.reserve(groups.groups.size());
        for (const auto& group : groups.groups) {
            std::vector<DevAmsSlotId> slots;
            slots.reserve(group.fila_refill_slots.size());
            for (const auto& refill_slot : group.fila_refill_slots) {
                slots.emplace_back(refill_slot.slot_id);
            }
            backup_groups.emplace_back(std::move(slots));
        }
        return backup_groups;
    }

    std::optional<int> DevExtder::GetPickedNozzleAddrId() const
    {
        if (HasNozzleInstalled()) {
            return system->Owner()->GetNozzleSystem()->GetExtNozzle(m_ext_id).GetAddrId();
        }

        return std::nullopt;
    }

    DevExtderSystem::DevExtderSystem(MachineObject* obj)
        : m_owner(obj)
    {
        m_total_extder_count = DevPrinterConfigUtil::get_default_extruder_count(obj ? obj->printer_type : "");
        for (int ext_id = 0; ext_id < m_total_extder_count; ext_id++) {
            m_extders.emplace_back(this, ext_id);
        }
    }

    bool DevExtderSystem::CanQuitSwitching() const
    {
        if (!IsSwitchingFailed())
        {
            return false;
        }
        return !m_owner->is_in_printing() && !m_owner->is_in_printing_pause();
    }

    std::optional<DevExtder> DevExtderSystem::GetCurrentExtder() const
    {
        return GetExtderById(m_current_extder_id);
    }

    std::optional<DevExtder> DevExtderSystem::GetLoadingExtder() const
    {
        if (m_current_loading_extder_id.has_value()) {
            return GetExtderById(m_current_loading_extder_id.value());
        }

        return std::nullopt;
    }

    std::optional<DevExtder> DevExtderSystem::GetExtderById(int extder_id) const
    {
        if (extder_id == 0xF) {
            BOOST_LOG_TRIVIAL(info) << "the extruder maybe changing: " << extder_id;
            return std::nullopt;
        }

        if (extder_id < 0 || extder_id >= m_extders.size()) {
            BOOST_LOG_TRIVIAL(error) << "No extruder found for " << extder_id;
            return std::nullopt;
        }

        return m_extders[extder_id];
    }

    std::string DevExtderSystem::GetCurrentAmsId() const
    {
        auto cur_extder = GetCurrentExtder();
        return cur_extder ? cur_extder->GetSlotNow().ams_id : DevAmsSlotInfo().ams_id;
    }

    std::string DevExtderSystem::GetCurrentSlotId() const
    {
        auto cur_extder = GetCurrentExtder();
        return cur_extder ? GetCurrentExtder()->GetSlotNow().slot_id : DevAmsSlotInfo().slot_id;
    }

    std::string DevExtderSystem::GetTargetAmsId() const
    {
        auto cur_extder = GetCurrentExtder();
        return cur_extder ? GetCurrentExtder()->GetSlotTarget().ams_id : DevAmsSlotInfo().ams_id;
    }

    std::string DevExtderSystem::GetTargetSlotId() const
    {
        auto cur_extder = GetCurrentExtder();
        return cur_extder ? GetCurrentExtder()->GetSlotTarget().slot_id : DevAmsSlotInfo().slot_id;
    }

    bool DevExtderSystem::HasFilamentBackup() const
    {
        return m_owner && m_owner->GetFilaSystem() &&
               m_owner->GetFilaSystem()->GetAutoRefillMgr().HasRelation();
    }

    std::vector<DevAmsSlotId> DevExtderSystem::GetBackupAmsSlotInGroup(const DevAmsSlotId& ams_slot_id)
    {
        std::vector<DevAmsSlotId> backup_group;
        if (!m_owner || !m_owner->GetFilaSystem()) {
            return backup_group;
        }

        for (const auto& group : m_owner->GetFilaSystem()->GetAutoRefillMgr().GetGroupsBySlot(ams_slot_id).groups) {
            for (const auto& refill_slot : group.fila_refill_slots) {
                const auto& slot_id = refill_slot.slot_id;
                if (slot_id != ams_slot_id) {
                    backup_group.emplace_back(slot_id);
                }
            }
        }
        return backup_group;
    }

    void ExtderSystemParser::ParseV1_0(const nlohmann::json& print_json, DevExtderSystem* system)
    {
        if (system->GetTotalExtderCount() != 1)
        {
            return;
        }

        if (print_json.contains("nozzle_temper") && print_json["nozzle_temper"].is_number())
        {
            system->m_extders[MAIN_EXTRUDER_ID].m_cur_temp = print_json["nozzle_temper"].get<float>();
        }

        if (print_json.contains("nozzle_target_temper") && print_json["nozzle_target_temper"].is_number())
        {
            system->m_extders[MAIN_EXTRUDER_ID].m_target_temp = print_json["nozzle_target_temper"].get<float>();
        }

        if (print_json.contains("ams") && print_json["ams"].contains("tray_tar"))
        {
            const std::string& tray_tar = print_json["ams"]["tray_tar"].get<std::string>();
            if (!tray_tar.empty())
            {
                int tray_tar_int = atoi(tray_tar.c_str());
                if (tray_tar_int == VIRTUAL_TRAY_MAIN_ID) /*255 means unloading*/ {
                    system->m_extders[MAIN_EXTRUDER_ID].m_star.ams_id = "";
                    system->m_extders[MAIN_EXTRUDER_ID].m_star.slot_id = std::to_string(VIRTUAL_TRAY_MAIN_ID);
                }
                else if (tray_tar_int == VIRTUAL_TRAY_DEPUTY_ID) /*254 means loading ext spool*/ {
                    system->m_extders[MAIN_EXTRUDER_ID].m_star.ams_id = std::to_string(VIRTUAL_TRAY_MAIN_ID);
                    system->m_extders[MAIN_EXTRUDER_ID].m_star.slot_id = "0";
                }
                else
                {
                    if (tray_tar_int >= 0x80 && tray_tar_int <= 0x87)
                    {
                        system->m_extders[MAIN_EXTRUDER_ID].m_star.ams_id = std::to_string(tray_tar_int);
                    }
                    else
                    {
                        system->m_extders[MAIN_EXTRUDER_ID].m_star.ams_id = std::to_string(tray_tar_int >> 2);
                    }

                    system->m_extders[MAIN_EXTRUDER_ID].m_star.slot_id = std::to_string(tray_tar_int & 0x3);
                }
            }
        }

        if (print_json.contains("ams") && print_json["ams"].contains("tray_now"))
        {
            const std::string& tray_now = print_json["ams"]["tray_now"].get<std::string>();
            if (!tray_now.empty())
            {
                int tray_now_int = atoi(tray_now.c_str());
                if (tray_now_int == VIRTUAL_TRAY_MAIN_ID)
                {
                    system->m_extders[MAIN_EXTRUDER_ID].m_snow.ams_id = "";
                    system->m_extders[MAIN_EXTRUDER_ID].m_snow.slot_id = "";
                }
                else if (tray_now_int == VIRTUAL_TRAY_DEPUTY_ID)
                {
                    system->m_extders[MAIN_EXTRUDER_ID].m_snow.ams_id = std::to_string(VIRTUAL_TRAY_MAIN_ID);
                    system->m_extders[MAIN_EXTRUDER_ID].m_snow.slot_id = "0";
                }
                else
                {
                    if (tray_now_int >= 0x80 && tray_now_int <= 0x87)
                    {
                        system->m_extders[MAIN_EXTRUDER_ID].m_snow.ams_id = std::to_string(tray_now_int);
                    }
                    else
                    {
                        system->m_extders[MAIN_EXTRUDER_ID].m_snow.ams_id = std::to_string(tray_now_int >> 2);
                    }

                    system->m_extders[MAIN_EXTRUDER_ID].m_snow.slot_id = std::to_string(tray_now_int & 0x3);
                }
            }
        }

        system->m_current_busy_for_loading = false;
        system->m_current_loading_extder_id = INVALID_EXTRUDER_ID;
        if (!system->m_extders[MAIN_EXTRUDER_ID].m_star.ams_id.empty()) {
            system->m_current_busy_for_loading = (system->m_extders[MAIN_EXTRUDER_ID].m_snow != system->m_extders[MAIN_EXTRUDER_ID].m_star);
            if (system->m_current_busy_for_loading) {
                system->m_current_loading_extder_id = MAIN_EXTRUDER_ID;
            }
        }
    }

    void ExtderSystemParser::ParseV2_0(const nlohmann::json& extruder_json, DevExtderSystem* system)
    {
        system->m_total_extder_count = DevUtil::get_flag_bits(extruder_json["state"].get<int>(), 0, 4);
        // limit extuder count to 2
        if (system->m_total_extder_count > 2){
            system->m_total_extder_count = 2;
        }

        if (system->Owner()->GetFilaSystem()->GetVirtualTrays().size() > 2) {
            system->m_total_extder_count = 1;// hint to 1 since firmware, to be removed if firmware fixed
        }

        if (system->m_current_extder_id != DevUtil::get_flag_bits(extruder_json["state"].get<int>(), 4, 4))
        {
            system->Owner()->targ_nozzle_id_from_pc = INVALID_EXTRUDER_ID;
            system->m_current_extder_id = DevUtil::get_flag_bits(extruder_json["state"].get<int>(), 4, 4);
        }
        else if (system->m_switch_extder_state == ES_SWITCHING_FAILED)
        {
            system->Owner()->targ_nozzle_id_from_pc = INVALID_EXTRUDER_ID;
        }

        system->m_target_extder_id = DevUtil::get_flag_bits(extruder_json["state"].get<int>(), 8, 4);
        system->m_switch_extder_state = (DevExtderSwitchState)DevUtil::get_flag_bits(extruder_json["state"].get<int>(), 12, 3);
        system->m_current_loading_extder_id = DevUtil::get_flag_bits(extruder_json["state"].get<int>(), 15, 4);
        system->m_current_busy_for_loading = DevUtil::get_flag_bits(extruder_json["state"].get<int>(), 19);

        system->m_extders.clear();
        for (auto it = extruder_json["info"].begin(); it != extruder_json["info"].cend(); it++)
        {

            DevExtder extder_obj(system);

            const auto&  njon = it.value();
            extder_obj.SetExtId(njon["id"].get<int>());

            extder_obj.m_filam_bak.clear();
            if (njon.contains("filam_bak")) {
                const json& filam_bak_items = njon["filam_bak"];
                for (const auto& filam_bak_item : filam_bak_items) {
                    const auto& filam_bak_val = filam_bak_item.get<int>();
                    extder_obj.m_filam_bak.emplace_back(filam_bak_val);
                }
            }

            const int extder_info = njon["info"].get<int>();
            extder_obj.m_ext_has_filament = (DevUtil::get_flag_bits(extder_info, 1) != 0);
            extder_obj.m_buffer_has_filament = (DevUtil::get_flag_bits(extder_info, 2) != 0);
            extder_obj.m_has_nozzle = (DevUtil::get_flag_bits(extder_info, 3) != 0);
            extder_obj.m_extrusion_direction = decode_extrusion_direction(static_cast<unsigned int>(extder_info));
            extder_obj.m_cur_temp = DevUtil::get_flag_bits(njon["temp"].get<int>(), 0, 16);
            extder_obj.m_target_temp = DevUtil::get_flag_bits(njon["temp"].get<int>(), 16, 16);

            if(system->m_total_extder_count == 1 && DevUtil::get_flag_bits(njon["spre"].get<int>(), 0, 8) == 0xff) {
                extder_obj.m_spre.slot_id = "";
                extder_obj.m_spre.ams_id = "";
            } else{
                extder_obj.m_spre.slot_id = std::to_string(DevUtil::get_flag_bits(njon["spre"].get<int>(), 0, 8));
                extder_obj.m_spre.ams_id = std::to_string(DevUtil::get_flag_bits(njon["spre"].get<int>(), 8, 8));
            }

            if(system->m_total_extder_count == 1 && DevUtil::get_flag_bits(njon["snow"].get<int>(), 0, 8) == 0xff) {
                extder_obj.m_snow.slot_id = "";
                extder_obj.m_snow.ams_id = "";
            } else{
                extder_obj.m_snow.slot_id = std::to_string(DevUtil::get_flag_bits(njon["snow"].get<int>(), 0, 8));
                extder_obj.m_snow.ams_id = std::to_string(DevUtil::get_flag_bits(njon["snow"].get<int>(), 8, 8));
            }

            if(system->m_total_extder_count == 1 && DevUtil::get_flag_bits(njon["star"].get<int>(), 0, 8) == 0xff) {
                extder_obj.m_star.slot_id = "";
                extder_obj.m_star.ams_id = "";
            } else{
                extder_obj.m_star.slot_id = std::to_string(DevUtil::get_flag_bits(njon["star"].get<int>(), 0, 8));
                extder_obj.m_star.ams_id = std::to_string(DevUtil::get_flag_bits(njon["star"].get<int>(), 8, 8));
            }

            extder_obj.m_ams_stat = DevUtil::get_flag_bits(njon["stat"].get<int>(), 0, 16);
            extder_obj.m_rfid_stat = DevUtil::get_flag_bits(njon["stat"].get<int>(), 16, 16);

            extder_obj.m_current_filament_change_step = static_cast<DevFilamentStep> (DevUtil::get_flag_bits(njon["stat"].get<int>(), 0, 8));
            if (extder_obj.m_current_filament_change_step.has_value() &&  extder_obj.m_current_filament_change_step.value() >= DevFilamentStep::STEP_COUNT) {
                extder_obj.m_current_filament_change_step.reset();
            }

            //current nozzle info
            extder_obj.m_current_nozzle_id = njon["hnow"].get<int>();

            // limit extuder count to 2
            if (system->m_extders.size() < system->m_total_extder_count) {
                system->m_extders.push_back(extder_obj);
            }
        }
    }
}