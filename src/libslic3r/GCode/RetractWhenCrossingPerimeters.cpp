#include "../ClipperUtils.hpp"
#include "../Layer.hpp"
#include "../Polyline.hpp"
#include "../BoundingBox.hpp"
#include "../ExPolygon.hpp"
#include "../Polygon.hpp"
#include "./Utils.hpp"
#include <algorithm>
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

bool RetractWhenCrossingPerimeters::travel_near_perimeters_too_long(
    const Polyline &travel, coord_t wall_proximity_distance, coord_t max_near_wall_travel)
{
    if (wall_proximity_distance <= 0 || max_near_wall_travel <= 0)
        return false;

    if (travel.length() <= max_near_wall_travel)
        return false;

    // 沿空驶路径流式采样：近墙时细查，远墙时按安全余量跳步。
    // 连续近墙长度达到最小回抽距离时保留回抽。
    const double near_wall_sample_distance = 0.5 * double(max_near_wall_travel);
    size_t segment_idx = 0;     //表示当前线段索引
    double segment_offset = 0.; //采样点在线段内的累计偏移
    Vec2d sample_point = travel.points.front().cast<double>();

    // 沿空驶折线向前移动指定距离；距离不足时自动跨到下一条线段。
    auto advance_sample = [&travel, &segment_idx, &segment_offset, &sample_point](double distance) {
        while (segment_idx + 1 < travel.points.size()) {
            const Vec2d segment_start = travel.points[segment_idx].cast<double>();
            const Vec2d segment = travel.points[segment_idx + 1].cast<double>() - segment_start;
            const double segment_length = segment.norm();
            if (segment_length <= 0.) {
                ++segment_idx;
                segment_offset = 0.;
                continue;
            }

            const double available = segment_length - segment_offset;
            if (distance <= available) {
                // 目标仍在当前线段内：用偏移占线段长度的比例计算坐标。
                segment_offset += distance;
                sample_point = segment_start + segment * (segment_offset / segment_length);
                return true;
            }

            // 当前线段不够长：先走到线段终点，再携带剩余距离进入下一段。
            distance -= available;
            ++segment_idx;
            segment_offset = 0.;
            sample_point = travel.points[segment_idx].cast<double>();
        }
        // 已到达整条空驶路径末端。
        return false;
    };

    // 只有相邻采样点都近墙时，才累计它们之间的路径长度。
    bool previous_sample_near_wall = false;
    double distance_from_previous_sample = 0.;
    double consecutive_near_wall_length = 0.;
    while (true) {
        // 复用当前层墙线的 AABB Tree，查询喷嘴中心到最近墙中心线的距离。
        const double wall_distance = m_aabbtree_lines_distancer.distance_from_lines<false>(sample_point.cast<coord_t>());
        const bool sample_near_wall = wall_distance <= wall_proximity_distance;
        if (sample_near_wall) {
            // 从远墙进入近墙区域时从零开始；连续近墙时累计上一步的路径长度。
            consecutive_near_wall_length = previous_sample_near_wall ?
                consecutive_near_wall_length + distance_from_previous_sample : 0.;
            if (consecutive_near_wall_length >= max_near_wall_travel)
                return true;
        } else {
            // 中途离墙后重新计数，互不连续的近墙区间不能合并。
            consecutive_near_wall_length = 0.;
        }

        // 近墙时固定小步检查；远墙时最多前进“当前墙距 - 安全墙距”。
        // 距离函数的变化量不会超过移动距离，因此该跳步不会越过近墙边界。
        const double next_sample_distance = sample_near_wall ?
            near_wall_sample_distance :
            std::max(near_wall_sample_distance, wall_distance - double(wall_proximity_distance));
        // 保存本次状态，供下一个采样点判断是否属于同一段连续近墙路径。
        previous_sample_near_wall = sample_near_wall;
        distance_from_previous_sample = next_sample_distance;
        if (!advance_sample(next_sample_distance))
            break;
    }
    // 检查完整条空驶路径后，没有发现超过阈值的连续近墙区间。
    return false;
}

bool RetractWhenCrossingPerimeters::travel_inside_internal_regions_no_wall_crossing(
    const Layer &layer, const Polyline &travel, coord_t wall_proximity_distance, coord_t max_near_wall_travel)
{
    // 只有完全位于内部、不穿墙且未长距离贴墙的空驶，才允许跳过回抽。
    if (!travel_inside_internal_regions(layer, travel)) return false;
    if (travel_cross_perimeters(layer, travel)) return false;
    return !travel_near_perimeters_too_long(travel, wall_proximity_distance, max_near_wall_travel);
}

} // namespace Slic3r
