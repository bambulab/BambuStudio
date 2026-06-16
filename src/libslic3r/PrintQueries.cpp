#include "Print.hpp"

#include "Model.hpp"
#include "PrintConfig.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <cassert>

namespace Slic3r {

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::object_extruders() const
{
    std::vector<unsigned int> extruders;
    extruders.reserve(m_print_regions.size() * m_objects.size() * 3);
    // BBS
#if 0
    for (const PrintObject *object : m_objects)
        for (const PrintRegion &region : object->all_regions())
            region.collect_object_printing_extruders(*this, extruders);
#else
    for (const PrintObject *object : m_objects) {
        const ModelObject *mo = object->model_object();
        for (const ModelVolume *mv : mo->volumes) {
            std::vector<int> volume_extruders = mv->get_extruders();
            for (int extruder : volume_extruders) {
                assert(extruder > 0);
                extruders.push_back(extruder - 1);
            }
        }

        // layer range
        for (auto layer_range : mo->layer_config_ranges) {
            if (layer_range.second.has("extruder")) {
                //BBS: actually when user doesn't change filament by height range(value is default 0), height range should not save key "extruder".
                //Don't know why height range always save key "extruder" because of no change(should only save difference)...
                //Add protection here to avoid overflow
                auto value = layer_range.second.option("extruder")->getInt();
                if (value > 0)
                    extruders.push_back(value - 1);
            }
        }
    }
#endif
    sort_remove_duplicates(extruders);
    return extruders;
}

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::support_material_extruders() const
{
    std::vector<unsigned int> extruders;
    bool support_uses_current_extruder = false;
    // BBS
    auto num_extruders = (unsigned int)m_config.filament_diameter.size();

    for (PrintObject *object : m_objects) {
        if (object->has_support_material()) {
            assert(object->config().support_filament >= 0);
            if (object->config().support_filament == 0)
                support_uses_current_extruder = true;
            else {
                unsigned int i = (unsigned int)object->config().support_filament - 1;
                extruders.emplace_back((i >= num_extruders) ? 0 : i);
            }
            assert(object->config().support_interface_filament >= 0);
            if (object->config().support_interface_filament == 0)
                support_uses_current_extruder = true;
            else {
                unsigned int i = (unsigned int)object->config().support_interface_filament - 1;
                extruders.emplace_back((i >= num_extruders) ? 0 : i);
            }
        }
    }

    if (support_uses_current_extruder)
        // Add all object extruders to the support extruders as it is not know which one will be used to print supports.
        append(extruders, this->object_extruders());

    sort_remove_duplicates(extruders);
    return extruders;
}

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::extruders(bool conside_custom_gcode) const
{
    std::vector<unsigned int> extruders = this->object_extruders();
    append(extruders, this->support_material_extruders());

    if (conside_custom_gcode) {
        //BBS
        int num_extruders = m_config.filament_colour.size();
        if (m_model.plates_custom_gcodes.find(m_model.curr_plate_index) != m_model.plates_custom_gcodes.end()) {
            for (auto item : m_model.plates_custom_gcodes.at(m_model.curr_plate_index).gcodes) {
                if (item.type == CustomGCode::Type::ToolChange && item.extruder <= num_extruders)
                    extruders.push_back((unsigned int)(item.extruder - 1));
            }
        }
    }

    sort_remove_duplicates(extruders);
    return extruders;
}

unsigned int Print::num_object_instances() const
{
    unsigned int instances = 0;
    for (const PrintObject *print_object : m_objects)
        instances += (unsigned int)print_object->instances().size();
    return instances;
}

double Print::max_allowed_layer_height() const
{
    double nozzle_diameter_max = 0.;
    for (unsigned int extruder_id : this->extruders())
        nozzle_diameter_max = std::max(nozzle_diameter_max, m_config.nozzle_diameter.get_at(extruder_id));
    return nozzle_diameter_max;
}

std::vector<ObjectID> Print::print_object_ids() const
{
    std::vector<ObjectID> out;
    // Reserve one more for the caller to append the ID of the Print itself.
    out.reserve(m_objects.size() + 1);
    for (const PrintObject *print_object : m_objects)
        out.emplace_back(print_object->id());
    return out;
}

bool Print::has_infinite_skirt() const
{
    return (m_config.draft_shield == dsEnabled && m_config.skirt_loops > 0) || (m_config.ooze_prevention && this->extruders().size() > 1);
}

bool Print::has_skirt() const
{
    return (m_config.skirt_height > 0 && m_config.skirt_loops > 0) || m_config.draft_shield != dsDisabled;
}

bool Print::has_brim() const
{
    return std::any_of(m_objects.begin(), m_objects.end(), [](PrintObject *object) { return object->has_brim(); });
}

} // namespace Slic3r
