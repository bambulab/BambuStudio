#ifndef DEVICEPAGESCHEMA_HOTENDRACKPROGRESS_HPP
#define DEVICEPAGESCHEMA_HOTENDRACKPROGRESS_HPP

#include "Schema.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r { namespace GUI { namespace AmsHotendSchema { namespace format {

struct HotendRackProgressStatus
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

inline void to_json(nlohmann::json& j, const HotendRackProgressStatus& v)
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

}}}} // namespace Slic3r::GUI::AmsHotendSchema::format

#endif // DEVICEPAGESCHEMA_HOTENDRACKPROGRESS_HPP
