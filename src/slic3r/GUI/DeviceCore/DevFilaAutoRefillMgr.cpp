#include "DevFilaAutoRefillMgr.h"

#include "DevExtruderSystem.h"
#include "DevFilaSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"

#include <cmath>
#include "libslic3r/Log.hpp"

namespace Slic3r {
namespace {

std::optional<DevAmsSlotId> parse_refill_slot(const nlohmann::json& spool)
{
    if (!spool.is_object() || !spool.contains("pos") || !spool["pos"].is_number()) {
        return std::nullopt;
    }

    const auto pos = spool["pos"].get<std::uint32_t>();
    return DevAmsSlotId{static_cast<int>((pos >> 8) & 0xff), static_cast<int>(pos & 0xff)};
}

namespace detail {

inline DevFilaAutoRefillGroups ResolveAutoRefillGroups(
    const std::vector<int>& raw_groups,
    const std::map<int, DevAmsSlotId>& slot_map)
{
    DevFilaAutoRefillGroups groups;
    groups.groups.reserve(raw_groups.size());

    for (int raw_group : raw_groups) {
        DevFilaAutoRefillGroup group;
        const auto mask = static_cast<std::uint32_t>(raw_group);
        for (const auto& [slot_index, slot_id] : slot_map) {
            if (slot_index >= 0 && slot_index < 32 &&
                (mask & (std::uint32_t{1} << slot_index)) != 0) {
                group.fila_refill_slots.emplace_back(DevAmsRefillSlot(slot_id));
            }
        }
        groups.groups.emplace_back(std::move(group));
    }

    return groups;
}

inline DevFilaAutoRefillGroups FindAutoRefillGroupsBySlot(
    const DevFilaAutoRefillGroups& groups,
    const DevAmsSlotId& slot_id)
{
    DevFilaAutoRefillGroups matches;
    for (const auto& group : groups.groups) {
        const auto slot = std::find_if(group.fila_refill_slots.begin(), group.fila_refill_slots.end(),
                                       [&slot_id](const DevAmsRefillSlot& refill_slot) { return refill_slot.slot_id == slot_id; });
        if (slot != group.fila_refill_slots.end()) {
            matches.groups.emplace_back(group);
        }
    }
    return matches;
}

} // namespace detail

} // namespace

bool DevFilaAutoRefillMgr::IsSupported() const
{
    return m_owner && m_owner->GetOwner() && m_owner->GetOwner()->is_support_filament_backup;
}

bool DevFilaAutoRefillMgr::IsEnabled() const
{
    return m_owner && m_owner->GetAmsSystemSetting().IsAutoRefillEnabled();
}

bool DevFilaAutoRefillMgr::HasRelation() const
{
    if (!m_owner || !m_owner->GetOwner() || !m_owner->GetOwner()->GetExtderSystem()) {
        return false;
    }

    for (const auto& extruder : m_owner->GetOwner()->GetExtderSystem()->GetExtruders()) {
        if (!GetGroups(extruder.GetExtId()).groups.empty()) {
            return true;
        }
    }
    return false;
}

bool DevFilaAutoRefillMgr::CanShow() const
{
    return IsSupported() && IsEnabled() && m_owner->HasAms() && HasRelation();
}

DevFilaAutoRefillGroups DevFilaAutoRefillMgr::GetGroups() const
{
    DevFilaAutoRefillGroups all_groups;
    if (!m_owner || !m_owner->GetOwner() || !m_owner->GetOwner()->GetExtderSystem()) {
        return all_groups;
    }

    for (const auto& extruder : m_owner->GetOwner()->GetExtderSystem()->GetExtruders()) {
        auto groups = GetGroups(extruder.GetExtId());
        all_groups.groups.insert(all_groups.groups.end(), groups.groups.begin(), groups.groups.end());
    }
    return all_groups;
}

DevFilaAutoRefillGroups DevFilaAutoRefillMgr::GetGroups(int extruder_id) const
{
    if (m_version == RefillVersion::FilaBackup) {
        return GetGroupsByFilaBackup(extruder_id);
    } else if (m_version == RefillVersion::RefillInfo) {
        return GetGroupsByRefillInfo(extruder_id);
    }

    return {};
}

DevFilaAutoRefillGroups DevFilaAutoRefillMgr::GetGroupsBySlot(const DevAmsSlotId& slot_id) const
{
    DevFilaAutoRefillGroups matches;
    if (!m_owner || !m_owner->GetOwner() || !m_owner->GetOwner()->GetExtderSystem()) {
        return matches;
    }

    for (const auto& extruder : m_owner->GetOwner()->GetExtderSystem()->GetExtruders()) {
        auto groups = detail::FindAutoRefillGroupsBySlot(GetGroups(extruder.GetExtId()), slot_id);
        matches.groups.insert(matches.groups.end(), groups.groups.begin(), groups.groups.end());
    }
    return matches;
}

std::map<int, DevAmsSlotId> sGetRefillIndexMap(DevFilaSystem* fila_sys)
{
    std::map<int, DevAmsSlotId> tray_id_map;
    for (auto& [virtual_tray_id, virtual_tray_item] : fila_sys->GetVirtualTrays()) {
        try {
            int virtual_tray_id_int = stoi(virtual_tray_id);
            tray_id_map[virtual_tray_id_int] = DevAmsSlotId{ virtual_tray_id_int, 0 };
        } catch (...) {
            BOOST_LOG_TRIVIAL(warning) << "invalid virtual tray id: " << virtual_tray_id;
        }
    }

    for (auto& [ams_id, ams_item] : fila_sys->GetAmsList()) {
        for (auto& [slot_id, slot_item] : ams_item->GetTrays()) {
            if (ams_item && slot_item) {
                try {
                    int ams_id_int = stoi(ams_id);
                    int slot_id_int = stoi(slot_id);
                    int tray_index = -1;
                    if (ams_item->GetAmsType() == DevAmsType::N3S) {
                        tray_index = ams_id_int - 128 + 16;
                    } else if (ams_item->GetAmsType() == DevAmsType::AMS_LITE && ams_item->IsAmsLiteMixed()) {
                        tray_index = 24 + slot_id_int;
                    } else if (ams_item->GetAmsType() == DevAmsType::ZMULTI_A || ams_item->GetAmsType() == DevAmsType::ZMULTI_B) {
                        tray_index = 28 + slot_id_int;
                    } else if (ams_item->GetAmsType() == DevAmsType::ZMULTI_B) {
                        tray_index = 37 + slot_id_int;
                    } else {
                        tray_index = (ams_id_int * 4 + slot_id_int);
                    }
                    tray_id_map[tray_index] = { ams_id_int, slot_id_int };
                } catch (...) {
                    BOOST_LOG_TRIVIAL(warning) << "invalid ams id: " << ams_id << " or slot id: " << slot_id;
                }
            }
        }
    }

    return  tray_id_map;
}

DevFilaAutoRefillGroups DevFilaAutoRefillMgr::GetGroupsByFilaBackup(int extruder_id) const
{
    if (m_version != RefillVersion::FilaBackup) {
        return {};
    };

    if (!m_owner || !m_owner->GetOwner() || !m_owner->GetOwner()->GetExtderSystem()) {
        return {};
    }

    const auto extruder = m_owner->GetOwner()->GetExtderSystem()->GetExtderById(extruder_id);
    if (!extruder) {
        return {};
    }

    auto groups = detail::ResolveAutoRefillGroups(extruder->GetFilamBackup(), sGetRefillIndexMap(m_owner));
    for (auto& group : groups.groups) {
        for (const auto& refill_slot : group.fila_refill_slots) {
            const auto& slot_id = refill_slot.slot_id;
            const auto* tray = m_owner->GetAmsTray(std::to_string(slot_id.first), std::to_string(slot_id.second));
            if (!tray) {
                continue;
            }
            auto& tag = group.fila_refill_tag;

            if (!m_owner->GetOwner()->GetFilaSwitch()->IsInstalled() && m_owner->GetOwner()->GetExtderSystem()->GetTotalExtderCount() > 1) {
                tag.extruder_id = extruder_id;
            }

            tag.filament_id = tray->setting_id;
            tag.filament_clr = tray->cols;
            if (tag.filament_clr.empty() && !tray->color.empty()) {
                tag.filament_clr.emplace_back(tray->color);
            }
            tag.filament_clr_type = tray->ctype;
            break;
        }
    }
    return groups;
}

DevFilaAutoRefillGroups DevFilaAutoRefillMgr::GetGroupsByRefillInfo(int extruder_id) const
{
    if (m_version != RefillVersion::RefillInfo) {
        return {};
    };

    if (!m_owner || !m_owner->GetOwner() || !m_owner->GetOwner()->GetExtderSystem()) {
        return {};
    }

    const auto extruder = m_owner->GetOwner()->GetExtderSystem()->GetExtderById(extruder_id);
    if (!extruder) {
        return {};
    }

    DevFilaAutoRefillGroups groups;
    for (const auto& group : m_refill_info_groups.groups) {
        const auto& tag = group.fila_refill_tag;
        if (tag.extruder_id.has_value() && tag.extruder_id.value() != extruder_id) {
            continue;
        }

        groups.groups.emplace_back(group);
    }
    return groups;
}

std::optional<NozzleFlowType> parse_refill_nozzle_flow_type(const std::string& value)
{
    if (value == "S") return NozzleFlowType::S_FLOW;
    if (value == "H") return NozzleFlowType::H_FLOW;
    if (value == "U") return NozzleFlowType::U_FLOW;
    if (value == "T") return NozzleFlowType::T_FLOW;
    if (value == "E") return NozzleFlowType::E_FLOW;
    return std::nullopt;
}


void DevFilaAutoRefillMgr::ParseRefillInfo(const nlohmann::json& print_json)
{
    if (!print_json.is_object() || !print_json.contains("RefillInfo")) {
        return;
    }

    DevFilaAutoRefillGroups parsed_groups;
    const auto& refill_info = print_json["RefillInfo"];
    if (!refill_info.is_array()) {
        m_refill_info_groups = std::move(parsed_groups);
        return;
    }

    for (const auto& refill_item : refill_info) {
        if (!refill_item.is_object()) {
            continue;
        }

        DevFilaAutoRefillGroup base_group;
        auto& tag = base_group.fila_refill_tag;
        if (refill_item.contains("chain_fila") && refill_item["chain_fila"].is_string()) {
            tag.filament_id = refill_item["chain_fila"].get<std::string>();
        }

        if (refill_item.contains("tag") && refill_item["tag"].is_string()) {
            base_group.refill_tag_id = refill_item["tag"].get<std::string>();
        }

        if (refill_item.contains("color") && refill_item["color"].is_object()) {
            const auto& color = refill_item["color"];
            if (color.contains("col") && color["col"].is_array()) {
                for (const auto& value : color["col"]) {
                    if (value.is_string()) {
                        tag.filament_clr.emplace_back(value.get<std::string>());
                    }
                }
            }
            if (color.contains("ctype") && color["ctype"].is_number_integer()) {
                const int ctype = color["ctype"].get<int>();
                if (ctype >= DevFilaColorType::CTYPE_GRADIANT && ctype <= DevFilaColorType::CTYPE_SINGLE) {
                    tag.filament_clr_type = static_cast<DevFilaColorType>(ctype);
                }
            }
        }

        if (refill_item.contains("nozzle") && refill_item["nozzle"].is_object()) {
            const auto& nozzle = refill_item["nozzle"];
            if (nozzle.contains("dia") && nozzle["dia"].is_number()) {
                tag.nozzle_diameter = nozzle["dia"].get<float>() / 100.0f;
            }
            if (nozzle.contains("vol") && nozzle["vol"].is_string()) {
                tag.nozzle_flow_type = parse_refill_nozzle_flow_type(nozzle["vol"].get<std::string>());
            }
        }

        if (refill_item.contains("spool") && refill_item["spool"].is_array()) {
            for (const auto& spool : refill_item["spool"]) {
                if (!spool.is_object()) {
                    continue;
                }
                const auto slot = parse_refill_slot(spool);
                if (slot) {
                    std::optional<DevAmsRefillSlot::CreateType> create_type;
                    if (spool.contains("ext") && spool["ext"].is_number_integer()) {
                        const int ext = spool["ext"].get<int>();
                        if (ext == 0) {
                            create_type = DevAmsRefillSlot::CreateType::Auto;
                        } else if (ext == 1) {
                            create_type = DevAmsRefillSlot::CreateType::Manual;
                        }
                    }
                    base_group.fila_refill_slots.emplace_back(*slot, create_type);
                }
            }
        }

        parsed_groups.groups.emplace_back(std::move(base_group));
    }

    m_refill_info_groups = std::move(parsed_groups);
}

} // namespace Slic3r
