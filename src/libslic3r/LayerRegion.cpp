#include "Layer.hpp"
#include "BridgeDetector.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "PerimeterGenerator.hpp"
#include "Print.hpp"
#include "Surface.hpp"
#include "BoundingBox.hpp"
#include "SVG.hpp"
#include "RegionExpansion.hpp"

#include <string>
#include <map>
#include <cmath>
#include "libslic3r/Log.hpp"

static const double max_deviation = scale_(0.5);
static const double max_variance  = 5 * scale_(0.01) * scale_(0.01);

namespace Slic3r {

Flow LayerRegion::flow(FlowRole role) const
{
    return this->flow(role, m_layer->height);
}

Flow LayerRegion::flow(FlowRole role, double layer_height) const
{
    return m_region->flow(*m_layer->object(), role, layer_height, m_layer->id() == 0);
}

Flow LayerRegion::bridging_flow(FlowRole role, bool thick_bridge) const
{
    const PrintRegion       &region         = this->region();
    const PrintRegionConfig &region_config  = region.config();
    const PrintObject       &print_object   = *this->layer()->object();
    if (thick_bridge) {
        // The old Slic3r way (different from all other slicers): Use rounded extrusions.
        // Get the configured nozzle_diameter for the extruder that this role's filament is grouped to.
        auto nozzle_diameter = nozzle_diameter_for_filament(print_object.print()->config(), region.extruder(role));
        // Applies default bridge spacing.
        return Flow::bridging_flow(float(sqrt(region_config.bridge_flow)) * nozzle_diameter, nozzle_diameter);
    } else {
        // The same way as other slicers: Use normal extrusions. Apply bridge_flow while maintaining the original spacing.
        return this->flow(role).with_flow_ratio(region_config.bridge_flow);
    }
}

// Fill in layerm->fill_surfaces by trimming the layerm->slices by the cummulative layerm->fill_surfaces.
void LayerRegion::slices_to_fill_surfaces_clipped()
{
    // Note: this method should be idempotent, but fill_surfaces gets modified
    // in place. However we're now only using its boundaries (which are invariant)
    // so we're safe. This guarantees idempotence of prepare_infill() also in case
    // that combine_infill() turns some fill_surface into VOID surfaces.
    // Collect polygons per surface type.
    std::array<SurfacesPtr, size_t(stCount)> by_surface;
    for (Surface &surface : this->slices.surfaces)
        by_surface[size_t(surface.surface_type)].emplace_back(&surface);
    // Trim surfaces by the fill_boundaries.
    this->fill_surfaces.surfaces.clear();
    for (size_t surface_type = 0; surface_type < size_t(stCount); ++ surface_type) {
        const SurfacesPtr &this_surfaces = by_surface[surface_type];
        if (! this_surfaces.empty())
            this->fill_surfaces.append(intersection_ex(this_surfaces, this->fill_expolygons), SurfaceType(surface_type));
    }
}

void LayerRegion::auto_circle_compensation(SurfaceCollection& slices, const AutoContourHolesCompensationParams &auto_contour_holes_compensation_params, float manual_offset)
{
     // filament is 1 base
     int filament_idx = this->region().config().wall_filament - 1;

     double limited_speed = auto_contour_holes_compensation_params.circle_compensation_speed[filament_idx];
     double counter_speed_coef      = auto_contour_holes_compensation_params.counter_speed_coef[filament_idx];
     double counter_diameter_coef   = auto_contour_holes_compensation_params.counter_diameter_coef[filament_idx];
     double counter_compensate_coef = scale_(auto_contour_holes_compensation_params.counter_compensate_coef[filament_idx]);

     double hole_speed_coef      = auto_contour_holes_compensation_params.hole_speed_coef[filament_idx];
     double hole_diameter_coef   = auto_contour_holes_compensation_params.hole_diameter_coef[filament_idx];
     double hole_compensate_coef = scale_(auto_contour_holes_compensation_params.hole_compensate_coef[filament_idx]);

     double counter_limit_min_value = scale_(auto_contour_holes_compensation_params.counter_limit_min_value[filament_idx]);
     double counter_limit_max_value = scale_(auto_contour_holes_compensation_params.counter_limit_max_value[filament_idx]);
     double hole_limit_min_value    = scale_(auto_contour_holes_compensation_params.hole_limit_min_value[filament_idx]);
     double hole_limit_max_value    = scale_(auto_contour_holes_compensation_params.hole_limit_max_value[filament_idx]);

     double diameter_limit_value = scale_(auto_contour_holes_compensation_params.diameter_limit[filament_idx]);

    for (Surface &surface : slices.surfaces) {
        Point  center;
        double diameter = 0;
        if (surface.expolygon.contour.is_approx_circle(max_deviation, max_variance, center, diameter)) {
            double offset_value = scale_(counter_speed_coef * limited_speed) + counter_diameter_coef * diameter + counter_compensate_coef;
            if (offset_value < counter_limit_min_value) {
                offset_value = counter_limit_min_value;
            } else if (offset_value > counter_limit_max_value) {
                offset_value = counter_limit_max_value;
            }
            offset_value -= manual_offset / 2;
            Polygons offseted_polys = offset(surface.expolygon.contour, offset_value);
            if (offseted_polys.size() == 1) {
                surface.expolygon.contour = offseted_polys[0];
                if (diameter < diameter_limit_value)
                    surface.counter_circle_compensation = true;
            }
        }
        for (size_t i = 0; i < surface.expolygon.holes.size(); ++i) {
            Polygon &hole = surface.expolygon.holes[i];
            if (hole.is_approx_circle(max_deviation, max_variance, center, diameter)) {
                double offset_value = scale_(hole_speed_coef * limited_speed) + hole_diameter_coef * diameter + hole_compensate_coef;
                if (offset_value < hole_limit_min_value) {
                    offset_value = hole_limit_min_value;
                } else if (offset_value > hole_limit_max_value) {
                    offset_value = hole_limit_max_value;
                }
                // positive value means shrinking hole, which oppsite to contour
                offset_value            = -offset_value;
                offset_value -= manual_offset / 2;
                Polygons offseted_polys = offset(hole, offset_value);
                if (offseted_polys.size() == 1) {
                    hole = offseted_polys[0];
                    if (diameter < diameter_limit_value)
                        surface.holes_circle_compensation.push_back(i);
                }
            }
        }
    }
}

void LayerRegion::make_perimeters(const SurfaceCollection &slices, const PerimeterRegions &perimeter_regions, SurfaceCollection *fill_surfaces, ExPolygons *fill_no_overlap, std::vector<LoopNode> &loop_nodes, double perimeter_layer_height, bool clear_outputs, const ExPolygons *lower_slices_override, const ExPolygons *upper_slices_override, bool fill_only, bool starts_at_object_base)
{
    if (clear_outputs && ! fill_only) {
        this->perimeters.clear();
        this->thin_fills.clear();
    }

    ExtrusionEntityCollection discarded_perimeters;
    ExtrusionEntityCollection discarded_thin_fills;
    std::vector<LoopNode>     discarded_loop_nodes;

    const PrintConfig &      print_config  = this->layer()->object()->print()->config();
    const PrintRegionConfig &region_config = this->region().config();
    const PrintObjectConfig& object_config = this->layer()->object()->config();
    // BBS multi-diameter: the merged core is printed with a larger effective height; walls and their flow
    // must use that height so the extrusion volume matches. 0 means "use the physical layer height".
    const double             wall_layer_height = perimeter_layer_height > 0. ? perimeter_layer_height : this->layer()->height;
    const bool               first_object_layer = this->layer()->id() == 0 || starts_at_object_base;
    auto wall_flow = [&](FlowRole role) {
        return this->region().flow(*this->layer()->object(), role, wall_layer_height, first_object_layer);
    };
    // This needs to be in sync with PrintObject::_slice() slicing_mode_normal_below_layer!
    bool spiral_mode = print_config.spiral_mode &&
        //FIXME account for raft layers.
        (this->layer()->id() >= size_t(region_config.bottom_shell_layers.value) &&
         this->layer()->print_z >= region_config.bottom_shell_thickness - EPSILON);

    PerimeterGenerator g(
        // input:
        &slices,
        wall_layer_height,
        wall_flow(frPerimeter),
        &region_config,
        &this->layer()->object()->config(),
        &print_config,
        float(primary_nozzle_diameter(this->layer()->object()->print()->full_print_config())),
        multi_nozzle_diameter_mode_enabled(this->layer()->object()->print()->full_print_config()),
        spiral_mode,
        // output:
        fill_only ? &discarded_perimeters : &this->perimeters,
        fill_only ? &discarded_thin_fills : &this->thin_fills,
        fill_surfaces,
        //BBS
        fill_no_overlap,
        fill_only ? &discarded_loop_nodes : &loop_nodes
    );

    // A merged band anchored at the object base stands on the bed, like layer 0. The physical layer below
    // its group top lies inside the band itself, so it is no support for the walls.
    if (lower_slices_override != nullptr)
        g.lower_slices = lower_slices_override;
    else if (this->layer()->lower_layer != nullptr && ! first_object_layer)
        // Cummulative sum of polygons over all the regions.
        g.lower_slices = &this->layer()->lower_layer->lslices;
    if (upper_slices_override != nullptr)
        g.upper_slices = upper_slices_override;
    else if (this->layer()->upper_layer != NULL)
        g.upper_slices = &this->layer()->upper_layer->lslices;

    g.layer_id              = (int)this->layer()->id();
    g.starts_at_object_base = first_object_layer;
    g.slice_z               = this->layer()->slice_z;
    g.ext_perimeter_flow    = wall_flow(frExternalPerimeter);
    // A merged overhang wall is the only extrusion filling its Z span, so it needs the merged height just
    // like the regular walls. Keep the original bridge flow for unmerged perimeters.
    g.overhang_flow         = wall_layer_height > this->layer()->height + EPSILON ?
        wall_flow(frPerimeter).with_flow_ratio(region_config.bridge_flow) :
        this->bridging_flow(frPerimeter, object_config.thick_bridges);
    g.solid_infill_flow     = wall_flow(frSolidInfill);
    g.perimeter_regions     = &perimeter_regions;

    if (this->layer()->object()->config().wall_generator.value == PerimeterGeneratorType::Arachne && !spiral_mode)
        g.process_arachne();
    else
        g.process_classic();
}


#if 1

// Extract surfaces of given type from surfaces, extract fill (layer) thickness of one of the surfaces.
static ExPolygons fill_surfaces_extract_expolygons(Surfaces &surfaces, std::initializer_list<SurfaceType> surface_types, double &thickness)
{
    size_t cnt = 0;
    for (const Surface &surface : surfaces)
        if (std::find(surface_types.begin(), surface_types.end(), surface.surface_type) != surface_types.end()) {
            ++cnt;
            thickness = surface.thickness;
        }
    if (cnt == 0)
        return {};

    ExPolygons out;
    out.reserve(cnt);
    for (Surface &surface : surfaces)
        if (std::find(surface_types.begin(), surface_types.end(), surface.surface_type) != surface_types.end())
            out.emplace_back(std::move(surface.expolygon));
    return out;
}

// Cache for detecting bridge orientation and merging regions with overlapping expansions.
struct Bridge {
    ExPolygon expolygon;
    uint32_t group_id;
    std::vector<Algorithm::RegionExpansionEx>::const_iterator bridge_expansion_begin;
    std::optional<double> angle{std::nullopt};
    // Group of the overlapping anchors before splitting by direction, the bridge itself if it overlaps no other bridge.
    uint32_t overlap_group_id;
};

// Group the bridge surfaces by overlaps.
uint32_t group_id(std::vector<Bridge> &bridges, uint32_t src_id) {
    uint32_t group_id = bridges[src_id].group_id;
    while (group_id != src_id) {
        src_id = group_id;
        group_id = bridges[src_id].group_id;
    }
    bridges[src_id].group_id = group_id;
    return group_id;
};

std::vector<Bridge> get_grouped_bridges(
    ExPolygons&& bridge_expolygons,
    const std::vector<Algorithm::RegionExpansionEx>& bridge_expansions
) {
    using namespace Algorithm;

    std::vector<Bridge> result;
    {
        result.reserve(bridge_expansions.size());
        uint32_t group_id = 0;
        using std::move_iterator;
        for (ExPolygon& expolygon : bridge_expolygons) {
            result.push_back({ std::move(expolygon), group_id, bridge_expansions.end(), std::nullopt, group_id });
            ++ group_id;
        }
    }


    // Detect overlaps of bridge anchors inside their respective shell regions.
    // bridge_expansions are sorted by boundary id and source id.
    for (auto expansion_iterator = bridge_expansions.begin(); expansion_iterator != bridge_expansions.end();) {
        auto boundary_region_begin = expansion_iterator;
        auto boundary_region_end = std::find_if(
            next(expansion_iterator),
            bridge_expansions.end(),
            [&](const RegionExpansionEx& expansion){
                return expansion.boundary_id != expansion_iterator->boundary_id;
            }
        );

        // Cache of bboxes per expansion boundary.
        std::vector<BoundingBox> bounding_boxes;
        bounding_boxes.reserve(std::distance(boundary_region_begin, boundary_region_end));
        std::transform(
            boundary_region_begin,
            boundary_region_end,
            std::back_inserter(bounding_boxes),
            [](const RegionExpansionEx& expansion){
                return get_extents(expansion.expolygon.contour);
            }
        );

        // For each bridge anchor of the current source:
        for (;expansion_iterator != boundary_region_end; ++expansion_iterator) {
            auto candidate_iterator = std::next(expansion_iterator);
            for (;candidate_iterator != boundary_region_end; ++candidate_iterator) {
                const BoundingBox& current_bounding_box{
                    bounding_boxes[expansion_iterator - boundary_region_begin]
                };
                const BoundingBox& candidate_bounding_box{
                    bounding_boxes[candidate_iterator - boundary_region_begin]
                };
                if (
                    expansion_iterator->src_id != candidate_iterator->src_id
                    && current_bounding_box.overlap(candidate_bounding_box)
                    // One may ignore holes, they are irrelevant for intersection test.
                    && !intersection(expansion_iterator->expolygon.contour, candidate_iterator->expolygon.contour).empty()
                ) {
                    // The two bridge regions intersect. Give them the same (lower) group id.
                    uint32_t id  = group_id(result, expansion_iterator->src_id);
                    uint32_t id2 = group_id(result, candidate_iterator->src_id);
                    if (id < id2)
                        result[id2].group_id = id;
                    else
                        result[id].group_id = id2;
                }
            }
        }
    }
    return result;
}

void detect_bridge_directions(
    const Algorithm::WaveSeeds& bridge_anchors,
    std::vector<Bridge>& bridges,
    const std::vector<ExpansionZone>& expansion_zones
) {
    if (expansion_zones.empty()) {
        throw std::runtime_error("At least one expansion zone must exist!");
    }
    auto it_bridge_anchor = bridge_anchors.begin();
    for (uint32_t bridge_id = 0; bridge_id < uint32_t(bridges.size()); ++ bridge_id) {
        Bridge &bridge = bridges[bridge_id];
        Polygons anchor_areas;
        int32_t last_anchor_id = -1;
        for (; it_bridge_anchor != bridge_anchors.end() && it_bridge_anchor->src == bridge_id; ++ it_bridge_anchor) {
            if (last_anchor_id != int(it_bridge_anchor->boundary)) {
                last_anchor_id = int(it_bridge_anchor->boundary);

                unsigned start_index{};
                unsigned end_index{};
                for (const ExpansionZone& expansion_zone: expansion_zones) {
                    end_index += expansion_zone.expolygons.size();
                    if (last_anchor_id < static_cast<int64_t>(end_index)) {
                        append(anchor_areas, to_polygons(expansion_zone.expolygons[last_anchor_id - start_index]));
                        break;
                    }
                    start_index += expansion_zone.expolygons.size();
                }
            }
        }
        Lines lines{to_lines(diff_pl(to_polylines(bridge.expolygon), expand(anchor_areas, float(SCALED_EPSILON))))};
        auto [bridging_dir, unsupported_dist] = detect_bridging_direction(lines, to_polygons(bridge.expolygon));
        bridge.angle = M_PI + std::atan2(bridging_dir.y(), bridging_dir.x());

        if constexpr (false) {
            coordf_t    stroke_width = scale_(0.06);
            BoundingBox bbox         = get_extents(anchor_areas);
            bbox.merge(get_extents(bridge.expolygon));
            bbox.offset(scale_(1.));
            ::Slic3r::SVG
                svg(debug_out_path(("bridge" + std::to_string(*bridge.angle) + "_" /* + std::to_string(this->layer()->bottom_z())*/).c_str()),
                bbox);
            svg.draw(bridge.expolygon, "cyan");
            svg.draw(lines, "green", stroke_width);
            svg.draw(anchor_areas, "red");
        }
    }
}

// Returns per overlap group, indexed by the id of its head, whether the group was split by direction.
static std::vector<bool> separate_bridge_directions(std::vector<Bridge>& bridges)
{
    std::vector<uint32_t> overlap_groups;
    overlap_groups.reserve(bridges.size());
    for (uint32_t bridge_id = 0; bridge_id < uint32_t(bridges.size()); ++bridge_id)
        overlap_groups.push_back(group_id(bridges, bridge_id));

    // get_grouped_bridges() always makes the lower id the head, so the head is the lowest id of its group:
    // when bridge_id is reached, all candidates below it are already regrouped by direction.
    std::vector<bool> split(bridges.size(), false);
    for (uint32_t bridge_id = 0; bridge_id < uint32_t(bridges.size()); ++bridge_id) {
        assert(overlap_groups[bridge_id] <= bridge_id);
        Bridge &bridge = bridges[bridge_id];
        bridge.overlap_group_id = overlap_groups[bridge_id];
        bridge.group_id = bridge_id;
        for (uint32_t candidate_id = overlap_groups[bridge_id]; candidate_id < bridge_id; ++candidate_id) {
            const Bridge &candidate = bridges[candidate_id];
            if (overlap_groups[candidate_id] == overlap_groups[bridge_id] && candidate.group_id == candidate_id &&
                bridge.angle && candidate.angle &&
                // Directions are detected per bridge, neighbours with curved or fully anchored edges differ by a few degrees.
                // Only split bridges running in clearly different directions, 10 degrees apart or more.
                Geometry::directions_parallel(std::fmod(*bridge.angle, M_PI), std::fmod(*candidate.angle, M_PI), Geometry::deg2rad(10.))) {
                bridge.group_id = candidate_id;
                break;
            }
        }
        if (bridge.group_id != overlap_groups[bridge_id])
            split[overlap_groups[bridge_id]] = true;
    }
    return split;
}

// Bridges of one direction group, merged with their anchors.
struct BridgeGroupArea {
    uint32_t   overlap_group_id;
    double     angle;
    // Bridge bodies without anchors, only collected when anchors have to be partitioned.
    ExPolygons bodies;
    ExPolygons area;
};

// Partition the anchors shared by bridge groups, which overlap but run in different directions.
// Each contested point goes to the group with the nearest bridge body: all bodies grow in lock step,
// so the dividing line is off by at most one step, ties within a step go to the lower group.
// overlap_area: the whole overlap group closed at once, as it was merged before splitting by direction.
static void partition_bridge_anchors(std::vector<BridgeGroupArea*> &groups, const ExPolygons &overlap_area, const float solid_spacing)
{
    // A bridge body always belongs to its own group.
    for (BridgeGroupArea *group : groups) {
        Polygons other_bodies;
        for (const BridgeGroupArea *other : groups)
            if (other != group)
                append(other_bodies, to_polygons(other->bodies));
        group->area = diff_ex(group->area, other_bodies);
    }

    // Closing each group alone leaves the narrow gaps between the groups open, closing the whole overlap group fills them.
    // Offer the gaps to every group, the growth below hands them to the nearest body.
    {
        Polygons covered;
        for (const BridgeGroupArea *group : groups)
            append(covered, to_polygons(group->area));
        const Polygons gaps = diff(to_polygons(overlap_area), covered);
        if (! gaps.empty())
            for (BridgeGroupArea *group : groups) {
                Polygons area = to_polygons(group->area);
                append(area, gaps);
                group->area = union_ex(area);
            }
    }

    Polygons contested;
    {
        Polygons covered;
        for (const BridgeGroupArea *group : groups) {
            Polygons area = to_polygons(group->area);
            if (! covered.empty())
                append(contested, intersection(area, covered));
            append(covered, std::move(area));
        }
        contested = union_(contested);
    }
    if (contested.empty())
        return;

    // Anchors scale with the extrusion width, so does the step: the error stays within a quarter of a line
    // and the number of steps does not depend on the nozzle diameter.
    const float  step    = 0.25f * solid_spacing;
    const double arc_tol = scaled<double>(0.01);
    // Slivers of numeric noise left by the boolean operations may never be covered by the growth and would keep it
    // running to the last step. They are only dropped from the candidates and from the area still unreached,
    // the contested area left unclaimed is handed out after the growth.
    const double min_area   = 0.01 * double(step) * double(step);
    auto         drop_noise = [min_area](ExPolygons &&expolygons) {
        expolygons.erase(std::remove_if(expolygons.begin(), expolygons.end(),
                             [min_area](const ExPolygon &expolygon) { return expolygon.area() < min_area; }),
                         expolygons.end());
        return to_polygons(std::move(expolygons));
    };

    // Contested area each group may claim, the part of the contested area inside the group.
    std::vector<Polygons> candidates(groups.size());
    std::vector<Polygons> claimed(groups.size());
    std::vector<Polygons> bodies(groups.size());
    const BoundingBox     contested_bbox = get_extents(contested);
    BoundingBox           bbox           = contested_bbox;
    for (size_t i = 0; i < groups.size(); ++ i) {
        candidates[i] = drop_noise(intersection_ex(to_polygons(groups[i]->area), contested));
        bodies[i]     = to_polygons(groups[i]->bodies);
        bbox.merge(get_extents(bodies[i]));
    }

    // Each contested point is reached at the latest when the growth spans the whole extent,
    // in practice once the farthest anchor point is covered.
    const size_t max_steps   = size_t(std::ceil(bbox.size().cast<double>().norm() / step)) + 1;
    bool         all_reached = false;
    // Every step works on the fixed inputs above and on the reach of the previous step only. Feeding the result of
    // one boolean operation into the next one step after step piles up their degenerate edges, the vertex count
    // then multiplies with every operation and the growth never finishes.
    // A point is claimed by the group reaching it first, ties within a step go to the lower group:
    // the claim of group i is its reach minus the reach of all groups in the previous step
    // and minus the reach of the lower groups in this step.
    Polygons reached_before;
    for (size_t istep = 1; istep <= max_steps; ++ istep) {
        const float dist = step * istep;
        // Body parts farther than dist from the contested area cannot reach anything in this step,
        // an exact clip keeps the offset small without changing the distances that matter.
        const Polygons clip_box{ contested_bbox.inflated(dist + step).polygon() };
        // Reached before this step or by a lower group in this step.
        Polygons taken = reached_before;
        // Reached by any group up to this step.
        Polygons reached_now;
        for (size_t i = 0; i < groups.size(); ++ i) {
            if (candidates[i].empty())
                continue;
            Polygons body = intersection(bodies[i], clip_box);
            if (body.empty())
                continue;
            Polygons reach = intersection(candidates[i], expand(body, dist, ClipperLib::jtRound, arc_tol));
            if (reach.empty())
                continue;
            append(claimed[i], taken.empty() ? reach : diff(reach, taken));
            append(taken, reach);
            append(reached_now, std::move(reach));
        }
        // The reach of a group only grows from step to step, so this step alone covers all the earlier ones.
        reached_before = union_(reached_now);
        if (drop_noise(diff_ex(contested, reached_before)).empty()) {
            all_reached = true;
            break;
        }
    }
    if (! all_reached)
        BOOST_LOG_TRIVIAL(warning) << "Bridge anchor partition left an unreached area, the lower group takes it";
    // Dropped noise and unreached leftovers still have to be covered, the lower group takes them.
    // A leftover touching only another group moves over there with the orphans below.
    {
        Polygons all_claimed;
        for (const Polygons &area : claimed)
            append(all_claimed, area);
        append(claimed.front(), diff(contested, all_claimed));
    }

    for (size_t i = 0; i < groups.size(); ++ i) {
        Polygons area = diff(to_polygons(groups[i]->area), contested);
        append(area, std::move(claimed[i]));
        groups[i]->area = union_ex(area);
    }

    // Pieces cut off from their own body follow a touching group instead of becoming a separate bridge.
    const float touch = scaled<float>(0.01);
    std::vector<ExPolygons> orphans(groups.size());
    for (size_t i = 0; i < groups.size(); ++ i) {
        const Polygons body_touch = expand(to_polygons(groups[i]->bodies), touch);
        ExPolygons     kept;
        for (ExPolygon &piece : groups[i]->area)
            (intersection_ex(piece, body_touch).empty() ? orphans[i] : kept).emplace_back(std::move(piece));
        groups[i]->area = std::move(kept);
    }
    // Touch areas are taken before any orphan moves, so the result does not depend on the visiting order.
    // An orphan touching no other group stays where it is.
    std::vector<Polygons> area_touch(groups.size());
    for (size_t i = 0; i < groups.size(); ++ i)
        area_touch[i] = expand(to_polygons(groups[i]->area), touch);
    for (size_t i = 0; i < groups.size(); ++ i)
        for (ExPolygon &orphan : orphans[i]) {
            size_t owner = i;
            for (size_t j = 0; j < groups.size(); ++ j)
                if (j != i && ! intersection_ex(orphan, area_touch[j]).empty()) {
                    owner = j;
                    break;
                }
            groups[owner]->area.emplace_back(std::move(orphan));
        }
    for (size_t i = 0; i < groups.size(); ++ i)
        if (! orphans[i].empty() || groups[i]->area.size() > 1)
            groups[i]->area = union_ex(groups[i]->area);
}

Surfaces merge_bridges(
    std::vector<Bridge>& bridges,
    const std::vector<Algorithm::RegionExpansionEx>& bridge_expansions,
    const float closing_radius,
    const float solid_spacing,
    const std::vector<bool>& split_overlap_groups
) {
    for (auto it = bridge_expansions.begin(); it != bridge_expansions.end(); ) {
        bridges[it->src_id].bridge_expansion_begin = it;
        uint32_t src_id = it->src_id;
        for (++ it; it != bridge_expansions.end() && it->src_id == src_id; ++ it) ;
    }

    std::vector<BridgeGroupArea> groups;
    // Unclosed area of each overlap group split by direction.
    std::map<uint32_t, Polygons> overlap_areas;
    for (uint32_t bridge_id = 0; bridge_id < uint32_t(bridges.size()); ++ bridge_id) {
        if (group_id(bridges, bridge_id) == bridge_id) {
            // Head of the group.
            const uint32_t overlap_group_id = bridges[bridge_id].overlap_group_id;
            const bool     split            = split_overlap_groups[overlap_group_id];
            Polygons   acc;
            ExPolygons bodies;
            for (uint32_t bridge_id2 = bridge_id; bridge_id2 < uint32_t(bridges.size()); ++ bridge_id2)
                if (group_id(bridges, bridge_id2) == bridge_id) {
                    if (split)
                        bodies.emplace_back(bridges[bridge_id2].expolygon);
                    append(acc, to_polygons(std::move(bridges[bridge_id2].expolygon)));
                    auto it_bridge_expansion = bridges[bridge_id2].bridge_expansion_begin;
                    assert(it_bridge_expansion == bridge_expansions.end() || it_bridge_expansion->src_id == bridge_id2);
                    for (; it_bridge_expansion != bridge_expansions.end() && it_bridge_expansion->src_id == bridge_id2; ++ it_bridge_expansion)
                        append(acc, to_polygons(it_bridge_expansion->expolygon));
                }
            if (!bridges[bridge_id].angle) {
                assert(false && "Bridge angle must be pre-calculated!");
            }
            //NOTE: The current regularization of the shells can create small unasigned regions in the object (E.G. benchy)
            // without the following closing operation, those regions will stay unfilled and cause small holes in the expanded surface.
            // look for narrow_ensure_vertical_wall_thickness_region_radius filter.
            // without safety offset, artifacts are generated (GH #2494)
            // union_safety_offset_ex(acc)
            if (split)
                append(overlap_areas[overlap_group_id], acc);
            groups.push_back({ overlap_group_id, bridges[bridge_id].angle ? *bridges[bridge_id].angle : -1,
                               std::move(bodies), closing_ex(acc, closing_radius) });
        }
    }

    std::map<uint32_t, std::vector<BridgeGroupArea*>> overlapping;
    for (BridgeGroupArea &group : groups)
        if (split_overlap_groups[group.overlap_group_id])
            overlapping[group.overlap_group_id].push_back(&group);
    for (auto &[overlap_group_id, members] : overlapping) {
        assert(members.size() > 1);
        partition_bridge_anchors(members, closing_ex(overlap_areas[overlap_group_id], closing_radius), solid_spacing);
    }

    Surfaces result;
    for (BridgeGroupArea &group : groups) {
        Surface templ{ stBottomBridge, {} };
        templ.bridge_angle = group.angle;
        for (ExPolygon &ex : group.area)
            result.emplace_back(templ, std::move(ex));
    }
    return result;
}

struct ExpansionResult {
    Algorithm::WaveSeeds anchors;
    std::vector<Algorithm::RegionExpansionEx> expansions;
};

ExpansionResult expand_expolygons(
    const ExPolygons& expolygons,
    std::vector<ExpansionZone>& expansion_zones
) {
    using namespace Algorithm;
    WaveSeeds bridge_anchors;
    std::vector<RegionExpansionEx> bridge_expansions;

    unsigned processed_bridges_count = 0;
    for (ExpansionZone& expansion_zone : expansion_zones) {
        WaveSeeds seeds{wave_seeds(
            expolygons,
            expansion_zone.expolygons,
            expansion_zone.parameters.tiny_expansion,
            true
        )};
        std::vector<RegionExpansionEx> expansions{propagate_waves_ex(
            seeds,
            expansion_zone.expolygons,
            expansion_zone.parameters
        )};

        for (WaveSeed &seed : seeds)
            seed.boundary += processed_bridges_count;
        for (RegionExpansionEx &expansion : expansions)
            expansion.boundary_id += processed_bridges_count;

        expansion_zone.expanded_into = ! expansions.empty();

        append(bridge_anchors, std::move(seeds));
        append(bridge_expansions, std::move(expansions));

        processed_bridges_count += expansion_zone.expolygons.size();
    }
    return {bridge_anchors, bridge_expansions};
}

// Extract bridging surfaces from "surfaces", expand them into "shells" using expansion_params,
// detect bridges.
// Trim "shells" by the expanded bridges.
Surfaces expand_bridges_detect_orientations(
    Surfaces &surfaces,
    std::vector<ExpansionZone>& expansion_zones,
    const float closing_radius,
    const float solid_spacing
)
{
    using namespace Slic3r::Algorithm;

    if (! (solid_spacing > 0.f))
        throw std::runtime_error("Detecting bridges needs a positive solid infill spacing");

    double thickness;
    ExPolygons bridge_expolygons = fill_surfaces_extract_expolygons(surfaces, {stBottomBridge}, thickness);
    if (bridge_expolygons.empty())
        return {};

    // Calculate bridge anchors and their expansions in their respective shell region.
    ExpansionResult expansion_result{expand_expolygons(
        bridge_expolygons,
        expansion_zones
    )};

    std::vector<Bridge> bridges{get_grouped_bridges(
        std::move(bridge_expolygons),
        expansion_result.expansions
    )};
    bridge_expolygons.clear();

    std::sort(expansion_result.anchors.begin(), expansion_result.anchors.end(), Algorithm::lower_by_src_and_boundary);
    detect_bridge_directions(expansion_result.anchors, bridges, expansion_zones);
    const std::vector<bool> split_overlap_groups = separate_bridge_directions(bridges);

    // Merge the groups with the same group id, produce surfaces by merging source overhangs with their newly expanded anchors.
    std::sort(expansion_result.expansions.begin(), expansion_result.expansions.end(), [](auto &l, auto &r) {
        return l.src_id < r.src_id || (l.src_id == r.src_id && l.boundary_id < r.boundary_id);
    });
    Surfaces out{merge_bridges(bridges, expansion_result.expansions, closing_radius, solid_spacing, split_overlap_groups)};

    // Clip by the expanded bridges.
    for (ExpansionZone& expansion_zone : expansion_zones)
        if (expansion_zone.expanded_into)
            expansion_zone.expolygons = diff_ex(expansion_zone.expolygons, out);
    return out;
}

Surfaces expand_merge_surfaces(
    Surfaces &surfaces,
    SurfaceType surface_type,
    std::vector<ExpansionZone>& expansion_zones,
    const float closing_radius,
    const double bridge_angle
)
{
    using namespace Slic3r::Algorithm;

    double thickness;
    ExPolygons src = fill_surfaces_extract_expolygons(surfaces, {surface_type}, thickness);
    if (src.empty())
        return {};

    unsigned processed_expolygons_count = 0;
    std::vector<RegionExpansion> expansions;
    for (ExpansionZone& expansion_zone : expansion_zones) {
        std::vector<RegionExpansion> zone_expansions = propagate_waves(src, expansion_zone.expolygons, expansion_zone.parameters);
        expansion_zone.expanded_into = !zone_expansions.empty();

        for (RegionExpansion &expansion : zone_expansions)
            expansion.boundary_id += processed_expolygons_count;

        processed_expolygons_count += expansion_zone.expolygons.size();
        append(expansions, std::move(zone_expansions));
    }

    std::vector<ExPolygon> expanded = merge_expansions_into_expolygons(std::move(src), std::move(expansions));
    //NOTE: The current regularization of the shells can create small unasigned regions in the object (E.G. benchy)
    // without the following closing operation, those regions will stay unfilled and cause small holes in the expanded surface.
    // look for narrow_ensure_vertical_wall_thickness_region_radius filter.
    expanded = closing_ex(expanded, closing_radius);
    // Trim the zones by the expanded expolygons.
    for (ExpansionZone& expansion_zone : expansion_zones)
        if (expansion_zone.expanded_into)
            expansion_zone.expolygons = diff_ex(expansion_zone.expolygons, expanded);

    Surface templ{ surface_type, {} };
    templ.bridge_angle = bridge_angle;
    Surfaces out;
    out.reserve(expanded.size());
    for (auto &expoly : expanded)
        out.emplace_back(templ, std::move(expoly));
    return out;
}

void LayerRegion::process_external_surfaces(const Layer *lower_layer, const Polygons *lower_layer_covered)
{
    using namespace Slic3r::Algorithm;

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_fill_surfaces_to_svg_debug("4_process_external_surfaces-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    // Width of the perimeters.
    float shell_width = 0;
    float expansion_min = 0;
    const PrintRegionConfig &region_config = this->region().config();
    int num_perimeters = region_config.wall_loops.value;
    const PrintObject *print_object = this->layer()->object();
    if (multi_nozzle_diameter_mode_enabled(print_object->print()->full_print_config())) {
        const double nozzle_diameter = nozzle_diameter_for_filament(
            print_object->print()->config(), (unsigned int) region_config.wall_filament.value);
        num_perimeters = multi_nozzle_wall_loops(nozzle_diameter, num_perimeters);
    }
    if (num_perimeters > 0) {
        Flow external_perimeter_flow = this->flow(frExternalPerimeter);
        Flow perimeter_flow          = this->flow(frPerimeter);
        shell_width  = 0.5f * external_perimeter_flow.scaled_width() + external_perimeter_flow.scaled_spacing();
        shell_width += perimeter_flow.scaled_spacing() * (num_perimeters - 1);
        expansion_min = perimeter_flow.scaled_spacing();
    } else {
        // TODO: Maybe there is better solution when printing with zero perimeters, but this works reasonably well, given the situation
        shell_width   = float(SCALED_EPSILON);
        expansion_min = float(SCALED_EPSILON);;
    }

    // Scaled expansions of the respective external surfaces.
    float                           expansion_top           = shell_width * sqrt(2.);
    float                           expansion_bottom        = expansion_top;
    float                           expansion_bottom_bridge = expansion_top;
    // Expand by waves of expansion_step size (expansion_step is scaled), but with no more steps than max_nr_expansion_steps.
    static constexpr const float    expansion_step          = scaled<float>(0.1);
    // Don't take more than max_nr_steps for small expansion_step.
    static constexpr const size_t   max_nr_expansion_steps  = 5;
    // Radius (with added epsilon) to absorb empty regions emering from regularization of ensuring, viz  const float narrow_ensure_vertical_wall_thickness_region_radius = 0.5f * 0.65f * min_perimeter_infill_spacing;
    const float closing_radius = 0.55f * 0.65f * 1.05f * this->flow(frSolidInfill).scaled_spacing();

    // Expand the top / bottom / bridge surfaces into the shell thickness solid infills.
    double     layer_thickness;
    ExPolygons shells = union_ex(fill_surfaces_extract_expolygons(fill_surfaces.surfaces, { stInternalSolid }, layer_thickness));
    ExPolygons sparse = union_ex(fill_surfaces_extract_expolygons(fill_surfaces.surfaces, { stInternal }, layer_thickness));
    ExPolygons top_expolygons = union_ex(fill_surfaces_extract_expolygons(fill_surfaces.surfaces, { stTop }, layer_thickness));
    const auto expansion_params_into_sparse_infill = RegionExpansionParameters::build(expansion_min, expansion_step, max_nr_expansion_steps);
    const auto expansion_params_into_solid_infill  = RegionExpansionParameters::build(expansion_bottom_bridge, expansion_step, max_nr_expansion_steps);

    std::vector<ExpansionZone> expansion_zones{
        ExpansionZone{std::move(shells), expansion_params_into_solid_infill},
        ExpansionZone{std::move(sparse), expansion_params_into_sparse_infill},
        ExpansionZone{std::move(top_expolygons), expansion_params_into_solid_infill}
    };

    SurfaceCollection bridges;
    {
        BOOST_LOG_TRIVIAL(trace) << "Processing external surface, detecting bridges. layer" << this->layer()->print_z;
        const double custom_angle = this->region().config().bridge_angle.value;
        bridges.surfaces = custom_angle > 0 ?
            expand_merge_surfaces(fill_surfaces.surfaces, stBottomBridge, expansion_zones, closing_radius, Geometry::deg2rad(custom_angle)) :
            expand_bridges_detect_orientations(fill_surfaces.surfaces, expansion_zones, closing_radius, this->flow(frSolidInfill).scaled_spacing());
        BOOST_LOG_TRIVIAL(trace) << "Processing external surface, detecting bridges - done";
#if 0
        {
            static int iRun = 0;
            bridges.export_to_svg(debug_out_path("bridges-after-grouping-%d.svg", iRun++), true);
        }
#endif
    }

    fill_surfaces.remove_types({ stTop });
    {
        Surface top_templ(stTop, {});
        top_templ.thickness = layer_thickness;
        fill_surfaces.append(std::move(expansion_zones.back().expolygons), top_templ);
    }

    expansion_zones.pop_back();

    expansion_zones.at(0).parameters = RegionExpansionParameters::build(expansion_bottom, expansion_step, max_nr_expansion_steps);
    Surfaces bottoms = expand_merge_surfaces(fill_surfaces.surfaces, stBottom, expansion_zones, closing_radius);

    expansion_zones.at(0).parameters = RegionExpansionParameters::build(expansion_top, expansion_step, max_nr_expansion_steps);
    Surfaces tops = expand_merge_surfaces(fill_surfaces.surfaces, stTop, expansion_zones, closing_radius);

    //expansion_zone[0]: shell , expansion_zone[1]: sparse
    //apply minimu sparse infill area logic, this should also be added in bridge_over_infill
    if (!this->layer()->object()->print()->config().spiral_mode && this->region().config().sparse_infill_density.value > 0) {
        auto &sparse=expansion_zones[1].expolygons;
        auto &shells=expansion_zones[0].expolygons;
        double min_area = scale_(scale_(this->region().config().minimum_sparse_infill_area.value));
        ExPolygons areas_to_be_solid{};
        sparse.erase(std::remove_if(sparse.begin(), sparse.end(), [min_area, &areas_to_be_solid](ExPolygon& expoly) {
            if (expoly.area() <= min_area) {
                areas_to_be_solid.push_back(expoly);
                return true;
            }
            return false;
        }), sparse.end());

        if (!areas_to_be_solid.empty())
            shells = union_ex(shells, areas_to_be_solid);
    }


//    m_fill_surfaces.remove_types({ stBottomBridge, stBottom, stTop, stInternal, stInternalSolid });
    fill_surfaces.clear();
    unsigned zones_expolygons_count = 0;
    for (const ExpansionZone& zone : expansion_zones)
        zones_expolygons_count += zone.expolygons.size();
    reserve_more(fill_surfaces.surfaces, zones_expolygons_count + bridges.size() + bottoms.size() + tops.size());
    {
        Surface solid_templ(stInternalSolid, {});
        solid_templ.thickness = layer_thickness;
        fill_surfaces.append(std::move(expansion_zones[0].expolygons), solid_templ);
    }
    {
        Surface sparse_templ(stInternal, {});
        sparse_templ.thickness = layer_thickness;
        fill_surfaces.append(std::move(expansion_zones[1].expolygons), sparse_templ);
    }
    fill_surfaces.append(std::move(bridges.surfaces));
    fill_surfaces.append(std::move(bottoms));
    fill_surfaces.append(std::move(tops));

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_fill_surfaces_to_svg_debug("4_process_external_surfaces-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
}

#else

#endif

void LayerRegion::prepare_fill_surfaces()
{
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_slices_to_svg_debug("2_prepare_fill_surfaces-initial");
    export_region_fill_surfaces_to_svg_debug("2_prepare_fill_surfaces-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    /*  Note: in order to make the psPrepareInfill step idempotent, we should never
        alter fill_surfaces boundaries on which our idempotency relies since that's
        the only meaningful information returned by psPerimeters. */

    bool spiral_mode = this->layer()->object()->print()->config().spiral_mode;

#if 0
    // if no solid layers are requested, turn top/bottom surfaces to internal
    if (! spiral_mode && this->region().config().top_shell_layers == 0) {
        for (Surface &surface : this->fill_surfaces.surfaces)
            if (surface.is_top())
                //BBS
                //surface.surface_type = this->layer()->object()->config().infill_only_where_needed ? stInternalVoid : stInternal;
                surface.surface_type = PrintObject::infill_only_where_needed ? stInternalVoid : stInternal;
    }
    if (this->region().config().bottom_shell_layers == 0) {
        for (Surface &surface : this->fill_surfaces.surfaces)
            if (surface.is_bottom()) // (surface.surface_type == stBottom)
                surface.surface_type = stInternal;
    }
#endif

    // turn too small internal regions into solid regions according to the user setting
    if (! spiral_mode && this->region().config().sparse_infill_density.value > 0) {
        // scaling an area requires two calls!
        double min_area = scale_(scale_(this->region().config().minimum_sparse_infill_area.value));
        for (Surface &surface : this->fill_surfaces.surfaces)
            if (surface.surface_type == stInternal && surface.area() <= min_area)
                surface.surface_type = stInternalSolid;
    }

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_slices_to_svg_debug("2_prepare_fill_surfaces-final");
    export_region_fill_surfaces_to_svg_debug("2_prepare_fill_surfaces-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
}

double LayerRegion::infill_area_threshold() const
{
    double ss = this->flow(frSolidInfill).scaled_spacing();
    return ss*ss;
}

void LayerRegion::trim_surfaces(const Polygons &trimming_polygons)
{
#ifndef NDEBUG
    for (const Surface &surface : this->slices.surfaces)
        assert(surface.surface_type == stInternal);
#endif /* NDEBUG */
	this->slices.set(intersection_ex(this->slices.surfaces, trimming_polygons), stInternal);
}

void LayerRegion::elephant_foot_compensation_step(const float elephant_foot_compensation_perimeter_step, const Polygons &trimming_polygons)
{
#ifndef NDEBUG
    for (const Surface &surface : this->slices.surfaces)
        assert(surface.surface_type == stInternal);
#endif /* NDEBUG */
    Polygons tmp = intersection(this->slices.surfaces, trimming_polygons);
    append(tmp, diff(this->slices.surfaces, opening(this->slices.surfaces, elephant_foot_compensation_perimeter_step)));
    this->slices.set(union_ex(tmp), stInternal);
}

void LayerRegion::export_region_slices_to_svg(const char *path) const
{
    BoundingBox bbox;
    for (Surfaces::const_iterator surface = this->slices.surfaces.begin(); surface != this->slices.surfaces.end(); ++surface)
        bbox.merge(get_extents(surface->expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (Surfaces::const_iterator surface = this->slices.surfaces.begin(); surface != this->slices.surfaces.end(); ++surface)
        svg.draw(surface->expolygon, surface_type_to_color_name(surface->surface_type), transparency);
    for (Surfaces::const_iterator surface = this->fill_surfaces.surfaces.begin(); surface != this->fill_surfaces.surfaces.end(); ++surface)
        svg.draw(surface->expolygon.lines(), surface_type_to_color_name(surface->surface_type));
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void LayerRegion::export_region_slices_to_svg_debug(const char *name) const
{
    static std::map<std::string, size_t> idx_map;
    size_t &idx = idx_map[name];
    this->export_region_slices_to_svg(debug_out_path("LayerRegion-slices-%s-%d.svg", name, idx ++).c_str());
}

void LayerRegion::export_region_fill_surfaces_to_svg(const char *path) const
{
    BoundingBox bbox;
    for (Surfaces::const_iterator surface = this->fill_surfaces.surfaces.begin(); surface != this->fill_surfaces.surfaces.end(); ++surface)
        bbox.merge(get_extents(surface->expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const Surface &surface : this->fill_surfaces.surfaces) {
        svg.draw(surface.expolygon, surface_type_to_color_name(surface.surface_type), transparency);
        svg.draw_outline(surface.expolygon, "black", "blue", scale_(0.05));
    }
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void LayerRegion::export_region_fill_surfaces_to_svg_debug(const char *name) const
{
    static std::map<std::string, size_t> idx_map;
    size_t &idx = idx_map[name];
    this->export_region_fill_surfaces_to_svg(debug_out_path("LayerRegion-fill_surfaces-%s-%d.svg", name, idx ++).c_str());
}

void LayerRegion::simplify_entity_collection(ExtrusionEntityCollection* entity_collection)
{
    for (size_t i = 0; i < entity_collection->entities.size(); i++) {
        if (ExtrusionEntityCollection* collection = dynamic_cast<ExtrusionEntityCollection*>(entity_collection->entities[i]))
            this->simplify_entity_collection(collection);
        else if (ExtrusionPath* path = dynamic_cast<ExtrusionPath*>(entity_collection->entities[i]))
            this->simplify_path(path);
        else if (ExtrusionMultiPath* multipath = dynamic_cast<ExtrusionMultiPath*>(entity_collection->entities[i]))
            this->simplify_multi_path(multipath);
        else if (ExtrusionLoop* loop = dynamic_cast<ExtrusionLoop*>(entity_collection->entities[i]))
            this->simplify_loop(loop);
        else
            throw Slic3r::InvalidArgument("Invalid extrusion entity supplied to simplify_entity_collection()");
    }
}

void LayerRegion::simplify_path(ExtrusionPath* path)
{
    const auto print_config = this->layer()->object()->print()->config();
    const bool spiral_mode = print_config.spiral_mode;
    const bool enable_arc_fitting = print_config.enable_arc_fitting;
    const auto scaled_resolution = scaled<double>(print_config.resolution.value);

    if (enable_arc_fitting &&
        !spiral_mode) {
        if (path->role() == erInternalInfill)
            path->simplify_by_fitting_arc(SCALED_SPARSE_INFILL_RESOLUTION);
        else
            path->simplify_by_fitting_arc(scaled_resolution);
    } else {
        path->simplify(scaled_resolution);
    }
}

void LayerRegion::simplify_multi_path(ExtrusionMultiPath* multipath)
{
    const auto print_config = this->layer()->object()->print()->config();
    const bool spiral_mode = print_config.spiral_mode;
    const bool enable_arc_fitting = print_config.enable_arc_fitting;
    const auto scaled_resolution = scaled<double>(print_config.resolution.value);

    for (size_t i = 0; i < multipath->paths.size(); ++i) {
        if (enable_arc_fitting &&
            !spiral_mode) {
            if (multipath->paths[i].role() == erInternalInfill)
                multipath->paths[i].simplify_by_fitting_arc(SCALED_SPARSE_INFILL_RESOLUTION);
            else
                multipath->paths[i].simplify_by_fitting_arc(scaled_resolution);
        } else {
            multipath->paths[i].simplify(scaled_resolution);
        }
    }
}

void LayerRegion::simplify_loop(ExtrusionLoop* loop)
{
    const auto print_config = this->layer()->object()->print()->config();
    const bool spiral_mode = print_config.spiral_mode;
    const bool enable_arc_fitting = print_config.enable_arc_fitting;
    const auto scaled_resolution = scaled<double>(print_config.resolution.value);

    for (size_t i = 0; i < loop->paths.size(); ++i) {
        if (enable_arc_fitting &&
            !spiral_mode) {
            if (loop->paths[i].role() == erInternalInfill)
                loop->paths[i].simplify_by_fitting_arc(SCALED_SPARSE_INFILL_RESOLUTION);
            else
                loop->paths[i].simplify_by_fitting_arc(scaled_resolution);
        } else {
            loop->paths[i].simplify(scaled_resolution);
        }
    }
}

}
