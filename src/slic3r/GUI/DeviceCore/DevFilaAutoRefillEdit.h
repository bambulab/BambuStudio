#pragma once

#include "DevDefs.h"

#include <optional>
#include <vector>

namespace Slic3r {

class DevFilaSystem;

// ACK `result` returned by refill commands.
enum class DevFilaRefillAckResult : int
{
    SuccessWithWarning   = 1,
    Success              = 0,
    RequestParseFailed   = -1,
    AutoRefillDisabled   = -2,
    InvalidArgument      = -3,
    SlotNotOperable      = -4,
    EmptySlot            = -5,
    HeadConfigureFailed  = -6,
};

std::optional<DevFilaRefillAckResult> DevFilaRefillParseAckResult(const nlohmann::json& print_json);
bool DevFilaRefillAckSucceeded(DevFilaRefillAckResult result);
const char* DevFilaRefillAckResultMessage(DevFilaRefillAckResult result);

// MQTT refill_command 5 (Query) / 6 (QueryNext) block codes.
enum class DevFilaRefillBlockCode : int
{
    Available                    = 0,
    EmptySlot                    = 1,
    InvalidFeedChannel           = 2,
    ToolheadDisconnected         = 3,
    ToolheadIsNotNozzle          = 4,
    ToolRackSlotNotCalibrated    = 5,
    FilamentNotLoadedToExtruder  = 6,
    HiddenByNozzleExclusion      = 7,
    FilamentIdentityUnavailable  = 8,
    AlreadyInAnotherChain        = 9,
    AutoAssignedToAnotherChain   = 10,
    SourceFilamentTagUnknown     = 20,
    DuplicateTargetPosition      = 21,
    NozzleDiameterMismatch       = 22,
    NozzleFlowMismatch           = 23,
    CustomFilamentNotFound       = 24,
    FilamentCategoryMismatch     = 25,
    SupportPropertyMismatch      = 26,
    NozzleNotCalibrated          = 27,
    FilamentSubtypeMismatch      = 28,
    VisibleLayerLines            = 29,
    NozzleInfoUnknown            = 30,
};

// How a slot may be used when building a new refill group.
enum class DevFilaRefillBlockLevel
{
    Available,
    Warning,
    Disabled,
};

DevFilaRefillBlockLevel DevFilaRefillClassifyBlock(DevFilaRefillBlockCode block_code);

struct DevFilaRefillQuerySlot
{
    DevAmsSlotId slot_id;
    std::vector<DevFilaRefillBlockCode> block_codes;// Every reason the device reported for this slot
};

// Parsed payload of MQTT refill_command 5 (Query) / 6 (QueryNext).
struct DevFilaRefillQueryResult
{
    DevFilaRefillAckResult             ack_result = DevFilaRefillAckResult::Success;
    std::vector<DevFilaRefillQuerySlot> slots;

    std::vector<DevFilaRefillBlockCode> FindBlockCodes(const DevAmsSlotId& slot_id) const;// All reasons reported
    std::optional<DevFilaRefillBlockCode> FindBlockCode(const DevAmsSlotId& slot_id) const;// The most severe reason
    DevFilaRefillBlockLevel GetBlockLevel(const DevAmsSlotId& slot_id) const;
};

class DevFilaAutoRefillEdit
{
public:
    explicit DevFilaAutoRefillEdit(DevFilaSystem* owner) : m_owner(owner) {}
    std::optional<DevFilaRefillQueryResult> ParseQueryResult(const nlohmann::json& print_json) const;

private:
    DevFilaSystem* m_owner = nullptr;
};

} // namespace Slic3r
