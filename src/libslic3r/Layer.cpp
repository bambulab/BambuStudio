#include "Layer.hpp"
#include "ClipperUtils.hpp"
#include "Print.hpp"
#include "Fill/Fill.hpp"
#include "ShortestPath.hpp"
#include "SVG.hpp"
#include "BoundingBox.hpp"
#include "libslic3r/AABBTreeLines.hpp"
#include <boost/log/trivial.hpp>
static const int Continuitious_length = scale_(0.01);
static const int dist_scale_threshold = 1.2;

namespace Slic3r {

Layer::~Layer()
{
    this->lower_layer = this->upper_layer = nullptr;
    for (LayerRegion *region : m_regions)
        delete region;
    m_regions.clear();
}

// Test whether whether there are any slices assigned to this layer.
bool Layer::empty() const
{
	for (const LayerRegion *layerm : m_regions)
        if (layerm != nullptr && ! layerm->slices.empty())
            // Non empty layer.
            return false;
    return true;
}

LayerRegion* Layer::add_region(const PrintRegion *print_region)
{
    m_regions.emplace_back(new LayerRegion(this, print_region));
    return m_regions.back();
}
void Layer::apply_auto_circle_compensation()
{
    for (LayerRegion *layerm : m_regions) {
        layerm->auto_circle_compensation(layerm->slices, this->object()->get_auto_circle_compenstaion_params(), scale_(this->object()->config().circle_compensation_manual_offset));
    }
}

// merge all regions' slices to get islands
void Layer::make_slices()
{
    ExPolygons slices;
    if (m_regions.size() == 1) {
        // optimization: if we only have one region, take its slices
        slices = to_expolygons(m_regions.front()->slices.surfaces);
    } else {
        Polygons slices_p;
        for (LayerRegion *layerm : m_regions)
            polygons_append(slices_p, to_polygons(layerm->slices.surfaces));
        slices = union_safety_offset_ex(slices_p);
    }

    this->lslices.clear();
    this->lslices.reserve(slices.size());

    // prepare ordering points
    Points ordering_points;
    ordering_points.reserve(slices.size());
    for (const ExPolygon &ex : slices)
        ordering_points.push_back(ex.contour.first_point());

    // sort slices
    std::vector<Points::size_type> order = chain_points(ordering_points);

    // populate slices vector
    for (size_t i : order)
        this->lslices.emplace_back(std::move(slices[i]));
}

static inline bool layer_needs_raw_backup(const Layer *layer)
{
    // BBS: backup raw slice for generating support
    //return ! (layer->regions().size() == 1 && (layer->id() > 0 || layer->object()->config().elefant_foot_compensation.value == 0));
    return true;
}

void Layer::backup_untyped_slices()
{
    if (layer_needs_raw_backup(this)) {
        for (LayerRegion *layerm : m_regions)
            layerm->raw_slices = to_expolygons(layerm->slices.surfaces);
    } else {
        assert(m_regions.size() == 1);
        m_regions.front()->raw_slices.clear();
    }
}

void Layer::restore_untyped_slices()
{
    if (layer_needs_raw_backup(this)) {
        for (LayerRegion *layerm : m_regions)
            layerm->slices.set(layerm->raw_slices, stInternal);
    } else {
        assert(m_regions.size() == 1);
        m_regions.front()->slices.set(this->lslices, stInternal);
    }
}

// Similar to Layer::restore_untyped_slices()
// To improve robustness of detect_surfaces_type() when reslicing (working with typed slices), see GH issue #7442.
// Only resetting layerm->slices if Slice::extra_perimeters is always zero or it will not be used anymore
// after the perimeter generator.
void Layer::restore_untyped_slices_no_extra_perimeters()
{
    if (layer_needs_raw_backup(this)) {
        for (LayerRegion *layerm : m_regions)
            //BBS: remove extra_perimeters. Always false
        	//if (! layerm->region().config().extra_perimeters.value)
            	layerm->slices.set(layerm->raw_slices, stInternal);
    } else {
    	assert(m_regions.size() == 1);
    	LayerRegion *layerm = m_regions.front();
    	// This optimization is correct, as extra_perimeters are only reused by prepare_infill() with multi-regions.
        //if (! layerm->region().config().extra_perimeters.value)
        	layerm->slices.set(this->lslices, stInternal);
    }
}

ExPolygons Layer::merged(float offset_scaled) const
{
	assert(offset_scaled >= 0.f);
    // If no offset is set, apply EPSILON offset before union, and revert it afterwards.
	float offset_scaled2 = 0;
	if (offset_scaled == 0.f) {
		offset_scaled  = float(  EPSILON);
		offset_scaled2 = float(- EPSILON);
    }
    Polygons polygons;
	for (LayerRegion *layerm : m_regions) {
		const PrintRegionConfig &config = layerm->region().config();
		// Our users learned to bend Slic3r to produce empty volumes to act as subtracters. Only add the region if it is non-empty.
		if (config.bottom_shell_layers > 0 || config.top_shell_layers > 0 || config.sparse_infill_density > 0. || config.wall_loops > 0)
			append(polygons, offset(layerm->slices.surfaces, offset_scaled));
	}
    ExPolygons out = union_ex(polygons);
	if (offset_scaled2 != 0.f)
		out = offset_ex(out, offset_scaled2);
    return out;
}

// Here the perimeters are created cummulatively for all layer regions sharing the same parameters influencing the perimeters.
// The perimeter paths and the thin fills (ExtrusionEntityCollection) are assigned to the first compatible layer region.
// The resulting fill surface is split back among the originating regions.
void Layer::make_perimeters()
{
    BOOST_LOG_TRIVIAL(trace) << "Generating perimeters for layer " << this->id();
    // keep track of regions whose perimeters we have already generated
    std::vector<unsigned char> done(m_regions.size(), false);
 
    for (LayerRegionPtrs::iterator layerm = m_regions.begin(); layerm != m_regions.end(); ++ layerm)
    	if ((*layerm)->slices.empty()) {
 			(*layerm)->perimeters.clear();
 			(*layerm)->fills.clear();
 			(*layerm)->thin_fills.clear();
    	} else {
	        size_t region_id = layerm - m_regions.begin();
	        if (done[region_id])
	            continue;
	        BOOST_LOG_TRIVIAL(trace) << "Generating perimeters for layer " << this->id() << ", region " << region_id;
	        done[region_id] = true;
	        const PrintRegionConfig &config = (*layerm)->region().config();

	        // find compatible regions
	        LayerRegionPtrs layerms;
	        layerms.push_back(*layerm);
	        for (LayerRegionPtrs::const_iterator it = layerm + 1; it != m_regions.end(); ++it)
	            if (! (*it)->slices.empty()) {
		            LayerRegion* other_layerm = *it;
		            const PrintRegionConfig &other_config = other_layerm->region().config();
		            if (config.wall_filament             == other_config.wall_filament
		                && config.wall_loops                  == other_config.wall_loops
		                && config.inner_wall_speed.get_at(get_extruder_id(config.wall_filament))  == other_config.inner_wall_speed.get_at(get_extruder_id(config.wall_filament))
		                && config.outer_wall_speed.get_at(get_extruder_id(config.wall_filament))  == other_config.outer_wall_speed.get_at(get_extruder_id(config.wall_filament))
		                && config.gap_infill_speed.get_at(get_extruder_id(config.wall_filament))  == other_config.gap_infill_speed.get_at(get_extruder_id(config.wall_filament))
		                && config.detect_overhang_wall                   == other_config.detect_overhang_wall
		                && config.filter_out_gap_fill.value == other_config.filter_out_gap_fill.value
		                && config.opt_serialize("inner_wall_line_width") == other_config.opt_serialize("inner_wall_line_width")
		                && config.detect_thin_wall                  == other_config.detect_thin_wall
		                && config.infill_wall_overlap              == other_config.infill_wall_overlap
                        && config.fuzzy_skin                  == other_config.fuzzy_skin
                        && config.fuzzy_skin_thickness        == other_config.fuzzy_skin_thickness
                        && config.fuzzy_skin_point_distance       == other_config.fuzzy_skin_point_distance
                        && config.seam_slope_conditional == other_config.seam_slope_conditional
                        //&& config.scarf_angle_threshold  == other_config.scarf_angle_threshold
                        && config.seam_slope_entire_loop  == other_config.seam_slope_entire_loop
                        && config.seam_slope_steps        == other_config.seam_slope_steps
                        && config.seam_slope_inner_walls  == other_config.seam_slope_inner_walls)
		            {
			 			other_layerm->perimeters.clear();
			 			other_layerm->fills.clear();
			 			other_layerm->thin_fills.clear();
		                layerms.push_back(other_layerm);
		                done[it - m_regions.begin()] = true;
		            }
		        }

	        if (layerms.size() == 1) {  // optimization
	            (*layerm)->fill_surfaces.surfaces.clear();
                (*layerm)->make_perimeters((*layerm)->slices, &(*layerm)->fill_surfaces, &(*layerm)->fill_no_overlap_expolygons, this->loop_nodes);

	            (*layerm)->fill_expolygons = to_expolygons((*layerm)->fill_surfaces.surfaces);
	        } else {
	            SurfaceCollection new_slices;
	            // Use the region with highest infill rate, as the make_perimeters() function below decides on the gap fill based on the infill existence.
	            LayerRegion *layerm_config = layerms.front();
	            {
	                // group slices (surfaces) according to number of extra perimeters
	                std::map<unsigned short, Surfaces> slices;  // extra_perimeters => [ surface, surface... ]
	                for (LayerRegion *layerm : layerms) {
	                    for (const Surface &surface : layerm->slices.surfaces)
	                        slices[surface.extra_perimeters].emplace_back(surface);
	                    if (layerm->region().config().sparse_infill_density > layerm_config->region().config().sparse_infill_density)
	                    	layerm_config = layerm;
	                }
	                // merge the surfaces assigned to each group
	                for (std::pair<const unsigned short,Surfaces> &surfaces_with_extra_perimeters : slices)
	                    new_slices.append(offset_ex(surfaces_with_extra_perimeters.second, ClipperSafetyOffset), surfaces_with_extra_perimeters.second.front());
	            }

	            // make perimeters
	            SurfaceCollection fill_surfaces;
                //BBS
                ExPolygons fill_no_overlap;
                layerm_config->make_perimeters(new_slices, &fill_surfaces, &fill_no_overlap, this->loop_nodes);

	            // assign fill_surfaces to each layer
	            if (!fill_surfaces.surfaces.empty()) {
	                for (LayerRegionPtrs::iterator l = layerms.begin(); l != layerms.end(); ++l) {
	                    // Separate the fill surfaces.
	                    ExPolygons expp = intersection_ex(fill_surfaces.surfaces, (*l)->slices.surfaces);
	                    (*l)->fill_expolygons = expp;
	                    (*l)->fill_surfaces.set(std::move(expp), fill_surfaces.surfaces.front());
                        //BBS: Separate fill_no_overlap
                        (*l)->fill_no_overlap_expolygons = intersection_ex((*l)->slices.surfaces, fill_no_overlap);
	                }
	            }
	        }
	    }

    BOOST_LOG_TRIVIAL(trace) << "Generating perimeters for layer " << this->id() << " - Done";
}

//BBS: use aabbtree to get distance
class ContinuitiousDistancer
{
    std::vector<Linef>                lines;
    AABBTreeIndirect::Tree<2, double> tree;

public:
    ContinuitiousDistancer(const Points &pts)
    {
        Lines pt_to_lines = to_lines(pts);
        for (const auto &line : pt_to_lines)
            lines.emplace_back(line.a.cast<double>(), line.b.cast<double>());

        tree = AABBTreeLines::build_aabb_tree_over_indexed_lines(lines);
    }

    float distance_from_perimeter(const Vec2f &point) const
    {
        Vec2d  p = point.cast<double>();
        size_t hit_idx_out{};
        Vec2d  hit_point_out = Vec2d::Zero();
        auto   distance      = AABBTreeLines::squared_distance_to_indexed_lines(lines, tree, p, hit_idx_out, hit_point_out);
        if (distance < 0) {
            return std::numeric_limits<float>::max();
        }

        distance          = sqrt(distance);
        const Linef &line = lines[hit_idx_out];
        Vec2d        v1   = line.b - line.a;
        Vec2d        v2   = p - line.a;
        if ((v1.x() * v2.y()) - (v1.y() * v2.x()) > 0.0) { distance *= -1; }
        return distance;
    }

    Lines to_lines(const Points &pts)
    {
        Lines lines;
        if (pts.size() >= 2) {
            lines.reserve(pts.size() - 1);
            for (Points::const_iterator it = pts.begin(); it != pts.end() - 1; ++it) { lines.push_back(Line(*it, *(it + 1))); }
        }
        return lines;
    }
};


static double get_node_continuity_rang_limit(const std::vector<coord_t> &Prev_node_widths, int prev_pt_idx) {
    double width = 0;
    double prev_width = Prev_node_widths.front();
    if (prev_pt_idx!=0 && prev_pt_idx < Prev_node_widths.size()) {
            prev_width = static_cast<double>(Prev_node_widths[prev_pt_idx]);
    }

    width = prev_width * dist_scale_threshold;
    return width;
}

void Layer::calculate_perimeter_continuity(std::vector<LoopNode> &prev_nodes) {
    for (size_t node_pos = 0; node_pos < loop_nodes.size(); ++node_pos) {
        LoopNode &node=loop_nodes[node_pos];
        double width = 0;
        ContinuitiousDistancer node_distancer(node.node_contour.pts);
        for (size_t prev_pos = 0; prev_pos < prev_nodes.size(); ++prev_pos) {
            LoopNode &prev_node = prev_nodes[prev_pos];

            // no overlap or has diff speed
            if (!node.bbox.overlap(prev_node.bbox))
                continue;

            //calculate dist, checkout the continuity
            Polyline continuitious_pl;
            //check start pt
            size_t start = 0;
            bool conntiouitious_flag = false;
            int end = prev_node.node_contour.pts.size() - 1;

            //if the countor is loop
            if (prev_node.node_contour.is_loop) {
                for (; end >= 0; --end) {
                    if (continuitious_pl.length() >= Continuitious_length) {
                        node.lower_node_id.push_back(prev_node.node_id);
                        prev_node.upper_node_id.push_back(node.node_id);
                        conntiouitious_flag = true;
                        break;
                    }

                    Point pt   = prev_node.node_contour.pts[end];
                    float dist = node_distancer.distance_from_perimeter(pt.cast<float>());
                    // get corr width
                    width = get_node_continuity_rang_limit(prev_node.node_contour.widths, end);

                    if (dist < width && dist > -width)
                        continuitious_pl.append_before(pt);
                    else
                        break;
                }

                if (conntiouitious_flag || end < 0)
                    continue;
            }

            int last_pt_idx = end;
            // line need to check end point
            if (!prev_node.node_contour.is_loop)
                last_pt_idx ++;

            for (; start < last_pt_idx; ++start) {
                Point pt   = prev_node.node_contour.pts[start];
                float dist = node_distancer.distance_from_perimeter(pt.cast<float>());
                //get corr width
                width = get_node_continuity_rang_limit(prev_node.node_contour.widths, start);

                if (dist < width && dist > -width) {
                    continuitious_pl.append(pt);
                    continue;
                }

                if (continuitious_pl.empty() || continuitious_pl.length() < Continuitious_length) {
                    continuitious_pl.clear();
                    continue;
                }

                node.lower_node_id.push_back(prev_node.node_id);
                prev_node.upper_node_id.push_back(node.node_id);
                continuitious_pl.clear();
                break;

            }

            if (continuitious_pl.length() >= Continuitious_length) {
                node.lower_node_id.push_back(prev_node.node_id);
                prev_node.upper_node_id.push_back(node.node_id);
            }
        }
    }

}

void Layer::recrod_cooling_node_for_each_extrusion() {
    for (LayerRegion *region : this->regions()) {
        for (int extrusion_idx = 0; extrusion_idx < region->perimeters.entities.size(); extrusion_idx++) {
            const auto *extrusions = static_cast<const ExtrusionEntityCollection *>(region->perimeters.entities[extrusion_idx]);
            int         start      = extrusions->loop_node_range.first;
            int         end        = extrusions->loop_node_range.second;
            if (start >= end)
                continue;

            int cooling_node = this->loop_nodes[start].merged_id;
            int pos          = this->loop_nodes[start].loop_id;
            int next_pos     = start + 1 < end ? this->loop_nodes[start + 1].loop_id : -1;
            for (int idx = 0; idx < extrusions->entities.size(); idx++) {
                if (idx == next_pos && next_pos > 0) {
                    start++;
                    cooling_node = this->loop_nodes[start].merged_id;
                    next_pos     = start + 1 < end ? this->loop_nodes[start + 1].loop_id : -1;
                }

                extrusions->entities[idx]->set_cooling_node(cooling_node);
            }

        }
    }
}

void Layer::export_region_slices_to_svg(const char *path) const
{
    BoundingBox bbox;
    for (const auto *region : m_regions)
        for (const auto &surface : region->slices.surfaces)
            bbox.merge(get_extents(surface.expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const auto *region : m_regions)
        for (const auto &surface : region->slices.surfaces)
            svg.draw(surface.expolygon, surface_type_to_color_name(surface.surface_type), transparency);
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void Layer::export_region_slices_to_svg_debug(const char *name) const
{
    static size_t idx = 0;
    this->export_region_slices_to_svg(debug_out_path("Layer-slices-%s-%d.svg", name, idx ++).c_str());
}

void Layer::export_region_fill_surfaces_to_svg(const char *path) const
{
    BoundingBox bbox;
    for (const auto *region : m_regions)
        for (const auto &surface : region->slices.surfaces)
            bbox.merge(get_extents(surface.expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const auto *region : m_regions)
        for (const auto &surface : region->slices.surfaces)
            svg.draw(surface.expolygon, surface_type_to_color_name(surface.surface_type), transparency);
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

//BBS: method to simplify support path
void Layer::simplify_support_entity_collection(ExtrusionEntityCollection* entity_collection)
{
    for (size_t i = 0; i < entity_collection->entities.size(); i++) {
        if (ExtrusionEntityCollection* collection = dynamic_cast<ExtrusionEntityCollection*>(entity_collection->entities[i]))
            this->simplify_support_entity_collection(collection);
        else if (ExtrusionPath* path = dynamic_cast<ExtrusionPath*>(entity_collection->entities[i]))
            this->simplify_support_path(path);
        else if (ExtrusionMultiPath* multipath = dynamic_cast<ExtrusionMultiPath*>(entity_collection->entities[i]))
            this->simplify_support_multi_path(multipath);
        else if (ExtrusionLoop* loop = dynamic_cast<ExtrusionLoop*>(entity_collection->entities[i]))
            this->simplify_support_loop(loop);
        else
            throw Slic3r::InvalidArgument("Invalid extrusion entity supplied to simplify_support_entity_collection()");
    }
}
//BBS: method to simplify support path
void Layer::simplify_support_path(ExtrusionPath * path)
{
    const auto print_config = this->object()->print()->config();
    const bool spiral_mode = print_config.spiral_mode;
    const bool enable_arc_fitting = print_config.enable_arc_fitting;
    const auto scaled_resolution = scaled<double>(print_config.resolution.value);

    if (enable_arc_fitting &&
        !spiral_mode) {
        path->simplify_by_fitting_arc(SCALED_SUPPORT_RESOLUTION);
    } else {
        path->simplify(scaled_resolution);
    }
}
//BBS: method to simplify support path
void Layer::simplify_support_multi_path(ExtrusionMultiPath* multipath)
{
    const auto print_config = this->object()->print()->config();
    const bool spiral_mode = print_config.spiral_mode;
    const bool enable_arc_fitting = print_config.enable_arc_fitting;
    const auto scaled_resolution = scaled<double>(print_config.resolution.value);

    for (size_t i = 0; i < multipath->paths.size(); ++i) {
        if (enable_arc_fitting &&
            !spiral_mode) {
            multipath->paths[i].simplify_by_fitting_arc(SCALED_SUPPORT_RESOLUTION);
        } else {
            multipath->paths[i].simplify(scaled_resolution);
        }
    }
}
//BBS: method to simplify support path
void Layer::simplify_support_loop(ExtrusionLoop* loop)
{
    const auto print_config = this->object()->print()->config();
    const bool spiral_mode = print_config.spiral_mode;
    const bool enable_arc_fitting = print_config.enable_arc_fitting;
    const auto scaled_resolution = scaled<double>(print_config.resolution.value);

    for (size_t i = 0; i < loop->paths.size(); ++i) {
        if (enable_arc_fitting &&
            !spiral_mode) {
            loop->paths[i].simplify_by_fitting_arc(SCALED_SUPPORT_RESOLUTION);
        } else {
            loop->paths[i].simplify(scaled_resolution);
        }
    }
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void Layer::export_region_fill_surfaces_to_svg_debug(const char *name) const
{
    static size_t idx = 0;
    this->export_region_fill_surfaces_to_svg(debug_out_path("Layer-fill_surfaces-%s-%d.svg", name, idx ++).c_str());
}

coordf_t Layer::get_sparse_infill_max_void_area()
{
    double max_void_area = 0.;
    for (auto layerm : m_regions) {
        Flow flow = layerm->flow(frInfill);
        float density = layerm->region().config().sparse_infill_density;
        InfillPattern pattern = layerm->region().config().sparse_infill_pattern;
        if (density == 0.)
            return -1;

        //BBS: rough estimation and need to be optimized
        double spacing = flow.scaled_spacing() * (100 - density) / density;
        switch (pattern) {
            case ipConcentric:
            case ipRectilinear:
            case ipLine:
            case ipGyroid:
            case ipAlignedRectilinear:
            case ipOctagramSpiral:
            case ipHilbertCurve:
            case ip3DHoneycomb:
            case ipArchimedeanChords:
                max_void_area = std::max(max_void_area, spacing * spacing);
                break;
            case ipGrid:
            case ipHoneycomb:
            case ipLightning:
                max_void_area = std::max(max_void_area, 4.0 * spacing * spacing);
                break;
            case ipCubic:
            case ipAdaptiveCubic:
            case ipTriangles:
            case ipStars:
            case ipSupportCubic:
                max_void_area = std::max(max_void_area, 4.5 * spacing * spacing);
                break;
            default:
                max_void_area = std::max(max_void_area, spacing * spacing);
                break;
        }
    };
    return max_void_area;
}

size_t Layer::get_extruder_id(unsigned int filament_id) const
{
    return m_object->print()->get_extruder_id(filament_id);
}

BoundingBox get_extents(const LayerRegion &layer_region)
{
    BoundingBox bbox;
    if (!layer_region.slices.surfaces.empty()) {
        bbox = get_extents(layer_region.slices.surfaces.front());
        for (auto it = layer_region.slices.surfaces.cbegin() + 1; it != layer_region.slices.surfaces.cend(); ++it)
            bbox.merge(get_extents(*it));
    }
    return bbox;
}

BoundingBox get_extents(const LayerRegionPtrs &layer_regions)
{
    BoundingBox bbox;
    if (!layer_regions.empty()) {
        bbox = get_extents(*layer_regions.front());
        for (auto it = layer_regions.begin() + 1; it != layer_regions.end(); ++it)
            bbox.merge(get_extents(**it));
    }
    return bbox;
}

}
