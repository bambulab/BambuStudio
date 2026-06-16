#include "Print.hpp"

#include "PrintConfig.hpp"
#include "Utils.hpp"

namespace Slic3r {

double Print::skirt_first_layer_height() const
{
    return m_config.initial_layer_print_height.value;
}

Flow Print::brim_flow() const
{
    ConfigOptionFloat width = m_config.initial_layer_line_width;
    if (width.value == 0)
        width = m_print_regions.front()->config().inner_wall_line_width;
    if (width.value == 0)
        width = m_objects.front()->config().line_width;

    /* We currently use a random region's perimeter extruder.
       While this works for most cases, we should probably consider all of the perimeter
       extruders and take the one with, say, the smallest index.
       The same logic should be applied to the code that selects the extruder during G-code
       generation as well. */
    return Flow::new_from_config_width(
        frPerimeter,
        width,
        (float)m_config.nozzle_diameter.get_at(m_print_regions.front()->config().wall_filament - 1),
        (float)this->skirt_first_layer_height());
}

Flow Print::skirt_flow() const
{
    ConfigOptionFloat width = m_config.initial_layer_line_width;
    if (width.value == 0)
        width = m_objects.front()->config().line_width;

    /* We currently use a random object's support material extruder.
       While this works for most cases, we should probably consider all of the support material
       extruders and take the one with, say, the smallest index;
       The same logic should be applied to the code that selects the extruder during G-code
       generation as well. */
    return Flow::new_from_config_width(
        frPerimeter,
        width,
        (float)m_config.nozzle_diameter.get_at(m_objects.front()->config().support_filament - 1),
        (float)this->skirt_first_layer_height());
}

bool Print::has_support_material() const
{
    for (const PrintObject *object : m_objects)
        if (object->has_support_material())
            return true;
    return false;
}

Vec2d Print::translate_to_print_space(const Vec2d &point) const
{
    return Vec2d(point(0) - m_origin(0), point(1) - m_origin(1));
}

Vec2d Print::translate_to_print_space(const Point &point) const
{
    return Vec2d(unscaled(point.x()) - m_origin(0), unscaled(point.y()) - m_origin(1));
}

} // namespace Slic3r
