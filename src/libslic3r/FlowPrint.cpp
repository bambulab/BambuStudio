#include "Flow.hpp"
#include "Print.hpp"

namespace Slic3r {

Flow support_material_flow(const PrintObject *object, float layer_height)
{
    return Flow::new_from_config_width(
        frSupportMaterial,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (object->config().support_line_width.value > 0) ? object->config().support_line_width : object->config().line_width,
        // if object->config().support_filament == 0 (which means to not trigger tool change, but use the current extruder instead), get_at will return the 0th component.
        float(object->print()->config().nozzle_diameter.get_at(object->config().support_filament-1)),
        (layer_height > 0.f) ? layer_height : float(object->config().layer_height.value));
}

//BBS
Flow support_transition_flow(const PrintObject* object)
{
    //BBS: support transition of tree support is bridge flow
    float dmr = float(object->print()->config().nozzle_diameter.get_at(object->config().support_filament - 1));
    return Flow::bridging_flow(dmr, dmr);
}

Flow support_material_1st_layer_flow(const PrintObject *object, float layer_height)
{
    const PrintConfig &print_config = object->print()->config();
    const auto &width = (print_config.initial_layer_line_width.value > 0) ? print_config.initial_layer_line_width : object->config().support_line_width;
    return Flow::new_from_config_width(
        frSupportMaterial,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (width.value > 0) ? width : object->config().line_width,
        float(print_config.nozzle_diameter.get_at(object->config().support_filament-1)),
        (layer_height > 0.f) ? layer_height : float(print_config.initial_layer_print_height.value));
}

Flow support_material_interface_flow(const PrintObject *object, float layer_height)
{
    return Flow::new_from_config_width(
        frSupportMaterialInterface,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (object->config().support_line_width > 0) ? object->config().support_line_width : object->config().line_width,
        // if object->config().support_interface_filament == 0 (which means to not trigger tool change, but use the current extruder instead), get_at will return the 0th component.
        float(object->print()->config().nozzle_diameter.get_at(object->config().support_interface_filament-1)),
        (layer_height > 0.f) ? layer_height : float(object->config().layer_height.value));
}

}
