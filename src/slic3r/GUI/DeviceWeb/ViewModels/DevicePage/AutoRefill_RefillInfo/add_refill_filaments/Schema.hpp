#ifndef AUTOREFILLADDREFILLFILAMENTSSCHEMA_HPP
#define AUTOREFILLADDREFILLFILAMENTSSCHEMA_HPP

// Schema for the "add refill filaments" picker (submod `add_refill_filaments`).
// The C++ structs below ARE the contract: JSON keys match member names.
// Web counterpart:
//   .../auto-refill-refill-info/add-refill-filaments/types.ts

#include <nlohmann/json.hpp>

#include "slic3r/GUI/DeviceCore/DevFilaAutoRefillEdit.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace AutoRefillAddRefillFilamentsSchema {

namespace values {

namespace mode {
    inline constexpr const char* create_group = "create_group";
    inline constexpr const char* add_to_group = "add_to_group";
}

// Unit::kind. `hotend` is the rack's associated-filament row (one card per
// rack nozzle address); `ams` is a plain AMS unit listing its physical slots.
namespace unit_kind {
    inline constexpr const char* hotend = "hotend";
    inline constexpr const char* ams    = "ams";
}

namespace color_type {
    inline constexpr const char* gradient = "gradient";
    inline constexpr const char* multi    = "multi";
    inline constexpr const char* single   = "single";
}

namespace nozzle_state {
    inline constexpr const char* normal     = "Normal";
    inline constexpr const char* active     = "Active";
    inline constexpr const char* lost       = "Lost";
    inline constexpr const char* error      = "Error";
    inline constexpr const char* non_nozzle = "NonNozzle";
    inline constexpr const char* empty      = "Empty";
    inline constexpr const char* unknown    = "Unknown";
}

namespace action {
    inline constexpr const char* get             = "get";
    inline constexpr const char* init            = "init";
    inline constexpr const char* confirm         = "confirm";
    inline constexpr const char* cancel          = "cancel";
    inline constexpr const char* query           = "query";
    inline constexpr const char* query_next      = "query_next";
    inline constexpr const char* query_result    = "query_result";
    inline constexpr const char* query_failed    = "query_failed";
    inline constexpr const char* confirm_result  = "confirm_result";
    inline constexpr const char* confirm_failed  = "confirm_failed";
    // Follow-up button of a block-code dialog; performs the jump the code asks
    // for. Payload: { block_code, ams_id, slot_id, display_index }.
    inline constexpr const char* follow_up       = "follow_up";
    // "Don't show again" of a warning dialog. Payload: { block_code }.
    inline constexpr const char* dismiss_warning = "dismiss_warning";
    inline constexpr const char* check_warning_suppressed = "check_warning_suppressed";// Lightweight check before opening a warning dialog.
}

} // namespace values

namespace format {

struct Nozzle
{
    std::string state = values::nozzle_state::empty;
    std::string label;
    std::string subtext;
};

// One slot card. Empty slots still serialize every member; filament fields
// are empty / default when has_filament is false.
struct Slot
{
    int                      ams_id       = -1;
    int                      slot_id      = -1;
    std::string              slot_name;

    // Rack nozzle address and its 1-based display index (same convention as
    // the filament/hotend panel). AMS units fill `display_index` too when the
    // slot is already mapped to a rack nozzle.
    int                      addr_id       = -1;
    int                      display_index = 0;
    // Hotend unit only: even display indices form the upper row of cards.
    bool                     is_top_tier   = false;
    // False for a rack nozzle position with no bound AMS slot: the card is
    // rendered as an empty placeholder and `ams_id` / `slot_id` stay -1.
    bool                     has_slot      = true;

    bool                     has_filament = false;
    std::string              filament_id;
    std::string              filament_name;
    std::string              filament_type;
    int                      remain = 100;
    std::string              color;
    std::vector<std::string> colors;
    std::string              color_type   = values::color_type::single;
    bool                     selectable   = false;
    bool                     selected     = false;
    std::string              disable_reason;

    // `block` code driving the card, picked by BlockCodeUtil::SelectPrimary()
    // out of everything the device reported. 0 means unrestricted;
    // `warning` marks the codes that stay selectable behind a confirmation.
    DevFilaRefillBlockCode   block_code   = DevFilaRefillBlockCode::Available;
    bool                     warning      = false;
    // Every reason the device reported, primary one first. The card only
    // shows the primary; the rest let the page explain a slot that is
    // blocked for more than one reason.
    std::vector<int>         block_codes;
    // How the card reacts to the primary code: BlockCodeUtil::ToJson().
    // Null when the slot carries no `block` code at all.
    nlohmann::json           interaction = nlohmann::json();
    Nozzle                   nozzle;
};

// One block of cards. `kind` picks the source: the rack's associated
// filaments, or one AMS unit. `ams_id` / `ams_type` are only meaningful on
// `ams` units; `ams_type` is DevAmsType as a decimal string (same convention
// as ConnectedSlots).
struct Unit
{
    std::string      kind       = values::unit_kind::ams;
    int              ams_id     = -1;
    std::string      ams_type;
    std::string      name;
    int              slot_count = 0;
    std::vector<Slot> slots;
};

// What the group being built is compared against. Feeds the "Refill group
// filament:" / "Refill group hotend:" lines of the mismatch dialogs; members
// are empty until the first slot has been picked.
struct GroupReference
{
    std::string filament_name;
    std::string filament_type;
    std::string nozzle;
};

struct State
{
    std::string      selected_dev_id;
    std::uint32_t    session_seq = 0;
    std::string      mode        = values::mode::create_group;
    std::string      mode_title;
    // `notice` stays an untranslated template so Web can run it through t();
    // the filament type is passed separately as the `{{type}}` value.
    std::string      notice;
    std::string      notice_filament_type;
    GroupReference   group_reference;
    std::vector<Unit> units;
};

// confirm / get payload item: { slots: SlotRef[] }
struct SlotRef
{
    int ams_id  = 0;
    int slot_id = 0;
};

inline void to_json(nlohmann::json& j, const Nozzle& v)
{
    j = {
        {"state",   v.state},
        {"label",   v.label},
        {"subtext", v.subtext},
    };
}

inline void to_json(nlohmann::json& j, const Slot& v)
{
    j = {
        {"ams_id",          v.ams_id},
        {"slot_id",         v.slot_id},
        {"slot_name",       v.slot_name},
        {"addr_id",         v.addr_id},
        {"display_index",   v.display_index},
        {"is_top_tier",     v.is_top_tier},
        {"has_slot",        v.has_slot},
        {"has_filament",    v.has_filament},
        {"filament_id",     v.filament_id},
        {"filament_name",   v.filament_name},
        {"filament_type",   v.filament_type},
        {"remain",          v.remain},
        {"color",           v.color},
        {"colors",          v.colors},
        {"color_type",      v.color_type},
        {"selectable",      v.selectable},
        {"selected",        v.selected},
        {"disable_reason",  v.disable_reason},
        {"block_code",      static_cast<int>(v.block_code)},
        {"warning",         v.warning},
        {"block_codes",     v.block_codes},
        {"interaction",     v.interaction},
        {"nozzle",          v.nozzle},
    };
}

inline void to_json(nlohmann::json& j, const Unit& v)
{
    j = {
        {"kind",       v.kind},
        {"ams_id",     v.ams_id},
        {"ams_type",   v.ams_type},
        {"name",       v.name},
        {"slot_count", v.slot_count},
        {"slots",      v.slots},
    };
}

inline void to_json(nlohmann::json& j, const GroupReference& v)
{
    j = {
        {"filament_name", v.filament_name},
        {"filament_type", v.filament_type},
        {"nozzle",        v.nozzle},
    };
}

inline void to_json(nlohmann::json& j, const State& v)
{
    j = {
        {"selected_dev_id",  v.selected_dev_id},
        {"session_seq",      v.session_seq},
        {"mode",             v.mode},
        {"mode_title",       v.mode_title},
        {"notice",           v.notice},
        {"notice_filament_type", v.notice_filament_type},
        {"group_reference",  v.group_reference},
        {"units",            v.units},
    };
}

inline void to_json(nlohmann::json& j, const SlotRef& v)
{
    j = {
        {"ams_id",  v.ams_id},
        {"slot_id", v.slot_id},
    };
}

} // namespace format

}}} // namespace Slic3r::GUI::AutoRefillAddRefillFilamentsSchema

#endif // AUTOREFILLADDREFILLFILAMENTSSCHEMA_HPP
