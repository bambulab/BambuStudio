#ifndef DEVICEPAGESCHEMA_AMSEXTOPERATIONPAGE_HPP
#define DEVICEPAGESCHEMA_AMSEXTOPERATIONPAGE_HPP

#include "Schema.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace AmsHotendSchema { namespace format {

struct BindAms
{
    std::string ams_id;
    std::string ams_type;
    int         slot_count = 0;
};

struct AmsExtOperationSlot
{
    int                  addr_id = 0;
    int                  display_index = 0;
    std::string          ams_id;
    std::string          slot_id;
    std::vector<BindAms> bind_ams;
    int                  bind_ams_count = 0;
    nlohmann::json       units = nlohmann::json::array();
};

struct AmsExtOperationPage
{
    std::vector<AmsExtOperationSlot> slots;
};

inline void to_json(nlohmann::json& j, const BindAms& v)
{
    namespace SK = AmsHotendSchema::keys;
    j = {
        {SK::bind_ams::ams_id,     v.ams_id},
        {SK::bind_ams::ams_type,   v.ams_type},
        {SK::bind_ams::slot_count, v.slot_count}
    };
}

inline void to_json(nlohmann::json& j, const AmsExtOperationSlot& v)
{
    namespace SK = AmsHotendSchema::keys;
    j = {
        {SK::ams_ext_operation_slot::addr_id,             v.addr_id},
        {SK::ams_ext_operation_slot::display_index,       v.display_index},
        {SK::ams_ext_operation_slot::ams_id,              v.ams_id},
        {SK::ams_ext_operation_slot::slot_id,             v.slot_id},
        {SK::ams_ext_operation_slot::bind_ams,            v.bind_ams},
        {SK::ams_ext_operation_slot::bind_ams_count,      v.bind_ams_count},
        {SK::ams_ext_operation_slot::units,               v.units}
    };
}

inline void to_json(nlohmann::json& j, const AmsExtOperationPage& v)
{
    namespace SK = AmsHotendSchema::keys;
    j = {
        {SK::ams_ext_operation_page::slots, v.slots}
    };
}

}}}} // namespace Slic3r::GUI::AmsHotendSchema::format

#endif // DEVICEPAGESCHEMA_AMSEXTOPERATIONPAGE_HPP
