#include <assert.h>
#include <stdio.h>
#include <memory>

#include "../ClipperUtils.hpp"
#include "../Geometry.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"
#include "../PrintConfig.hpp"
#include "../Surface.hpp"

#include "FillBase.hpp"
#include "FillRectilinear.hpp"
#include "FillLightning.hpp"
#include "FillConcentricInternal.hpp"
#include "FillConcentric.hpp"
#include "FillFloatingConcentric.hpp"

#define NARROW_INFILL_AREA_THRESHOLD 3

namespace Slic3r {

struct SurfaceFillParams
{
	// Zero based extruder ID.
    unsigned int 	extruder = 0;
	// Infill pattern, adjusted for the density etc.
    InfillPattern  	pattern = InfillPattern(0);

    // FillBase
    // in unscaled coordinates
    coordf_t    	spacing = 0.;
    // infill / perimeter overlap, in unscaled coordinates
    coordf_t    	overlap = 0.;
    // Angle as provided by the region config, in radians.
    float       	angle = 0.f;
    // Is bridging used for this fill? Bridging parameters may be used even if this->flow.bridge() is not set.
    bool 			bridge;
    // Non-negative for a bridge.
    float 			bridge_angle = 0.f;

    // FillParams
    float       	density = 0.f;
    // Don't adjust spacing to fill the space evenly.
//    bool        	dont_adjust = false;
    // Length of the infill anchor along the perimeter line.
    // 1000mm is roughly the maximum length line that fits into a 32bit coord_t.
    float 			anchor_length     = 1000.f;
    float 			anchor_length_max = 1000.f;
    //BBS
    // width, height of extrusion, nozzle diameter, is bridge
    // For the output, for fill generator.
    Flow 			flow;

	// For the output
    ExtrusionRole	extrusion_role = ExtrusionRole(0);

	// Various print settings?

	// Index of this entry in a linear vector.
    size_t 			idx = 0;
	// infill speed settings
	float			sparse_infill_speed = 0;
	float			top_surface_speed = 0;
	float			solid_infill_speed = 0;
    float           infill_shift_step          = 0;// param for cross zag
    float           infill_rotate_step         = 0; // param for zig zag to get cross texture
    float           infill_lock_depth          = 0;
    float           skin_infill_depth          = 0;
    bool            symmetric_infill_y_axis = false;

	bool operator<(const SurfaceFillParams &rhs) const {
#define RETURN_COMPARE_NON_EQUAL(KEY) if (this->KEY < rhs.KEY) return true; if (this->KEY > rhs.KEY) return false;
#define RETURN_COMPARE_NON_EQUAL_TYPED(TYPE, KEY) if (TYPE(this->KEY) < TYPE(rhs.KEY)) return true; if (TYPE(this->KEY) > TYPE(rhs.KEY)) return false;

		// Sort first by decreasing bridging angle, so that the bridges are processed with priority when trimming one layer by the other.
		if (this->bridge_angle > rhs.bridge_angle) return true;
		if (this->bridge_angle < rhs.bridge_angle) return false;

		RETURN_COMPARE_NON_EQUAL(extruder);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, pattern);
		RETURN_COMPARE_NON_EQUAL(spacing);
		RETURN_COMPARE_NON_EQUAL(overlap);
		RETURN_COMPARE_NON_EQUAL(angle);
		RETURN_COMPARE_NON_EQUAL(density);
//		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, dont_adjust);
		RETURN_COMPARE_NON_EQUAL(anchor_length);
		RETURN_COMPARE_NON_EQUAL(anchor_length_max);
		RETURN_COMPARE_NON_EQUAL(flow.width());
		RETURN_COMPARE_NON_EQUAL(flow.height());
		RETURN_COMPARE_NON_EQUAL(flow.nozzle_diameter());
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, bridge);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, extrusion_role);
		RETURN_COMPARE_NON_EQUAL(sparse_infill_speed);
		RETURN_COMPARE_NON_EQUAL(top_surface_speed);
		RETURN_COMPARE_NON_EQUAL(solid_infill_speed);
		RETURN_COMPARE_NON_EQUAL(infill_shift_step);
		RETURN_COMPARE_NON_EQUAL(infill_rotate_step);
		RETURN_COMPARE_NON_EQUAL(symmetric_infill_y_axis);
		RETURN_COMPARE_NON_EQUAL(infill_lock_depth);
		RETURN_COMPARE_NON_EQUAL(skin_infill_depth);
		return false;
	}

	bool operator==(const SurfaceFillParams &rhs) const {
		return  this->extruder 			== rhs.extruder 		&&
				this->pattern 			== rhs.pattern 			&&
				this->spacing 			== rhs.spacing 			&&
				this->overlap 			== rhs.overlap 			&&
				this->angle   			== rhs.angle   			&&
				this->bridge   			== rhs.bridge   		&&
//				this->bridge_angle 		== rhs.bridge_angle		&&
				this->density   		== rhs.density   		&&
//				this->dont_adjust   	== rhs.dont_adjust 		&&
				this->anchor_length  	== rhs.anchor_length    &&
				this->anchor_length_max == rhs.anchor_length_max &&
				this->flow 				== rhs.flow 			&&
				this->extrusion_role	== rhs.extrusion_role	&&
				this->sparse_infill_speed	== rhs.sparse_infill_speed &&
				this->top_surface_speed		== rhs.top_surface_speed &&
				this->solid_infill_speed	== rhs.solid_infill_speed &&
				this->infill_shift_step             == rhs.infill_shift_step &&
				this->infill_rotate_step            == rhs.infill_rotate_step &&
				this->symmetric_infill_y_axis	== rhs.symmetric_infill_y_axis &&
				this->infill_lock_depth      ==  rhs.infill_lock_depth &&
				this->skin_infill_depth      ==  rhs.skin_infill_depth;
	}
};

struct SurfaceFill {
	SurfaceFill(const SurfaceFillParams& params) : region_id(size_t(-1)), surface(stCount, ExPolygon()), params(params) {}

	size_t 				region_id;
	Surface 			surface;
	ExPolygons       	expolygons;
	SurfaceFillParams	params;
    // BBS
    std::vector<size_t> region_id_group;
    ExPolygons          no_overlap_expolygons;
};

// BBS: used to judge whether the internal solid infill area is narrow
static bool is_narrow_infill_area(const ExPolygon& expolygon)
{
	ExPolygons offsets = offset_ex(expolygon, -scale_(NARROW_INFILL_AREA_THRESHOLD));
	if (offsets.empty())
		return true;

	return false;
}

std::vector<SurfaceFill> group_fills(const Layer &layer, LockRegionParam &lock_param)
{
	std::vector<SurfaceFill> surface_fills;
	// Fill in a map of a region & surface to SurfaceFillParams.
	std::set<SurfaceFillParams> 						set_surface_params;
	std::vector<std::vector<const SurfaceFillParams*>> 	region_to_surface_params(layer.regions().size(), std::vector<const SurfaceFillParams*>());
    SurfaceFillParams									params;
    bool 												has_internal_voids = false;
	const PrintObjectConfig&							object_config = layer.object()->config();

	auto append_flow_param = [](std::map<Flow, ExPolygons> &flow_params, Flow flow, const ExPolygon &exp) {
        auto it = flow_params.find(flow);
        if (it == flow_params.end())
            flow_params.insert({flow, {exp}});
        else
            it->second.push_back(exp);
        it++;
    };

	auto append_density_param = [](std::map<float, ExPolygons> &density_params, float density, const ExPolygon &exp) {
        auto it = density_params.find(density);
        if (it == density_params.end())
            density_params.insert({density, {exp}});
        else
            it->second.push_back(exp);
        it++;
    };

	for (size_t region_id = 0; region_id < layer.regions().size(); ++ region_id) {
		const LayerRegion  &layerm = *layer.regions()[region_id];
		region_to_surface_params[region_id].assign(layerm.fill_surfaces.size(), nullptr);
	    for (const Surface &surface : layerm.fill_surfaces.surfaces)
	        if (surface.surface_type == stInternalVoid)
	        	has_internal_voids = true;
	        else {
		        const PrintRegionConfig &region_config = layerm.region().config();
		        FlowRole extrusion_role = surface.is_top() ? frTopSolidInfill : (surface.is_solid() ? frSolidInfill : frInfill);
		        bool     is_bridge 	    = layer.id() > 0 && surface.is_bridge();
		        params.extruder 	 = layerm.region().extruder(extrusion_role);
		        params.pattern 		 = region_config.sparse_infill_pattern.value;
		        params.density       = float(region_config.sparse_infill_density);
                if (params.pattern == ipLockedZag) {
                    params.infill_lock_depth = scale_(region_config.infill_lock_depth);
                    params.skin_infill_depth = scale_(region_config.skin_infill_depth);

				}
            if (params.pattern == ipCrossZag || params.pattern == ipLockedZag){
                params.infill_shift_step     = scale_(region_config.infill_shift_step);
                params.symmetric_infill_y_axis  = region_config.symmetric_infill_y_axis;
            }else if (params.pattern == ipZigZag){
                params.infill_rotate_step    =  region_config.infill_rotate_step * M_PI / 360;
								params.symmetric_infill_y_axis  = region_config.symmetric_infill_y_axis;
            }

				if (surface.is_solid()) {
		            params.density = 100.f;
					//FIXME for non-thick bridges, shall we allow a bottom surface pattern?
					if (surface.is_floating_vertical_shell())
						params.pattern = InfillPattern::ipFloatingConcentric;
					else if (surface.is_solid_infill())
                        params.pattern = region_config.internal_solid_infill_pattern.value;
					else if (surface.is_external() && !is_bridge)
						params.pattern = surface.is_top() ? region_config.top_surface_pattern.value : region_config.bottom_surface_pattern.value;
					else
						params.pattern = region_config.top_surface_pattern == ipMonotonic ? ipMonotonic : ipRectilinear;

		        } else if (params.density <= 0)
		            continue;

		        params.extrusion_role =
		            is_bridge ?
		                erBridgeInfill :
		                (surface.is_solid() ?
		                    (surface.is_top() ? erTopSolidInfill : (surface.is_bottom()? erBottomSurface : surface.is_floating_vertical_shell()?erFloatingVerticalShell:erSolidInfill)) :
		                    erInternalInfill);
		        params.bridge_angle = float(surface.bridge_angle);
		        params.angle 		= float(Geometry::deg2rad(region_config.infill_direction.value));

		        // Calculate the actual flow we'll be using for this infill.
		        params.bridge = is_bridge || Fill::use_bridge_flow(params.pattern);
				params.flow   = params.bridge ?
					//BBS: always enable thick bridge for internal bridge
					layerm.bridging_flow(extrusion_role, (surface.is_bridge() && !surface.is_external()) || object_config.thick_bridges) :
					layerm.flow(extrusion_role, (surface.thickness == -1) ? layer.height : surface.thickness);
				//BBS: record speed params
                if (!params.bridge) {
                    if (params.extrusion_role == erInternalInfill)
                        params.sparse_infill_speed = region_config.sparse_infill_speed.get_at(layer.get_extruder_id(params.extruder));
                    else if (params.extrusion_role == erTopSolidInfill)
                        params.top_surface_speed = region_config.top_surface_speed.get_at(layer.get_extruder_id(params.extruder));
                    else if (params.extrusion_role == erSolidInfill)
                        params.solid_infill_speed = region_config.internal_solid_infill_speed.get_at(layer.get_extruder_id(params.extruder));
					else if (params.extrusion_role == erFloatingVerticalShell)
						params.solid_infill_speed = region_config.bridge_speed.get_at(layer.get_extruder_id(params.extruder));
                }
				// Calculate flow spacing for infill pattern generation.
		        if (surface.is_solid() || is_bridge) {
		            params.spacing = params.flow.spacing();
		            // Don't limit anchor length for solid or bridging infill.
		            params.anchor_length = 1000.f;
					params.anchor_length_max = 1000.f;
		        } else {
					// Internal infill. Calculating infill line spacing independent of the current layer height and 1st layer status,
					// so that internall infill will be aligned over all layers of the current region.
		            params.spacing = layerm.region().flow(*layer.object(), frInfill, layer.object()->config().layer_height, false).spacing();
		            // Anchor a sparse infill to inner perimeters with the following anchor length:
					params.anchor_length = float(region_config.sparse_infill_anchor);
					if (region_config.sparse_infill_anchor.percent)
						params.anchor_length = float(params.anchor_length * 0.01 * params.spacing);
					params.anchor_length_max = float(region_config.sparse_infill_anchor_max);
					if (region_config.sparse_infill_anchor_max.percent)
						params.anchor_length_max = float(params.anchor_length_max * 0.01 * params.spacing);
					params.anchor_length = std::min(params.anchor_length, params.anchor_length_max);
				}

				//get locked region param
				if (params.pattern == ipLockedZag){
					const PrintObject *object = layerm.layer()->object();
					auto nozzle_diameter = float(object->print()->config().nozzle_diameter.get_at(layerm.region().extruder(extrusion_role) - 1));
					Flow skin_flow = params.bridge ? params.flow : Flow::new_from_config_width(extrusion_role, region_config.skin_infill_line_width, nozzle_diameter, float((surface.thickness == -1) ? layer.height : surface.thickness));
					//add skin flow
					append_flow_param(lock_param.skin_flow_params, skin_flow, surface.expolygon);

					Flow skeleton_flow = params.bridge ? params.flow : Flow::new_from_config_width(extrusion_role, region_config.skeleton_infill_line_width, nozzle_diameter, float((surface.thickness == -1) ? layer.height : surface.thickness)) ;
					// add skeleton flow
					append_flow_param(lock_param.skeleton_flow_params, skeleton_flow, surface.expolygon);

					// add skin density
					append_density_param(lock_param.skin_density_params, float(0.01 * region_config.skin_infill_density), surface.expolygon);

					// add skin density
					append_density_param(lock_param.skeleton_density_params, float(0.01 * region_config.skeleton_infill_density), surface.expolygon);

				}

                auto it_params = set_surface_params.find(params);

		        if (it_params == set_surface_params.end())
		        	it_params = set_surface_params.insert(it_params, params);
		        region_to_surface_params[region_id][&surface - &layerm.fill_surfaces.surfaces.front()] = &(*it_params);
		    }
	}

	surface_fills.reserve(set_surface_params.size());
	for (const SurfaceFillParams &params : set_surface_params) {
		const_cast<SurfaceFillParams&>(params).idx = surface_fills.size();
		surface_fills.emplace_back(params);
	}

	for (size_t region_id = 0; region_id < layer.regions().size(); ++ region_id) {
		const LayerRegion &layerm = *layer.regions()[region_id];
	    for (const Surface &surface : layerm.fill_surfaces.surfaces)
	        if (surface.surface_type != stInternalVoid) {
	        	const SurfaceFillParams *params = region_to_surface_params[region_id][&surface - &layerm.fill_surfaces.surfaces.front()];
				if (params != nullptr) {
	        		SurfaceFill &fill = surface_fills[params->idx];
                    if (fill.region_id == size_t(-1)) {
	        			fill.region_id = region_id;
	        			fill.surface = surface;
	        			fill.expolygons.emplace_back(std::move(fill.surface.expolygon));
						//BBS
						fill.region_id_group.push_back(region_id);
						fill.no_overlap_expolygons = layerm.fill_no_overlap_expolygons;
					} else {
						fill.expolygons.emplace_back(surface.expolygon);
						//BBS
						auto t = find(fill.region_id_group.begin(), fill.region_id_group.end(), region_id);
						if (t == fill.region_id_group.end()) {
							fill.region_id_group.push_back(region_id);
							fill.no_overlap_expolygons = union_ex(fill.no_overlap_expolygons, layerm.fill_no_overlap_expolygons);
						}
					}
				}
	        }
	}

	{
		Polygons all_polygons;
		for (SurfaceFill &fill : surface_fills)
			if (! fill.expolygons.empty()) {
				if (fill.expolygons.size() > 1 || ! all_polygons.empty()) {
					Polygons polys = to_polygons(std::move(fill.expolygons));
		            // Make a union of polygons, use a safety offset, subtract the preceding polygons.
				    // Bridges are processed first (see SurfaceFill::operator<())
		            fill.expolygons = all_polygons.empty() ? union_safety_offset_ex(polys) : diff_ex(polys, all_polygons, ApplySafetyOffset::Yes);
					append(all_polygons, std::move(polys));
				} else if (&fill != &surface_fills.back())
					append(all_polygons, to_polygons(fill.expolygons));
	        }
	}

    // we need to detect any narrow surfaces that might collapse
    // when adding spacing below
    // such narrow surfaces are often generated in sloping walls
    // by bridge_over_infill() and combine_infill() as a result of the
    // subtraction of the combinable area from the layer infill area,
    // which leaves small areas near the perimeters
    // we are going to grow such regions by overlapping them with the void (if any)
    // TODO: detect and investigate whether there could be narrow regions without
    // any void neighbors
    if (has_internal_voids) {
    	// Internal voids are generated only if "infill_only_where_needed" or "infill_every_layers" are active.
        coord_t  distance_between_surfaces = 0;
        Polygons surfaces_polygons;
        Polygons voids;
		int      region_internal_infill = -1;
		int		 region_solid_infill = -1;
		int		 region_some_infill = -1;
    	for (SurfaceFill &surface_fill : surface_fills)
			if (! surface_fill.expolygons.empty()) {
    			distance_between_surfaces = std::max(distance_between_surfaces, surface_fill.params.flow.scaled_spacing());
				append((surface_fill.surface.surface_type == stInternalVoid) ? voids : surfaces_polygons, to_polygons(surface_fill.expolygons));
				if (surface_fill.surface.surface_type == stInternalSolid)
					region_internal_infill = (int)surface_fill.region_id;
				if (surface_fill.surface.is_solid())
					region_solid_infill = (int)surface_fill.region_id;
				if (surface_fill.surface.surface_type != stInternalVoid)
					region_some_infill = (int)surface_fill.region_id;
			}
    	if (! voids.empty() && ! surfaces_polygons.empty()) {
    		// First clip voids by the printing polygons, as the voids were ignored by the loop above during mutual clipping.
    		voids = diff(voids, surfaces_polygons);
	        // Corners of infill regions, which would not be filled with an extrusion path with a radius of distance_between_surfaces/2
	        Polygons collapsed = diff(
	            surfaces_polygons,
				opening(surfaces_polygons, float(distance_between_surfaces /2), float(distance_between_surfaces / 2 + ClipperSafetyOffset)));
	        //FIXME why the voids are added to collapsed here? First it is expensive, second the result may lead to some unwanted regions being
	        // added if two offsetted void regions merge.
	        // polygons_append(voids, collapsed);
	        ExPolygons extensions = intersection_ex(expand(collapsed, float(distance_between_surfaces)), voids, ApplySafetyOffset::Yes);
	        // Now find an internal infill SurfaceFill to add these extrusions to.
	        SurfaceFill *internal_solid_fill = nullptr;
			unsigned int region_id = 0;
			if (region_internal_infill != -1)
				region_id = region_internal_infill;
			else if (region_solid_infill != -1)
				region_id = region_solid_infill;
			else if (region_some_infill != -1)
				region_id = region_some_infill;
			const LayerRegion& layerm = *layer.regions()[region_id];
	        for (SurfaceFill &surface_fill : surface_fills)
	        	if (surface_fill.surface.surface_type == stInternalSolid && std::abs(layer.height - surface_fill.params.flow.height()) < EPSILON) {
	        		internal_solid_fill = &surface_fill;
	        		break;
	        	}
	        if (internal_solid_fill == nullptr) {
	        	// Produce another solid fill.
		        params.extruder 	 = layerm.region().extruder(frSolidInfill);
	            params.pattern 		 = layerm.region().config().top_surface_pattern == ipMonotonic ? ipMonotonic : ipRectilinear;
	            params.density 		 = 100.f;
		        params.extrusion_role = erInternalInfill;
		        params.angle 		= float(Geometry::deg2rad(layerm.region().config().infill_direction.value));
		        // calculate the actual flow we'll be using for this infill
				params.flow = layerm.flow(frSolidInfill);
		        params.spacing = params.flow.spacing();
				surface_fills.emplace_back(params);
				surface_fills.back().surface.surface_type = stInternalSolid;
				surface_fills.back().surface.thickness = layer.height;
				surface_fills.back().expolygons = std::move(extensions);
	        } else {
	        	append(extensions, std::move(internal_solid_fill->expolygons));
	        	internal_solid_fill->expolygons = union_ex(extensions);
	        }
		}
    }

	// BBS: detect narrow internal solid infill area and use ipConcentricInternal pattern instead
	if (layer.object()->config().detect_narrow_internal_solid_infill) {
		const coordf_t narrow_threshold = scale_(NARROW_INFILL_AREA_THRESHOLD) * 2;
		ExPolygons lower_internal_areas;
		BoundingBox lower_internal_bbox;
		if (layer.lower_layer) {
			for (auto layerm : layer.lower_layer->regions()) {
				auto internal_surfaces = layerm->fill_surfaces.filter_by_types({ stInternal,stInternalVoid });
				for (auto surface : internal_surfaces)
					lower_internal_areas.push_back(surface->expolygon);
			}
			lower_internal_bbox = get_extents(lower_internal_areas);
		}
		size_t surface_fills_size = surface_fills.size();
		for (size_t i = 0; i < surface_fills_size; i++) {
			if (surface_fills[i].surface.surface_type != stInternalSolid)
				continue;

			size_t expolygons_size = surface_fills[i].expolygons.size();
			std::vector<size_t> narrow_expoly_idx;
			std::vector<size_t> narrow_floating_expoly_idx;
			std::vector<bool> use_floating_filler;
			// BBS: get the index list of narrow expolygon
			for (size_t j = 0; j < expolygons_size; j++) {
				auto bbox = get_extents(surface_fills[i].expolygons[j]);
				auto clipped_internals = ClipperUtils::clip_clipper_polygons_with_subject_bbox(lower_internal_areas, bbox.inflated(scale_(2))); // expand a little
				auto clipped_internal_bbox = get_extents(clipped_internals);
				if (is_narrow_infill_area(surface_fills[i].expolygons[j])) {
					if (!clipped_internals.empty() && bbox.overlap(clipped_internal_bbox) && !intersection_ex(offset_ex(surface_fills[i].expolygons[j],SCALED_EPSILON), clipped_internals).empty()) {
						narrow_floating_expoly_idx.emplace_back(j);
					}
					else {
						narrow_expoly_idx.emplace_back(j);
					}
				}
			}

			if (narrow_expoly_idx.empty() && narrow_floating_expoly_idx.empty()) {
				// BBS: has no narrow expolygon
				continue;
			}
			else if (narrow_floating_expoly_idx.size() == expolygons_size) {
				surface_fills[i].params.pattern = ipFloatingConcentric;
				surface_fills[i].params.extrusion_role = erFloatingVerticalShell;
				surface_fills[i].surface.surface_type = stFloatingVerticalShell;
			}
			else if (narrow_expoly_idx.size() == expolygons_size) {
				surface_fills[i].params.pattern = ipConcentricInternal;
			}
			else {
				// BBS: some expolygons are narrow, spilit surface_fills[i] and rearrange the expolygons
				if (!narrow_expoly_idx.empty()) {
					params = surface_fills[i].params;
					params.pattern = ipConcentricInternal;
					surface_fills.emplace_back(params);
					surface_fills.back().region_id = surface_fills[i].region_id;
					surface_fills.back().surface.surface_type = stInternalSolid;
					surface_fills.back().surface.thickness = surface_fills[i].surface.thickness;
					surface_fills.back().region_id_group = surface_fills[i].region_id_group;
					surface_fills.back().no_overlap_expolygons = surface_fills[i].no_overlap_expolygons;
					for (size_t j = 0; j < narrow_expoly_idx.size(); j++) {
						// BBS: move the narrow expolygons to new surface_fills.back();
						surface_fills.back().expolygons.emplace_back(std::move(surface_fills[i].expolygons[narrow_expoly_idx[j]]));
					}
				}

				if (!narrow_floating_expoly_idx.empty()) {
					params = surface_fills[i].params;
					params.pattern = ipFloatingConcentric;
					params.extrusion_role = erFloatingVerticalShell;
					surface_fills.emplace_back(params);
					surface_fills.back().region_id = surface_fills[i].region_id;
					surface_fills.back().surface.surface_type = stFloatingVerticalShell;
					surface_fills.back().surface.thickness = surface_fills[i].surface.thickness;
					surface_fills.back().region_id_group = surface_fills[i].region_id_group;
					surface_fills.back().no_overlap_expolygons = surface_fills[i].no_overlap_expolygons;
					for (size_t j = 0; j < narrow_floating_expoly_idx.size(); j++) {
						// BBS: move the narrow expolygons to new surface_fills.back();
						surface_fills.back().expolygons.emplace_back(std::move(surface_fills[i].expolygons[narrow_floating_expoly_idx[j]]));
					}
				}

				std::vector<size_t> to_be_delete = narrow_floating_expoly_idx;
				to_be_delete.insert(to_be_delete.end(), narrow_expoly_idx.begin(), narrow_expoly_idx.end());
				std::sort(to_be_delete.begin(), to_be_delete.end());

				for (int j = to_be_delete.size() - 1; j >= 0; j--) {
					// BBS: delete the narrow expolygons from old surface_fills
					surface_fills[i].expolygons.erase(surface_fills[i].expolygons.begin() + to_be_delete[j]);
				}
			}
		}
	}

	return surface_fills;
}

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
void export_group_fills_to_svg(const char *path, const std::vector<SurfaceFill> &fills)
{
    BoundingBox bbox;
    for (const auto &fill : fills)
        for (const auto &expoly : fill.expolygons)
            bbox.merge(get_extents(expoly));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const auto &fill : fills)
        for (const auto &expoly : fill.expolygons)
            svg.draw(expoly, surface_type_to_color_name(fill.surface.surface_type), transparency);
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}
#endif

// friend to Layer
void Layer::make_fills(FillAdaptive::Octree* adaptive_fill_octree, FillAdaptive::Octree* support_fill_octree, FillLightning::Generator* lightning_generator)
{
	for (LayerRegion *layerm : m_regions)
		layerm->fills.clear();

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
//	this->export_region_fill_surfaces_to_svg_debug("10_fill-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
    LockRegionParam lock_param;
    std::vector<SurfaceFill>     surface_fills = group_fills(*this, lock_param);
	const Slic3r::BoundingBox bbox 			= this->object()->bounding_box();
	const auto                resolution 	= this->object()->print()->config().resolution.value;

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
	{
		static int iRun = 0;
		export_group_fills_to_svg(debug_out_path("Layer-fill_surfaces-10_fill-final-%d.svg", iRun ++).c_str(), surface_fills);
	}
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    for (SurfaceFill &surface_fill : surface_fills) {
        // Create the filler object.
        std::unique_ptr<Fill> f = std::unique_ptr<Fill>(Fill::new_from_type(surface_fill.params.pattern));
        f->set_bounding_box(bbox);
        f->layer_id = this->id();
        f->z 		= this->print_z;
        f->angle 	= surface_fill.params.angle;
        f->adapt_fill_octree = (surface_fill.params.pattern == ipSupportCubic) ? support_fill_octree : adaptive_fill_octree;
        if (surface_fill.params.pattern == ipZigZag) {
            if (f->layer_id % 2 == 0)
                f->angle -= surface_fill.params.infill_rotate_step * (f->layer_id / 2);
            else
                f->angle += surface_fill.params.infill_rotate_step * (f->layer_id / 2);
        }
		if (surface_fill.params.pattern == ipConcentricInternal) {
            FillConcentricInternal *fill_concentric = dynamic_cast<FillConcentricInternal *>(f.get());
            assert(fill_concentric != nullptr);
            fill_concentric->print_config        = &this->object()->print()->config();
            fill_concentric->print_object_config = &this->object()->config();
        } else if (surface_fill.params.pattern == ipConcentric) {
            FillConcentric *fill_concentric = dynamic_cast<FillConcentric *>(f.get());
            assert(fill_concentric != nullptr);
            fill_concentric->print_config = &this->object()->print()->config();
            fill_concentric->print_object_config = &this->object()->config();
        } else if (surface_fill.params.pattern == ipLightning){
            dynamic_cast<FillLightning::Filler*>(f.get())->generator = lightning_generator;
		}
		else if (surface_fill.params.pattern == ipFloatingConcentric) {
			FillFloatingConcentric* fill_contour = dynamic_cast<FillFloatingConcentric*>(f.get());
			assert(fill_contour != nullptr);
			ExPolygons lower_unsuporrt_expolys;
			Polygons lower_sparse_polys;
			if (lower_layer) {
				for (LayerRegion* layerm : lower_layer->regions()) {
					auto surfaces = layerm->fill_surfaces.filter_by_types({ stInternal,stInternalVoid });
					ExPolygons sexpolys;
					for (auto surface : surfaces) {
						sexpolys.push_back(surface->expolygon);
					}
					sexpolys = union_ex(sexpolys);
					lower_unsuporrt_expolys = union_ex(lower_unsuporrt_expolys, sexpolys);
				}
				lower_unsuporrt_expolys = shrink_ex(lower_unsuporrt_expolys, SCALED_EPSILON);
        LockRegionParam temp_skin_inner_param;
				std::vector<SurfaceFill> lower_fills = group_fills(*lower_layer, temp_skin_inner_param);
				bool detect_lower_sparse_lines = true;
				for (auto& fill : lower_fills) {
					if (fill.params.pattern == ipAdaptiveCubic || fill.params.pattern == ipLightning || fill.params.pattern == ipSupportCubic) {
						detect_lower_sparse_lines = false;
						break;
					}
				}
				if (detect_lower_sparse_lines) {
					float internal_infill_width = 0;
					for (auto layerm : lower_layer->regions())
						internal_infill_width += layerm->flow(frInfill).scaled_width();
					internal_infill_width /= lower_layer->m_regions.size();
					Polylines lower_sparse_lines = lower_layer->generate_sparse_infill_polylines_for_anchoring(nullptr, nullptr, nullptr);
					lower_sparse_polys = offset(lower_sparse_lines, internal_infill_width / 2);
					lower_sparse_polys = union_(lower_sparse_polys);
				}
			}
			fill_contour->lower_sparse_polys = lower_sparse_polys;
			fill_contour->lower_layer_unsupport_areas = lower_unsuporrt_expolys;
			fill_contour->print_config = &this->object()->print()->config();
			fill_contour->print_object_config = &this->object()->config();
		}
        // calculate flow spacing for infill pattern generation
        bool using_internal_flow = ! surface_fill.surface.is_solid() && ! surface_fill.params.bridge;
        double link_max_length = 0.;
        if (! surface_fill.params.bridge) {
#if 0
            link_max_length = layerm.region()->config().get_abs_value(surface.is_external() ? "external_fill_link_max_length" : "fill_link_max_length", flow.spacing());
//            printf("flow spacing: %f,  is_external: %d, link_max_length: %lf\n", flow.spacing(), int(surface.is_external()), link_max_length);
#else
            if (surface_fill.params.density > 80.) // 80%
                link_max_length = 3. * f->spacing;
#endif
        }

        // Maximum length of the perimeter segment linking two infill lines.
        f->link_max_length = (coord_t)scale_(link_max_length);
        // Used by the concentric infill pattern to clip the loops to create extrusion paths.
        f->loop_clipping = coord_t(scale_(surface_fill.params.flow.nozzle_diameter()) * LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER);

        // apply half spacing using this flow's own spacing and generate infill
        FillParams params;
        params.density 		     = float(0.01 * surface_fill.params.density);
		params.dont_adjust		 = false; //  surface_fill.params.dont_adjust;
        params.anchor_length     = surface_fill.params.anchor_length;
		params.anchor_length_max = surface_fill.params.anchor_length_max;
		params.resolution        = resolution;
		params.use_arachne = surface_fill.params.pattern == ipConcentric || surface_fill.params.pattern == ipFloatingConcentric;
		params.layer_height      = m_regions[surface_fill.region_id]->layer()->height;

		// BBS
		params.flow = surface_fill.params.flow;
		params.extrusion_role = surface_fill.params.extrusion_role;
		params.using_internal_flow = using_internal_flow;
		params.no_extrusion_overlap = surface_fill.params.overlap;
		if( surface_fill.params.pattern == ipLockedZag ) {
			params.locked_zag = true;
            params.infill_lock_depth = surface_fill.params.infill_lock_depth;
            params.skin_infill_depth = surface_fill.params.skin_infill_depth;
            f->set_lock_region_param(lock_param);
		}
        if (surface_fill.params.pattern == ipCrossZag || surface_fill.params.pattern == ipLockedZag) {
            if (f->layer_id % 2 == 0) {
                params.horiz_move -= surface_fill.params.infill_shift_step * (f->layer_id / 2);
            } else {
                params.horiz_move += surface_fill.params.infill_shift_step * (f->layer_id / 2);
            }

            params.symmetric_infill_y_axis = surface_fill.params.symmetric_infill_y_axis;

        } else if( surface_fill.params.pattern == ipZigZag ) {
			params.symmetric_infill_y_axis = surface_fill.params.symmetric_infill_y_axis;
		}

		if (surface_fill.params.pattern == ipGrid || surface_fill.params.pattern == ipFloatingConcentric)
			params.can_reverse = false;
		LayerRegion* layerm = this->m_regions[surface_fill.region_id];
		for (ExPolygon& expoly : surface_fill.expolygons) {

      f->no_overlap_expolygons = intersection_ex(surface_fill.no_overlap_expolygons, ExPolygons() = {expoly}, ApplySafetyOffset::Yes);
            if (params.symmetric_infill_y_axis) {
                params.symmetric_y_axis = f->extended_object_bounding_box().center().x();
                expoly.symmetric_y(params.symmetric_y_axis);
            }

			// Spacing is modified by the filler to indicate adjustments. Reset it for each expolygon.
			f->spacing = surface_fill.params.spacing;
			surface_fill.surface.expolygon = std::move(expoly);
			// BBS: make fill
			f->fill_surface_extrusion(&surface_fill.surface, params, m_regions[surface_fill.region_id]->fills.entities);
		}
    }

    // add thin fill regions
    // Unpacks the collection, creates multiple collections per path.
    // The path type could be ExtrusionPath, ExtrusionLoop or ExtrusionEntityCollection.
    // Why the paths are unpacked?
	for (LayerRegion *layerm : m_regions)
	    for (const ExtrusionEntity *thin_fill : layerm->thin_fills.entities) {
	        ExtrusionEntityCollection &collection = *(new ExtrusionEntityCollection());
	        layerm->fills.entities.push_back(&collection);
	        collection.entities.push_back(thin_fill->clone());
	    }

#ifndef NDEBUG
	for (LayerRegion *layerm : m_regions)
	    for (size_t i = 0; i < layerm->fills.entities.size(); ++ i)
    	    assert(dynamic_cast<ExtrusionEntityCollection*>(layerm->fills.entities[i]) != nullptr);
#endif
}

Polylines Layer::generate_sparse_infill_polylines_for_anchoring(FillAdaptive::Octree* adaptive_fill_octree, FillAdaptive::Octree* support_fill_octree, FillLightning::Generator* lightning_generator) const
{
    LockRegionParam skin_inner_param;
    std::vector<SurfaceFill> surface_fills = group_fills(*this, skin_inner_param);
	const Slic3r::BoundingBox bbox = this->object()->bounding_box();
	const auto                resolution = this->object()->print()->config().resolution.value;

	Polylines sparse_infill_polylines{};

	for (SurfaceFill& surface_fill : surface_fills) {
		if (surface_fill.surface.surface_type != stInternal) {
			continue;
		}

		switch (surface_fill.params.pattern) {
		case ipCount: continue; break;
		case ipSupportBase: continue; break;
		//case ipEnsuring: continue; break;
		case ipLightning:
		case ipAdaptiveCubic:
        case ipSupportCubic:
        case ipRectilinear:
        case ipMonotonic:
        case ipAlignedRectilinear:
        case ipGrid:
        case ipTriangles:
        case ipStars:
        case ipCubic:
        case ipLine:
        case ipConcentric:
        case ipHoneycomb:
        case ip3DHoneycomb:
        case ipGyroid:
        case ipHilbertCurve:
        case ipArchimedeanChords:
        case ipOctagramSpiral:
        case ipZigZag:
        case ipCrossZag:
		case ipLockedZag: break;
        }

		// Create the filler object.
		std::unique_ptr<Fill> f = std::unique_ptr<Fill>(Fill::new_from_type(surface_fill.params.pattern));
		f->set_bounding_box(bbox);
		f->layer_id = this->id() - this->object()->get_layer(0)->id(); // We need to subtract raft layers.
		f->z = this->print_z;
		f->angle = surface_fill.params.angle;
		f->adapt_fill_octree = (surface_fill.params.pattern == ipSupportCubic) ? support_fill_octree : adaptive_fill_octree;


		if (surface_fill.params.pattern == ipLightning)
			dynamic_cast<FillLightning::Filler*>(f.get())->generator = lightning_generator;

		// calculate flow spacing for infill pattern generation
		double link_max_length = 0.;
		if (!surface_fill.params.bridge) {
#if 0
			link_max_length = layerm.region()->config().get_abs_value(surface.is_external() ? "external_fill_link_max_length" : "fill_link_max_length", flow.spacing());
			//            printf("flow spacing: %f,  is_external: %d, link_max_length: %lf\n", flow.spacing(), int(surface.is_external()), link_max_length);
#else
			if (surface_fill.params.density > 80.) // 80%
				link_max_length = 3. * f->spacing;
#endif
		}

		// Maximum length of the perimeter segment linking two infill lines.
		f->link_max_length = (coord_t)scale_(link_max_length);
		// Used by the concentric infill pattern to clip the loops to create extrusion paths.
		f->loop_clipping = coord_t(scale_(surface_fill.params.flow.nozzle_diameter()) * LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER);

		LayerRegion& layerm = *m_regions[surface_fill.region_id];

		// apply half spacing using this flow's own spacing and generate infill
		FillParams params;
		params.density = float(0.01 * surface_fill.params.density);
		params.dont_adjust = false; //  surface_fill.params.dont_adjust;
		params.anchor_length = surface_fill.params.anchor_length;
		params.anchor_length_max = surface_fill.params.anchor_length_max;
		params.resolution = resolution;
		params.use_arachne = false;
		params.layer_height = layerm.layer()->height;

		for (ExPolygon& expoly : surface_fill.expolygons) {
			// Spacing is modified by the filler to indicate adjustments. Reset it for each expolygon.
			f->spacing = surface_fill.params.spacing;
			surface_fill.surface.expolygon = std::move(expoly);
			try {
				Polylines polylines = f->fill_surface(&surface_fill.surface, params);
				sparse_infill_polylines.insert(sparse_infill_polylines.end(), polylines.begin(), polylines.end());
			}
			catch (InfillFailedException&) {}
		}
	}

	return sparse_infill_polylines;
}


// Create ironing extrusions over top surfaces.
void Layer::make_ironing()
{
	// LayerRegion::slices contains surfaces marked with SurfaceType.
	// Here we want to collect top surfaces extruded with the same extruder.
	// A surface will be ironed with the same extruder to not contaminate the print with another material leaking from the nozzle.

	// First classify regions based on the extruder used.
	struct IroningParams {
		InfillPattern pattern;
		int 		extruder 	= -1;
		bool 		just_infill = false;
		// Spacing of the ironing lines, also to calculate the extrusion flow from.
		double 		line_spacing;
		// Height of the extrusion, to calculate the extrusion flow from.
		double 		height;
		double 		speed;
		double 		angle;
		double 		inset;

		bool operator<(const IroningParams &rhs) const {
			if (this->extruder < rhs.extruder)
				return true;
			if (this->extruder > rhs.extruder)
				return false;
			if (int(this->just_infill) < int(rhs.just_infill))
				return true;
			if (int(this->just_infill) > int(rhs.just_infill))
				return false;
			if (this->line_spacing < rhs.line_spacing)
				return true;
			if (this->line_spacing > rhs.line_spacing)
				return false;
			if (this->height < rhs.height)
				return true;
			if (this->height > rhs.height)
				return false;
			if (this->speed < rhs.speed)
				return true;
			if (this->speed > rhs.speed)
				return false;
			if (this->angle < rhs.angle)
				return true;
			if (this->angle > rhs.angle)
				return false;
			if (this->inset < rhs.inset)
				return true;
			if (this->inset > rhs.inset)
				return false;
			return false;
		}

		bool operator==(const IroningParams &rhs) const {
			return this->extruder == rhs.extruder && this->just_infill == rhs.just_infill &&
				   this->line_spacing == rhs.line_spacing && this->height == rhs.height && this->speed == rhs.speed && this->angle == rhs.angle && this->pattern == rhs.pattern && this->inset == rhs.inset;
		}

		LayerRegion *layerm		= nullptr;

		// IdeaMaker: ironing
		// ironing flowrate (5% percent)
		// ironing speed (10 mm/sec)

		// Kisslicer:
		// iron off, Sweep, Group
		// ironing speed: 15 mm/sec

		// Cura:
		// Pattern (zig-zag / concentric)
		// line spacing (0.1mm)
		// flow: from normal layer height. 10%
		// speed: 20 mm/sec
	};

	std::vector<IroningParams> by_extruder;
    double default_layer_height = this->object()->config().layer_height;

	for (LayerRegion *layerm : m_regions)
		if (! layerm->slices.empty()) {
			IroningParams ironing_params;
			const PrintRegionConfig &config = layerm->region().config();
			if (config.ironing_type != IroningType::NoIroning &&
				(config.ironing_type == IroningType::AllSolid ||
				 	(config.top_shell_layers > 0 &&
						(config.ironing_type == IroningType::TopSurfaces ||
					 	(config.ironing_type == IroningType::TopmostOnly && layerm->layer()->upper_layer == nullptr))))) {
				if (config.wall_filament == config.solid_infill_filament || config.wall_loops == 0) {
					// Iron the whole face.
					ironing_params.extruder = config.solid_infill_filament;
				} else {
					// Iron just the infill.
					ironing_params.extruder = config.solid_infill_filament;
				}
			}
			if (ironing_params.extruder != -1) {
				//TODO just_infill is currently not used.
				ironing_params.just_infill 	= false;
				ironing_params.line_spacing = config.ironing_spacing;
				ironing_params.inset 		= config.ironing_inset;
				ironing_params.height 		= default_layer_height * 0.01 * config.ironing_flow;
				ironing_params.speed 		= config.ironing_speed;
				ironing_params.angle 		= (int(config.ironing_direction.value+layerm->region().config().infill_direction.value)%180) * M_PI / 180.;
				ironing_params.pattern      = config.ironing_pattern;
				ironing_params.layerm 		= layerm;
				by_extruder.emplace_back(ironing_params);
			}
		}
	std::sort(by_extruder.begin(), by_extruder.end());

    FillParams 			fill_params;
    fill_params.density 	 = 1.;
    fill_params.monotonic    = true;
    InfillPattern         f_pattern = ipRectilinear;
    std::unique_ptr<Fill> f         = std::unique_ptr<Fill>(Fill::new_from_type(f_pattern));
    f->set_bounding_box(this->object()->bounding_box());
    f->layer_id = this->id();
    f->z        = this->print_z;
    f->overlap  = 0;
	for (size_t i = 0; i < by_extruder.size();) {
		// Find span of regions equivalent to the ironing operation.
		IroningParams &ironing_params = by_extruder[i];
		// Create the filler object.
		if( f_pattern != ironing_params.pattern )
		{
            f_pattern               = ironing_params.pattern;
            f = std::unique_ptr<Fill>(Fill::new_from_type(f_pattern));
            f->set_bounding_box(this->object()->bounding_box());
            f->layer_id = this->id();
            f->z        = this->print_z;
            f->overlap  = 0;
		}

		size_t j = i;
		for (++ j; j < by_extruder.size() && ironing_params == by_extruder[j]; ++ j) ;

		// Create the ironing extrusions for regions <i, j)
		ExPolygons ironing_areas;
		double nozzle_dmr = this->object()->print()->config().nozzle_diameter.get_at(ironing_params.extruder - 1);
		if (ironing_params.just_infill) {
			//TODO just_infill is currently not used.
			// Just infill.
		} else {
			// Infill and perimeter.
			// Merge top surfaces with the same ironing parameters.
			Polygons polys;
			Polygons infills;
			for (size_t k = i; k < j; ++ k) {
				const IroningParams		 &ironing_params  = by_extruder[k];
				const PrintRegionConfig  &region_config   = ironing_params.layerm->region().config();
				bool					  iron_everything = region_config.ironing_type == IroningType::AllSolid;
				bool					  iron_completely = iron_everything;
				if (iron_everything) {
					// Check whether there is any non-solid hole in the regions.
					bool internal_infill_solid = region_config.sparse_infill_density.value > 95.;
					for (const Surface &surface : ironing_params.layerm->fill_surfaces.surfaces)
						if ((!internal_infill_solid && surface.surface_type == stInternal) || surface.surface_type == stInternalBridge || surface.surface_type == stInternalVoid) {
							// Some fill region is not quite solid. Don't iron over the whole surface.
							iron_completely = false;
							break;
						}
				}
				if (iron_completely) {
					// Iron everything. This is likely only good for solid transparent objects.
					for (const Surface &surface : ironing_params.layerm->slices.surfaces)
						polygons_append(polys, surface.expolygon);
				} else {
					for (const Surface &surface : ironing_params.layerm->slices.surfaces)
						if ((surface.surface_type == stTop && region_config.top_shell_layers > 0) || (iron_everything && surface.surface_type == stBottom && region_config.bottom_shell_layers > 0))
							// stBottomBridge is not being ironed on purpose, as it would likely destroy the bridges.
							polygons_append(polys, surface.expolygon);
				}
				if (iron_everything && ! iron_completely) {
					// Add solid fill surfaces. This may not be ideal, as one will not iron perimeters touching these
					// solid fill surfaces, but it is likely better than nothing.
					for (const Surface &surface : ironing_params.layerm->fill_surfaces.surfaces)
						if (surface.surface_type == stInternalSolid)
							polygons_append(infills, surface.expolygon);
				}
			}

			if (! infills.empty() || j > i + 1) {
				// Ironing over more than a single region or over solid internal infill.
				if (! infills.empty())
					// For IroningType::AllSolid only:
					// Add solid infill areas for layers, that contain some non-ironable infil (sparse infill, bridge infill).
					append(polys, std::move(infills));
				polys = union_safety_offset(polys);
			}
			// Trim the top surfaces with half the nozzle diameter.
			//BBS: ironing inset
            double ironing_areas_offset = ironing_params.inset == 0 ? float(scale_(0.5 * nozzle_dmr)) : scale_(ironing_params.inset);
			ironing_areas = intersection_ex(polys, offset(this->lslices, - ironing_areas_offset));
		}

        // Create the filler object.
        f->spacing = ironing_params.line_spacing;
        f->angle = float(ironing_params.angle);
        f->link_max_length = (coord_t) scale_(3. * f->spacing);
		double  extrusion_height = ironing_params.height * f->spacing / nozzle_dmr;
		float  extrusion_width  = Flow::rounded_rectangle_extrusion_width_from_spacing(float(nozzle_dmr), float(extrusion_height));
		double flow_mm3_per_mm = nozzle_dmr * extrusion_height;
        Surface surface_fill(stTop, ExPolygon());
        for (ExPolygon &expoly : ironing_areas) {
			surface_fill.expolygon = std::move(expoly);
			Polylines polylines;
			try {
				polylines = f->fill_surface(&surface_fill, fill_params);
			} catch (InfillFailedException &) {
			}
	        if (! polylines.empty()) {
		        // Save into layer.
				ExtrusionEntityCollection *eec = nullptr;
		        ironing_params.layerm->fills.entities.push_back(eec = new ExtrusionEntityCollection());
		        // Don't sort the ironing infill lines as they are monotonicly ordered.
				eec->no_sort = true;
		        extrusion_entities_append_paths(
		            eec->entities, std::move(polylines),
		            erIroning,
		            flow_mm3_per_mm, extrusion_width, float(extrusion_height));
		    }
		}

		// Regions up to j were processed.
		i = j;
	}
}

} // namespace Slic3r
