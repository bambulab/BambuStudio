#include "GCodeProcessor.hpp"
#include "BedExcludeChecker.hpp"

#include "libslic3r/EdgeGrid.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Polygon.hpp"

#include <cstdint>
#include <utility>

namespace Slic3r {

namespace {

constexpr double kGridCellSizeMM = 2.0;

Point scaled_point(const Vec3f& point)
{
    return Point::new_scale(point.x(), point.y());
}

bool is_valid_extrusion_move(const GCodeProcessorResult::MoveVertex& move)
{
    return move.type == EMoveType::Extrude && move.extrusion_role != erCustom && move.width > 0.0f && move.height > 0.0f;
}

template <typename SegmentVisitor>
bool visit_extrusion_segments(const GCodeProcessorResult& gcode_result, SegmentVisitor&& visitor)
{
    for (size_t i = 1; i < gcode_result.moves.size(); ++i) {
        const GCodeProcessorResult::MoveVertex& prev = gcode_result.moves[i - 1];
        const GCodeProcessorResult::MoveVertex& curr = gcode_result.moves[i];
        if (!is_valid_extrusion_move(curr))
            continue;

        Point segment_start = scaled_point(prev.position);
        if (curr.is_arc_move_with_interpolation_points()) {
            for (const Vec3f& interpolation_point : curr.interpolation_points) {
                Point segment_end = scaled_point(interpolation_point);
                if (segment_start != segment_end && !visitor(segment_start, segment_end))
                    return false;
                segment_start = segment_end;
            }
        }

        Point segment_end = scaled_point(curr.position);
        if (segment_start != segment_end && !visitor(segment_start, segment_end))
            return false;
    }

    return true;
}

bool clip_segment_to_bbox(const Point& start, const Point& end, const BoundingBox& bbox, Point& clipped_start, Point& clipped_end)
{
    clipped_start = start;
    clipped_end   = end;
    if (bbox.contains(clipped_start) && bbox.contains(clipped_end))
        return true;

    const Vec2d line_start  = start.cast<double>();
    const Vec2d line_vector = (end - start).cast<double>();
    const BoundingBoxf bboxf(bbox.min.cast<double>(), bbox.max.cast<double>());

    std::pair<double, double> interval;
    if (!Geometry::liang_barsky_line_clipping_interval(line_start, line_vector, bboxf, interval))
        return false;

    clipped_start = (line_start + interval.first * line_vector).cast<coord_t>();
    clipped_end   = (line_start + interval.second * line_vector).cast<coord_t>();
    return true;
}

bool segment_intersects_exclude_grid(
    const Point& start,
    const Point& end,
    const BoundingBox& exclude_bbox,
    const EdgeGrid::Grid& grid,
    const std::vector<const Polygon*>& exclude_polygons)
{
    Point clipped_start;
    Point clipped_end;
    if (!clip_segment_to_bbox(start, end, exclude_bbox, clipped_start, clipped_end))
        return false;

    std::vector<uint8_t> tested_polygon(exclude_polygons.size(), 0);
    bool intersects = false;

    auto visit_candidate_cell = [&](size_t row, size_t col) {
        auto cell_data_range = grid.cell_data_range(coord_t(row), coord_t(col));
        for (auto it = cell_data_range.first; it != cell_data_range.second; ++it) {
            const size_t polygon_id = it->first;
            if (tested_polygon[polygon_id] == 0) {
                tested_polygon[polygon_id] = 1;
                const Polygon& polygon = *exclude_polygons[polygon_id];
                if (polygon.contains(start) || polygon.contains(end)) {
                    intersects = true;
                    return false;
                }
            }

            auto edge = grid.segment(*it);
            if (Geometry::segments_intersect(start, end, edge.first, edge.second)) {
                intersects = true;
                return false;
            }
        }

        return true;
    };

    grid.visit_cells_intersecting_line(clipped_start, clipped_end, visit_candidate_cell, true);
    return intersects;
}

} // namespace

bool toolpath_intersects_bed_exclude_area_2d(const GCodeProcessorResult& gcode_result, const std::vector<Polygon>& combined_exclude_area_for_toolpath_check)
{
    std::vector<const Polygon*> exclude_polygons;
    exclude_polygons.reserve(combined_exclude_area_for_toolpath_check.size());
    for (const Polygon& polygon : combined_exclude_area_for_toolpath_check) {
        if (!polygon.empty())
            exclude_polygons.push_back(&polygon);
    }

    if (exclude_polygons.empty())
        return false;

    // The exact check should be bounded by the runtime exclude areas themselves.
    // wrapping_exclude_area may sit on / beyond the printable_area edge, so clipping
    // to printable_area may incorrectly discard the very segments we need to test.
    const BoundingBox exclude_bbox = get_extents(combined_exclude_area_for_toolpath_check);
    if (!exclude_bbox.defined)
        return false;

#if 1
    EdgeGrid::Grid grid;
    grid.create(exclude_polygons, scale_(kGridCellSizeMM));

    const bool all_segments_clear = visit_extrusion_segments(gcode_result, [&](const Point& start, const Point& end) {
        return !segment_intersects_exclude_grid(start, end, exclude_bbox, grid, exclude_polygons);
    });

    return !all_segments_clear;
#else
    // Direct brute-force implementation kept for local performance comparison.
    auto segment_intersects_polygon = [](const Point& start, const Point& end, const Polygon& polygon) {
        if (polygon.points.size() < 3)
            return false;
        if (polygon.contains(start) || polygon.contains(end))
            return true;
        for (size_t i = 0, j = polygon.points.size() - 1; i < polygon.points.size(); j = i++) {
            if (Geometry::segments_intersect(start, end, polygon.points[j], polygon.points[i]))
                return true;
        }
        return false;
    };

    const bool all_segments_clear = visit_extrusion_segments(gcode_result, [&](const Point& start, const Point& end) {
        const BoundingBox segment_bbox(start, end);
        if (!exclude_bbox.contains(start) && !exclude_bbox.contains(end) && !segment_bbox.overlap(exclude_bbox))
            return true;

        for (const Polygon* polygon : exclude_polygons) {
            if (!segment_bbox.overlap(get_extents(*polygon)))
                continue;
            if (segment_intersects_polygon(start, end, *polygon))
                return false;
        }

        return true;
    });

    return !all_segments_clear;
#endif
}

} // namespace Slic3r
