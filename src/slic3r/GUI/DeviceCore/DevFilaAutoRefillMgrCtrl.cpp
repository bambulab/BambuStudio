#include "DevFilaAutoRefillMgr.h"

#include "DevCtrlToken.h"
#include "DevFilaSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include "libslic3r/Log.hpp"

namespace Slic3r {

namespace {

// MQTT print.command = "refill" (protocol version 0).
enum class RefillCommand : int
{
    Reset     = 1,
    Edit      = 3,
    Query     = 5,
    QueryNext = 6,
};

constexpr const char* kMqttCommand = "refill";
constexpr int         kMqttVersion = 0;

// A group can also be created in one shot by the same "refill" command bumped
// to version 1, which carries the head in `to_pos` and every other member in a
// `pos` array instead of a single slot.
constexpr int         kMqttEditByQueneVersion = 1;

std::uint16_t encode_slot_pos(const DevAmsSlotId& slot_id)
{
    return static_cast<std::uint16_t>(((slot_id.first & 0xff) << 8) | (slot_id.second & 0xff));
}

nlohmann::json make_refill_command(RefillCommand refill_command, const std::string& sequence_id)
{
    nlohmann::json j;
    j["print"]["command"]        = kMqttCommand;
    j["print"]["sequence_id"]    = sequence_id;
    j["print"]["version"]        = kMqttVersion;
    j["print"]["refill_command"] = static_cast<int>(refill_command);
    return j;
}

std::shared_ptr<DevCtrlToken> publish_refill(DevFilaSystem* owner, nlohmann::json& j, const std::string& sequence_id)
{
    if (!owner || !owner->GetOwner()) {
        return nullptr;
    }

    MachineObject* obj = owner->GetOwner();
    const auto token = obj->GetCtrlTokenMgr().Build(sequence_id);
    token->SetSendResult(obj->publish_json(j));
    return token;
}

} // namespace

std::shared_ptr<DevCtrlToken> DevFilaAutoRefillMgr::CtrlReset(const std::string& group_tag)
{
    const std::string sequence_id = std::to_string(MachineObject::m_sequence_id++);
    auto j = make_refill_command(RefillCommand::Reset, sequence_id);
    j["print"]["group_tag"] = group_tag;
    return publish_refill(m_owner, j, sequence_id);
}

std::shared_ptr<DevCtrlToken> DevFilaAutoRefillMgr::CtrlEdit(std::uint16_t pos, std::uint16_t to_pos, bool as_head)
{
    const std::string sequence_id = std::to_string(MachineObject::m_sequence_id++);
    auto j = make_refill_command(RefillCommand::Edit, sequence_id);
    j["print"]["pos"]     = pos;
    j["print"]["to_pos"]  = to_pos;
    j["print"]["as_head"] = as_head;
    return publish_refill(m_owner, j, sequence_id);
}

std::shared_ptr<DevCtrlToken> DevFilaAutoRefillMgr::CtrlEdit(const DevAmsSlotId& from, const DevAmsSlotId& to, bool as_head)
{
    return CtrlEdit(encode_slot_pos(from), encode_slot_pos(to), as_head);
}

std::shared_ptr<DevCtrlToken> DevFilaAutoRefillMgr::CtrlEditByQuene(std::uint16_t to_pos, const std::vector<std::uint16_t>& pos)
{
    const std::string sequence_id = std::to_string(MachineObject::m_sequence_id++);

    auto j = make_refill_command(RefillCommand::Edit, sequence_id);
    j["print"]["version"] = kMqttEditByQueneVersion;
    j["print"]["to_pos"]  = to_pos;
    j["print"]["pos"]     = pos;
    return publish_refill(m_owner, j, sequence_id);
}

std::shared_ptr<DevCtrlToken> DevFilaAutoRefillMgr::CtrlEditByQuene(const DevAmsSlotId& to, const std::vector<DevAmsSlotId>& slots)
{
    std::vector<std::uint16_t> pos;
    pos.reserve(slots.size());
    for (const auto& slot : slots) {
        pos.push_back(encode_slot_pos(slot));
    }
    return CtrlEditByQuene(encode_slot_pos(to), pos);
}

std::shared_ptr<DevCtrlToken> DevFilaAutoRefillMgr::CtrlCreateGroup(const DevAmsSlotId& head)
{
    // pos == to_pos with as_head opens a new chain on that slot.
    return CtrlEdit(head, head, true);
}

std::shared_ptr<DevCtrlToken> DevFilaAutoRefillMgr::CtrlCreateGroup(const DevAmsSlotId& head, const std::vector<DevAmsSlotId>& members)
{
    if (members.empty()) {
        return CtrlCreateGroup(head);
    }
    return CtrlEditByQuene(head, members);
}

std::shared_ptr<DevCtrlToken> DevFilaAutoRefillMgr::CtrlPushBack(const DevAmsSlotId& slot, const DevAmsSlotId& member)
{
    // Appending never changes the head, so as_head must stay false.
    return CtrlEdit(slot, member, false);
}

std::shared_ptr<DevCtrlToken> DevFilaAutoRefillMgr::CtrlPopBack(const DevAmsSlotId& member)
{
    const auto groups = GetGroupsBySlot(member);
    if (groups.groups.empty()) {
        return nullptr;
    }

    const auto& slots = groups.groups.front().fila_refill_slots;
    if (slots.size() < 2) {
        return nullptr;// dropping the only slot would leave an empty group
    }
    return CtrlRemove(slots.back().slot_id);
}

std::shared_ptr<DevCtrlToken> DevFilaAutoRefillMgr::CtrlRemove(const DevAmsSlotId& slot)
{
    // pos == to_pos without as_head unlinks the slot from its chain.
    return CtrlEdit(slot, slot, false);
}

std::shared_ptr<DevCtrlToken> DevFilaAutoRefillMgr::CtrlMove(const DevAmsSlotId& slot, const DevAmsSlotId& member, bool as_head)
{
    return CtrlEdit(slot, member, as_head);
}

std::shared_ptr<DevCtrlToken> DevFilaAutoRefillMgr::CtrlQuery()
{
    const std::string sequence_id = std::to_string(MachineObject::m_sequence_id++);
    auto j = make_refill_command(RefillCommand::Query, sequence_id);
    return publish_refill(m_owner, j, sequence_id);
}

std::optional<DevFilaRefillAckResult> DevFilaRefillParseAckResult(const nlohmann::json& print_json)
{
    if (!print_json.is_object()) {
        return std::nullopt;
    }
    const auto result = print_json.find("result");
    if (result == print_json.end() || !result->is_number_integer()) {
        return std::nullopt;
    }

    switch (result->get<int>()) {
    case 1:  return DevFilaRefillAckResult::SuccessWithWarning;
    case 0:  return DevFilaRefillAckResult::Success;
    case -1: return DevFilaRefillAckResult::RequestParseFailed;
    case -2: return DevFilaRefillAckResult::AutoRefillDisabled;
    case -3: return DevFilaRefillAckResult::InvalidArgument;
    case -4: return DevFilaRefillAckResult::SlotNotOperable;
    case -5: return DevFilaRefillAckResult::EmptySlot;
    case -6: return DevFilaRefillAckResult::HeadConfigureFailed;
    default: return std::nullopt;
    }
}

bool DevFilaRefillAckSucceeded(DevFilaRefillAckResult result)
{
    return result == DevFilaRefillAckResult::Success ||
           result == DevFilaRefillAckResult::SuccessWithWarning;
}

const char* DevFilaRefillAckResultMessage(DevFilaRefillAckResult result)
{
    switch (result) {
    case DevFilaRefillAckResult::SuccessWithWarning:  return "success with warning";
    case DevFilaRefillAckResult::Success:             return "success";
    case DevFilaRefillAckResult::RequestParseFailed:  return "request parse failed";
    case DevFilaRefillAckResult::AutoRefillDisabled:  return "auto-refill switch is off";
    case DevFilaRefillAckResult::InvalidArgument:     return "invalid pos, to_pos, or group_tag";
    case DevFilaRefillAckResult::SlotNotOperable:     return "slot cannot be operated";
    case DevFilaRefillAckResult::EmptySlot:           return "cannot operate an empty slot";
    case DevFilaRefillAckResult::HeadConfigureFailed: return "failed to configure group head or target chain";
    }
    return "unknown refill result";
}

DevFilaRefillBlockLevel DevFilaRefillClassifyBlock(DevFilaRefillBlockCode block_code)
{
    const int value = static_cast<int>(block_code);
    if (block_code == DevFilaRefillBlockCode::Available) {
        return DevFilaRefillBlockLevel::Available;
    }
    if (value >= static_cast<int>(DevFilaRefillBlockCode::NozzleNotCalibrated) &&
        value <= static_cast<int>(DevFilaRefillBlockCode::VisibleLayerLines)) {
        return DevFilaRefillBlockLevel::Warning;
    }
    return DevFilaRefillBlockLevel::Disabled;
}

static int DevFilaRefillBlockRank(DevFilaRefillBlockCode block_code)
{
    switch (DevFilaRefillClassifyBlock(block_code)) {
    case DevFilaRefillBlockLevel::Available: return 0;
    case DevFilaRefillBlockLevel::Warning:   return 1;
    case DevFilaRefillBlockLevel::Disabled:  return 2;
    }
    return 2;
}

std::vector<DevFilaRefillBlockCode> DevFilaRefillQueryResult::FindBlockCodes(const DevAmsSlotId& slot_id) const
{
    const auto slot = std::find_if(slots.begin(), slots.end(), [&slot_id](const DevFilaRefillQuerySlot& item) { return item.slot_id == slot_id; });
    return slot == slots.end() ? std::vector<DevFilaRefillBlockCode>{} : slot->block_codes;
}

std::optional<DevFilaRefillBlockCode> DevFilaRefillQueryResult::FindBlockCode(const DevAmsSlotId& slot_id) const
{
    const auto codes = FindBlockCodes(slot_id);
    if (codes.empty()) {
        return DevFilaRefillBlockCode::Available;// default to not block if not in query result
    }
    return *std::max_element(codes.begin(), codes.end(),
                             [](DevFilaRefillBlockCode lhs, DevFilaRefillBlockCode rhs) {
                                 return DevFilaRefillBlockRank(lhs) < DevFilaRefillBlockRank(rhs);
                             });
}

DevFilaRefillBlockLevel DevFilaRefillQueryResult::GetBlockLevel(const DevAmsSlotId& slot_id) const
{
    const auto block_code = FindBlockCode(slot_id);
    return block_code ? DevFilaRefillClassifyBlock(*block_code) : DevFilaRefillBlockLevel::Disabled;
}

std::optional<DevFilaRefillQueryResult> DevFilaAutoRefillEdit::ParseQueryResult(const nlohmann::json& print_json) const
{
    try {
        BOOST_LOG_TRIVIAL(info) << print_json;
    } catch(...){
        BOOST_LOG_TRIVIAL(warning) << current_exception_message();
    }

    if (!print_json.is_object()) {
        return std::nullopt;
    }

    const auto command = print_json.find("command");
    if (command == print_json.end() || !command->is_string() || command->get<std::string>() != kMqttCommand) {
        return std::nullopt;
    }

    const auto refill_command = print_json.find("refill_command");
    if (refill_command == print_json.end() || !refill_command->is_number_integer()) {
        return std::nullopt;
    }
    // Query and QueryNext answer with the same `blocked` payload.
    const int refill_command_value = refill_command->get<int>();
    if (refill_command_value != static_cast<int>(RefillCommand::Query) &&
        refill_command_value != static_cast<int>(RefillCommand::QueryNext)) {
        return std::nullopt;
    }

    const auto ack_result = DevFilaRefillParseAckResult(print_json);
    if (!ack_result || !DevFilaRefillAckSucceeded(*ack_result)) {
        return std::nullopt;
    }

    DevFilaRefillQueryResult parsed;
    parsed.ack_result = *ack_result;
    if (!m_owner) {
        return std::nullopt;
    }

    // Start with every physical slot on each available AMS. A slot missing
    // from `blocked` therefore keeps an empty reason list, i.e. available.
    for (const auto& [_, ams] : m_owner->GetAmsList()) {
        for (int slot_id = 0; slot_id < ams->GetSlotCount(); ++slot_id) {
            parsed.slots.push_back({{ams->GetAmdIdInt(), slot_id}, {}});
        }
    }

    for (const auto& [_, vt_tray] : m_owner->GetVirtualTrays()) {
        try {
            parsed.slots.push_back({ { std::stoi(vt_tray.ams_id) , 0}, {} });
        } catch (...) {
        }
    }

    const auto blocked = print_json.find("blocked");
    if (blocked == print_json.end()) {
        return parsed;
    }
    if (!blocked->is_array()) {
        return std::nullopt;
    }

    for (const auto& item : *blocked) {
        if (!item.is_object()) {
            continue;
        }
        const auto pos = item.find("pos");
        const auto block = item.find("block");
        if (pos == item.end() || !pos->is_number_integer() ||
            block == item.end() || !block->is_number_integer()) {
            continue;
        }

        const auto raw_pos_signed = pos->get<std::int64_t>();
        if (raw_pos_signed < 0 || raw_pos_signed > std::numeric_limits<std::uint16_t>::max()) {
            continue;
        }
        const auto raw_pos = static_cast<std::uint16_t>(raw_pos_signed);
        const DevAmsSlotId slot_id{
            static_cast<int>((raw_pos >> 8) & 0xff),
            static_cast<int>(raw_pos & 0xff),
        };
        auto slot = std::find_if(parsed.slots.begin(), parsed.slots.end(),
                                 [&slot_id](const DevFilaRefillQuerySlot& item) { return item.slot_id == slot_id; });
        if (slot == parsed.slots.end()) {
            continue;
        }

        const auto raw_block_code = block->get<std::int64_t>();
        if (raw_block_code < std::numeric_limits<int>::min() ||
            raw_block_code > std::numeric_limits<int>::max()) {
            continue;
        }
        const auto block_code = static_cast<DevFilaRefillBlockCode>(static_cast<int>(raw_block_code));
        if (block_code == DevFilaRefillBlockCode::Available) {
            continue;
        }
        // Keep every distinct reason: the UI offers a different follow-up
        // for each, and the caller decides which one to show first.
        if (std::find(slot->block_codes.begin(), slot->block_codes.end(), block_code) == slot->block_codes.end()) {
            slot->block_codes.push_back(block_code);
        }
    }

    return parsed;
}

std::shared_ptr<DevCtrlToken> DevFilaAutoRefillMgr::CtrlQueryNext(std::uint16_t to_pos)
{
    const std::string sequence_id = std::to_string(MachineObject::m_sequence_id++);
    auto j = make_refill_command(RefillCommand::QueryNext, sequence_id);
    j["print"]["to_pos"] = to_pos;
    return publish_refill(m_owner, j, sequence_id);
}

std::shared_ptr<DevCtrlToken> DevFilaAutoRefillMgr::CtrlQueryNext(const DevAmsSlotId& to)
{
    return CtrlQueryNext(encode_slot_pos(to));
}

} // namespace Slic3r
