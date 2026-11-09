#pragma once

#include "DevDefs.h"
#include "DevFilaAutoRefillEdit.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace Slic3r {

class DevCtrlToken;
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

    // Device-side identity of the chain, as reported in RefillInfo `tag`
    // (e.g. "GFA00_2_1_C12E1FFF_H_0.4").
    std::string refill_tag_id;

    std::vector<DevAmsRefillSlot> fila_refill_slots;

    bool operator==(const DevFilaAutoRefillGroup& other) const
    {
        return fila_refill_tag == other.fila_refill_tag &&
               refill_tag_id == other.refill_tag_id &&
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

    bool IsSupported() const;
    bool IsEnabled() const;
    bool HasRelation() const;
    bool CanShow() const;

    DevFilaAutoRefillGroups GetGroups() const;
    DevFilaAutoRefillGroups GetGroups(int extruder_id) const;
    DevFilaAutoRefillGroups GetGroupsBySlot(const DevAmsSlotId& slot_id) const;

    // RefillInfo, Whether the user may manually edit RefillInfo relations on this printer.
    void SetSupportManualEdit(bool supported) { m_support_manual_edit = supported; }
    bool IsSupportManualEdit() const { return m_version == RefillVersion::RefillInfo && m_support_manual_edit; }

    // Asks which slots may start a new group.
    std::shared_ptr<DevCtrlToken> CtrlQuery();
    // Asks which slots may still join a group once `to` has been picked.
    std::shared_ptr<DevCtrlToken> CtrlQueryNext(const DevAmsSlotId& to);

    // Creates a group holding `head` alone.
    std::shared_ptr<DevCtrlToken> CtrlCreateGroup(const DevAmsSlotId& head);
    std::shared_ptr<DevCtrlToken> CtrlCreateGroup(const DevAmsSlotId& head, const std::vector<DevAmsSlotId>& members);
    std::shared_ptr<DevCtrlToken> CtrlPushBack(const DevAmsSlotId& slot, const DevAmsSlotId& member);
    std::shared_ptr<DevCtrlToken> CtrlPopBack(const DevAmsSlotId& member);
    std::shared_ptr<DevCtrlToken> CtrlRemove(const DevAmsSlotId& slot);
    std::shared_ptr<DevCtrlToken> CtrlMove(const DevAmsSlotId& slot, const DevAmsSlotId& member, bool as_head);
    std::shared_ptr<DevCtrlToken> CtrlReset(const std::string& group_tag);

    void ParseRefillInfo(const nlohmann::json& print_json);

private:
    DevFilaAutoRefillGroups GetGroupsByFilaBackup(int extruder_id) const;
    DevFilaAutoRefillGroups GetGroupsByRefillInfo(int extruder_id) const;

    // Raw protocol wrappers. Callers should go through the named operations
    // above so the pos/to_pos/as_head conventions stay in one place.
    std::shared_ptr<DevCtrlToken> CtrlQueryNext(std::uint16_t to_pos);
    std::shared_ptr<DevCtrlToken> CtrlEdit(std::uint16_t pos, std::uint16_t to_pos, bool as_head);
    std::shared_ptr<DevCtrlToken> CtrlEdit(const DevAmsSlotId& from, const DevAmsSlotId& to, bool as_head);
    std::shared_ptr<DevCtrlToken> CtrlEditByQuene(std::uint16_t to_pos, const std::vector<std::uint16_t>& pos);
    std::shared_ptr<DevCtrlToken> CtrlEditByQuene(const DevAmsSlotId& to, const std::vector<DevAmsSlotId>& slots);

private:
    DevFilaSystem* m_owner = nullptr;
    RefillVersion m_version = RefillVersion::FilaBackup;
    bool m_support_manual_edit = false; // fun2 bit 26; RefillInfo manual editing
    DevFilaAutoRefillGroups m_refill_info_groups; // RefillVersion::RefillInfo
};

} // namespace Slic3r
