#ifndef DEVICEPAGESCHEMA_AMSHOTENDPAGE_HPP
#define DEVICEPAGESCHEMA_AMSHOTENDPAGE_HPP

#include "DevicePageSchema_AmsFilamentHotend.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r { namespace GUI { namespace AmsHotendSchema {

// Value candidates for the Hotend-page fields, co-located with the hotend page
// format structs. Other pages' value candidates live in
// DevicePageSchema_AmsFilamentHotend.hpp under the same `values` namespace.
namespace values {

namespace rack_mode {
    inline constexpr const char* idle   = "idle";
    inline constexpr const char* single = "single";
    inline constexpr const char* all    = "all";
}

namespace rack_operation {
    inline constexpr const char* idle        = "idle";
    inline constexpr const char* take        = "take";
    inline constexpr const char* place       = "place";
    inline constexpr const char* exchange    = "exchange";
    inline constexpr const char* read_all    = "read_all";
    inline constexpr const char* read_single = "read_single";
    inline constexpr const char* move        = "move";
}

namespace rack_state {
    inline constexpr const char* idle    = "idle";
    inline constexpr const char* running = "running";
    inline constexpr const char* failed  = "failed";
}

namespace rack_step_state {
    inline constexpr const char* done    = "done";
    inline constexpr const char* active  = "active";
    inline constexpr const char* pending = "pending";
    inline constexpr const char* failed  = "failed";
}

namespace tool_kind {
    inline constexpr const char* nozzle  = "nozzle";
    inline constexpr const char* cutter  = "cutter";
    inline constexpr const char* laser   = "laser";
    inline constexpr const char* fan     = "fan";
    inline constexpr const char* pen     = "pen";
    inline constexpr const char* unknown = "unknown";
    inline constexpr const char* empty   = "empty";
}

namespace hotend_op_state {
    inline constexpr const char* idle     = "Idle";
    inline constexpr const char* heating  = "Heating";
    inline constexpr const char* cooling  = "Cooling";
    inline constexpr const char* reading  = "Reading";
    inline constexpr const char* taking   = "Taking";
    inline constexpr const char* placing  = "Placing";
    inline constexpr const char* removing = "Removing";
    inline constexpr const char* error    = "Error";
    inline constexpr const char* lost_in_holder  = "Lost";
    // Tool present but its nozzle type is unknown (not yet read): card shows a
    // "?" badge, still displays temperature, and mutes the label to "--".
    inline constexpr const char* unknown  = "Unknown";
}

} // namespace values

namespace format {

struct HotendActions
{
    bool        can_read_all_hotends = false;
    std::string read_all_disabled_reason;
};

struct HotendRackStatus
{
    bool        active = false;
    std::string mode = values::rack_mode::idle;
    int         display_index = 0;
    int         current = 0;
    int         total = 0;
    std::string message;
    std::string tips;
    int         job = 0;
    std::string operation = values::rack_operation::idle;
    std::string state = values::rack_state::idle;
    int         target_display_index = 0;
    int         current_step = 0;
    nlohmann::json steps = nlohmann::json::array();
    bool        can_stop = false;
    bool        can_retry = false;
};

struct HotendPage
{
    HotendActions    actions;
    HotendRackStatus hotend_rack_status;
    nlohmann::json   hotend_slots = nlohmann::json::array();
};

inline void to_json(nlohmann::json& j, const HotendActions& v)
{
    namespace SK = AmsHotendSchema::keys;
    j = {
        {SK::hotend_actions::can_read_all_hotends,     v.can_read_all_hotends},
        {SK::hotend_actions::read_all_disabled_reason, v.read_all_disabled_reason}
    };
}

inline void to_json(nlohmann::json& j, const HotendRackStatus& v)
{
    namespace SK = AmsHotendSchema::keys;
    j = {
        {SK::rack_status::active,               v.active},
        {SK::rack_status::mode,                 v.mode},
        {SK::rack_status::display_index,        v.display_index},
        {SK::rack_status::current,              v.current},
        {SK::rack_status::total,                v.total},
        {SK::rack_status::message,              v.message},
        {SK::rack_status::tips,                 v.tips},
        {SK::rack_status::job,                  v.job},
        {SK::rack_status::operation,            v.operation},
        {SK::rack_status::state,                v.state},
        {SK::rack_status::target_display_index, v.target_display_index},
        {SK::rack_status::current_step,         v.current_step},
        {SK::rack_status::steps,                v.steps},
        {SK::rack_status::can_stop,             v.can_stop},
        {SK::rack_status::can_retry,            v.can_retry}
    };
}

inline void to_json(nlohmann::json& j, const HotendPage& v)
{
    namespace SK = AmsHotendSchema::keys;
    j = {
        {SK::hotend_page::actions,            v.actions},
        {SK::hotend_page::hotend_rack_status, v.hotend_rack_status},
        {SK::hotend_page::hotend_slots,       v.hotend_slots}
    };
}

}}}} // namespace Slic3r::GUI::AmsHotendSchema::format

#endif // DEVICEPAGESCHEMA_AMSHOTENDPAGE_HPP
