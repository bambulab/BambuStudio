#ifndef slic3r_RetractWhenCrossingPerimeters_hpp_
#define slic3r_RetractWhenCrossingPerimeters_hpp_

#include <vector>

#include "../AABBTreeIndirect.hpp"
#include "../AABBTreeLines.hpp"
#include "../Line.hpp"

namespace Slic3r {

// Forward declarations.
class ExPolygon;
class Layer;
class Polyline;

class RetractWhenCrossingPerimeters
{
public:
    bool travel_inside_internal_regions_no_wall_crossing(const Layer &layer, const Polyline &travel);

private:
    bool travel_cross_perimeters(const Layer &layer, const Polyline &travel);
    bool travel_inside_internal_regions(const Layer &layer, const Polyline &travel);

private:
    // Last object layer visited, for which a cache of internal islands was created.
    const Layer *m_layer;
    // Search structure over internal islands.
    BoundingBox                         m_internal_islands_bbox;
    AABBTreeLines::LinesDistancer<Line> m_aabbtree_lines_distancer;
    std::vector<Line>                   m_internal_islands_lines;
    bool                                cross_perimeters_flag = false;

    // Internal islands only, referencing data owned by m_layer->regions()->surfaces().
    std::vector<const ExPolygon *> m_internal_islands;
    // Search structure over internal islands.
    using AABBTree = AABBTreeIndirect::Tree<2, coord_t>;
    AABBTree m_aabbtree_internal_islands;
};

} // namespace Slic3r

#endif // slic3r_RetractWhenCrossingPerimeters_hpp_
