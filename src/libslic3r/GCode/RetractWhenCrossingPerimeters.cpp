#include "../ClipperUtils.hpp"
#include "../Layer.hpp"
#include "../Polyline.hpp"
#include "../BoundingBox.hpp"
#include "../ExPolygon.hpp"
#include "../Polygon.hpp"
#include "./Utils.hpp"
#include <vector>

#include "RetractWhenCrossingPerimeters.hpp"

// #define RETRACT_WHEN_CROSSING_PERIMETERS_DEBUG

namespace Slic3r {

bool RetractWhenCrossingPerimeters::travel_cross_perimeters(const Layer &layer, const Polyline &travel)
{
    if (!cross_perimeters_flag) {
        cross_perimeters_flag = true;
        // get all the external perimeters and internal perimeters
        m_internal_islands_lines.clear();
        Polylines perimeters_polylines;
        for (const auto &layer_region_ptr : layer.regions()) {
            bool is_internal = false;
            for (const Surface &surface : layer_region_ptr->get_slices().surfaces)
                if (surface.is_internal()) {
                    is_internal = true;
                    auto lines  = surface.expolygon.lines();
                    m_internal_islands_lines.insert(m_internal_islands_lines.end(), std::make_move_iterator(lines.begin()), std::make_move_iterator(lines.end()));
                }
            if (is_internal) layer_region_ptr->perimeters.collect_polylines(perimeters_polylines);
        }
        for (const auto &perimeter_polyline : perimeters_polylines) {
            // Convert Polyline to Lines and add to m_internal_islands_lines.
            auto lines = perimeter_polyline.lines();
            m_internal_islands_lines.insert(m_internal_islands_lines.end(), std::make_move_iterator(lines.begin()), std::make_move_iterator(lines.end()));
        }

        m_aabbtree_lines_distancer = AABBTreeLines::LinesDistancer<Line>{std::move(m_internal_islands_lines)};

#ifdef RETRACT_WHEN_CROSSING_PERIMETERS_DEBUG
        m_internal_islands_bbox = BoundingBox();
        for (const auto &line : m_internal_islands_lines) {
            // Update the bounding box of internal islands.
            m_internal_islands_bbox.merge(get_extents<true>({line.a, line.b}));
        }
        m_internal_islands_bbox.offset(SCALED_EPSILON);
#endif // RETRACT_WHEN_CROSSING_PERIMETERS_DEBUG
    }

#ifdef RETRACT_WHEN_CROSSING_PERIMETERS_DEBUG
    static int travel_idx = 0;
    SVG        svg(debug_out_path("travel_cross_perimeters_layer_%d_travel_%d.svg", layer.id(), travel_idx++), m_internal_islands_bbox);
    svg.draw(travel, "blue");
    for (const auto &perimeter_line : m_aabbtree_lines_distancer.get_lines()) { svg.draw(perimeter_line, "green"); }
#endif // RETRACT_WHEN_CROSSING_PERIMETERS_DEBUG

    bool has_intersection = false;
    for (const auto &line : travel.lines()) {
        // Check if the travel line intersects with any of the internal islands.
        auto intersections = m_aabbtree_lines_distancer.intersections_with_line<false>(line);
        if (!intersections.empty()) {
            has_intersection = true;
            break;
        }
    }

    return has_intersection;
}

bool RetractWhenCrossingPerimeters::travel_inside_internal_regions(const Layer &layer, const Polyline &travel)
{
    if (m_layer != &layer) {
        cross_perimeters_flag = false;
        // Update cache.
        m_layer = &layer;
        m_internal_islands.clear();
        m_aabbtree_internal_islands.clear();
        // Collect expolygons of internal slices.
        for (const LayerRegion *layerm : layer.regions())
            for (const Surface &surface : layerm->get_slices().surfaces)
                if (surface.is_internal()) m_internal_islands.emplace_back(&surface.expolygon);
        // Calculate bounding boxes of internal slices.
        std::vector<AABBTreeIndirect::BoundingBoxWrapper> bboxes;
        bboxes.reserve(m_internal_islands.size());
        for (size_t i = 0; i < m_internal_islands.size(); ++i) bboxes.emplace_back(i, get_extents(*m_internal_islands[i]));
        // Build AABB tree over bounding boxes of internal slices.
        m_aabbtree_internal_islands.build_modify_input(bboxes);
    }

    BoundingBox           bbox_travel = get_extents(travel);
    AABBTree::BoundingBox bbox_travel_eigen{bbox_travel.min, bbox_travel.max};
    int                   result = -1;
    bbox_travel.offset(SCALED_EPSILON);
    AABBTreeIndirect::traverse(
        m_aabbtree_internal_islands, [&bbox_travel_eigen](const AABBTree::Node &node) { return bbox_travel_eigen.intersects(node.bbox); },
        [&travel, &bbox_travel, &result, &islands = m_internal_islands](const AABBTree::Node &node) {
            assert(node.is_leaf());
            assert(node.is_valid());
            Polygons clipped = ClipperUtils::clip_clipper_polygons_with_subject_bbox(*islands[node.idx], bbox_travel);
            if (diff_pl(travel, clipped).empty()) {
                // Travel path is completely inside an "internal" island. Don't retract.
                result = int(node.idx);
                // Stop traversal.
                return false;
            }
            // Continue traversal.
            return true;
        });
    return result != -1;
}

bool RetractWhenCrossingPerimeters::travel_inside_internal_regions_no_wall_crossing(const Layer &layer, const Polyline &travel)
{
    if (!travel_inside_internal_regions(layer, travel)) return false;
    return !travel_cross_perimeters(layer, travel);
}

} // namespace Slic3r
