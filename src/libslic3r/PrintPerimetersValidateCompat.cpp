#include "Print.hpp"

namespace Slic3r {

StringObjectException Print::validate(StringObjectException *warning, Polygons *collison_polygons, std::vector<std::pair<Polygon, float>> *height_polygons) const
{
    (void) warning;
    (void) collison_polygons;
    (void) height_polygons;
    return {};
}

} // namespace Slic3r
