#pragma once

#include "DevDefs.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace Slic3r {

class DevFilaSystem;

struct DevAmsRefillSlot
{
    enum class CreateType
    {
        Auto = 0,
        Manual = 1,
    };

    DevAmsSlotId slot_id;
    std::optional<CreateType> create_type;

    DevAmsRefillSlot() = default;
    explicit DevAmsRefillSlot(const DevAmsSlotId& id, std::optional<CreateType> type = std::nullopt)
        : slot_id(id), create_type(type)
    {}

    bool operator==(const DevAmsRefillSlot& other) const
    {
        return slot_id == other.slot_id && create_type == other.create_type;
    }
};

struct DevFilaAutoRefillGroup
{
    struct Tag {
        std::string filament_id;
        std::vector<std::string> filament_clr;
        DevFilaColorType filament_clr_type = DevFilaColorType::CTYPE_SINGLE;

        // useful in dual extruder without fila switcher
        // when extruder_id has_value(), the group must with same extruder id
        std::optional<int> extruder_id;

        // useful in M2Y
        std::optional<NozzleFlowType> nozzle_flow_type;// optional
        std::optional<float> nozzle_diameter;// optional

        bool operator==(const Tag& other) const
        {
            return filament_id == other.filament_id &&
                   filament_clr == other.filament_clr &&
                   filament_clr_type == other.filament_clr_type &&
                   nozzle_flow_type == other.nozzle_flow_type &&
                   nozzle_diameter == other.nozzle_diameter;
        }
    } fila_refill_tag;

    std::vector<DevAmsRefillSlot> fila_refill_slots;

    bool operator==(const DevFilaAutoRefillGroup& other) const
    {
        return fila_refill_tag == other.fila_refill_tag &&
               fila_refill_slots == other.fila_refill_slots;
    }
};

struct DevFilaAutoRefillGroups
{
    std::vector<DevFilaAutoRefillGroup> groups;

    bool operator==(const DevFilaAutoRefillGroups& other) const { return groups == other.groups; }
};

class DevFilaAutoRefillMgr
{
public:
    enum RefillVersion
    {
        FilaBackup = 0,
        RefillInfo = 1
    };

    explicit DevFilaAutoRefillMgr(DevFilaSystem* owner) : m_owner(owner) {}

public:
    void SetVersion(RefillVersion version) { m_version = version; }
    RefillVersion GetVersion() const { return m_version; }

    // Whether the user may manually edit RefillInfo relations on this printer.
    // Driven by fun2 bit 26; only meaningful for RefillVersion::RefillInfo.
    void SetSupportManualEdit(bool supported) { m_support_manual_edit = supported; }
    bool IsSupportManualEdit() const { return m_version == RefillVersion::RefillInfo && m_support_manual_edit; }

    bool IsSupported() const;
    bool IsEnabled() const;
    bool HasRelation() const;
    bool CanShow() const;

    DevFilaAutoRefillGroups GetGroups() const;
    DevFilaAutoRefillGroups GetGroups(int extruder_id) const;
    DevFilaAutoRefillGroups GetGroupsBySlot(const DevAmsSlotId& slot_id) const;

public:
    void ParseRefillInfo(const nlohmann::json& print_json);

private:
    DevFilaAutoRefillGroups GetGroupsByFilaBackup(int extruder_id) const;
    DevFilaAutoRefillGroups GetGroupsByRefillInfo(int extruder_id) const;

private:
    DevFilaSystem* m_owner = nullptr;
    RefillVersion m_version = RefillVersion::FilaBackup;
    bool m_support_manual_edit = false; // fun2 bit 26; RefillInfo manual editing
    DevFilaAutoRefillGroups m_refill_info_groups; // RefillVersion::RefillInfo
};

} // namespace Slic3r
