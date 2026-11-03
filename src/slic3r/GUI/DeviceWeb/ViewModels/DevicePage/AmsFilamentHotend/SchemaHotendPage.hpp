#ifndef DEVICEPAGESCHEMA_AMSHOTENDPAGE_HPP
#define DEVICEPAGESCHEMA_AMSHOTENDPAGE_HPP

#include "Schema.hpp"
#include "SchemaHotendRackProgress.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r { namespace GUI { namespace AmsHotendSchema { namespace format {

struct HotendActions
{
    bool        can_read_all_hotends = false;
    std::string read_all_disabled_reason;
};

struct HotendPage
{
    HotendActions             actions;
    HotendRackProgressStatus  hotend_rack_status;
    nlohmann::json            hotend_slots = nlohmann::json::array();
};

inline void to_json(nlohmann::json& j, const HotendActions& v)
{
    namespace SK = AmsHotendSchema::keys;
    j = {
        {SK::hotend_actions::can_read_all_hotends,     v.can_read_all_hotends},
        {SK::hotend_actions::read_all_disabled_reason, v.read_all_disabled_reason}
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
