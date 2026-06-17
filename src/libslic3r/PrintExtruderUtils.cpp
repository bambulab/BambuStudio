#include "Print.hpp"

#include "Model.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

/*  This method assigns extruders to the volumes having a material
    but not having extruders set in the volume config. */
void Print::auto_assign_extruders(ModelObject *model_object) const
{
    // only assign extruders if object has more than one volume
    if (model_object->volumes.size() < 2)
        return;

//    size_t extruders = m_config.nozzle_diameter.values.size();
    for (size_t volume_id = 0; volume_id < model_object->volumes.size(); ++volume_id) {
        ModelVolume *volume = model_object->volumes[volume_id];
        //FIXME Vojtech: This assigns an extruder ID even to a modifier volume, if it has a material assigned.
        if ((volume->is_model_part() || volume->is_modifier()) && !volume->material_id().empty() && !volume->config.has("extruder"))
            volume->config.set("extruder", int(volume_id + 1));
    }
}

size_t Print::get_extruder_id(unsigned int filament_id) const
{
    std::vector<int> filament_map = get_filament_maps();
    if (filament_id < filament_map.size()) {
        return filament_map[filament_id] - 1;
    }
    return 0;
}

size_t Print::get_filament_config_idx(unsigned int filament_id) const
{
    return Slic3r::get_filament_config_idx(m_config, filament_id);
}

size_t Print::get_process_config_idx(unsigned int filament_id) const
{
    return Slic3r::get_process_config_idx(m_config, filament_id);
}

} // namespace Slic3r
