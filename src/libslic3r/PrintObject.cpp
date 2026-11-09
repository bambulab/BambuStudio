#include "Exception.hpp"
#include "AdaptiveLayerMerge.hpp"
#include "Print.hpp"
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "ElephantFootCompensation.hpp"
#include "Geometry.hpp"
#include "I18N.hpp"
#include "Layer.hpp"
#include "MutablePolygon.hpp"
#include "Support/SupportMaterial.hpp"
#include "Support/TreeSupport.hpp"
#include "Surface.hpp"
#include "Slicing.hpp"
#include "Tesselate.hpp"
#include "Time.hpp"
#include "TriangleMeshSlicer.hpp"
#include "Utils.hpp"
#include "Fill/FillAdaptive.hpp"
#include "Fill/FillRadialZigZag.hpp"
#include "Fill/FillLightning.hpp"
#include "Format/STL.hpp"
#include "InternalBridgeDetector.hpp"
#include "AABBTreeLines.hpp"

#include <float.h>
#include <string_view>
#include <utility>
#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

#include <tbb/parallel_for.h>
#include <tbb/concurrent_vector.h>
#include <tbb/concurrent_unordered_set.h>

#include <Shiny/Shiny.h>

#include "format.hpp"
#include "libslic3r/Log.hpp"

using namespace std::literals;

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
#define SLIC3R_DEBUG
#endif

// #define SLIC3R_DEBUG
// Make assert active if SLIC3R_DEBUG
#ifdef SLIC3R_DEBUG
    #undef NDEBUG
    #define DEBUG
    #define _DEBUG
    #include "SVG.hpp"
    #undef assert
    #include <cassert>
#endif

#define USE_TBB_IN_INFILL 1

namespace Slic3r {

// Constructor is called from the main thread, therefore all Model / ModelObject / ModelIntance data are valid.
PrintObject::PrintObject(Print* print, ModelObject* model_object, const Transform3d& trafo, PrintInstances&& instances) :
    PrintObjectBaseWithState(print, model_object),
    m_trafo(trafo),
    // BBS
    m_tree_support_preview_cache(nullptr)
{
    // Compute centering offet to be applied to our meshes so that we work with smaller coordinates
    // requiring less bits to represent Clipper coordinates.

	// Snug bounding box of a rotated and scaled object by the 1st instantion, without the instance translation applied.
	// All the instances share the transformation matrix with the exception of translation in XY and rotation by Z,
	// therefore a bounding box from 1st instance of a ModelObject is good enough for calculating the object center,
	// snug height and an approximate bounding box in XY.
    BoundingBoxf3  bbox        = model_object->raw_bounding_box();
    Vec3d 		   bbox_center = bbox.center();

	// We may need to rotate the bbox / bbox_center from the original instance to the current instance.
	double z_diff = Geometry::rotation_diff_z(model_object->instances.front()->get_rotation(), instances.front().model_instance->get_rotation());
	if (std::abs(z_diff) > EPSILON) {
		auto z_rot  = Eigen::AngleAxisd(z_diff, Vec3d::UnitZ());
		bbox 		= bbox.transformed(Transform3d(z_rot));
		bbox_center = (z_rot * bbox_center).eval();
	}

    // Center of the transformed mesh (without translation).
    m_center_offset = Point::new_scale(bbox_center.x(), bbox_center.y());
    // Size of the transformed mesh. This bounding may not be snug in XY plane, but it is snug in Z.
    m_size = (bbox.size() * (1. / SCALING_FACTOR)).cast<coord_t>();
    m_max_z = scaled(model_object->instance_bounding_box(0).max(2));

    this->set_instances(std::move(instances));
}

PrintObject::~PrintObject()
{
    BOOST_LOG_TRIVIAL(info) << boost::format("this=%1%, m_shared_object %2%")%this%m_shared_object;
    if (m_shared_regions && -- m_shared_regions->m_ref_cnt == 0) delete m_shared_regions;
    clear_layers();
    clear_support_layers();
}

PrintBase::ApplyStatus PrintObject::set_instances(PrintInstances &&instances)
{
    for (PrintInstance &i : instances)
    	// Add the center offset, which will be subtracted from the mesh when slicing.
    	i.shift += m_center_offset;
    // Invalidate and set copies.
    PrintBase::ApplyStatus status = PrintBase::APPLY_STATUS_UNCHANGED;
    bool equal_length = instances.size() == m_instances.size();
    bool equal = equal_length && std::equal(instances.begin(), instances.end(), m_instances.begin(),
    	[](const PrintInstance& lhs, const PrintInstance& rhs) { return lhs.model_instance == rhs.model_instance && lhs.shift == rhs.shift; });
    if (! equal) {
        status = PrintBase::APPLY_STATUS_CHANGED;
        if (m_print->invalidate_steps({psWipeTower,psSkirtBrim, psGCodeExport}) ||
            (! equal_length && m_print->invalidate_step(psWipeTower)))
            status = PrintBase::APPLY_STATUS_INVALIDATED;
        m_instances = std::move(instances);
	    for (PrintInstance &i : m_instances)
	    	i.print_object = this;
    }
    return status;
}

std::vector<std::reference_wrapper<const PrintRegion>> PrintObject::all_regions() const
{
    std::vector<std::reference_wrapper<const PrintRegion>> out;
    out.reserve(m_shared_regions->all_regions.size());
    for (const std::unique_ptr<Slic3r::PrintRegion> &region : m_shared_regions->all_regions)
        out.emplace_back(*region.get());
    return out;
}

void PrintObject::merge_layer_node(const size_t layer_id, int &max_merged_id, std::map<int, std::vector<std::pair<int, int>>> &node_record)
{
    Layer *this_layer = m_layers[layer_id];
    std::vector<LoopNode> &loop_nodes = this_layer->loop_nodes;
    for (size_t idx = 0; idx < loop_nodes.size(); ++idx) {
        //new cool node
        if (loop_nodes[idx].lower_node_id.empty()) {
            max_merged_id++;
            loop_nodes[idx].merged_id = max_merged_id;
            std::vector<std::pair<int, int>> node_pos;
            node_pos.emplace_back(layer_id, idx);
            node_record.emplace(max_merged_id, node_pos);
            continue;
        }

        //it should finds key in map
        if (loop_nodes[idx].lower_node_id.size() == 1) {
            loop_nodes[idx].merged_id = m_layers[layer_id - 1]->loop_nodes[loop_nodes[idx].lower_node_id.front()].merged_id;
            node_record[loop_nodes[idx].merged_id].emplace_back(layer_id, idx);
            continue;
        }

        //min index
        int min_merged_id = -1;
        std::vector<int> appear_id;
        for (size_t lower_idx = 0; lower_idx < loop_nodes[idx].lower_node_id.size(); ++lower_idx) {
            int id = m_layers[layer_id - 1]->loop_nodes[loop_nodes[idx].lower_node_id[lower_idx]].merged_id;
            if (min_merged_id == -1 || min_merged_id > id)
                min_merged_id = id;
            appear_id.push_back(id);
        }

        loop_nodes[idx].merged_id = min_merged_id;
        node_record[min_merged_id].emplace_back(layer_id, idx);

        //update other node merged id
        for (size_t appear_node_idx = 0; appear_node_idx < appear_id.size(); ++appear_node_idx) {
            if (appear_id[appear_node_idx] == min_merged_id)
                continue;

            auto it = node_record.find(appear_id[appear_node_idx]);
            //protect
            if (it == node_record.end())
                continue;

            std::vector<std::pair<int, int>> &appear_node_pos = it->second;

            for (size_t node_idx = 0; node_idx < appear_node_pos.size(); ++node_idx) {
                int node_layer = appear_node_pos[node_idx].first;
                int node_pos = appear_node_pos[node_idx].second;

                LoopNode &node = m_layers[node_layer]->loop_nodes[node_pos];

                node.merged_id = min_merged_id;
                node_record[min_merged_id].emplace_back(node_layer, node_pos);
            }
            node_record.erase(it);
        }
    }
}

std::vector<std::set<int>> PrintObject::detect_extruder_geometric_unprintables() const
{
    int extruder_size = m_print->config().nozzle_diameter.size();
    if(extruder_size == 1)
        return std::vector<std::set<int>>(1, std::set<int>());

    std::vector<std::set<int>> geometric_unprintables(extruder_size); // the container to return

    std::vector<double> printable_height_per_extruder = m_print->config().extruder_printable_height.values;
    assert(printable_height_per_extruder.size() == extruder_size);

    // check unprintable filaments caused by printable height limit
    for (size_t extruder_id = 0; extruder_id < printable_height_per_extruder.size(); ++extruder_id) {
        double printable_height = printable_height_per_extruder[extruder_id];
        for (size_t layer_idx = 0; layer_idx < m_layers.size(); ++layer_idx) {
            auto layer = m_layers[layer_idx];
            if (layer->print_z <= printable_height)
                continue;
            for (auto layerm : layer->regions()) {
                auto region = layerm->region();
                int wall_filament = region.config().wall_filament;
                int solid_infill_filament = region.config().solid_infill_filament;
                int sparse_infill_filament = region.config().sparse_infill_filament;

                if (!layerm->fills.entities.empty()) {
                    if (solid_infill_filament > 0)
                        geometric_unprintables[extruder_id].insert(solid_infill_filament - 1);
                    if (sparse_infill_filament > 0)
                        geometric_unprintables[extruder_id].insert(sparse_infill_filament - 1);
                }
                if (!layerm->perimeters.entities.empty() && wall_filament > 0)
                    geometric_unprintables[extruder_id].insert(wall_filament - 1);
            }
        }
    }

    std::vector<tbb::concurrent_unordered_set<int>> tbb_geometric_unprintables(extruder_size); // the container used in tbb

    std::vector<Polygons> unprintable_area_in_obj_coord =  m_print->get_extruder_unprintable_polygons();
    std::vector<BoundingBox> unprintable_area_bbox;

    // transform the unprintable areas to obj coord is cheaper than thransform obj into world coord
    for (auto& polys : unprintable_area_in_obj_coord) {
        for (auto& poly : polys) {
            poly.translate(-m_instances.front().shift_without_plate_offset());
        }
        // 禁区略内收，避免层片与禁区只共边时被当成相交
        polys = shrink(polys, float(SCALED_EPSILON));
        unprintable_area_bbox.emplace_back(get_extents(polys));
    }

    // check unprintbale filaments caused by printable area limit
    tbb::parallel_for(tbb::blocked_range<int>(0, m_layers.size()),
        [this, &tbb_geometric_unprintables, &unprintable_area_in_obj_coord, &unprintable_area_bbox](const tbb::blocked_range<int>& range) {
            for (int j = range.begin(); j < range.end(); ++j) {
                auto layer = m_layers[j];
                for (auto layerm : layer->regions()) {
                    const auto& region = layerm->region();
                    int wall_filament = region.config().wall_filament;
                    int solid_infill_filament = region.config().solid_infill_filament;
                    int sparse_infill_filament = region.config().sparse_infill_filament;
                    std::optional<ExPolygons> fill_expolys;
                    BoundingBox fill_bbox;
                    std::optional<ExPolygons> wall_expolys;
                    BoundingBox wall_bbox;

                    for (size_t idx = 0; idx < unprintable_area_in_obj_coord.size(); ++idx) {
                        bool do_infill_filament_detect = (solid_infill_filament > 0 && tbb_geometric_unprintables[idx].count(solid_infill_filament - 1) == 0) ||
                            (sparse_infill_filament > 0 && tbb_geometric_unprintables[idx].count(sparse_infill_filament-1) == 0);

                        bool infill_unprintable = !layerm->fills.entities.empty() &&
                            ((solid_infill_filament > 0 && tbb_geometric_unprintables[idx].count(solid_infill_filament - 1) > 0) ||
                                (sparse_infill_filament > 0 && tbb_geometric_unprintables[idx].count(sparse_infill_filament - 1) > 0));

                        if (!layerm->fills.entities.empty() && do_infill_filament_detect) {
                            if (!fill_expolys) {
                                fill_expolys = layerm->fill_expolygons;
                                fill_bbox = get_extents(*fill_expolys);
                            }
                            if (fill_bbox.overlap(unprintable_area_bbox[idx]) &&
                                !intersection(*fill_expolys, unprintable_area_in_obj_coord[idx]).empty()) {
                                if (solid_infill_filament > 0)
                                    tbb_geometric_unprintables[idx].insert(solid_infill_filament - 1);
                                if (sparse_infill_filament > 0)
                                    tbb_geometric_unprintables[idx].insert(sparse_infill_filament - 1);
                                infill_unprintable = true;
                            }
                        }

                        bool do_wall_filament_detect = wall_filament > 0 && tbb_geometric_unprintables[idx].count(wall_filament - 1) == 0;
                        if (!layerm->perimeters.entities.empty() && do_wall_filament_detect) {
                            // if infill is unprintable, no need to check wall since wall contour surrounds infill contour
                            if (infill_unprintable) {
                                tbb_geometric_unprintables[idx].insert(wall_filament - 1);
                                continue;
                            }

                            if (!wall_expolys) {
                                if (!fill_expolys) {
                                    fill_expolys = layerm->fill_expolygons;
                                    fill_bbox = get_extents(*fill_expolys);
                                }
                                wall_expolys = diff_ex(layerm->raw_slices, *fill_expolys);
                                wall_bbox = get_extents(*wall_expolys);
                            }

                            if (wall_bbox.overlap(unprintable_area_bbox[idx]) &&
                                !intersection(*wall_expolys, unprintable_area_in_obj_coord[idx]).empty()) {
                                tbb_geometric_unprintables[idx].insert(wall_filament - 1);
                            }
                        }
                    }
                }
            }
        });

    // add the elems in tbb container to final contianer
    for (size_t idx = 0; idx < extruder_size; ++idx) {
        geometric_unprintables[idx].insert(tbb_geometric_unprintables[idx].begin(), tbb_geometric_unprintables[idx].end());
    }

    return geometric_unprintables;
}



std::unordered_map<int, std::unordered_map<int, double>> PrintObject::calc_estimated_filament_print_time() const
{
    auto                     full_print_config             = this->print()->m_ori_full_print_config;
    std::vector<std::string> extruder_variant_list         = this->print()->config().printer_extruder_variant.values;

    const auto* opt_filament_variant      = full_print_config.option<ConfigOptionStrings>("filament_extruder_variant");
    const auto* opt_filament_self_idx     = full_print_config.option<ConfigOptionInts>("filament_self_index");
    const auto* opt_filament_max_vol_spd  = full_print_config.option<ConfigOptionFloats>("filament_max_volumetric_speed");
    const auto* opt_filament_flow_ratio   = full_print_config.option<ConfigOptionFloats>("filament_flow_ratio");
    if (!opt_filament_variant || !opt_filament_self_idx || !opt_filament_max_vol_spd || !opt_filament_flow_ratio) {
        BOOST_LOG_TRIVIAL(warning) << "filament_* config option missing, skip estimation";
        return {};
    }
    std::vector<std::string> filament_variant_list         = opt_filament_variant->values;
    std::vector<int>         filament_self_idx             = opt_filament_self_idx->values;
    std::vector<double>      filament_max_volumetric_speed = opt_filament_max_vol_spd->values;
    std::vector<double>      filament_flow_ratio           = opt_filament_flow_ratio->values;

    auto get_limit_from_volumetric_speed = [&](int filament_idx, int extruder_idx, double width, double height) {
        std::string extruder_variant = extruder_variant_list[extruder_idx];

        int idx_for_filament = 0;
        for (size_t idx = 0; idx < filament_self_idx.size(); ++idx) {
            if (filament_self_idx[idx] - 1 == filament_idx && extruder_variant == filament_variant_list[idx]) {
                idx_for_filament = idx;
                break;
            }
        }

        double max_volumetric_speed = filament_max_volumetric_speed[idx_for_filament];
        double flow_ratio           = filament_flow_ratio[idx_for_filament];

        double mm3_per_mm = height * (width - height * (1 - PI / 4)) * flow_ratio;
        return max_volumetric_speed / mm3_per_mm;
    };

    auto get_speed_from_role_with_filament = [](ExtrusionRole role, int filament_id, const PrintRegionConfig &region_config, const PrintConfig &print_config, int extruder_id) -> double {
        switch (role) {
        case erPerimeter: return region_config.inner_wall_speed.values[extruder_id];
        case erExternalPerimeter: return region_config.outer_wall_speed.values[extruder_id];
        case erOverhangPerimeter:
        case erBridgeInfill:
        case erSupportTransition: {
            bool use_filament_bridge_speed = print_config.filament_enable_overhang_speed.get_at(filament_id);

            if (use_filament_bridge_speed)
                return print_config.filament_bridge_speed.values[filament_id];
            else
                return region_config.bridge_speed.values[extruder_id];
        }
        case erInternalInfill: return region_config.sparse_infill_speed.values[extruder_id];
        case erFloatingVerticalShell:
        case erSolidInfill: return region_config.internal_solid_infill_speed.values[extruder_id];
        case erTopSolidInfill: return region_config.top_surface_speed.values[extruder_id];
        case erBottomSurface: return print_config.initial_layer_speed.values[extruder_id];
        case erGapFill: return region_config.gap_infill_speed.values[extruder_id];
        }
        return region_config.internal_solid_infill_speed.values[extruder_id];
    };

    std::unordered_map<int, std::unordered_map<int, double>> filament_print_time;

    auto process_epath = [&](int filament, int eidx, ExtrusionPath *path, double speed_from_config) {
        double speed_from_filament = get_limit_from_volumetric_speed(filament, eidx, path->width, path->height);
        double speed_applied       = std::min(speed_from_filament, speed_from_config);
        filament_print_time[filament][eidx] += unscale_(path->length()) / speed_applied;
    };

    auto process_eloop = [&](int filament, int eidx, ExtrusionLoop *eloop, double speed_from_config) {
        for (auto &path : eloop->paths) {
            double speed_from_filament = get_limit_from_volumetric_speed(filament, eidx, path.width, path.height);
            double speed_applied       = std::min(speed_from_filament, speed_from_config);
            filament_print_time[filament][eidx] += unscale_(path.length()) / speed_applied;
        }
    };

    auto process_empath = [&](int filament, int eidx, ExtrusionMultiPath *empath, double speed_from_config) {
        for (auto &path : empath->paths) {
            double speed_from_filament = get_limit_from_volumetric_speed(filament, eidx, path.width, path.height);
            double speed_applied       = std::min(speed_from_filament, speed_from_config);
            filament_print_time[filament][eidx] += unscale_(path.length()) / speed_applied;
        }
    };

    std::function<void(int, int, ExtrusionEntity *, double)> process_entity = [&](int filament, int eidx, ExtrusionEntity *entity, double speed_from_config) {
        if (entity->is_collection()) {
            // 如果是集合类型，则递归处理其子entity
            auto *collection = dynamic_cast<ExtrusionEntityCollection *>(entity);
            if (collection) {
                for (auto &sub_entity : collection->entities) { process_entity(filament, eidx, sub_entity, speed_from_config); }
            }
        } else if (auto path_ptr = dynamic_cast<ExtrusionPath *>(entity)) {
            process_epath(filament, eidx, path_ptr, speed_from_config);
        } else if (auto loop_ptr = dynamic_cast<ExtrusionLoop *>(entity)) {
            process_eloop(filament, eidx, loop_ptr, speed_from_config);
        } else if (auto empath_ptr = dynamic_cast<ExtrusionMultiPath *>(entity)) {
            process_empath(filament, eidx, empath_ptr, speed_from_config);
        } else {
            BOOST_LOG_TRIVIAL(warning) << "Unknown extrusion entity type encountered during filament print time estimation.";
        }
    };

    // Iterate through all layers and regions to calculate the print area for each filament.
    int extruder_num = this->print()->config().nozzle_diameter.size();
    for (size_t idx = 0; idx < m_layers.size(); ++idx) {
        auto layer = m_layers[idx];

        for (auto layerm : layer->regions()) {
            const auto &region       = layerm->region();

            int wall_filament          = region.config().wall_filament - 1;
            int solid_infill_filament  = region.config().solid_infill_filament - 1;
            int sparse_infill_filament = region.config().sparse_infill_filament - 1;

            std::vector<ExtrusionEntitiesPtr> entity_list{layerm->fills.entities, layerm->thin_fills.entities, layerm->perimeters.entities};
            for (int eidx = 0; eidx < extruder_num; ++eidx) {
                for (auto &entities : entity_list) {
                    for (auto &entity : entities) {
                        auto   role              = entity->role();
                        int    filament          = 0;
                        if (is_perimeter(role))
                            filament = region.config().wall_filament - 1;
                        else if (is_solid_infill(role))
                            filament = region.config().solid_infill_filament - 1;
                        else if (is_infill(role))
                            filament = region.config().sparse_infill_filament - 1;
                        else
                            continue;

                        double speed_from_config = get_speed_from_role_with_filament(role, filament, region.config(), this->print()->config(), eidx);
                        process_entity(filament, eidx, entity, speed_from_config);
                    }
                }
            }
        }
    }
    return filament_print_time;
}

// 1) Merges typed region slices into stInternal type.
// 2) Increases an "extra perimeters" counter at region slices where needed.
// 3) Generates perimeters, gap fills and fill regions (fill regions of type stInternal).
void PrintObject::make_perimeters()
{
    // prerequisites
    this->slice();

    if (! this->set_started(posPerimeters))
        return;

    const bool record_wall_time = m_print->m_slice_time != nullptr;
    const long long wall_begin_time =
        record_wall_time ? Slic3r::Utils::get_current_milliseconds_time_monotonic() : 0;

    m_print->set_status(15, L("Generating walls"));
    BOOST_LOG_TRIVIAL(info) << "Generating walls..." << log_memory_info();

    // Revert the typed slices into untyped slices.
    if (m_typed_slices) {
        for (Layer *layer : m_layers) {
            layer->restore_untyped_slices();
            m_print->throw_if_canceled();
        }
        m_typed_slices = false;
    }

    // compare each layer to the one below, and mark those slices needing
    // one additional inner perimeter, like the top of domed objects-

    // this algorithm makes sure that at least one perimeter is overlapping
    // but we don't generate any extra perimeter if fill density is zero, as they would be floating
    // inside the object - infill_only_where_needed should be the method of choice for printing
    // hollow objects
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        const PrintRegion &region = this->printing_region(region_id);
        //BBS: remove extra_perimeters, always false
        //if (! region.config().extra_perimeters || region.config().wall_loops == 0 || region.config().sparse_infill_density == 0 || this->layer_count() < 2)
            continue;

        BOOST_LOG_TRIVIAL(debug) << "Generating extra perimeters for region " << region_id << " in parallel - start";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size() - 1),
            [this, &region, region_id](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    m_print->throw_if_canceled();
                    LayerRegion &layerm                     = *m_layers[layer_idx]->get_region(region_id);
                    const LayerRegion &upper_layerm         = *m_layers[layer_idx+1]->get_region(region_id);
                    const Polygons upper_layerm_polygons    = to_polygons(upper_layerm.slices.surfaces);
                    // Filter upper layer polygons in intersection_ppl by their bounding boxes?
                    // my $upper_layerm_poly_bboxes= [ map $_->bounding_box, @{$upper_layerm_polygons} ];
                    const double total_loop_length      = total_length(upper_layerm_polygons);
                    const coord_t perimeter_spacing     = layerm.flow(frPerimeter).scaled_spacing();
                    const Flow ext_perimeter_flow       = layerm.flow(frExternalPerimeter);
                    const coord_t ext_perimeter_width   = ext_perimeter_flow.scaled_width();
                    const coord_t ext_perimeter_spacing = ext_perimeter_flow.scaled_spacing();

                    for (Surface &slice : layerm.slices.surfaces) {
                        for (;;) {
                            // compute the total thickness of perimeters
                            const coord_t perimeters_thickness = ext_perimeter_width/2 + ext_perimeter_spacing/2
                                + (region.config().wall_loops-1 + slice.extra_perimeters) * perimeter_spacing;
                            // define a critical area where we don't want the upper slice to fall into
                            // (it should either lay over our perimeters or outside this area)
                            const coord_t critical_area_depth = coord_t(perimeter_spacing * 1.5);
                            const Polygons critical_area = diff(
                                offset(slice.expolygon, float(- perimeters_thickness)),
                                offset(slice.expolygon, float(- perimeters_thickness - critical_area_depth))
                            );
                            // check whether a portion of the upper slices falls inside the critical area
                            const Polylines intersection = intersection_pl(to_polylines(upper_layerm_polygons), critical_area);
                            // only add an additional loop if at least 30% of the slice loop would benefit from it
                            if (total_length(intersection) <=  total_loop_length*0.3)
                                break;
                            /*
                            if (0) {
                                require "Slic3r/SVG.pm";
                                Slic3r::SVG::output(
                                    "extra.svg",
                                    no_arrows   => 1,
                                    expolygons  => union_ex($critical_area),
                                    polylines   => [ map $_->split_at_first_point, map $_->p, @{$upper_layerm->slices} ],
                                );
                            }
                            */
                            ++ slice.extra_perimeters;
                        }
                        #ifdef DEBUG
                            if (slice.extra_perimeters > 0)
                                printf("  adding %d more perimeter(s) at layer %zu\n", slice.extra_perimeters, layer_idx);
                        #endif
                    }
                }
            });
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Generating extra perimeters for region " << region_id << " in parallel - end";
    }

#if 0
    { // for debug
        for (size_t layer_idx = 0; layer_idx < m_layers.size(); ++layer_idx) {
            auto regions = m_layers[layer_idx]->regions();
            for (size_t region_idx = 0; region_idx < regions.size(); ++region_idx) {
                LayerRegion *layer_region = regions[region_idx];
                std::string  name         = "before_make_perimeter_layer-" + std::to_string(layer_idx) + "-region-" + std::to_string(region_idx) + ".svg";
                layer_region->slices.export_to_svg(debug_out_path(name.c_str()).c_str(), true);
            }
        }
    }
#endif

    // BBS multi-diameter: decide layer-merge groups before generating walls (walls and infill share the grouping).
    this->compute_adaptive_merge_groups();
    m_print->throw_if_canceled();

    BOOST_LOG_TRIVIAL(debug) << "Generating perimeters in parallel - start";
#if 1
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, m_layers.size()),
        [this](const tbb::blocked_range<size_t>& range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                m_print->throw_if_canceled();
                m_layers[layer_idx]->make_perimeters();
            }
        }
    );
#else
    for (size_t layer_idx = 0; layer_idx < m_layers.size(); ++ layer_idx) {
        m_print->throw_if_canceled();
        m_layers[layer_idx]->make_perimeters();
    }
#endif
    m_print->throw_if_canceled();
    BOOST_LOG_TRIVIAL(debug) << "Generating perimeters in parallel - end";

    if (this->m_print->m_config.z_direction_outwall_speed_continuous) {
        // BBS: get continuity of nodes
        BOOST_LOG_TRIVIAL(debug) << "Calculating perimeters connection in parallel - start";
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layers.size()), [this](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
                m_print->throw_if_canceled();
                if (layer_idx > 1) {
                    Layer &prev_layer = *m_layers[layer_idx - 1];
                    m_layers[layer_idx]->calculate_perimeter_continuity(m_layers[layer_idx - 1]->loop_nodes);
                }
            }
        });

        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Calculating perimeters connection in parallel - end";

        BOOST_LOG_TRIVIAL(debug) << "Calculating cooling nodes - start";

        int max_merged_id = -1;
        std::map<int,std::vector<std::pair<int, int>>> node_record;
        for (size_t layer_idx = 1; layer_idx < m_layers.size(); ++layer_idx) {
            m_print->throw_if_canceled();
            merge_layer_node(layer_idx, max_merged_id, node_record);
        }
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Calculating cooling nodes - end";


        //write merged node to each perimeter
        BOOST_LOG_TRIVIAL(debug) << "Recrod cooling_node id for each extrusion in parallel - start";
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layers.size()), [this](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
                m_print->throw_if_canceled();
                if (layer_idx > 1) {
                    Layer &prev_layer = *m_layers[layer_idx - 1];
                    m_layers[layer_idx]->recrod_cooling_node_for_each_extrusion();
                }
            }
        });

        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Recrod cooling_node id for each extrusion in parallel - end";
    }
    this->set_done(posPerimeters);

    if (record_wall_time)
        (*m_print->m_slice_time)[TIME_WALL] +=
            Slic3r::Utils::get_current_milliseconds_time_monotonic() - wall_begin_time;
}

namespace {

// Minimum contact extent, mirroring length_thresh_small_overhang in TreeSupport.cpp.
const coord_t zero_gap_length_thresh = scale_(2);

// Same thresh_angle convention as SupportMaterial::detect_overhangs: +1, clamp 89.
double zero_gap_threshold_rad(const PrintObjectConfig &config)
{
    double thresh_angle = config.support_threshold_angle.value > 0 ? config.support_threshold_angle.value + 1 : 0;
    thresh_angle = std::min(thresh_angle, 89.);
    return Geometry::deg2rad(thresh_angle);
}

// Prefer the real external-perimeter flow width over config.line_width (which may be 0).
// Max across regions is conservative: a larger guard_r only shrinks the Bottom mask.
float scaled_external_perimeter_width(const Layer &layer)
{
    float fw = 0.f;
    for (const LayerRegion *layerm : layer.regions())
        fw = std::max(fw, float(layerm->flow(frExternalPerimeter).scaled_width()));
    return fw;
}

// Zero-gap Normal/auto support only: refine stBottomBridge on fill_surfaces where a
// conservative top-contact candidate exists. Prefer false Bridge over false Bottom.
// stNormalManual is not covered yet: its contacts come solely from the enforcer branch of
// detect_contacts, so the geometric overhang mask would not describe them. See GitHub #11540.
bool should_refine_zero_gap_contact(const PrintObjectConfig &config)
{
    return config.enable_support.value
        && config.support_top_z_distance.value == 0.
        && config.support_type.value == stNormalAuto
        && ! config.support_on_build_plate_only.value
        && ! config.support_critical_regions_only.value
        // Without interface layers the contact is printed with the sparse base pattern
        // (see SupportParameters), which is too weak a base for a regular bottom surface.
        && config.support_interface_top_layers.value > 0
        // Classic support drops fill bridges from contacts only after process_external_surfaces
        // assigns bridge_angle. Calling remove_bridges_from_contacts() here cannot see those
        // surfaces yet; subtracting every stBottomBridge would empty the mask. Keep legacy
        // Bridge classification whenever bridge_no_support is on.
        && ! config.bridge_no_support.value;
}

// Mirror TreeSupport check_small_overhang criterion B: keep pieces whose erosion
// remains non-empty and whose eroded bbox exceeds the length threshold on at least one axis.
bool is_significant_contact_piece(const ExPolygon &piece, float erode_r)
{
    if (erode_r <= 0.f)
        return false;
    ExPolygons eroded = offset_ex(piece, -erode_r);
    if (eroded.empty())
        return false;
    Point bbox_sz = get_extents(eroded).size();
    return bbox_sz.x() > zero_gap_length_thresh || bbox_sz.y() > zero_gap_length_thresh;
}

ExPolygons filter_significant_contact_pieces(ExPolygons pieces, float erode_r)
{
    ExPolygons out;
    out.reserve(pieces.size());
    for (ExPolygon &piece : pieces)
        if (is_significant_contact_piece(piece, erode_r))
            out.emplace_back(std::move(piece));
    return out;
}

struct ZeroGapContactMask
{
    ExPolygons polygons;
    float      sliver_r = 0.f; // leftover / inset sliver filter, <= the opening radius
};

// Lightweight zero-gap top-contact candidate. Intentionally conservative vs classic detect_overhangs.
ZeroGapContactMask compute_zero_gap_contact_mask(
    const PrintObject &object,
    const Layer &layer,
    const Layer &lower_layer,
    size_t layer_id,
    const std::vector<Polygons> &blockers,
    const std::vector<Polygons> &enforcers)
{
    ZeroGapContactMask result;
    const float extrusion_width_scaled = scaled_external_perimeter_width(layer);
    if (extrusion_width_scaled <= 0.f)
        return result;

    const PrintObjectConfig &config = object.config();
    const double threshold_rad = zero_gap_threshold_rad(config);
    const float  angle_offset  = (threshold_rad > 0.)
        ? float(scale_(lower_layer.height / tan(threshold_rad)))
        : 0.5f * extrusion_width_scaled;
    // Opening radius covering both the threshold angle and the small-overhang lower bound.
    const float guard_r = std::max(2.5f * extrusion_width_scaled, angle_offset);
    result.sliver_r = 2.5f * extrusion_width_scaled;
    if (guard_r <= 0.f)
        return result;

    const Polygons lower_polygons = to_polygons(lower_layer.lslices);

    // Automatic overhangs, approximating detect_overhangs: the opening drops what the
    // threshold angle and small-overhang removal would have dropped.
    ExPolygons mask;
    {
        Polygons lower_expanded = (angle_offset > 0.f)
            ? expand(lower_polygons, angle_offset, ClipperLib::jtSquare, 0.)
            : lower_polygons;
        ExPolygons raw = diff_ex(layer.lslices, lower_expanded);
        if (! raw.empty()) {
            mask = intersection_ex(opening_ex(raw, guard_r), raw);
            mask = filter_significant_contact_pieces(std::move(mask), guard_r);
        }
    }

    // Support enforcers, mirroring detect_contacts: they enforce support as if the slope were
    // 90 degrees, so neither the threshold angle nor small-overhang removal applies. Running the
    // opening here would discard narrow painted strips that do get a contact interface.
    if (layer_id < enforcers.size() && ! enforcers[layer_id].empty()) {
        ExPolygons enforced = diff_ex(
            intersection_ex(layer.lslices, enforcers[layer_id]),
            expand(lower_polygons, 0.05f * extrusion_width_scaled, ClipperLib::jtSquare, 0.));
        enforced = filter_significant_contact_pieces(std::move(enforced), result.sliver_r);
        if (! enforced.empty())
            mask = mask.empty() ? std::move(enforced) : union_ex(mask, enforced);
    }
    if (mask.empty())
        return result;

    // Support blockers (expand slightly, matching SupportMaterial). Classic support lets an
    // enforcer win over a blocker; subtracting from the whole mask is stricter on purpose.
    if (layer_id < blockers.size() && ! blockers[layer_id].empty()) {
        Polygons blocker = expand(union_(blockers[layer_id]), float(1000. * SCALED_EPSILON));
        mask = diff_ex(mask, blocker);
        if (mask.empty())
            return result;
    }

    // Negative support_expansion shrinks contacts in classic support; mirror that.
    // Classic support spares enforcers from it, so applying it here is stricter on purpose.
    const float xy_expansion = float(scale_(config.support_expansion.value));
    if (xy_expansion < -SCALED_EPSILON) {
        mask = offset_ex(mask, xy_expansion);
        if (mask.empty())
            return result;
    }

    // Lateral gap between support and object wall is not covered by the opening;
    // erode so the unsupported rim stays Bridge.
    const float xy_gap = float(scale_(config.support_object_xy_distance.value));
    if (xy_gap > SCALED_EPSILON) {
        mask = offset_ex(mask, -xy_gap);
        if (mask.empty())
            return result;
    }

    // Re-filter leftover slivers after shrinks. Use sliver_r, not guard_r: the angle
    // opening already ran, and xy_gap must not apply that radius a second time.
    result.polygons = filter_significant_contact_pieces(std::move(mask), result.sliver_r);
    return result;
}

// Split each stBottomBridge fill surface into supported (stBottom) / unsupported (stBottomBridge).
// Only significant contact pieces are promoted; everything else of the original surface stays
// a single Bridge remainder (slivers are merged back, not left as separate islands).
// slices are left unchanged.
void refine_fill_surfaces_for_zero_gap_contact(LayerRegion &layerm, const ExPolygons &mask, float sliver_r)
{
    if (mask.empty() || sliver_r <= 0.f)
        return;

    Surfaces out;
    out.reserve(layerm.fill_surfaces.surfaces.size());

    for (const Surface &surface : layerm.fill_surfaces.surfaces) {
        if (surface.surface_type != stBottomBridge) {
            out.emplace_back(surface);
            continue;
        }

        // fill_surfaces is already inset by perimeters; do not re-apply the full
        // overhang opening radius here or a valid contact would fail the bbox test.
        ExPolygons candidates = intersection_ex(surface.expolygon, mask);
        ExPolygons promoted;
        promoted.reserve(candidates.size());
        for (ExPolygon &expoly : candidates)
            if (is_significant_contact_piece(expoly, sliver_r))
                promoted.emplace_back(std::move(expoly));

        if (promoted.empty()) {
            out.emplace_back(surface);
            continue;
        }

        // Remainder must be computed before moving promoted polygons.
        ExPolygons remainder = diff_ex(surface.expolygon, promoted);
        for (ExPolygon &expoly : promoted) {
            Surface refined(surface, std::move(expoly));
            refined.surface_type = stBottom;
            out.emplace_back(std::move(refined));
        }
        // Original minus promoted: rim, true overhang, and rejected slivers stay Bridge together.
        for (ExPolygon &expoly : remainder)
            out.emplace_back(Surface(surface, std::move(expoly)));
    }

    layerm.fill_surfaces.surfaces = std::move(out);
}

} // namespace

// BBS multi-diameter: a suppressed orphan is sliced but never extruded, so the step above it rests on nothing
// there, while detect_surfaces_type() compared against the sliced step and typed it internal. Type it as the
// bridge it really is, in the outline of the face that was dropped, so bridge detection, the bottom shells over
// it and the slab merged onto that step all see the real bottom.
static void type_bridges_over_suppressed_orphans(const LayerPtrs &layers)
{
    for (size_t layer_idx = 1; layer_idx < layers.size(); ++ layer_idx) {
        const ExPolygons dropped = adaptive_suppressed_orphan_area(*layers[layer_idx - 1]);
        if (dropped.empty())
            continue;
        auto retype = [&dropped](SurfaceCollection &surfaces) {
            Surfaces out;
            bool     changed = false;
            for (const Surface &surface : surfaces.surfaces) {
                ExPolygons bridge;
                if (surface.surface_type == stInternal || surface.surface_type == stInternalSolid)
                    bridge = intersection_ex(ExPolygons{ surface.expolygon }, dropped);
                if (bridge.empty()) {
                    out.emplace_back(surface);
                    continue;
                }
                changed = true;
                for (ExPolygon &expolygon : diff_ex(ExPolygons{ surface.expolygon }, dropped))
                    out.emplace_back(surface, std::move(expolygon));
                for (ExPolygon &expolygon : bridge) {
                    out.emplace_back(surface, std::move(expolygon));
                    out.back().surface_type = stBottomBridge;
                    out.back().bridge_angle = -1;
                }
            }
            if (changed)
                surfaces.surfaces = std::move(out);
        };
        for (LayerRegion *layerm : layers[layer_idx]->regions()) {
            retype(layerm->slices);
            retype(layerm->fill_surfaces);
        }
    }
}

void PrintObject::prepare_infill()
{
    if (! this->set_started(posPrepareInfill))
        return;
    const bool record_prepare_infill_time = m_print->m_slice_time != nullptr;
    const long long prepare_infill_begin_time =
        record_prepare_infill_time ? Slic3r::Utils::get_current_milliseconds_time_monotonic() : 0;
    m_print->set_status(25, L("Generating infill regions"));
    if (m_typed_slices) {
        // To improve robustness of detect_surfaces_type() when reslicing (working with typed slices), see GH issue #7442.
        // The preceding step (perimeter generator) only modifies extra_perimeters and the extra perimeters are only used by discover_vertical_shells()
        // with more than a single region. If this step does not use Surface::extra_perimeters or Surface::extra_perimeters is always zero, it is safe
        // to reset to the untyped slices before re-runnning detect_surfaces_type().
        for (Layer* layer : m_layers) {
            layer->restore_untyped_slices_no_extra_perimeters();
            m_print->throw_if_canceled();
        }
    }

    // This will assign a type (top/bottom/internal) to $layerm->slices.
    // Then the classifcation of $layerm->slices is transfered onto
    // the $layerm->fill_surfaces by clipping $layerm->fill_surfaces
    // by the cummulative area of the previous $layerm->fill_surfaces.
    std::vector<std::vector<SurfaceCollection>> slice_surfaces_cpy;
    this->detect_surfaces_type(slice_surfaces_cpy);
    m_print->throw_if_canceled();
    if (this->print()->uses_multiple_nozzle_diameters())
        type_bridges_over_suppressed_orphans(m_layers);

    // Zero-gap Normal support: reclassify fill_surfaces that will be carried by the
    // support interface as stBottom, keep true overhangs as stBottomBridge (GitHub #11540).
    // Local masks only; slices stay typed as produced by detect_surfaces_type().
    if (m_layers.size() > 1 && should_refine_zero_gap_contact(m_config)) {
        // Both sources must be collected, exactly like SupportAnnotations does: slice_support_*()
        // only sees blocker / enforcer volumes, painted-on-mesh ones arrive as custom facets.
        std::vector<Polygons> blockers = this->slice_support_blockers();
        this->project_and_append_custom_facets(false, EnforcerBlockerType::BLOCKER, blockers);
        std::vector<Polygons> enforcers = this->slice_support_enforcers();
        this->project_and_append_custom_facets(false, EnforcerBlockerType::ENFORCER, enforcers);
        BOOST_LOG_TRIVIAL(debug) << "Refining zero-gap support contacts in parallel - start";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(1, m_layers.size()),
            [this, &blockers, &enforcers](const tbb::blocked_range<size_t> &range) {
                for (size_t layer_id = range.begin(); layer_id < range.end(); ++ layer_id) {
                    m_print->throw_if_canceled();
                    Layer *layer = m_layers[layer_id];
                    ZeroGapContactMask mask = compute_zero_gap_contact_mask(
                        *this, *layer, *m_layers[layer_id - 1], layer_id, blockers, enforcers);
                    if (mask.polygons.empty())
                        continue;
                    for (LayerRegion *layerm : layer->m_regions)
                        refine_fill_surfaces_for_zero_gap_contact(*layerm, mask.polygons, mask.sliver_r);
                }
            });
        BOOST_LOG_TRIVIAL(debug) << "Refining zero-gap support contacts in parallel - end";
    }

    // Also tiny stInternal surfaces are turned to stInternalSolid.
    BOOST_LOG_TRIVIAL(info) << "Preparing fill surfaces..." << log_memory_info();
    for (auto *layer : m_layers)
        for (auto *region : layer->m_regions) {
            region->prepare_fill_surfaces();
            m_print->throw_if_canceled();
        }

    // Add solid fills to ensure the shell vertical thickness.
    this->discover_vertical_shells();
    m_print->throw_if_canceled();


    // this will detect bridges and reverse bridges
    // and rearrange top/bottom/internal surfaces
    // It produces enlarged overlapping bridging areas.
    //
    // 1) stBottomBridge / stBottom infill is grown by 3mm and clipped by the total infill area. Bridges are detected. The areas may overlap.
    // 2) stTop is grown by 3mm and clipped by the grown bottom areas. The areas may overlap.
    // 3) Clip the internal surfaces by the grown top/bottom surfaces.
    // 4) Merge surfaces with the same style. This will mostly get rid of the overlaps.
    //FIXME This does not likely merge surfaces, which are supported by a material with different colors, but same properties.
    this->process_external_surfaces();
    m_print->throw_if_canceled();


    // Debugging output.
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        for (const Layer *layer : m_layers) {
            LayerRegion *layerm = layer->m_regions[region_id];
            layerm->export_region_slices_to_svg_debug("6_discover_vertical_shells-final");
            layerm->export_region_fill_surfaces_to_svg_debug("6_discover_vertical_shells-final");
        } // for each layer
    } // for each region
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    // Detect, which fill surfaces are near external layers.
    // They will be split in internal and internal-solid surfaces.
    // The purpose is to add a configurable number of solid layers to support the TOP surfaces
    // and to add a configurable number of solid layers above the BOTTOM / BOTTOMBRIDGE surfaces
    // to close these surfaces reliably.
    //FIXME Vojtech: Is this a good place to add supporting infills below sloping perimeters?
    this->discover_horizontal_shells();
    m_print->throw_if_canceled();

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        for (const Layer *layer : m_layers) {
            LayerRegion *layerm = layer->m_regions[region_id];
            layerm->export_region_slices_to_svg_debug("7_discover_horizontal_shells-final");
            layerm->export_region_fill_surfaces_to_svg_debug("7_discover_horizontal_shells-final");
        } // for each layer
    } // for each region
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    if (this->has_interlocking_beam())
        discover_shell_for_perimeters();
    reset_slice_surfaces(slice_surfaces_cpy);
    // Only active if config->infill_only_where_needed. This step trims the sparse infill,
    // so it acts as an internal support. It maintains all other infill types intact.
    // Here the internal surfaces and perimeters have to be supported by the sparse infill.
    //FIXME The surfaces are supported by a sparse infill, but the sparse infill is only as large as the area to support.
    // Likely the sparse infill will not be anchored correctly, so it will not work as intended.
    // Also one wishes the perimeters to be supported by a full infill.
    this->clip_fill_surfaces();
    m_print->throw_if_canceled();

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        for (const Layer *layer : m_layers) {
            LayerRegion *layerm = layer->m_regions[region_id];
            layerm->export_region_slices_to_svg_debug("8_clip_surfaces-final");
            layerm->export_region_fill_surfaces_to_svg_debug("8_clip_surfaces-final");
        } // for each layer
    } // for each region
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    // the following step needs to be done before combination because it may need
    // to remove only half of the combined infill
    this->bridge_over_infill();
    m_print->throw_if_canceled();

    // BBS multi-diameter: merge large-nozzle regions' internal fill into N x base height layers before the
    // regular infill combination, so the shared core is printed thick while the boundary keeps base height.
    this->combine_adaptive_infill();
    m_print->throw_if_canceled();

    // combine fill surfaces to honor the "infill every N layers" option
    this->combine_infill();
    m_print->throw_if_canceled();

    this->discover_sub_top_surfaces();
    m_print->throw_if_canceled();

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        for (const Layer *layer : m_layers) {
            LayerRegion *layerm = layer->m_regions[region_id];
            layerm->export_region_slices_to_svg_debug("9_prepare_infill-final");
            layerm->export_region_fill_surfaces_to_svg_debug("9_prepare_infill-final");
        } // for each layer
    } // for each region
    for (const Layer *layer : m_layers) {
        layer->export_region_slices_to_svg_debug("9_prepare_infill-final");
        layer->export_region_fill_surfaces_to_svg_debug("9_prepare_infill-final");
    } // for each layer
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    this->set_done(posPrepareInfill);

    if (record_prepare_infill_time)
        (*m_print->m_slice_time)[TIME_PREPARE_INFILL] +=
            Slic3r::Utils::get_current_milliseconds_time_monotonic() - prepare_infill_begin_time;
}

static const VolumeSlices *volume_slices_by_id(const std::vector<VolumeSlices> &all, const ObjectID id)
{
    for (const VolumeSlices &vs : all)
        if (vs.volume_id == id)
            return &vs;
    return nullptr;
}

// Island of a conformal modifier after earlier modifiers/model, before later
// volumes carve it. Radial rays are generated on this island.
static std::vector<std::vector<ExPolygon>> conformal_modifier_islands(const PrintObject &po, size_t region_id,
                                                                      size_t nlid, size_t lid0)
{
    std::vector<std::vector<ExPolygon>> out(nlid);
    const PrintObjectRegions *regions = po.shared_regions();
    const std::vector<VolumeSlices> &vol_slices = po.firstLayerObjSlice();
    if (regions == nullptr || vol_slices.empty() || po.layer_count() == 0)
        return out;

    for (size_t zi = 0; zi < po.layer_count(); ++zi) {
        const Layer *layer = po.get_layer(int(zi));
        const size_t lid   = layer->id() - lid0;
        if (lid >= nlid)
            continue;
        const double z = layer->slice_z;
        auto it = regions->layer_ranges.begin();
        for (; it != regions->layer_ranges.end(); ++it) {
            if (it->layer_height_range.first <= z + EPSILON && z <= it->layer_height_range.second + EPSILON)
                break;
        }
        if (it == regions->layer_ranges.end())
            continue;
        if (std::abs(z - it->layer_height_range.second) < EPSILON) {
            auto nxt = it;
            ++nxt;
            if (nxt != regions->layer_ranges.end() && std::abs(nxt->layer_height_range.first - z) < EPSILON)
                it = nxt;
        }
        const PrintObjectRegions::LayerRangeRegions &layer_range = *it;
        if (layer_range.volume_regions.empty())
            continue;

        std::vector<ExPolygons> temp(layer_range.volume_regions.size());
        for (size_t i = 0; i < layer_range.volume_regions.size(); ++i) {
            const VolumeSlices *vs = volume_slices_by_id(vol_slices, layer_range.volume_regions[i].model_volume->id());
            if (vs != nullptr && zi < vs->slices.size())
                temp[i] = vs->slices[zi];
        }

        for (int idx = 0; idx < int(layer_range.volume_regions.size()); ++idx) {
            if (temp[size_t(idx)].empty())
                continue;
            const PrintObjectRegions::VolumeRegion &vr = layer_range.volume_regions[size_t(idx)];
            if (!vr.model_volume->is_modifier() || vr.parent < 0)
                continue;
            const bool next_same = idx + 1 < int(layer_range.volume_regions.size()) &&
                layer_range.volume_regions[size_t(idx + 1)].model_volume == vr.model_volume;
            ExPolygons source = temp[size_t(idx)];
            const bool skip_periodic = vr.region != nullptr && vr.region->config().periodic_modifier.value &&
                !periodic_modifier_active(vr.region->config(), int(zi));
            if (skip_periodic || temp[size_t(vr.parent)].empty()) {
                temp[size_t(idx)].clear();
            } else {
                temp[size_t(idx)]           = intersection_ex(temp[size_t(vr.parent)], source);
                temp[size_t(vr.parent)]     = diff_ex(temp[size_t(vr.parent)], source);
            }
            if (next_same)
                temp[size_t(idx + 1)] = std::move(source);
            if (vr.region != nullptr && size_t(vr.region->print_object_region_id()) == region_id &&
                vr.region->config().conformal_infill && !temp[size_t(idx)].empty())
                append(out[lid], temp[size_t(idx)]);
        }
    }
    for (std::vector<ExPolygon> &row : out)
        if (row.size() > 1)
            row = union_ex(row);
    return out;
}

void PrintObject::infill()
{
    // prerequisites
    this->prepare_infill();

    if (this->set_started(posInfill)) {
        const bool record_infill_generate_time = m_print->m_slice_time != nullptr;
        const long long infill_generate_begin_time =
            record_infill_generate_time ? Slic3r::Utils::get_current_milliseconds_time_monotonic() : 0;
        m_print->set_status(35, L("Generating infill toolpath"));

        const auto& adaptive_fill_octree = this->m_adaptive_fill_octrees.first;
        const auto& support_fill_octree = this->m_adaptive_fill_octrees.second;

        bool   want_conformal = false;
        double chart_spacing  = 0.4;
        for (const PrintRegion &region : this->all_regions()) {
            if (region.config().conformal_infill) {
                want_conformal = true;
                if (region.config().sparse_infill_line_width > 0)
                    chart_spacing = region.config().sparse_infill_line_width;
                break;
            }
        }

        if (want_conformal && !m_layers.empty()) {
            const size_t lid0 = this->get_layer(0)->id();
            size_t       nlid = 0;
            for (const Layer *layer : m_layers)
                nlid = std::max(nlid, layer->id() - lid0 + 1);
            std::vector<double> zs(nlid, 0.);
            for (const Layer *layer : m_layers)
                zs[layer->id() - lid0] = layer->print_z;

            FillRadialZigZag::reset_n_lock();
            const size_t nreg = this->num_printing_regions();
            for (size_t region_id = 0; region_id < nreg; ++region_id) {
                const PrintRegionConfig &rcfg = this->printing_region(region_id).config();
                if (!rcfg.conformal_infill)
                    continue;
                const double density = std::max(0.05, 0.01 * rcfg.sparse_infill_density);
                double       line_w  = chart_spacing;
                if (rcfg.sparse_infill_line_width > 0)
                    line_w = rcfg.sparse_infill_line_width;
                std::vector<std::vector<ExPolygon>> islands_by_layer =
                    conformal_modifier_islands(*this, region_id, nlid, lid0);
                size_t n_cast = 0;
                for (const auto &row : islands_by_layer)
                    if (!row.empty())
                        ++n_cast;
                if (n_cast < 3) {
                    islands_by_layer.assign(nlid, {});
                    for (const Layer *layer : m_layers) {
                        if (region_id >= layer->regions().size())
                            continue;
                        const LayerRegion *lr = layer->regions()[region_id];
                        if (!lr->region().config().conformal_infill)
                            continue;
                        const size_t lid = layer->id() - lid0;
                        for (const Surface &surface : lr->fill_surfaces.surfaces)
                            if (surface.surface_type == stInternal)
                                islands_by_layer[lid].push_back(surface.expolygon);
                    }
                }
                FillRadialZigZag::pin_scan_counts(islands_by_layer, line_w / density, zs, region_id,
                                                   rcfg.conformal_ray_count.value);
            }
        } else {
            FillRadialZigZag::reset_n_lock();
        }

        //BOOST_LOG_TRIVIAL(debug) << "Filling layers in parallel - start";
        tbb::parallel_for(
           tbb::blocked_range<size_t>(0, m_layers.size()),
           [this, &adaptive_fill_octree = adaptive_fill_octree, &support_fill_octree = support_fill_octree](const tbb::blocked_range<size_t>& range) {
               for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                   m_print->throw_if_canceled();
                   m_layers[layer_idx]->make_fills(adaptive_fill_octree.get(), support_fill_octree.get(), this->m_lightning_generator.get());
                }
           }
        );
        m_print->throw_if_canceled();
        FillRadialZigZag::reset_n_lock();
        BOOST_LOG_TRIVIAL(debug) << "Filling layers in parallel - end";
        /*  we could free memory now, but this would make this step not idempotent
        ### $_->fill_surfaces->clear for map @{$_->regions}, @{$object->layers};
        */
        this->set_done(posInfill);

        if (record_infill_generate_time)
            (*m_print->m_slice_time)[TIME_INFILL_GENERATE] +=
                Slic3r::Utils::get_current_milliseconds_time_monotonic() - infill_generate_begin_time;
    }
}

void PrintObject::ironing()
{
    if (this->set_started(posIroning)) {
        BOOST_LOG_TRIVIAL(debug) << "Ironing in parallel - start";
        tbb::parallel_for(
            // Ironing starting with layer 0 to support ironing all surfaces.
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    m_print->throw_if_canceled();
                    m_layers[layer_idx]->make_ironing();
                }
            }
        );
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Ironing in parallel - end";
        this->set_done(posIroning);
    }
}

// BBS
void PrintObject::clear_overhangs_for_lift()
{
    if (!m_shared_object) {
        for (Layer* l : m_layers)
            l->loverhangs.clear();
    }
}

static const float g_min_overhang_percent_for_lift = 0.3f;

#define REGISTER_SUPPORTS_FOR_LIFT

void PrintObject::detect_overhangs_for_lift()
{
    if (this->set_started(posDetectOverhangsForLift)) {
        // This object-level heuristic has no region context. The first nozzle is an intentional
        // approximation used only to scale the overhang-detection tolerance.
        const float reference_nozzle = float(primary_nozzle_diameter(this->print()->full_print_config()));
        const float line_width = float(Flow::extrusion_width_for_nozzle(
            m_config.line_width, reference_nozzle, reference_nozzle,
            multi_nozzle_diameter_mode_enabled(this->print()->full_print_config())));
        const float min_overlap = line_width * g_min_overhang_percent_for_lift;
        size_t num_layers = this->layer_count();
        size_t num_raft_layers = m_slicing_params.raft_layers();

        m_print->set_status(71, L("Detect overhangs for auto-lift"));

        this->clear_overhangs_for_lift();

        tbb::spin_mutex layer_storage_mutex;
        tbb::parallel_for(tbb::blocked_range<size_t>(num_raft_layers + 1, num_layers), [this, min_overlap, line_width](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_id = range.begin(); layer_id < range.end(); ++layer_id) {
                Layer &layer       = *m_layers[layer_id];
                Layer &lower_layer = *layer.lower_layer;

                ExPolygons overhangs = diff_ex(layer.lslices, offset_ex(lower_layer.lslices, scale_(min_overlap)));
                layer.loverhangs     = std::move(offset2_ex(overhangs, -0.1f * scale_(line_width), 0.1f * scale_(line_width)));

#ifdef REGISTER_SUPPORTS_FOR_LIFT
                // register all supports to avoidance region for lift
                if (layer_id < m_support_layers.size()) {
                    auto &support_layer = m_support_layers[layer_id];
                    if (support_layer) {
                        if (!support_layer->support_islands.empty()) {
                            append(layer.loverhangs,
                                   std::move(offset2_ex(support_layer->support_islands, -0.1f * scale_(line_width), 0.1f * scale_(line_width))));
                        } else {
                            ExPolygons support_infill_polygons = union_ex(support_layer->support_fills.polygons_covered_by_spacing(double(coord_t(SCALED_EPSILON))));
                            append(layer.loverhangs, std::move(offset2_ex(support_infill_polygons, -0.1f * scale_(line_width), 0.1f * scale_(line_width))));
                        }
                    }
                }
#endif
                layer.loverhangs_bbox = get_extents(layer.loverhangs);
            }
        });
        this->set_done(posDetectOverhangsForLift);
    }
}

void PrintObject::generate_support_material()
{
    if (this->set_started(posSupportMaterial)) {
        this->clear_support_layers();

        if (!has_support() && !m_print->get_no_check_flag()) {
            // BBS: pop a warning if objects have significant amount of overhangs but support material is not enabled
            // Note: we also need to pop warning if support is disabled and only raft is enabled
            m_print->set_status(50, L("Checking support necessity"));
            typedef std::chrono::high_resolution_clock           clock_;
            typedef std::chrono::duration<double, std::ratio<1>> second_;
            std::chrono::time_point<clock_>                      t0{clock_::now()};

            SupportNecessaryType sntype = this->is_support_necessary();

            double duration{std::chrono::duration_cast<second_>(clock_::now() - t0).count()};
            BOOST_LOG_TRIVIAL(info) << std::fixed << std::setprecision(0) << "is_support_necessary takes " << duration << " secs.";

            if (sntype != NoNeedSupp) {
                std::map<SupportNecessaryType, std::string> reasons = {{SharpTail, L("floating regions")},
                                                                       {Cantilever, L("floating cantilever")},
                                                                       {LargeOverhang, L("large overhangs")}};
                std::string warning_message                         = Slic3r::format(L("It seems object %s has %s. Please re-orient the object or enable support generation."),
                                                                                     this->model_object()->name, reasons[sntype]);
                this->active_step_add_warning(PrintStateBase::WarningLevel::NON_CRITICAL, warning_message, PrintStateBase::SlicingNeedSupportOn);
            }
        }

        if ((this->has_support() && m_layers.size() > 1) || (this->has_raft() && !m_layers.empty())) {
            m_print->set_status(50, L("Generating support"));

            this->_generate_support_material();
            m_print->throw_if_canceled();

            // Gated on enable_support rather than has_support(): with enforce_support_layers alone the
            // support is confined to the bottom layers by design, while detect_overhangs() still fills
            // every layer, which would make the check below fire on the whole object.
            if (m_config.enable_support.value && is_tree(m_config.support_type.value) && !m_print->get_no_check_flag())
                this->warn_uncovered_overhangs();
        }
        this->set_done(posSupportMaterial);
    }
}

void PrintObject::simplify_extrusion_path()
{
    if (this->set_started(posSimplifyWall)) {
        m_print->set_status(75, L("Optimizing toolpath"));
        BOOST_LOG_TRIVIAL(debug) << "Simplify wall extrusion path of object in parallel - start";
        //BBS: walls
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    m_print->throw_if_canceled();
                    m_layers[layer_idx]->simplify_wall_extrusion_path();
                }
            }
        );
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Simplify wall extrusion path of object in parallel - end";
        this->set_done(posSimplifyWall);
    }

    if (this->set_started(posSimplifyInfill)) {
        m_print->set_status(75, L("Optimizing toolpath"));
        BOOST_LOG_TRIVIAL(debug) << "Simplify infill extrusion path of object in parallel - start";
        //BBS: infills
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
                    m_print->throw_if_canceled();
                    m_layers[layer_idx]->simplify_infill_extrusion_path();
                }
            }
        );
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Simplify infill extrusion path of object in parallel - end";
        this->set_done(posSimplifyInfill);
    }

    if (this->set_started(posSimplifySupportPath)) {
        m_print->set_status(75, L("Optimizing toolpath"));
        BOOST_LOG_TRIVIAL(debug) << "Simplify extrusion path of support in parallel - start";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_support_layers.size()),
            [this](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    m_print->throw_if_canceled();
                    m_support_layers[layer_idx]->simplify_support_extrusion_path();
                }
            }
        );
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Simplify extrusion path of support in parallel - end";
        this->set_done(posSimplifySupportPath);
    }
}

std::pair<FillAdaptive::OctreePtr, FillAdaptive::OctreePtr> PrintObject::prepare_adaptive_infill_data(
    const std::vector<std::pair<const Surface *, float>> &surfaces_w_bottom_z) const
{
    using namespace FillAdaptive;

    auto [adaptive_line_spacing, support_line_spacing] = adaptive_fill_line_spacing(*this);
    if ((adaptive_line_spacing == 0. && support_line_spacing == 0.) || this->layers().empty())
        return std::make_pair(OctreePtr(), OctreePtr());

    indexed_triangle_set mesh = this->model_object()->raw_indexed_triangle_set();
    // Rotate mesh and build octree on it with axis-aligned (standart base) cubes.
    auto to_octree = transform_to_octree().toRotationMatrix();
    its_transform(mesh, to_octree * this->trafo_centered(), true);

    // Triangulate internal bridging surfaces.
    std::vector<std::vector<Vec3d>> overhangs(std::max(surfaces_w_bottom_z.size(), size_t(1)));
    // ^ make sure vector is not empty, even with no briding surfaces we still want to build the adaptive trees later, some continue normally
    tbb::parallel_for(tbb::blocked_range<int>(0, surfaces_w_bottom_z.size()),
        [this, &to_octree, &overhangs, &surfaces_w_bottom_z](const tbb::blocked_range<int> &range) {
            for (int surface_idx = range.begin(); surface_idx < range.end(); ++surface_idx) {
                std::vector<Vec3d> &out = overhangs[surface_idx];
                m_print->throw_if_canceled();
                append(out, triangulate_expolygon_3d(surfaces_w_bottom_z[surface_idx].first->expolygon,
                    surfaces_w_bottom_z[surface_idx].second));
                for (Vec3d &p : out)
                    p = (to_octree * p).eval();
            }
        });
    // and gather them.
    for (size_t i = 1; i < overhangs.size(); ++ i)
        append(overhangs.front(), std::move(overhangs[i]));

    return std::make_pair(
        adaptive_line_spacing ? build_octree(mesh, overhangs.front(), adaptive_line_spacing, false) : OctreePtr(),
        support_line_spacing  ? build_octree(mesh, overhangs.front(), support_line_spacing, true) : OctreePtr());
}

FillLightning::GeneratorPtr PrintObject::prepare_lightning_infill_data()
{
    bool has_lightning_infill = false;
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id)
        if (const PrintRegionConfig& config = this->printing_region(region_id).config(); config.sparse_infill_density > 0 && config.sparse_infill_pattern == ipLightning) {
            has_lightning_infill = true;
            break;
        }

    return has_lightning_infill ? FillLightning::build_generator(std::as_const(*this), [this]() -> void { this->throw_if_canceled(); }) : FillLightning::GeneratorPtr();
}

void PrintObject::clear_layers()
{
    if (!m_shared_object) {
        for (Layer *l : m_layers)
            delete l;
        m_layers.clear();
    }
}

Layer* PrintObject::add_layer(int id, coordf_t height, coordf_t print_z, coordf_t slice_z)
{
    m_layers.emplace_back(new Layer(id, this, height, print_z, slice_z));
    return m_layers.back();
}

const SupportLayer* PrintObject::get_support_layer_at_printz(coordf_t print_z, coordf_t epsilon) const
{
    coordf_t limit = print_z - epsilon;
    auto it = Slic3r::lower_bound_by_predicate(m_support_layers.begin(), m_support_layers.end(), [limit](const SupportLayer* layer) { return layer->print_z < limit; });
    return (it == m_support_layers.end() || (*it)->print_z > print_z + epsilon) ? nullptr : *it;
}

SupportLayer* PrintObject::get_support_layer_at_printz(coordf_t print_z, coordf_t epsilon)
{
    return const_cast<SupportLayer*>(std::as_const(*this).get_support_layer_at_printz(print_z, epsilon));
}

void PrintObject::clear_support_layers()
{
    if (!m_shared_object) {
        for (SupportLayer* l : m_support_layers)
            delete l;
        m_support_layers.clear();
        for (auto l : m_layers) {
            l->sharp_tails.clear();
            l->sharp_tails_height.clear();
            l->cantilevers.clear();
        }
    }
}

std::shared_ptr<TreeSupportData> PrintObject::alloc_tree_support_preview_cache()
{
    if (!m_tree_support_preview_cache) {
        const coordf_t xy_distance = m_config.support_object_xy_distance.value;
        m_tree_support_preview_cache = std::make_shared<TreeSupportData>(*this, xy_distance, g_config_tree_support_collision_resolution);
    }

    return m_tree_support_preview_cache;
}

SupportLayer* PrintObject::add_tree_support_layer(int id, coordf_t height, coordf_t print_z, coordf_t slice_z)
{
    m_support_layers.emplace_back(new SupportLayer(id, 0, this, height, print_z, slice_z));
    return m_support_layers.back();
}

SupportLayer* PrintObject::add_support_layer(int id, int interface_id, coordf_t height, coordf_t print_z)
{
    m_support_layers.emplace_back(new SupportLayer(id, interface_id, this, height, print_z, -1));
    return m_support_layers.back();
}

SupportLayerPtrs::iterator PrintObject::insert_support_layer(SupportLayerPtrs::iterator pos, size_t id, size_t interface_id, coordf_t height, coordf_t print_z, coordf_t slice_z)
{
    return m_support_layers.insert(pos, new SupportLayer(id, interface_id, this, height, print_z, slice_z));
}

// Called by Print::apply().
// This method only accepts PrintObjectConfig and PrintRegionConfig option keys.
bool PrintObject::invalidate_state_by_config_options(
    const ConfigOptionResolver &old_config, const ConfigOptionResolver &new_config, const std::vector<t_config_option_key> &opt_keys)
{
    if (opt_keys.empty())
        return false;

    std::vector<PrintObjectStep> steps;
    bool invalidated = false;
    for (const t_config_option_key &opt_key : opt_keys) {
        if (   opt_key == "brim_width"
            || opt_key == "brim_object_gap"
            || opt_key == "brim_type"
            // BBS: brim generation depends on printing speed
            || opt_key == "outer_wall_speed"
            || opt_key == "sparse_infill_speed"
            || opt_key == "inner_wall_speed"
            || opt_key == "support_speed"
            || opt_key == "internal_solid_infill_speed"
            || opt_key == "top_surface_speed") {
            // Brim is printed below supports, support invalidates brim and skirt.
            steps.emplace_back(posSupportMaterial);
            if (opt_key == "brim_type") {
                const auto* old_brim_type = old_config.option<ConfigOptionEnum<BrimType>>(opt_key);
                const auto* new_brim_type = new_config.option<ConfigOptionEnum<BrimType>>(opt_key);
                //BBS: When switch to manual brim, the object must have brim, then re-generate perimeter
                //to make the wall order of first layer to be outer-first
                if (old_brim_type->value == btOuterOnly || new_brim_type->value == btOuterOnly)
                    steps.emplace_back(posPerimeters);
            }
        } else if (
               opt_key == "periodic_modifier"
            || opt_key == "periodic_modifier_skip_layers"
            || opt_key == "periodic_modifier_apply_layers"
            || opt_key == "modifier_ignore_infill") {
            // Region assignment changes per layer when a modifier is skipped.
            // Ignoring infill keys can also merge/split regions vs the parent.
            steps.emplace_back(posSlice);
        } else if (
               opt_key == "wall_loops"
            || opt_key == "top_one_wall_type"
            || opt_key == "top_area_threshold"
            || opt_key == "only_one_wall_first_layer"
            || opt_key == "initial_layer_line_width"
            || opt_key == "inner_wall_line_width"
            || opt_key == "infill_wall_overlap"
            || opt_key == "apply_scarf_seam_on_circles") {
            steps.emplace_back(posPerimeters);
        } else if (opt_key == "gap_infill_speed" || opt_key == "filter_out_gap_fill") {
            // Return true if gap-fill speed has changed from zero value to non-zero or from non-zero value to zero.
            auto is_gap_fill_changed_state_due_to_speed = [&opt_key, &old_config, &new_config]() -> bool {
                if (opt_key == "gap_infill_speed") {
                    const auto *old_gap_fill_speed = old_config.option<ConfigOptionFloatsNullable>(opt_key);
                    const auto *new_gap_fill_speed = new_config.option<ConfigOptionFloatsNullable>(opt_key);
                    assert(old_gap_fill_speed && new_gap_fill_speed);
                    return (old_gap_fill_speed->values.size() != new_gap_fill_speed->values.size())
                        || (old_gap_fill_speed->values != new_gap_fill_speed->values);
                }
                return false;
            };

            // Filtering of unprintable regions in multi-material segmentation depends on if gap-fill is enabled or not.
            // So step posSlice is invalidated when gap-fill was enabled/disabled by option "gap_fill_enabled" or by
            // changing "gap_infill_speed" to force recomputation of the multi-material segmentation.
            if (this->is_mm_painted() && ((opt_key == "gap_infill_speed" || opt_key == "filter_out_gap_fill") && is_gap_fill_changed_state_due_to_speed()))
                steps.emplace_back(posSlice);
            steps.emplace_back(posPerimeters);
        } else if (
               opt_key == "layer_height"
            || opt_key == "mmu_segmented_region_max_width"
            || opt_key == "mmu_segmented_region_interlocking_depth"
            || opt_key == "raft_layers"
            || opt_key == "raft_contact_distance"
            || opt_key == "slice_closing_radius"
            || opt_key == "slicing_mode"
            || opt_key == "interlocking_beam"
            || opt_key == "interlocking_orientation"
            || opt_key == "interlocking_beam_layer_count"
            || opt_key == "interlocking_depth"
            || opt_key == "interlocking_boundary_avoidance"
            || opt_key == "interlocking_beam_width") {
            steps.emplace_back(posSlice);
		} else if (
               opt_key == "elefant_foot_compensation"
            || opt_key == "support_top_z_distance"
            || opt_key == "support_bottom_z_distance"
            || opt_key == "xy_hole_compensation"
            || opt_key == "xy_contour_compensation") {
            steps.emplace_back(posSlice);
        } else if (opt_key == "enable_support") {
            steps.emplace_back(posSupportMaterial);
            if (m_config.support_top_z_distance == 0.) {
            	// Enabling / disabling supports while soluble support interface is enabled.
            	// This changes the bridging logic (bridging enabled without supports, disabled with supports).
            	// Reset everything.
            	// See GH #1482 for details.
	            steps.emplace_back(posSlice);
	        }
        } else if (
        	   opt_key == "support_type"
            || opt_key == "support_angle"
            || opt_key == "support_on_build_plate_only"
            || opt_key == "support_critical_regions_only"
            || opt_key == "support_remove_small_overhang"
            || opt_key == "enforce_support_layers"
            || opt_key == "support_filament"
            || opt_key == "support_line_width"
            || opt_key == "support_interface_top_layers"
            || opt_key == "support_interface_bottom_layers"
            || opt_key == "support_interface_pattern"
            || opt_key == "support_interface_loop_pattern"
            || opt_key == "support_interface_filament"
            || opt_key == "support_interface_not_for_body"
            || opt_key == "support_interface_spacing"
            || opt_key == "support_bottom_interface_spacing" //BBS
            || opt_key == "support_base_pattern"
            || opt_key == "support_style"
            || opt_key == "support_object_xy_distance"
            || opt_key == "support_object_first_layer_gap"
            || opt_key == "support_base_pattern_spacing"
            || opt_key == "support_expansion"
            //|| opt_key == "independent_support_layer_height" // BBS
            || opt_key == "support_threshold_angle"
            || opt_key == "enable_support_ironing"
            || opt_key == "support_ironing_pattern"
            || opt_key == "support_ironing_flow"
            || opt_key == "support_ironing_spacing"
            || opt_key == "support_ironing_inset"
            || opt_key == "support_ironing_direction"
            || opt_key == "support_ironing_speed"
            || opt_key == "raft_expansion"
            || opt_key == "raft_first_layer_density"
            || opt_key == "raft_first_layer_expansion"
            || opt_key == "bridge_no_support"
            || opt_key == "max_bridge_length"
            || opt_key == "initial_layer_line_width"
            || opt_key == "tree_support_branch_distance"
            || opt_key == "tree_support_branch_diameter"
            || opt_key == "tree_support_branch_angle"
            || opt_key == "tree_support_branch_diameter_angle"
            || opt_key == "tree_support_tip_diameter"
            || opt_key == "tree_support_wall_count") {
            steps.emplace_back(posSupportMaterial);
            if (m_config.support_top_z_distance == 0.) {
                // Zero-gap contact refinement in prepare_infill reads these support settings.
                steps.emplace_back(posPrepareInfill);
            }
        } else if (
               opt_key == "bottom_shell_layers"
            || opt_key == "top_shell_layers"
            || opt_key == "top_color_penetration_layers"
            || opt_key == "bottom_color_penetration_layers"
            || opt_key == "infill_instead_top_bottom_surfaces") {

            steps.emplace_back(posSlice);
#if (0)
            const auto *old_shell_layers = old_config.option<ConfigOptionInt>(opt_key);
            const auto *new_shell_layers = new_config.option<ConfigOptionInt>(opt_key);
            assert(old_shell_layers && new_shell_layers);

            bool value_changed = (old_shell_layers->value == 0 && new_shell_layers->value > 0) ||
                                 (old_shell_layers->value > 0 && new_shell_layers->value == 0);

            if (value_changed && this->object_extruders().size() > 1) {
                steps.emplace_back(posSlice);
            }
            else if (m_print->config().spiral_mode && opt_key == "bottom_shell_layers") {
                // Changing the number of bottom layers when a spiral vase is enabled requires re-slicing the object again.
                // Otherwise, holes in the bottom layers could be filled, as is reported in GH #5528.
                steps.emplace_back(posSlice);
            }
#endif
        } else if (
               opt_key == "interface_shells"
            || opt_key == "fill_multiline"
            || opt_key == "infill_combination"
            || opt_key == "bottom_shell_thickness"
            || opt_key == "top_shell_thickness"
            || opt_key == "minimum_sparse_infill_area"
            || opt_key == "sparse_infill_filament"
            || opt_key == "solid_infill_filament"
            || opt_key == "sparse_infill_line_width"
            || opt_key == "skin_infill_line_width"
            || opt_key == "skeleton_infill_line_width"
            || opt_key == "infill_direction"
            || opt_key == "ensure_vertical_shell_thickness"
            || opt_key == "bridge_angle"
            //BBS
        ) {
            steps.emplace_back(posPrepareInfill);
        } else if (
               opt_key == "top_surface_pattern"
            || opt_key == "top_surface_density"
            || opt_key == "monotonic_travel_into_wall"
            || opt_key == "bottom_surface_pattern"
            || opt_key == "bottom_surface_density"
            || opt_key == "internal_solid_infill_pattern"
            || opt_key == "sub_top_surface_pattern"
            || opt_key == "external_fill_link_max_length"
            || opt_key == "sparse_infill_anchor"
            || opt_key == "sparse_infill_anchor_max"
            || opt_key == "top_surface_line_width"
            || opt_key == "initial_layer_line_width"
            || opt_key == "detect_floating_vertical_shell") {
            steps.emplace_back(posInfill);
        } else if (opt_key == "sparse_infill_pattern"
                   || opt_key == "conformal_infill"
                   || opt_key == "conformal_stagger"
                   || opt_key == "conformal_link_keep_layers"
                   || opt_key == "conformal_link_flip_layers"
                   || opt_key == "conformal_pole"
                   || opt_key == "conformal_ray_count"
                   || opt_key == "conformal_hub_radius"
                   || opt_key == "symmetric_infill_y_axis"
                   || opt_key == "infill_shift_step"
                   || opt_key == "sparse_infill_lattice_angle_1"
                   || opt_key == "sparse_infill_lattice_angle_2"
                   || opt_key == "infill_rotate_step"
                   || opt_key == "skeleton_infill_density"
                   || opt_key == "skin_infill_density"
                   || opt_key == "infill_lock_depth"
                   || opt_key == "skin_infill_depth"
                   || opt_key == "locked_skin_infill_pattern"
                   || opt_key == "locked_skeleton_infill_pattern") {
            steps.emplace_back(posPrepareInfill);
        } else if (opt_key == "sparse_infill_density") {
            // One likely wants to reslice only when switching between zero infill to simulate boolean difference (subtracting volumes),
            // normal infill and 100% (solid) infill.
            const auto *old_density = old_config.option<ConfigOptionPercent>(opt_key);
            const auto *new_density = new_config.option<ConfigOptionPercent>(opt_key);
            assert(old_density && new_density);
            //FIXME Vojtech is not quite sure about the 100% here, maybe it is not needed.
            if (is_approx(old_density->value, 0.) || is_approx(old_density->value, 100.) ||
                is_approx(new_density->value, 0.) || is_approx(new_density->value, 100.))
                steps.emplace_back(posPerimeters);
            steps.emplace_back(posPrepareInfill);
        } else if (opt_key == "internal_solid_infill_line_width") {
            // This value is used for calculating perimeter - infill overlap, thus perimeters need to be recalculated.
            steps.emplace_back(posPerimeters);
            steps.emplace_back(posPrepareInfill);
        } else if (
               opt_key == "outer_wall_line_width"
            || opt_key == "wall_filament"
            || opt_key == "fuzzy_skin"
            || opt_key == "fuzzy_skin_thickness"
            || opt_key == "fuzzy_skin_point_distance"
            || opt_key == "fuzzy_skin_first_layer"
            || opt_key == "fuzzy_skin_noise_type"
            || opt_key == "fuzzy_skin_scale"
            || opt_key == "fuzzy_skin_octaves"
            || opt_key == "fuzzy_skin_persistence"
            || opt_key == "fuzzy_skin_mode"
            || opt_key == "detect_overhang_wall"
            //BBS
            || opt_key == "enable_overhang_speed"
            || opt_key == "detect_thin_wall"
            || opt_key == "precise_outer_wall") {
            // filament_shrink is applied to slice contours in slice_volumes() (posSlice) using wall_filament.
            if (opt_key == "wall_filament")
                steps.emplace_back(posSlice);
            steps.emplace_back(posPerimeters);
            steps.emplace_back(posSupportMaterial);
        } else if (opt_key == "bridge_flow") {
            steps.emplace_back(posPerimeters);
            steps.emplace_back(posInfill);
	        steps.emplace_back(posSupportMaterial);
        } else if (
                opt_key == "wall_generator"
            || opt_key == "wall_transition_length"
            || opt_key == "wall_transition_filter_deviation"
            || opt_key == "wall_transition_angle"
            || opt_key == "wall_distribution_count"
            || opt_key == "min_feature_size"
            || opt_key == "min_bead_width") {
            steps.emplace_back(posSlice);
        } else if (
               opt_key == "seam_position"
            || opt_key == "seam_placement_away_from_overhangs"
            || opt_key == "seam_slope_conditional"
            || opt_key == "scarf_angle_threshold"
            || opt_key == "seam_slope_entire_loop"
            || opt_key == "seam_slope_steps"
            || opt_key == "seam_slope_inner_walls"
            || opt_key == "seam_gap"
            || opt_key == "wipe_speed"
            || opt_key == "role_base_wipe_speed"
            || opt_key == "support_speed"
            || opt_key == "support_interface_speed"
            || opt_key == "smooth_speed_discontinuity_area"
            || opt_key == "smooth_coefficient"
            || opt_key == "overhang_1_4_speed"
            || opt_key == "overhang_2_4_speed"
            || opt_key == "overhang_3_4_speed"
            || opt_key == "overhang_4_4_speed"
            || opt_key == "overhang_totally_speed"
            || opt_key == "bridge_speed"
            || opt_key == "outer_wall_speed"
            || opt_key == "small_perimeter_speed"
            || opt_key == "small_perimeter_threshold"
            || opt_key == "sparse_infill_speed"
            || opt_key == "inner_wall_speed"
            || opt_key == "internal_solid_infill_speed"
            || opt_key == "top_surface_speed"
            || opt_key == "vertical_shell_speed"
            || opt_key == "enable_height_slowdown"
            || opt_key == "slowdown_start_height"
            || opt_key == "slowdown_start_speed"
            || opt_key == "slowdown_start_acc"
            || opt_key == "slowdown_end_height"
            || opt_key == "slowdown_end_speed"
            || opt_key == "slowdown_end_acc" ) {
            invalidated |= m_print->invalidate_step(psGCodeExport);
        } else if (
               opt_key == "flush_into_infill"
            || opt_key == "flush_into_objects"
            || opt_key == "flush_into_support") {
            invalidated |= m_print->invalidate_step(psWipeTower);
            invalidated |= m_print->invalidate_step(psGCodeExport);
        } else {
            // for legacy, if we can't handle this option let's invalidate all steps
            this->invalidate_all_steps();
            invalidated = true;
        }
    }

    sort_remove_duplicates(steps);
    for (PrintObjectStep step : steps)
        invalidated |= this->invalidate_step(step);
    return invalidated;
}

bool PrintObject::invalidate_step(PrintObjectStep step)
{
	bool invalidated = Inherited::invalidate_step(step);

    // propagate to dependent steps
    if (step == posPerimeters) {
		invalidated |= this->invalidate_steps({ posPrepareInfill, posInfill, posIroning, posSimplifyWall, posSimplifyInfill });
        invalidated |= m_print->invalidate_steps({ psSkirtBrim });
    } else if (step == posPrepareInfill) {
        invalidated |= this->invalidate_steps({ posInfill, posIroning, posSimplifyWall, posSimplifyInfill });
        invalidated |= m_print->invalidate_steps({psSkirtBrim});
    } else if (step == posInfill) {
        invalidated |= this->invalidate_steps({ posIroning, posSimplifyInfill });
        invalidated |= m_print->invalidate_steps({ psSkirtBrim });
    } else if (step == posSlice) {
		invalidated |= this->invalidate_steps({ posPerimeters, posPrepareInfill, posInfill, posIroning, posSupportMaterial, posSimplifyWall, posSimplifyInfill });
        invalidated |= m_print->invalidate_steps({ psSkirtBrim });
        m_slicing_params.valid = false;
    } else if (step == posSupportMaterial) {
        invalidated |= this->invalidate_steps({ posSimplifySupportPath });
        invalidated |= m_print->invalidate_steps({ psSkirtBrim });
        m_slicing_params.valid = false;
    }
    // BBS multi-diameter: the support merge plan reads which filaments the walls and fills of each layer extrude.
    if ((step == posPerimeters || step == posPrepareInfill || step == posInfill) &&
        multi_nozzle_diameter_mode_enabled(m_print->full_print_config()))
        invalidated |= this->invalidate_step(posSupportMaterial);

    // Wipe tower depends on the ordering of extruders, which in turn depends on everything.
    // It also decides about what the flush_into_infill / wipe_into_object / flush_into_support features will do,
    // and that too depends on many of the settings.
    invalidated |= m_print->invalidate_step(psWipeTower);
    // Invalidate G-code export in any case.
    invalidated |= m_print->invalidate_step(psGCodeExport);
    return invalidated;
}

bool PrintObject::invalidate_all_steps()
{
	// First call the "invalidate" functions, which may cancel background processing.
    bool result = Inherited::invalidate_all_steps() | m_print->invalidate_all_steps();
	// Then reset some of the depending values.
	m_slicing_params.valid = false;
	return result;
}

void PrintObject::reset_slice_surfaces(const std::vector<std::vector<SurfaceCollection>> &slice_surfaces_cpy){
    //reset infill surface and slice data back
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        for (size_t idx_layer = 0; idx_layer < m_layers.size(); ++ idx_layer) {

            Layer       *layer  = m_layers[idx_layer];
            LayerRegion *layerm = layer->m_regions[region_id];
            if (layerm->region().config().infill_instead_top_bottom_surfaces && layerm->region().config().sparse_infill_pattern == ipLockedZag) {
                layerm->slices = slice_surfaces_cpy[idx_layer][region_id];
                ExPolygons exps;
                layerm->fill_surfaces.keep_type(SurfaceType::stInternal, exps);
                layerm->fill_surfaces.append(exps, SurfaceType::stTop);
            }
        }
    }
}

// This function analyzes slices of a region (SurfaceCollection slices).
// Each region slice (instance of Surface) is analyzed, whether it is supported or whether it is the top surface.
// Initially all slices are of type stInternal.
// Slices are compared against the top / bottom slices and regions and classified to the following groups:
// stTop          - Part of a region, which is not covered by any upper layer. This surface will be filled with a top solid infill.
// stBottomBridge - Part of a region, which is not fully supported, but it hangs in the air, or it hangs losely on a support or a raft.
// stBottom       - Part of a region, which is not supported by the same region, but it is supported either by another region, or by a soluble interface layer.
// stInternal     - Part of a region, which is supported by the same region type.
// If a part of a region is of stBottom and stTop, the stBottom wins.
void PrintObject::detect_surfaces_type(std::vector<std::vector<SurfaceCollection>> &slice_surfaces_cpy)
{
    BOOST_LOG_TRIVIAL(info) << "Detecting solid surfaces..." << log_memory_info();

    // Interface shells: the intersecting parts are treated as self standing objects supporting each other.
    // Each of the objects will have a full number of top / bottom layers, even if these top / bottom layers
    // are completely hidden inside a collective body of intersecting parts.
    // This is useful if one of the parts is to be dissolved, or if it is transparent and the internal shells
    // should be visible.
    bool spiral_mode      = this->print()->config().spiral_mode.value;
    bool interface_shells = ! spiral_mode && m_config.interface_shells.value;
    size_t num_layers     = spiral_mode ? std::min(size_t(this->printing_region(0).config().bottom_shell_layers), m_layers.size()) : m_layers.size();

    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        BOOST_LOG_TRIVIAL(debug) << "Detecting solid surfaces for region " << region_id << " in parallel - start";
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
        for (Layer *layer : m_layers)
            layer->m_regions[region_id]->export_region_fill_surfaces_to_svg_debug("1_detect_surfaces_type-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

        // If interface shells are allowed, the region->surfaces cannot be overwritten as they may be used by other threads.
        // Cache the result of the following parallel_loop.
        std::vector<Surfaces> surfaces_new;
        if (interface_shells)
            surfaces_new.assign(num_layers, Surfaces());

        slice_surfaces_cpy.resize(m_layers.size());
        // interface_shell 启用与否，决定着是否区分不同材料。开启后，不同材料间的接触面都会被识别为顶面、底面
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0,
            	spiral_mode ?
            		// In spiral vase mode, reserve the last layer for the top surface if more than 1 layer is planned for the vase bottom.
            		((num_layers > 1) ? num_layers - 1 : num_layers) :
            		// In non-spiral vase mode, go over all layers.
            		m_layers.size()),
                [this, spiral_mode, region_id, interface_shells, &surfaces_new, &slice_surfaces_cpy](const tbb::blocked_range<size_t> &range) {
                // BBS coconut: can't set to stBottom when soluable support is used, as the support may not be actaully generated, e.g. when "on build plate only" option is enabled. See github #3507.
                // Zero-gap Normal/auto contacts may later be refined back to stBottom in prepare_infill, see github #11540.
                SurfaceType surface_type_bottom_other = stBottomBridge;
                for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                    m_print->throw_if_canceled();
                    // BOOST_LOG_TRIVIAL(trace) << "Detecting solid surfaces for region " << region_id << " and layer " << layer->print_z;
                    Layer       *layer  = m_layers[idx_layer];
                    LayerRegion *layerm = layer->m_regions[region_id];
                    slice_surfaces_cpy[idx_layer].resize(layer->m_regions.size());
                    //record surface data
                    if (layerm->region().config().infill_instead_top_bottom_surfaces && layerm->region().config().sparse_infill_pattern == ipLockedZag) {
                        // layerm->fill_surfaces_copy = layerm->fill_expolygons;
                        slice_surfaces_cpy[idx_layer][region_id] = layerm->slices;
                    }
                    // comparison happens against the *full* slices (considering all regions)
                    // unless internal shells are requested
                    Layer       *upper_layer = (idx_layer + 1 < this->layer_count()) ? m_layers[idx_layer + 1] : nullptr;
                    Layer       *lower_layer = (idx_layer > 0) ? m_layers[idx_layer - 1] : nullptr;
                    // collapse very narrow parts (using the safety offset in the diff is not enough)
                    float        offset = layerm->flow(frExternalPerimeter).scaled_width() / 10.f;

                    bool detect_top = spiral_mode || layerm->region().config().top_shell_layers;
                    bool detect_bottom = spiral_mode || layerm->region().config().bottom_shell_layers;

                    // When counterbore_hole_bridging is set to "Sacrificial Layer" (chbFilled),
                    // process_no_bridge() may have added extra fill surfaces that extend beyond the original slices.
                    // We need to union these with the slices to ensure proper top/bottom surface detection.
                    ExPolygons layerm_slices_surfaces = to_expolygons(layerm->slices.surfaces);
                    if (layerm->region().config().counterbore_hole_bridging.value == chbFilled) {
                        layerm_slices_surfaces = union_ex(layerm_slices_surfaces, to_expolygons(layerm->fill_surfaces.surfaces));
                    }

                    // find top surfaces (difference between current surfaces
                    // of current layer and upper one)
                    Surfaces top;
                    if (detect_top) {
                        if (upper_layer) {
                            ExPolygons upper_slices = interface_shells ?
                                diff_ex(layerm_slices_surfaces, upper_layer->m_regions[region_id]->slices.surfaces, ApplySafetyOffset::Yes) :
                                diff_ex(layerm_slices_surfaces, upper_layer->lslices, ApplySafetyOffset::Yes);
                            surfaces_append(top, opening_ex(upper_slices, offset), stTop);
                        }
                        else {
                            // if no upper layer, all surfaces of this one are solid
                            // we clone surfaces because we're going to clear the slices collection
                            top = layerm->slices.surfaces;
                            for (Surface& surface : top)
                                surface.surface_type = stTop;
                        }
                    }

                    // Find bottom surfaces (difference between current surfaces of current layer and lower one).
                    Surfaces bottom;
                    if (detect_bottom) {
                        if (lower_layer) {
#if 0
                            //FIXME Why is this branch failing t\multi.t ?
                            Polygons lower_slices = interface_shells ?
                                to_polygons(lower_layer->get_region(region_id)->slices.surfaces) :
                                to_polygons(lower_layer->slices);
                            surfaces_append(bottom,
                                opening_ex(diff(layerm->slices.surfaces, lower_slices, true), offset),
                                surface_type_bottom_other);
#else
                            // Any surface lying on the void is a true bottom bridge (an overhang)
                            surfaces_append(
                                bottom,
                                opening_ex(
                                    diff_ex(layerm_slices_surfaces, lower_layer->lslices, ApplySafetyOffset::Yes),//完全悬空
                                    offset),
                                surface_type_bottom_other);
                            // if user requested internal shells, we need to identify surfaces
                            // lying on other slices not belonging to this region
                            if (interface_shells) {
                                // non-bridging bottom surfaces: any part of this layer lying
                                // on something else, excluding those lying on our own region
                                surfaces_append(
                                    bottom,
                                    opening_ex(
                                        diff_ex(
                                            intersection(layerm_slices_surfaces, lower_layer->lslices), // 先扣掉完全悬空
                                            lower_layer->m_regions[region_id]->slices.surfaces,//再扣掉同材料的区域
                                            ApplySafetyOffset::Yes),
                                        offset),
                                    stBottom);
                            }
#endif
                        }
                        else {
                            // if no lower layer, all surfaces of this one are solid
                            // we clone surfaces because we're going to clear the slices collection
                            bottom = layerm->slices.surfaces;
                            for (Surface& surface : bottom)
                                surface.surface_type = stBottom;
                        }
                    }

                    // now, if the object contained a thin membrane, we could have overlapping bottom
                    // and top surfaces; let's do an intersection to discover them and consider them
                    // as bottom surfaces (to allow for bridge detection)
                    if (! top.empty() && ! bottom.empty()) {
                        Polygons top_polygons = to_polygons(std::move(top));
                        top.clear();
                        surfaces_append(top, diff_ex(top_polygons, bottom), stTop);
                    }

        #ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    {
                        static int iRun = 0;
                        std::vector<std::pair<Slic3r::ExPolygons, SVG::ExPolygonAttributes>> expolygons_with_attributes;
                        expolygons_with_attributes.emplace_back(std::make_pair(union_ex(top),                           SVG::ExPolygonAttributes("green")));
                        expolygons_with_attributes.emplace_back(std::make_pair(union_ex(bottom),                        SVG::ExPolygonAttributes("brown")));
                        expolygons_with_attributes.emplace_back(std::make_pair(to_expolygons(layerm->slices.surfaces),  SVG::ExPolygonAttributes("black")));
                        SVG::export_expolygons(debug_out_path("1_detect_surfaces_type_%d_region%d-layer_%f.svg", iRun ++, region_id, layer->print_z).c_str(), expolygons_with_attributes);
                    }
        #endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

                    // save surfaces to layer
                    Surfaces &surfaces_out = interface_shells ? surfaces_new[idx_layer] : layerm->slices.surfaces;
                    Surfaces  surfaces_backup;
                    if (! interface_shells) {
                        surfaces_backup = std::move(surfaces_out);
                        surfaces_out.clear();
                    }
                    const Surfaces &surfaces_prev = interface_shells ? layerm->slices.surfaces : surfaces_backup;

                    // find internal surfaces (difference between top/bottom surfaces and others)
                    {
                        Polygons topbottom = to_polygons(top);
                        polygons_append(topbottom, to_polygons(bottom));
                        surfaces_append(surfaces_out, diff_ex(surfaces_prev, topbottom), stInternal);
                    }

                    surfaces_append(surfaces_out, std::move(top));
                    surfaces_append(surfaces_out, std::move(bottom));

        #ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    layerm->export_region_slices_to_svg_debug("detect_surfaces_type-final");
        #endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                }
            }
        ); // for each layer of a region
        m_print->throw_if_canceled();

        if (interface_shells) {
            // Move surfaces_new to layerm->slices.surfaces
            for (size_t idx_layer = 0; idx_layer < num_layers; ++ idx_layer)
                m_layers[idx_layer]->m_regions[region_id]->slices.surfaces = std::move(surfaces_new[idx_layer]);
        }

        if (spiral_mode) {
        	if (num_layers > 1)
	        	// Turn the last bottom layer infill to a top infill, so it will be extruded with a proper pattern.
	        	m_layers[num_layers - 1]->m_regions[region_id]->slices.set_type(stTop);
	        for (size_t i = num_layers; i < m_layers.size(); ++ i)
	        	m_layers[i]->m_regions[region_id]->slices.set_type(stInternal);
        }

        BOOST_LOG_TRIVIAL(debug) << "Detecting solid surfaces for region " << region_id << " - clipping in parallel - start";
        // Fill in layerm->fill_surfaces by trimming the layerm->slices by the cummulative layerm->fill_surfaces.
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this, region_id](const tbb::blocked_range<size_t>& range) {
                for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                    m_print->throw_if_canceled();
                    LayerRegion *layerm = m_layers[idx_layer]->m_regions[region_id];
                    layerm->slices_to_fill_surfaces_clipped();
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    layerm->export_region_fill_surfaces_to_svg_debug("1_detect_surfaces_type-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                } // for each layer of a region
            });
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Detecting solid surfaces for region " << region_id << " - clipping in parallel - end";
    } // for each this->print->region_count

    // Mark the object to have the region slices classified (typed, which also means they are split based on whether they are supported, bridging, top layers etc.)
    m_typed_slices = true;
}

void PrintObject::process_external_surfaces()
{
    BOOST_LOG_TRIVIAL(info) << "Processing external surfaces..." << log_memory_info();

    // Cached surfaces covered by some extrusion, defining regions, over which the from the surfaces one layer higher are allowed to expand.
    std::vector<Polygons> surfaces_covered;
    // Is there any printing region, that has zero infill? If so, then we don't want the expansion to be performed over the complete voids, but only
    // over voids, which are supported by the layer below.
    bool 				  has_voids = false;
	for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id)
		if (this->printing_region(region_id).config().sparse_infill_density == 0) {
			has_voids = true;
			break;
		}
	if (has_voids && m_layers.size() > 1) {
	    // All but stInternal fill surfaces will get expanded and possibly trimmed.
	    std::vector<unsigned char> layer_expansions_and_voids(m_layers.size(), false);
	    for (size_t layer_idx = 1; layer_idx < m_layers.size(); ++ layer_idx) {
	    	const Layer *layer = m_layers[layer_idx];
	    	bool expansions = false;
	    	bool voids      = false;
	    	for (const LayerRegion *layerm : layer->regions()) {
	    		for (const Surface &surface : layerm->fill_surfaces.surfaces) {
	    			if (surface.surface_type == stInternal)
	    				voids = true;
	    			else
	    				expansions = true;
	    			if (voids && expansions) {
	    				layer_expansions_and_voids[layer_idx] = true;
	    				goto end;
	    			}
	    		}
	    	}
		end:;
		}
	    BOOST_LOG_TRIVIAL(debug) << "Collecting surfaces covered with extrusions in parallel - start";
	    surfaces_covered.resize(m_layers.size() - 1, Polygons());
    	auto unsupported_width = - float(scale_(0.3 * EXTERNAL_INFILL_MARGIN));
	    tbb::parallel_for(
	        tbb::blocked_range<size_t>(0, m_layers.size() - 1),
	        [this, &surfaces_covered, &layer_expansions_and_voids, unsupported_width](const tbb::blocked_range<size_t>& range) {
	            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx)
	            	if (layer_expansions_and_voids[layer_idx + 1]) {
		                m_print->throw_if_canceled();
		                Polygons voids;
		                for (const LayerRegion *layerm : m_layers[layer_idx]->regions()) {
		                	if (layerm->region().config().sparse_infill_density.value == 0.)
		                		for (const Surface &surface : layerm->fill_surfaces.surfaces)
		                			// Shrink the holes, let the layer above expand slightly inside the unsupported areas.
		                			polygons_append(voids, offset(surface.expolygon, unsupported_width));
		                }
		                surfaces_covered[layer_idx] = diff(m_layers[layer_idx]->lslices, voids);
	            	}
	        }
	    );
	    m_print->throw_if_canceled();
	    BOOST_LOG_TRIVIAL(debug) << "Collecting surfaces covered with extrusions in parallel - end";
	}

	for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id) {
        BOOST_LOG_TRIVIAL(debug) << "Processing external surfaces for region " << region_id << " in parallel - start";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this, &surfaces_covered, region_id](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    m_print->throw_if_canceled();
                    // BOOST_LOG_TRIVIAL(trace) << "Processing external surface, layer" << m_layers[layer_idx]->print_z;
                    m_layers[layer_idx]->get_region(int(region_id))->process_external_surfaces(
                    	(layer_idx == 0) ? nullptr : m_layers[layer_idx - 1],
                    	(layer_idx == 0 || surfaces_covered.empty() || surfaces_covered[layer_idx - 1].empty()) ? nullptr : &surfaces_covered[layer_idx - 1]);
                }
            }
        );
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Processing external surfaces for region " << region_id << " in parallel - end";
    }
}

static bool layer_skips_periodic_modifier(const PrintObject &object, int layer_id)
{
    for (size_t i = 0; i < object.num_printing_regions(); ++i) {
        const PrintRegionConfig &cfg = object.printing_region(i).config();
        if (cfg.periodic_modifier.value && !periodic_modifier_active(cfg, layer_id))
            return true;
    }
    return false;
}

struct EffectiveShellParameters
{
    int    layers;
    double thickness;
};

// Base layers one extrusion of this nozzle covers. A plate sliced for a single diameter already has its layers
// at that nozzle's band height, so every layer is one extrusion there.
static int shell_band_size(const PrintObject &object, double nozzle_diameter, double base_layer_height)
{
    if (object.print()->single_diameter_layering_nozzle() > EPSILON)
        return 1;
    return int(adaptive_layer_band_size(
        nozzle_diameter, layer_grid_nozzle_diameter(object.print()->full_print_config()), base_layer_height));
}

static EffectiveShellParameters effective_top_shell_parameters(
    const PrintObject &object, const PrintRegionConfig &config, double base_layer_height)
{
    EffectiveShellParameters out { config.top_shell_layers.value, config.top_shell_thickness.value };
    if (! multi_nozzle_diameter_mode_enabled(object.print()->full_print_config()))
        return out;

    const double nozzle_diameter = nozzle_diameter_for_filament(
        object.print()->config(), (unsigned int) config.solid_infill_filament.value);
    out.layers = multi_nozzle_top_shell_layers(nozzle_diameter, out.layers);
    out.layers *= shell_band_size(object, nozzle_diameter, base_layer_height);
    out.thickness = multi_nozzle_top_shell_thickness(nozzle_diameter, out.thickness);
    return out;
}

static EffectiveShellParameters effective_bottom_shell_parameters(
    const PrintObject &object, const PrintRegionConfig &config, double base_layer_height)
{
    EffectiveShellParameters out { config.bottom_shell_layers.value, config.bottom_shell_thickness.value };
    if (! multi_nozzle_diameter_mode_enabled(object.print()->full_print_config()))
        return out;

    const double nozzle_diameter = nozzle_diameter_for_filament(
        object.print()->config(), (unsigned int) config.solid_infill_filament.value);
    out.layers = multi_nozzle_bottom_shell_layers(nozzle_diameter, out.layers);
    out.layers *= shell_band_size(object, nozzle_diameter, base_layer_height);
    out.thickness = multi_nozzle_bottom_shell_thickness(nozzle_diameter, out.thickness);
    return out;
}

void PrintObject::discover_vertical_shells()
{
    PROFILE_FUNC();

    BOOST_LOG_TRIVIAL(info) << "Discovering vertical shells..." << log_memory_info();

    struct DiscoverVerticalShellsCacheEntry
    {
        // Collected polygons, offsetted
        Polygons    top_surfaces;
        Polygons    bottom_surfaces;
        Polygons    holes;
    };
    bool     spiral_mode      = this->print()->config().spiral_mode.value;
    size_t   num_layers       = spiral_mode ? std::min(size_t(this->printing_region(0).config().bottom_shell_layers), m_layers.size()) : m_layers.size();

    std::vector<DiscoverVerticalShellsCacheEntry> cache_top_botom_regions(num_layers, DiscoverVerticalShellsCacheEntry());
    bool top_bottom_surfaces_all_regions = this->num_printing_regions() > 1 && ! m_config.interface_shells.value;

    static constexpr float top_bottom_expansion_coeff = 0.05f;
    if (top_bottom_surfaces_all_regions) {
        // This is a multi-material print and interface_shells are disabled, meaning that the vertical shell thickness
        // is calculated over all materials.
        bool has_extra_layers = false;
        for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id) {
            const PrintRegionConfig &config = this->printing_region(region_id).config();
            if (config.ensure_vertical_shell_thickness.value!=EnsureVerticalThicknessLevel::evtDisabled) {
                has_extra_layers = true;
                break;
            }
        }
        if (! has_extra_layers)
            // The "ensure vertical wall thickness" feature is not applicable to any of the regions. Quit.
            return;

        BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells in parallel - start : cache top / bottom";
        //FIXME Improve the heuristics for a grain size.
        size_t grain_size = std::max(num_layers / 16, size_t(1));
        // 关闭interface_shell，不区分不同材料，所以遍历顺序是按层遍历，层中遍历region
        // top区域包含墙，bottom区域包含墙，holes为稀疏填充区域
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, num_layers, grain_size),
            [this, &cache_top_botom_regions](const tbb::blocked_range<size_t>& range) {
                const std::initializer_list<SurfaceType> surfaces_bottom{ stBottom, stBottomBridge };
                const size_t num_regions = this->num_printing_regions();
                for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++idx_layer) {
                    m_print->throw_if_canceled();
                    const Layer& layer = *m_layers[idx_layer];
                    DiscoverVerticalShellsCacheEntry& cache = cache_top_botom_regions[idx_layer];
                    // Simulate single set of perimeters over all merged regions.
                    float                             perimeter_offset = 0.f;
                    float                             perimeter_min_spacing = FLT_MAX;
                    for (size_t region_id = 0; region_id < num_regions; ++region_id) {
                        LayerRegion& layerm = *layer.m_regions[region_id];
                        float        top_bottom_expansion = float(layerm.flow(frSolidInfill).scaled_spacing()) * top_bottom_expansion_coeff;
                        // Top surfaces.
                        append(cache.top_surfaces, offset(layerm.slices.filter_by_type(stTop), top_bottom_expansion));
                        append(cache.bottom_surfaces, offset(layerm.slices.filter_by_types(surfaces_bottom), top_bottom_expansion));
                        unsigned int perimeters = 0;
                        for (const Surface& s : layerm.slices.surfaces)
                            perimeters = std::max<unsigned int>(perimeters, s.extra_perimeters);
                        const PrintRegionConfig &region_config = layerm.region().config();
                        int wall_loops = region_config.wall_loops.value;
                        if (multi_nozzle_diameter_mode_enabled(this->print()->full_print_config())) {
                            const double nozzle_diameter = nozzle_diameter_for_filament(
                                this->print()->config(), (unsigned int) region_config.wall_filament.value);
                            wall_loops = multi_nozzle_wall_loops(nozzle_diameter, wall_loops);
                        }
                        perimeters += wall_loops;
                        // Then calculate the infill offset.
                        if (perimeters > 0) {
                            Flow extflow = layerm.flow(frExternalPerimeter);
                            Flow flow = layerm.flow(frPerimeter);
                            perimeter_offset = std::max(perimeter_offset,
                                0.5f * float(extflow.scaled_width() + extflow.scaled_spacing()) + (float(perimeters) - 1.f) * flow.scaled_spacing());
                            perimeter_min_spacing = std::min(perimeter_min_spacing, float(std::min(extflow.scaled_spacing(), flow.scaled_spacing())));
                        }
                        polygons_append(cache.holes, to_polygons(layerm.fill_expolygons));
                    }
                    // For a multi-material print, simulate perimeter / infill split as if only a single extruder has been used for the whole print.
                    if (perimeter_offset > 0.) {
                        // The layer.lslices are forced to merge by expanding them first.
                        // 对于多材料，按照最大墙宽度，再算一次稀疏区域
                        polygons_append(cache.holes, offset2(layer.lslices, 0.3f * perimeter_min_spacing, -perimeter_offset - 0.3f * perimeter_min_spacing));
                    }
                    // Save some computing time by reducing the number of polygons.
                    cache.top_surfaces = union_(cache.top_surfaces);
                    cache.bottom_surfaces = union_(cache.bottom_surfaces);
                    cache.holes = union_(cache.holes);
                }});
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells in parallel - end : cache top / bottom";
    }

    // 逐region遍历
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        //FIXME Improve the heuristics for a grain size.
        const PrintRegion &region = this->printing_region(region_id);
        if (region.config().ensure_vertical_shell_thickness.value==EnsureVerticalThicknessLevel::evtDisabled)
            // This region will be handled by discover_horizontal_shells().
            continue;

        size_t grain_size = std::max(num_layers / 16, size_t(1));

        // 开启了interface_shell，代表顶底面计算时只有同region可以视为covered
        // 所以此时，对于某一个region，先逐层计算cache的top，bottom，由于稀疏填充是共用的，所以算一次即可
        if (! top_bottom_surfaces_all_regions) {
            // This is either a single material print, or a multi-material print and interface_shells are enabled, meaning that the vertical shell thickness
            // is calculated over a single material.
            BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells for region " << region_id << " in parallel - start : cache top / bottom";
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, num_layers, grain_size),
                [this, region_id, &cache_top_botom_regions](const tbb::blocked_range<size_t>& range) {
                    const std::initializer_list<SurfaceType> surfaces_bottom { stBottom, stBottomBridge };
                    for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                        m_print->throw_if_canceled();
                        Layer       &layer                = *m_layers[idx_layer];
                        LayerRegion &layerm               = *layer.m_regions[region_id];
                        float        top_bottom_expansion = float(layerm.flow(frSolidInfill).scaled_spacing()) * top_bottom_expansion_coeff;
                        // Top surfaces.
                        auto &cache = cache_top_botom_regions[idx_layer];
                        cache.top_surfaces = offset(layerm.slices.filter_by_type(stTop), top_bottom_expansion);
//                        append(cache.top_surfaces, offset(layerm.fill_surfaces().filter_by_type(stTop), top_bottom_expansion));
                        // Bottom surfaces.
                        cache.bottom_surfaces = offset(layerm.slices.filter_by_types(surfaces_bottom), top_bottom_expansion);
//                        append(cache.bottom_surfaces, offset(layerm.fill_surfaces().filter_by_types(surfaces_bottom), top_bottom_expansion));
                        // Holes over all regions. Only collect them once, they are valid for all region_id iterations.
                        if (cache.holes.empty()) {
                            for (size_t region_id = 0; region_id < layer.regions().size(); ++ region_id)
                                polygons_append(cache.holes, to_polygons(layer.regions()[region_id]->fill_expolygons));
                        }
                    }
                });
            m_print->throw_if_canceled();
            BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells for region " << region_id << " in parallel - end : cache top / bottom";
        }

        BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells for region " << region_id << " in parallel - start : ensure vertical wall thickness";
        grain_size = 1;
        // 从第低到高按层遍历
#if USE_TBB_IN_INFILL
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, num_layers, grain_size),
            [this, region_id, &cache_top_botom_regions]
            (const tbb::blocked_range<size_t>& range) {
                // printf("discover_vertical_shells from %d to %d\n", range.begin(), range.end());
                for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
#else
        for (size_t idx_layer = 0; idx_layer < num_layers; ++idx_layer) {
#endif
                    m_print->throw_if_canceled();
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
        			static size_t debug_idx = 0;
        			++ debug_idx;
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

                    Layer       	        *layer          = m_layers[idx_layer];
                    LayerRegion 	        *layerm         = layer->m_regions[region_id];
                    const PrintRegionConfig &region_config  = layerm->region().config();

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    layerm->export_region_slices_to_svg_debug("3_discover_vertical_shells-initial");
                    layerm->export_region_fill_surfaces_to_svg_debug("3_discover_vertical_shells-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

                    Flow         solid_infill_flow   = layerm->flow(frSolidInfill);
                    coord_t      infill_line_spacing = solid_infill_flow.scaled_spacing();
                    // Find a union of perimeters below / above this surface to guarantee a minimum shell thickness.
                    Polygons shell;
                    Polygons holes;
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    ExPolygons shell_ex;
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                    float min_perimeter_infill_spacing = float(infill_line_spacing) * 1.05f;
			        polygons_append(holes, cache_top_botom_regions[idx_layer].holes);
                    auto combine_holes = [&holes](const Polygons &holes2) {
                        if (holes.empty() || holes2.empty())
                            holes.clear();
                        else
                            holes = intersection(holes, holes2);
                    };
                    auto combine_shells = [&shell](const Polygons &shells2) {
                        if (shell.empty())
                            shell = std::move(shells2);
                        else if (! shells2.empty()) {
                            polygons_append(shell, shells2);
                            // Running the union_ using the Clipper library piece by piece is cheaper
                            // than running the union_ all at once.
                            shell = union_(shell);
                        }
                    };
                    static constexpr const bool one_more_layer_below_top_bottom_surfaces = false;
                    const EffectiveShellParameters top_shell = effective_top_shell_parameters(
                        *this, region_config, m_slicing_params.layer_height);
                    const EffectiveShellParameters bottom_shell = effective_bottom_shell_parameters(
                        *this, region_config, m_slicing_params.layer_height);
			        if (top_shell.layers > 0) {
                        // Gather top regions projected to this layer.
                        coordf_t print_z = layer->print_z;
                        int i = int(idx_layer) + 1;
                        int itop = int(idx_layer) + top_shell.layers;
                        bool at_least_one_top_projected = false;
	                    for (; i < int(cache_top_botom_regions.size()) &&
	                         (i < itop || m_layers[i]->print_z - print_z < top_shell.thickness - EPSILON);
	                        ++ i) {
                            at_least_one_top_projected = true;
	                        const DiscoverVerticalShellsCacheEntry &cache = cache_top_botom_regions[i];
                            if (region_config.ensure_vertical_shell_thickness.value != EnsureVerticalThicknessLevel::evtPartial) {
                                combine_holes(cache.holes);
                            }
                            combine_shells(cache.top_surfaces);
	                    }
                        if (!at_least_one_top_projected && i < int(cache_top_botom_regions.size())) {
                            // Lets consider this a special case - with only 1 top solid and minimal shell thickness settings, the
                            // boundaries of solid layers are not anchored over/under perimeters, so lets fix it by adding at least one
                            // perimeter width of area
                            Polygons anchor_area = intersection(expand(cache_top_botom_regions[idx_layer].top_surfaces,
                                                                       layerm->flow(frExternalPerimeter).scaled_spacing()),
                                                                to_polygons(m_layers[i]->lslices));
                            combine_shells(anchor_area);
                        }

                        if (one_more_layer_below_top_bottom_surfaces)
                            if (i < int(cache_top_botom_regions.size()) &&
                                (i <= itop || m_layers[i]->bottom_z() - print_z < top_shell.thickness - EPSILON))
                                combine_holes(cache_top_botom_regions[i].holes);
	                }
	                if (int n_bottom_layers = bottom_shell.layers; n_bottom_layers > 0) {
                        // Gather bottom regions projected to this layer.
                        coordf_t bottom_z = layer->bottom_z();
                        int i = int(idx_layer) - 1;
                        int ibottom = int(idx_layer) - n_bottom_layers;
                        bool at_least_one_bottom_projected = false;
	                    for (; i >= 0 &&
	                         (i > ibottom || bottom_z - m_layers[i]->bottom_z() < bottom_shell.thickness - EPSILON);
	                        -- i) {
                                at_least_one_bottom_projected = true;
	                        const DiscoverVerticalShellsCacheEntry &cache = cache_top_botom_regions[i];
                            if (region_config.ensure_vertical_shell_thickness.value != EnsureVerticalThicknessLevel::evtPartial) {
                                combine_holes(cache.holes);
                            }
                            combine_shells(cache.bottom_surfaces);
	                    }

                        if (!at_least_one_bottom_projected && i >= 0) {
                            Polygons anchor_area = intersection(expand(cache_top_botom_regions[idx_layer].bottom_surfaces,
                                                                       layerm->flow(frExternalPerimeter).scaled_spacing()),
                                                                to_polygons(m_layers[i]->lslices));
                            combine_shells(anchor_area);
                        }

                        if (one_more_layer_below_top_bottom_surfaces)
                            if (i >= 0 &&
                                (i > ibottom || bottom_z - m_layers[i]->print_z < bottom_shell.thickness - EPSILON))
                                combine_holes(cache_top_botom_regions[i].holes);
	                }
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    shell_ex = union_safety_offset_ex(shell);
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

                    //if (shell.empty())
                    //    continue;

                    // Trim the shells region by the internal & internal void surfaces.
                    const Polygons    polygonsInternal = to_polygons(layerm->fill_surfaces.filter_by_types({ stInternal, stInternalVoid, stInternalSolid }));
                    shell = intersection(shell, polygonsInternal, ApplySafetyOffset::Yes);
                    // A skipped periodic modifier leaves a 0-wall layer next to a walled layer.
                    // Do not fill that missing-wall band with solid infill.
                    if (!layer_skips_periodic_modifier(*this, int(idx_layer)))
                        polygons_append(shell, diff(polygonsInternal, holes));
                    if (shell.empty())
                        continue;

                    // Append the internal solids, so they will be merged with the new ones.
                    polygons_append(shell, to_polygons(layerm->fill_surfaces.filter_by_type(stInternalSolid)));

                    // These regions will be filled by a rectilinear full infill. Currently this type of infill
                    // only fills regions, which fit at least a single line. To avoid gaps in the sparse infill,
                    // make sure that this region does not contain parts narrower than the infill spacing width.
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    Polygons shell_before = shell;
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                    ExPolygons regularized_shell;
                    {
                        // Open to remove (filter out) regions narrower than a bit less than an infill extrusion line width.
                        // Such narrow regions are difficult to fill in with a gap fill algorithm (or Arachne), however they are most likely
                        // not needed for print stability / quality.
                        const float narrow_ensure_vertical_wall_thickness_region_radius = 0.5f * 0.65f * min_perimeter_infill_spacing;
                        // Then close gaps narrower than 1.2 * line width, such gaps are difficult to fill in with sparse infill,
                        // thus they will be merged into the solid infill.
                        const float narrow_sparse_infill_region_radius                  = 0.5f * 1.2f * min_perimeter_infill_spacing;
                        // Finally expand the infill a bit to remove tiny gaps between solid infill and the other regions.
                        const float tiny_overlap_radius                                 = 0.2f        * min_perimeter_infill_spacing;
                        regularized_shell = shrink_ex(offset2_ex(union_ex(shell),
                            // Open to remove (filter out) regions narrower than an infill extrusion line width.
                            -narrow_ensure_vertical_wall_thickness_region_radius,
                            // Then close gaps narrower than 1.2 * line width, such gaps are difficult to fill in with sparse infill.
                            narrow_ensure_vertical_wall_thickness_region_radius + narrow_sparse_infill_region_radius, ClipperLib::jtSquare),
                            // Finally expand the infill a bit to remove tiny gaps between solid infill and the other regions.
                            narrow_sparse_infill_region_radius - tiny_overlap_radius, ClipperLib::jtSquare);

                        Polygons object_volume;
                        Polygons internal_volume;
                        {
                            Polygons shrinked_bottom_slice = idx_layer > 0 ? to_polygons(m_layers[idx_layer - 1]->lslices) : Polygons{};
                            Polygons shrinked_upper_slice  = (idx_layer + 1) < m_layers.size() ?
                                                                 to_polygons(m_layers[idx_layer + 1]->lslices) :
                                                                 Polygons{};
                            object_volume = intersection(shrinked_bottom_slice, shrinked_upper_slice);
                            internal_volume = closing(polygonsInternal, float(SCALED_EPSILON));
                        }

                        // The regularization operation may cause scattered tiny drops on the smooth parts of the model, filter them out
                        // If the region checks both following conditions, it is removed:
                        //   1. the area is very small,
                        //      OR the area is quite small and it is fully wrapped in model (not visible)
                        //      the in-model condition is there due to small sloping surfaces, e.g. top of the hull of the benchy
                        //   2. the area does not fully cover an internal polygon
                        //         This is there mainly for a very thin parts, where the solid layers would be missing if the part area is quite small
                        regularized_shell.erase(std::remove_if(regularized_shell.begin(), regularized_shell.end(),
                                                               [&internal_volume, &min_perimeter_infill_spacing,
                                                                &object_volume](const ExPolygon &p) {
                                                                   return (p.area() < min_perimeter_infill_spacing * scaled(1.5) ||
                                                                           (p.area() < min_perimeter_infill_spacing * scaled(8.0) &&
                                                                            diff(to_polygons(p), object_volume).empty())) &&
                                                                          diff(internal_volume,
                                                                               expand(to_polygons(p), min_perimeter_infill_spacing))
                                                                                  .size() >= internal_volume.size();
                                                               }),
                                                regularized_shell.end());
                    }

                    if (regularized_shell.empty())
                        continue;

                    ExPolygons new_internal_solid = intersection_ex(polygonsInternal, regularized_shell);

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    {
                        Slic3r::SVG svg(debug_out_path("discover_vertical_shells-regularized-%d.svg", debug_idx), get_extents(shell_before));
                        // Source shell.
                        svg.draw(union_safety_offset_ex(shell_before));
                        // Shell trimmed to the internal surfaces.
                        svg.draw_outline(union_safety_offset_ex(shell), "black", "blue", scale_(0.05));
                        // Regularized infill region.
                        svg.draw_outline(new_internal_solid, "red", "magenta", scale_(0.05));
                        svg.Close();
                    }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                    // Trim the internal & internalvoid by the shell.
                    Slic3r::ExPolygons new_internal = diff_ex(layerm->fill_surfaces.filter_by_type(stInternal), regularized_shell);
                    Slic3r::ExPolygons new_internal_void = diff_ex(layerm->fill_surfaces.filter_by_type(stInternalVoid), regularized_shell);

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    {
                        SVG::export_expolygons(debug_out_path("discover_vertical_shells-new_internal-%d.svg", debug_idx), get_extents(shell), new_internal, "black", "blue", scale_(0.05));
        				SVG::export_expolygons(debug_out_path("discover_vertical_shells-new_internal_void-%d.svg", debug_idx), get_extents(shell), new_internal_void, "black", "blue", scale_(0.05));
        				SVG::export_expolygons(debug_out_path("discover_vertical_shells-new_internal_solid-%d.svg", debug_idx), get_extents(shell), new_internal_solid, "black", "blue", scale_(0.05));
                    }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

                    // Assign resulting internal surfaces to layer.
                    layerm->fill_surfaces.keep_types({ stTop, stBottom, stBottomBridge });
                    layerm->fill_surfaces.append(new_internal,       stInternal);
                    layerm->fill_surfaces.append(new_internal_void,  stInternalVoid);
                    layerm->fill_surfaces.append(new_internal_solid, stInternalSolid);
                } // for each layer
#if USE_TBB_IN_INFILL
            });
#endif
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells for region " << region_id << " in parallel - end";

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
		for (size_t idx_layer = 0; idx_layer < m_layers.size(); ++idx_layer) {
			LayerRegion *layerm = m_layers[idx_layer]->get_region(region_id);
			layerm->export_region_slices_to_svg_debug("4_discover_vertical_shells-final");
			layerm->export_region_fill_surfaces_to_svg_debug("4_discover_vertical_shells-final");
		}
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
    } // for each region

    // Write the profiler measurements to file
//    PROFILE_UPDATE();
//    PROFILE_OUTPUT(debug_out_path("discover_vertical_shells-profile.txt").c_str());
}



bool PrintObject::has_interlocking_beam() const
{
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id)
        if (this->printing_region(region_id).config().interlocking_beam.value)
            return true;
    return false;
}

void PrintObject::discover_shell_for_perimeters()
{
    const size_t num_regions = this->num_printing_regions();

    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layers.size()),
        [this,num_regions](const tbb::blocked_range<size_t> &range){
            for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++idx_layer) {
                Layer* layer = m_layers[idx_layer];
                if (!layer->lower_layer)
                    continue;
                Layer* lower_layer = layer->lower_layer;

                ExPolygons perimeter_areas;
                ExPolygons infill_areas;
                float max_line_width = 0;
                for (size_t region_id = 0; region_id < num_regions; ++region_id) {
                    LayerRegion* layerm = layer->m_regions[region_id];
                    Flow extflow = layerm->flow(frExternalPerimeter);
                    infill_areas.insert(infill_areas.end(), layerm->fill_expolygons.begin(), layerm->fill_expolygons.end());
                    max_line_width = std::max(max_line_width, 0.5f * float(extflow.scaled_width() + extflow.scaled_spacing()));
                }
                infill_areas = union_ex(infill_areas);
                perimeter_areas = offset_ex(diff_ex(layer->lslices, infill_areas), max_line_width);

                for (size_t region_id = 0; region_id < num_regions; ++region_id) {
                    LayerRegion* lower_layerm = lower_layer->m_regions[region_id];

                    ExPolygons new_perimeter_solid = intersection_ex(perimeter_areas, lower_layerm->fill_expolygons);
                    new_perimeter_solid.erase(std::remove_if(new_perimeter_solid.begin(), new_perimeter_solid.end(), [max_line_width](auto& expoly) {
                        return is_narrow_expolygon(expoly, 3 * max_line_width);
                        }), new_perimeter_solid.end());
                    if (new_perimeter_solid.empty())
                        continue;

                    ExPolygons old_internal = to_expolygons(lower_layerm->fill_surfaces.filter_by_type(stInternal));
                    ExPolygons old_internal_void = to_expolygons(lower_layerm->fill_surfaces.filter_by_type(stInternalVoid));
                    ExPolygons old_internal_solid = to_expolygons(lower_layerm->fill_surfaces.filter_by_type(stInternalSolid));
                    ExPolygons bottom_area        = to_expolygons(lower_layerm->fill_surfaces.filter_by_type(stBottom));

                    lower_layerm->fill_surfaces.remove_types({ stInternal,stInternalVoid,stInternalSolid });

                    ExPolygons new_internal_solid = diff_ex(union_ex(old_internal_solid, new_perimeter_solid), bottom_area);
                    ExPolygons new_internal = diff_ex(old_internal, new_perimeter_solid);
                    ExPolygons new_internal_void = diff_ex(old_internal_void, new_perimeter_solid);
                    lower_layerm->fill_surfaces.append(new_internal, stInternal);
                    lower_layerm->fill_surfaces.append(new_internal_void, stInternalVoid);
                    lower_layerm->fill_surfaces.append(new_internal_solid, stInternalSolid);
                }
            }
    });
}
// This method applies bridge flow to the first internal solid layer above sparse infill.
// This method applies bridge flow to the first internal solid layer above sparse infill.
void PrintObject::bridge_over_infill()
{
    BOOST_LOG_TRIVIAL(info) << "Bridge over infill - Start" << log_memory_info();

    // CandidateSurface存放一个需要桥接的区域
    struct CandidateSurface
    {
        CandidateSurface(const Surface     *original_surface,
            int                layer_index,
            Polygons           new_polys,
            const LayerRegion *region,
            double             bridge_angle)
            : original_surface(original_surface)
            , layer_index(layer_index)
            , new_polys(new_polys)
            , region(region)
            , bridge_angle(bridge_angle)
        {}
        const Surface     *original_surface;  // 下方需要生成桥接的surface
        int                layer_index;  // 下方生成桥接的层号
        Polygons           new_polys;    // 下方需要生成桥接的实心区域
        const LayerRegion *region;       // 下方需要生成桥接的region，主要提供参数
        double             bridge_angle; // 桥接方向
    };

    // 按层存放surface，存放着待桥接的信息
    std::map<size_t, std::vector<CandidateSurface>> surfaces_by_layer;

    // SECTION to gather and filter surfaces for expanding, and then cluster them by layer
    {
        tbb::concurrent_vector<CandidateSurface> candidate_surfaces;
#if USE_TBB_IN_INFILL
        tbb::parallel_for(tbb::blocked_range<size_t>(0, this->layers().size()), [po = static_cast<const PrintObject *>(this),
            &candidate_surfaces](tbb::blocked_range<size_t> r) {
                // 按层并行
                for (size_t lidx = r.begin(); lidx < r.end(); lidx++) {
#else
        auto po = static_cast<const PrintObject*>(this);
        for(size_t lidx =0;lidx<this->layers().size();++lidx){
#endif
                    const Layer *layer = po->get_layer(lidx);
                    if (layer->lower_layer == nullptr) {
                        continue;
                    }
                    double spacing = layer->regions().front()->flow(frSolidInfill).scaled_spacing();
                    // unsupported area will serve as a filter for polygons worth bridging.
                    Polygons   unsupported_area; // 下一层不提供支撑的区域
                    Polygons   lower_layer_solids;  // 下一层的实心区域，可以提供支撑
                    // 取当前层和下一层的数据
                    for (const LayerRegion *region : layer->lower_layer->regions()) {
                        Polygons fill_polys = to_polygons(region->fill_expolygons);
                        // initially consider the whole layer unsupported, but also gather solid layers to later cut off supported parts
                        unsupported_area.insert(unsupported_area.end(), fill_polys.begin(), fill_polys.end());
                        for (const Surface &surface : region->fill_surfaces.surfaces) {
                            if (surface.surface_type != stInternal || region->region().config().sparse_infill_density.value == 100) {
                                Polygons p = to_polygons(surface.expolygon);
                                lower_layer_solids.insert(lower_layer_solids.end(), p.begin(), p.end());
                            }
                        }
                    }
                    unsupported_area = closing(unsupported_area, float(SCALED_EPSILON));
                    // By expanding the lower layer solids, we avoid making bridges from the tiny internal overhangs that are (very likely) supported by previous layer solids
                    // NOTE that we cannot filter out polygons worth bridging by their area, because sometimes there is a very small internal island that will grow into large hole
                    lower_layer_solids = shrink(lower_layer_solids, 1 * spacing); // first remove thin regions that will not support anything
                    lower_layer_solids = expand(lower_layer_solids, (1 + 3) * spacing); // then expand back (opening), and further for parts supported by internal solids
                    // By shrinking the unsupported area, we avoid making bridges from narrow ensuring region along perimeters.
                    unsupported_area   = shrink(unsupported_area, 3 * spacing);
                    unsupported_area   = diff(unsupported_area, lower_layer_solids);

                    for (LayerRegion *region : layer->regions()) {
                        auto region_internal_solids =  region->fill_surfaces.filter_by_type(stInternalSolid); // 取当前层的实心区域
                        for (const Surface *s : region_internal_solids) {
                            Polygons unsupported         = intersection(to_polygons(s->expolygon), unsupported_area); // 当前层需要生成桥接的区域，通过当前层的实心区域与下一层的非实心区域求交得到
                            // The following flag marks those surfaces, which overlap with unuspported area, but at least part of them is supported.
                            // These regions can be filtered by area, because they for sure are touching solids on lower layers, and it does not make sense to bridge their tiny overhangs
                            bool     partially_supported = area(unsupported) < area(to_polygons(s->expolygon)) - EPSILON;
                            if (!unsupported.empty() && (!partially_supported || area(unsupported) > 3 * 3 * spacing * spacing)) {
                                Polygons worth_bridging = intersection(to_polygons(s->expolygon), expand(unsupported, 4 * spacing));
                                // after we extracted the part worth briding, we go over the leftovers and merge the tiny ones back, to not brake the surface too much
                                for (const Polygon& p : diff(to_polygons(s->expolygon), expand(worth_bridging, spacing))) {
                                    double area = p.area();
                                    if (area < spacing * scale_(12.0) && area > spacing * spacing) {
                                        worth_bridging.push_back(p);
                                    }
                                }
                                worth_bridging = intersection(closing(worth_bridging, float(SCALED_EPSILON)), s->expolygon);
                                // 对应哪个region下的那个surface需要生成桥接
                                candidate_surfaces.push_back(CandidateSurface(s, lidx, worth_bridging, region, 0));

#ifdef DEBUG_BRIDGE_OVER_INFILL
                                debug_draw(std::to_string(lidx) + "_candidate_surface_" + std::to_string(area(s->expolygon)),
                                    to_lines(region->layer()->lslices), to_lines(s->expolygon), to_lines(worth_bridging),
                                    to_lines(unsupported_area));
#endif
#ifdef DEBUG_BRIDGE_OVER_INFILL
                                debug_draw(std::to_string(lidx) + "_candidate_processing_" + std::to_string(area(unsupported)),
                                    to_lines(unsupported), to_lines(intersection(to_polygons(s->expolygon), expand(unsupported, 5 * spacing))),
                                    to_lines(diff(to_polygons(s->expolygon), expand(worth_bridging, spacing))),
                                    to_lines(unsupported_area));
#endif
                            }
                        }
                    }
                }
#if USE_TBB_IN_INFILL
                });
#endif

        // 按层重新存储
        for (const CandidateSurface &c : candidate_surfaces) {
            surfaces_by_layer[c.layer_index].push_back(c);
        }
    }

    // LIGHTNING INFILL SECTION - If lightning infill is used somewhere, we check the areas that are going to be bridges, and those that rely on the
    // lightning infill under them get expanded. This somewhat helps to ensure that most of the extrusions are anchored to the lightning infill at the ends.
    // It requires modifying this instance of print object in a specific way, so that we do not invalidate the pointers in our surfaces_by_layer structure.
    bool has_lightning_infill = false;
    for (size_t i = 0; i < this->num_printing_regions(); i++) {
        if (this->printing_region(i).config().sparse_infill_pattern == ipLightning) {
            has_lightning_infill = true;
            break;
        }
    }
    if (has_lightning_infill) {
        // Prepare backup data for the Layer Region infills. Before modfiyng the layer region, we backup its fill surfaces by moving! them into this map.
        // then a copy is created, modifiyed and passed to lightning infill generator. After generator is created, we restore the original state of the fills
        // again by moving the data from this map back to the layer regions. This ensures that pointers to surfaces stay valid.
        std::map<size_t, std::map<const LayerRegion *, SurfaceCollection>> backup_surfaces;
        for (size_t lidx = 0; lidx < this->layer_count(); lidx++) {
            backup_surfaces[lidx] = {};
        }

        tbb::parallel_for(tbb::blocked_range<size_t>(0, this->layers().size()), [po = this, &backup_surfaces,
            &surfaces_by_layer](tbb::blocked_range<size_t> r) {
                for (size_t lidx = r.begin(); lidx < r.end(); lidx++) {
                    if (surfaces_by_layer.find(lidx) == surfaces_by_layer.end())
                        continue;

                    Layer       *layer       = po->get_layer(lidx);
                    const Layer *lower_layer = layer->lower_layer;
                    if (lower_layer == nullptr)
                        continue;

                    Polygons lightning_fill;
                    for (LayerRegion *region : lower_layer->regions()) {
                        if (region->region().config().sparse_infill_pattern == ipLightning) {
                            Polygons lf = to_polygons(region->fill_surfaces.filter_by_type(stInternal));
                            lightning_fill.insert(lightning_fill.end(), lf.begin(), lf.end());
                        }
                    }

                    if (lightning_fill.empty())
                        continue;

                    for (LayerRegion *region : layer->regions()) {
                        backup_surfaces[lidx][region] = std::move(
                            region->fill_surfaces); // Make backup copy by move!! so that pointers in candidate surfaces stay valid
                        // Copy the surfaces back, this will make copy, but we will later discard it anyway
                        region->fill_surfaces = backup_surfaces[lidx][region];
                    }

                    for (LayerRegion *region : layer->regions()) {
                        ExPolygons sparse_infill = to_expolygons(region->fill_surfaces.filter_by_type(stInternal));
                        ExPolygons solid_infill  = to_expolygons(region->fill_surfaces.filter_by_type(stInternalSolid));

                        if (sparse_infill.empty()) {
                            break;
                        }
                        for (const auto &surface : surfaces_by_layer[lidx]) {
                            if (surface.region != region)
                                continue;
                            ExPolygons expansion = intersection_ex(sparse_infill, expand(surface.new_polys, scaled<float>(3.0)));
                            solid_infill.insert(solid_infill.end(), expansion.begin(), expansion.end());
                        }

                        solid_infill  = union_safety_offset_ex(solid_infill);
                        sparse_infill = diff_ex(sparse_infill, solid_infill);

                        region->fill_surfaces.remove_types({stInternalSolid, stInternal});
                        for (const ExPolygon &ep : solid_infill) {
                            region->fill_surfaces.surfaces.emplace_back(stInternalSolid, ep);
                        }
                        for (const ExPolygon &ep : sparse_infill) {
                            region->fill_surfaces.surfaces.emplace_back(stInternal, ep);
                        }
                    }
                }
            });

        // Use the modified surfaces to generate expanded lightning anchors
        this->m_lightning_generator = this->prepare_lightning_infill_data();

        // And now restore carefully the original surfaces, again using move to avoid reallocation and preserving the validity of the
        // pointers in surface candidates
        for (size_t lidx = 0; lidx < this->layer_count(); lidx++) {
            Layer *layer = this->get_layer(lidx);
            for (LayerRegion *region : layer->regions()) {
                if (backup_surfaces[lidx].find(region) != backup_surfaces[lidx].end()) {
                    region->fill_surfaces = std::move(backup_surfaces[lidx][region]);
                }
            }
        }
    }

    std::map<size_t, Polylines> infill_lines;
    // SECTION to generate infill polylines
    {
        std::vector<std::pair<const Surface *, float>> surfaces_w_bottom_z;
        for (const auto &pair : surfaces_by_layer) {
            for (const CandidateSurface &c : pair.second) {
                surfaces_w_bottom_z.emplace_back(c.original_surface, c.region->m_layer->bottom_z());
            }
        }

        this->m_adaptive_fill_octrees = this->prepare_adaptive_infill_data(surfaces_w_bottom_z);

        std::vector<size_t> layers_to_generate_infill;
        for (const auto &pair : surfaces_by_layer) {
            assert(pair.first > 0);
            infill_lines[pair.first - 1] = {};
            layers_to_generate_infill.push_back(pair.first - 1);
        }

        tbb::parallel_for(tbb::blocked_range<size_t>(0, layers_to_generate_infill.size()), [po = static_cast<const PrintObject *>(this),
            &layers_to_generate_infill,
            &infill_lines](tbb::blocked_range<size_t> r) {
                for (size_t job_idx = r.begin(); job_idx < r.end(); job_idx++) {
                    size_t lidx = layers_to_generate_infill[job_idx];
                    infill_lines.at(
                        lidx) = po->get_layer(lidx)->generate_sparse_infill_polylines_for_anchoring(po->m_adaptive_fill_octrees.first.get(),
                            po->m_adaptive_fill_octrees.second.get(),
                            po->m_lightning_generator.get());
                }
            });
#ifdef DEBUG_BRIDGE_OVER_INFILL
        for (const auto &il : infill_lines) {
            debug_draw(std::to_string(il.first) + "_infill_lines", to_lines(get_layer(il.first)->lslices), to_lines(il.second), {}, {});
        }
#endif
    }

    // cluster layers by depth needed for thick bridges. Each cluster is to be processed by single thread sequentially, so that bridges cannot appear one on another
    std::vector<std::vector<size_t>> clustered_layers_for_threads;
    float target_flow_height_factor = 0.9f;
    {
        std::vector<size_t> layers_with_candidates; // 存储所有需要生成桥接的层号
        std::map<size_t, Polygons> layer_area_covered_by_candidates; // 存储每一层，需要生成桥接区域的bbox的并集
        for (const auto& pair : surfaces_by_layer) {
            layers_with_candidates.push_back(pair.first);
            layer_area_covered_by_candidates[pair.first] = {};
        }

        // prepare inflated filter for each candidate on each layer. layers will be put into single thread cluster if they are close to each other (z-axis-wise)
        // and if the inflated AABB polygons overlap somewhere
        tbb::parallel_for(tbb::blocked_range<size_t>(0, layers_with_candidates.size()), [&layers_with_candidates, &surfaces_by_layer,
            &layer_area_covered_by_candidates](
                tbb::blocked_range<size_t> r) {
                    // 按层并行
                    for (size_t job_idx = r.begin(); job_idx < r.end(); job_idx++) {
                        size_t lidx = layers_with_candidates[job_idx];
                        for (const auto &candidate : surfaces_by_layer.at(lidx)) {
                            Polygon candiate_inflated_aabb = get_extents(candidate.new_polys).inflated(scale_(7)).polygon();
                            layer_area_covered_by_candidates.at(lidx) = union_(layer_area_covered_by_candidates.at(lidx),
                                Polygons{candiate_inflated_aabb});
                        }
                    }
            });

        // note: surfaces_by_layer is ordered map
        for (const auto &pair : surfaces_by_layer) {
            // 初次操作 || z方向距离较远 || 桥接区域无交集， 那么就可以重新划分一个组，否则分配到前一个组
            if (clustered_layers_for_threads.empty() ||
                this->get_layer(clustered_layers_for_threads.back().back())->print_z <
                this->get_layer(pair.first)->print_z -
                this->get_layer(pair.first)->regions()[0]->bridging_flow(frSolidInfill, true).height() * target_flow_height_factor -
                EPSILON ||
                intersection(layer_area_covered_by_candidates[clustered_layers_for_threads.back().back()],
                    layer_area_covered_by_candidates[pair.first])
                .empty()) {
                clustered_layers_for_threads.push_back({pair.first});
            } else {
                clustered_layers_for_threads.back().push_back(pair.first);
            }
        }

#ifdef DEBUG_BRIDGE_OVER_INFILL
        std::cout << "BRIDGE OVER INFILL CLUSTERED LAYERS FOR SINGLE THREAD" << std::endl;
        for (auto cluster : clustered_layers_for_threads) {
            std::cout << "CLUSTER: ";
            for (auto l : cluster) {
                std::cout << l << "  ";
            }
            std::cout << std::endl;
        }
#endif
    }

    // LAMBDA to gather areas with sparse infill deep enough that we can fit thick bridges there.
    auto gather_areas_w_depth = [target_flow_height_factor](const PrintObject *po, int lidx, float target_flow_height) {
        // Gather layers sparse infill areas, to depth defined by used bridge flow
        ExPolygons layers_sparse_infill{};
        ExPolygons not_sparse_infill{};
        double   bottom_z = po->get_layer(lidx)->print_z - target_flow_height * target_flow_height_factor - EPSILON;
        for (int i = int(lidx) - 1; i >= 0; --i) {
            // Stop iterating if layer is lower than bottom_z and at least one iteration was made
            const Layer *layer = po->get_layer(i);
            if (layer->print_z < bottom_z && i < int(lidx) - 1)
                break;

            for (const LayerRegion *region : layer->regions()) {
                bool has_low_density = region->region().config().sparse_infill_density.value < 100;
                for (const Surface &surface : region->fill_surfaces.surfaces) {
                    if ((surface.surface_type == stInternal && has_low_density) || surface.surface_type == stInternalVoid ) {
                        layers_sparse_infill.push_back(surface.expolygon);
                    } else {
                        not_sparse_infill.push_back(surface.expolygon);
                    }
                }
            }
        }
        // 收集一定z范围内的稀疏和实心区域，判断有没有交集，如果有交集，则不能使用thick bridge(thick bridge的流量会侵占实心区域)
        layers_sparse_infill = union_ex(layers_sparse_infill);
        layers_sparse_infill = closing_ex(layers_sparse_infill, float(SCALED_EPSILON));
        not_sparse_infill    = union_ex(not_sparse_infill);
        not_sparse_infill    = closing_ex(not_sparse_infill, float(SCALED_EPSILON));
        return diff(layers_sparse_infill, not_sparse_infill);
        };

    // LAMBDA do determine optimal bridging angle
    auto determine_bridging_angle = [](const Polygons &bridged_area, const Lines &anchors, InfillPattern dominant_pattern, double infill_direction) {
        AABBTreeLines::LinesDistancer<Line> lines_tree(anchors);

        // Check it the infill that require a fixed infill angle.
        switch (dominant_pattern) {
        case ip3DHoneycomb:
        case ipCrossHatch:
            return (infill_direction + 45.0) * 2.0 * M_PI / 360.;
        default: break;
        }

        std::map<double, int> counted_directions;
        for (const Polygon &p : bridged_area) {
            double acc_distance = 0;
            for (int point_idx = 0; point_idx < int(p.points.size()) - 1; ++point_idx) {
                Vec2d  start        = p.points[point_idx].cast<double>();
                Vec2d  next         = p.points[point_idx + 1].cast<double>();
                Vec2d  v            = next - start; // vector from next to current
                double dist_to_next = v.norm();
                acc_distance += dist_to_next;
                if (acc_distance > scaled(2.0)) {
                    acc_distance = 0.0;
                    v.normalize();
                    int   lines_count = int(std::ceil(dist_to_next / scaled(2.0)));
                    float step_size   = dist_to_next / lines_count;
                    for (int i = 0; i < lines_count; ++i) {
                        Point a                   = (start + v * (i * step_size)).cast<coord_t>();
                        auto [distance, index, p] = lines_tree.distance_from_lines_extra<false>(a);
                        double angle = lines_tree.get_line(index).orientation();
                        if (angle > PI) {
                            angle -= PI;
                        }
                        angle += PI * 0.5;
                        counted_directions[angle]++;
                    }
                }
            }
        }

        std::pair<double, int> best_dir{0, 0};
        // sliding window accumulation
        for (const auto &dir : counted_directions) {
            int    score_acc          = 0;
            double dir_acc            = 0;
            double window_start_angle = dir.first - PI * 0.1;
            double window_end_angle   = dir.first + PI * 0.1;
            for (auto dirs_window = counted_directions.lower_bound(window_start_angle);
                dirs_window != counted_directions.upper_bound(window_end_angle); dirs_window++) {
                dir_acc += dirs_window->first * dirs_window->second;
                score_acc += dirs_window->second;
            }
            // current span of directions is 0.5 PI to 1.5 PI (due to the aproach.). Edge values should also account for the
            //  opposite direction.
            if (window_start_angle < 0.5 * PI) {
                for (auto dirs_window = counted_directions.lower_bound(1.5 * PI - (0.5 * PI - window_start_angle));
                    dirs_window != counted_directions.end(); dirs_window++) {
                    dir_acc += dirs_window->first * dirs_window->second;
                    score_acc += dirs_window->second;
                }
            }
            if (window_start_angle > 1.5 * PI) {
                for (auto dirs_window = counted_directions.begin();
                    dirs_window != counted_directions.upper_bound(window_start_angle - 1.5 * PI); dirs_window++) {
                    dir_acc += dirs_window->first * dirs_window->second;
                    score_acc += dirs_window->second;
                }
            }

            if (score_acc > best_dir.second) {
                best_dir = {dir_acc / score_acc, score_acc};
            }
        }
        double bridging_angle = best_dir.first;
        if (bridging_angle == 0) {
            bridging_angle = 0.001;
        }
        switch (dominant_pattern) {
        case ipHilbertCurve: bridging_angle += 0.25 * PI; break;
        case ipOctagramSpiral: bridging_angle += (1.0 / 16.0) * PI; break;
        default: break;
        }

        return bridging_angle;
        };

    // LAMBDA that will fill given polygons with lines, exapand the lines to the nearest anchor, and reconstruct polygons from the newly
    // generated lines
    auto construct_anchored_polygon = [](Polygons bridged_area, Lines anchors, const Flow &bridging_flow, double bridging_angle) {
        auto lines_rotate = [](Lines &lines, double cos_angle, double sin_angle) {
            for (Line &l : lines) {
                double ax = double(l.a.x());
                double ay = double(l.a.y());
                l.a.x()   = coord_t(round(cos_angle * ax - sin_angle * ay));
                l.a.y()   = coord_t(round(cos_angle * ay + sin_angle * ax));
                double bx = double(l.b.x());
                double by = double(l.b.y());
                l.b.x()   = coord_t(round(cos_angle * bx - sin_angle * by));
                l.b.y()   = coord_t(round(cos_angle * by + sin_angle * bx));
            }
            };

        auto segments_overlap = [](coord_t alow, coord_t ahigh, coord_t blow, coord_t bhigh) {
            return (alow >= blow && alow <= bhigh) || (ahigh >= blow && ahigh <= bhigh) || (blow >= alow && blow <= ahigh) ||
                (bhigh >= alow && bhigh <= ahigh);
            };

        Polygons expanded_bridged_area{};
        double   aligning_angle = -bridging_angle + PI * 0.5;
        {
            polygons_rotate(bridged_area, aligning_angle);
            lines_rotate(anchors, cos(aligning_angle), sin(aligning_angle));
            BoundingBox bb_x = get_extents(bridged_area);
            BoundingBox bb_y = get_extents(anchors);

            const size_t n_vlines = (bb_x.max.x() - bb_x.min.x() + bridging_flow.scaled_spacing() - 1) / bridging_flow.scaled_spacing();
            std::vector<Line> vertical_lines(n_vlines);
            for (size_t i = 0; i < n_vlines; i++) {
                coord_t x           = bb_x.min.x() + i * bridging_flow.scaled_spacing();
                coord_t y_min       = bb_y.min.y() - bridging_flow.scaled_spacing();
                coord_t y_max       = bb_y.max.y() + bridging_flow.scaled_spacing();
                vertical_lines[i].a = Point{x, y_min};
                vertical_lines[i].b = Point{x, y_max};
            }

            auto anchors_and_walls_tree = AABBTreeLines::LinesDistancer<Line>{std::move(anchors)};
            auto bridged_area_tree      = AABBTreeLines::LinesDistancer<Line>{to_lines(bridged_area)};

            std::vector<std::vector<Line>> polygon_sections(n_vlines);
            for (size_t i = 0; i < n_vlines; i++) {
                auto area_intersections = bridged_area_tree.intersections_with_line<true>(vertical_lines[i]);
                for (int intersection_idx = 0; intersection_idx < int(area_intersections.size()) - 1; intersection_idx++) {
                    if (bridged_area_tree.outside(
                        (area_intersections[intersection_idx].first + area_intersections[intersection_idx + 1].first) / 2) < 0) {
                        polygon_sections[i].emplace_back(area_intersections[intersection_idx].first,
                            area_intersections[intersection_idx + 1].first);
                    }
                }
                auto anchors_intersections = anchors_and_walls_tree.intersections_with_line<true>(vertical_lines[i]);

                for (Line &section : polygon_sections[i]) {
                    auto maybe_below_anchor = std::upper_bound(anchors_intersections.rbegin(), anchors_intersections.rend(), section.a,
                        [](const Point &a, const std::pair<Point, size_t> &b) {
                            return a.y() > b.first.y();
                        });
                    if (maybe_below_anchor != anchors_intersections.rend()) {
                        section.a = maybe_below_anchor->first;
                        section.a.y() -= bridging_flow.scaled_width() * (0.5 + 0.5);
                    }

                    auto maybe_upper_anchor = std::upper_bound(anchors_intersections.begin(), anchors_intersections.end(), section.b,
                        [](const Point &a, const std::pair<Point, size_t> &b) {
                            return a.y() < b.first.y();
                        });
                    if (maybe_upper_anchor != anchors_intersections.end()) {
                        section.b = maybe_upper_anchor->first;
                        section.b.y() += bridging_flow.scaled_width() * (0.5 + 0.5);
                    }
                }

                for (int section_idx = 0; section_idx < int(polygon_sections[i].size()) - 1; section_idx++) {
                    Line &section_a = polygon_sections[i][section_idx];
                    Line &section_b = polygon_sections[i][section_idx + 1];
                    if (segments_overlap(section_a.a.y(), section_a.b.y(), section_b.a.y(), section_b.b.y())) {
                        section_b.a = section_a.a.y() < section_b.a.y() ? section_a.a : section_b.a;
                        section_b.b = section_a.b.y() < section_b.b.y() ? section_b.b : section_a.b;
                        section_a.a = section_a.b;
                    }
                }

                polygon_sections[i].erase(std::remove_if(polygon_sections[i].begin(), polygon_sections[i].end(),
                    [](const Line &s) { return s.a == s.b; }),
                    polygon_sections[i].end());
                std::sort(polygon_sections[i].begin(), polygon_sections[i].end(),
                    [](const Line &a, const Line &b) { return a.a.y() < b.b.y(); });
            }

            // reconstruct polygon from polygon sections
            struct TracedPoly
            {
                Points lows;
                Points highs;
            };

            std::vector<TracedPoly> current_traced_polys;
            for (const auto &polygon_slice : polygon_sections) {
                std::unordered_set<const Line *> used_segments;
                for (TracedPoly &traced_poly : current_traced_polys) {
                    auto candidates_begin = std::upper_bound(polygon_slice.begin(), polygon_slice.end(), traced_poly.lows.back(),
                        [](const Point &low, const Line &seg) { return seg.b.y() > low.y(); });
                    auto candidates_end   = std::upper_bound(polygon_slice.begin(), polygon_slice.end(), traced_poly.highs.back(),
                        [](const Point &high, const Line &seg) { return seg.a.y() > high.y(); });

                    bool segment_added = false;
                    for (auto candidate = candidates_begin; candidate != candidates_end && !segment_added; candidate++) {
                        if (used_segments.find(&(*candidate)) != used_segments.end()) {
                            continue;
                        }

                        if ((traced_poly.lows.back() - candidate->a).cast<double>().squaredNorm() <
                            36.0 * double(bridging_flow.scaled_spacing()) * bridging_flow.scaled_spacing()) {
                            traced_poly.lows.push_back(candidate->a);
                        } else {
                            traced_poly.lows.push_back(traced_poly.lows.back() + Point{bridging_flow.scaled_spacing() / 2, 0});
                            traced_poly.lows.push_back(candidate->a - Point{bridging_flow.scaled_spacing() / 2, 0});
                            traced_poly.lows.push_back(candidate->a);
                        }

                        if ((traced_poly.highs.back() - candidate->b).cast<double>().squaredNorm() <
                            36.0 * double(bridging_flow.scaled_spacing()) * bridging_flow.scaled_spacing()) {
                            traced_poly.highs.push_back(candidate->b);
                        } else {
                            traced_poly.highs.push_back(traced_poly.highs.back() + Point{bridging_flow.scaled_spacing() / 2, 0});
                            traced_poly.highs.push_back(candidate->b - Point{bridging_flow.scaled_spacing() / 2, 0});
                            traced_poly.highs.push_back(candidate->b);
                        }
                        segment_added = true;
                        used_segments.insert(&(*candidate));
                    }

                    if (!segment_added) {
                        // Zero overlapping segments, we just close this polygon
                        traced_poly.lows.push_back(traced_poly.lows.back() + Point{bridging_flow.scaled_spacing() / 2, 0});
                        traced_poly.highs.push_back(traced_poly.highs.back() + Point{bridging_flow.scaled_spacing() / 2, 0});
                        Polygon &new_poly = expanded_bridged_area.emplace_back(std::move(traced_poly.lows));
                        new_poly.points.insert(new_poly.points.end(), traced_poly.highs.rbegin(), traced_poly.highs.rend());
                        traced_poly.lows.clear();
                        traced_poly.highs.clear();
                    }
                }

                current_traced_polys.erase(std::remove_if(current_traced_polys.begin(), current_traced_polys.end(),
                    [](const TracedPoly &tp) { return tp.lows.empty(); }),
                    current_traced_polys.end());

                for (const auto &segment : polygon_slice) {
                    if (used_segments.find(&segment) == used_segments.end()) {
                        TracedPoly &new_tp = current_traced_polys.emplace_back();
                        new_tp.lows.push_back(segment.a - Point{bridging_flow.scaled_spacing() / 2, 0});
                        new_tp.lows.push_back(segment.a);
                        new_tp.highs.push_back(segment.b - Point{bridging_flow.scaled_spacing() / 2, 0});
                        new_tp.highs.push_back(segment.b);
                    }
                }
            }

            // add not closed polys
            for (TracedPoly &traced_poly : current_traced_polys) {
                Polygon &new_poly = expanded_bridged_area.emplace_back(std::move(traced_poly.lows));
                new_poly.points.insert(new_poly.points.end(), traced_poly.highs.rbegin(), traced_poly.highs.rend());
            }
            expanded_bridged_area = union_safety_offset(expanded_bridged_area);
        }

        polygons_rotate(expanded_bridged_area, -aligning_angle);
        return expanded_bridged_area;
        };

    tbb::parallel_for(tbb::blocked_range<size_t>(0, clustered_layers_for_threads.size()), [po = static_cast<const PrintObject *>(this),
        target_flow_height_factor, &surfaces_by_layer,
        &clustered_layers_for_threads,
        gather_areas_w_depth, &infill_lines,
        determine_bridging_angle,
        construct_anchored_polygon](
            tbb::blocked_range<size_t> r) {
                for (size_t cluster_idx = r.begin(); cluster_idx < r.end(); cluster_idx++) {
                    for (size_t job_idx = 0; job_idx < clustered_layers_for_threads[cluster_idx].size(); job_idx++) {
                        size_t       lidx  = clustered_layers_for_threads[cluster_idx][job_idx];
                        const Layer *layer = po->get_layer(lidx);
                        // this thread has exclusive access to all surfaces in layers enumerated in
                        // clustered_layers_for_threads[cluster_idx]

                        // Presort the candidate polygons. This will help choose the same angle for neighbournig surfaces, that
                        // would otherwise compete over anchoring sparse infill lines, leaving one area unachored
                        std::sort(surfaces_by_layer[lidx].begin(), surfaces_by_layer[lidx].end(),
                            [](const CandidateSurface &left, const CandidateSurface &right) {
                                auto a = get_extents(left.new_polys);
                                auto b = get_extents(right.new_polys);

                                if (a.min.x() == b.min.x()) {
                                    return a.min.y() < b.min.y();
                                };
                                return a.min.x() < b.min.x();
                            });
                        if (surfaces_by_layer[lidx].size() > 2) {
                            Vec2d origin = get_extents(surfaces_by_layer[lidx].front().new_polys).max.cast<double>();
                            std::stable_sort(surfaces_by_layer[lidx].begin() + 1, surfaces_by_layer[lidx].end(),
                                [origin](const CandidateSurface &left, const CandidateSurface &right) {
                                    auto a = get_extents(left.new_polys);
                                    auto b = get_extents(right.new_polys);

                                    return (origin - a.min.cast<double>()).squaredNorm() <
                                        (origin - b.min.cast<double>()).squaredNorm();
                                });
                        }

                        // Gather deep infill areas, where thick bridges fit
                        coordf_t spacing            = surfaces_by_layer[lidx].front().region->bridging_flow(frSolidInfill, true).scaled_spacing();
                        coordf_t target_flow_height = surfaces_by_layer[lidx].front().region->bridging_flow(frSolidInfill, true).height() *
                            target_flow_height_factor;
                        // 收集当前层中可以应用thick_bridge的区域
                        Polygons deep_infill_area = gather_areas_w_depth(po, lidx, target_flow_height);

                        {
                            // Now also remove area that has been already filled on lower layers by bridging expansion - For this
                            // reason we did the clustering of layers per thread.
                            Polygons filled_polyons_on_lower_layers;
                            double   bottom_z = layer->print_z - target_flow_height - EPSILON;
                            if (job_idx > 0) {
                                for (int lower_job_idx = job_idx - 1; lower_job_idx >= 0; lower_job_idx--) {
                                    size_t       lower_layer_idx = clustered_layers_for_threads[cluster_idx][lower_job_idx];
                                    const Layer *lower_layer     = po->get_layer(lower_layer_idx);
                                    if (lower_layer->print_z >= bottom_z) {
                                        for (const auto &c : surfaces_by_layer[lower_layer_idx]) {
                                            filled_polyons_on_lower_layers.insert(filled_polyons_on_lower_layers.end(), c.new_polys.begin(),
                                                c.new_polys.end());
                                        }
                                    } else {
                                        break;
                                    }
                                }
                            }
                            // 再减去别的层已经生成的桥接区域
                            deep_infill_area = diff(deep_infill_area, filled_polyons_on_lower_layers);
                        }
                        // 得到thick_bridge区域，bridge区域扩1.5倍
                        deep_infill_area = expand(deep_infill_area, spacing * 1.5);

                        // Now gather expansion polygons - internal infill on current layer, from which we can cut off anchors
                        Polygons lightning_area;
                        Polygons expansion_area; // 可以提供扩张的区域
                        Polygons total_fill_area; // 所有填充区域
                        Polygons top_area; // 顶面区域

                        for (LayerRegion *region : layer->regions()) {
                            Polygons internal_polys = to_polygons(region->fill_surfaces.filter_by_types({stInternal, stInternalSolid}));
                            expansion_area.insert(expansion_area.end(), internal_polys.begin(), internal_polys.end());
                            Polygons fill_polys = to_polygons(region->fill_expolygons);
                            total_fill_area.insert(total_fill_area.end(), fill_polys.begin(), fill_polys.end());
                            Polygons top_polys = to_polygons(region->fill_surfaces.filter_by_type(stTop));
                            top_area.insert(top_area.end(), top_polys.begin(), top_polys.end());
                            if (region->region().config().sparse_infill_pattern == ipLightning) {
                                Polygons l = to_polygons(region->fill_surfaces.filter_by_type(stInternal));
                                lightning_area.insert(lightning_area.end(), l.begin(), l.end());
                            }
                        }
                        total_fill_area   = closing(total_fill_area, float(SCALED_EPSILON));
                        expansion_area    = closing(expansion_area, float(SCALED_EPSILON));
                        expansion_area    = intersection(expansion_area, deep_infill_area);
                        Polylines anchors = intersection_pl(infill_lines[lidx - 1], shrink(expansion_area, spacing));
                        Polygons internal_unsupported_area = shrink(deep_infill_area, spacing * 4.5);

#ifdef DEBUG_BRIDGE_OVER_INFILL
                        debug_draw(std::to_string(lidx) + "_" + std::to_string(cluster_idx) + "_" + std::to_string(job_idx) + "_" + "_total_area",
                            to_lines(total_fill_area), to_lines(expansion_area), to_lines(deep_infill_area), to_lines(anchors));
#endif

                        std::vector<CandidateSurface> expanded_surfaces;
                        expanded_surfaces.reserve(surfaces_by_layer[lidx].size());
                        for (const CandidateSurface &candidate : surfaces_by_layer[lidx]) {
                            const Flow &flow              = candidate.region->bridging_flow(frSolidInfill, true);
                            Polygons    area_to_be_bridge = expand(candidate.new_polys, flow.scaled_spacing()); // 待生成桥接区域
                            area_to_be_bridge             = intersection(area_to_be_bridge, deep_infill_area);
                            ExPolygons area_to_be_bridge_ex = union_ex(area_to_be_bridge);
                            area_to_be_bridge_ex.erase(std::remove_if(area_to_be_bridge_ex.begin(), area_to_be_bridge_ex.end(),
                                [internal_unsupported_area](const ExPolygon &p) {
                                    return intersection({p}, internal_unsupported_area).empty();
                                }),
                                area_to_be_bridge_ex.end());

                            area_to_be_bridge = to_polygons(area_to_be_bridge_ex);

                            Polygons limiting_area = union_(area_to_be_bridge, expansion_area); // 桥接区域 + 可扩张区域

                            if (area_to_be_bridge.empty())
                                continue;

                            Polylines boundary_plines = to_polylines(expand(total_fill_area, 1.3 * flow.scaled_spacing()));
                            {
                                Polylines limiting_plines = to_polylines(expand(limiting_area, 0.3*flow.spacing()));
                                boundary_plines.insert(boundary_plines.end(), limiting_plines.begin(), limiting_plines.end());
                            }

#ifdef DEBUG_BRIDGE_OVER_INFILL
                            int r = rand();
                            debug_draw(std::to_string(lidx) + "_" + std::to_string(cluster_idx) + "_" + std::to_string(job_idx) + "_" +
                                "_anchors_" + std::to_string(r),
                                to_lines(area_to_be_bridge), to_lines(boundary_plines), to_lines(anchors), to_lines(expansion_area));
#endif

                            double bridging_angle = 0;
                            if (!anchors.empty()) {
                                bridging_angle = determine_bridging_angle(area_to_be_bridge, to_lines(anchors),
                                                                            candidate.region->region().config().sparse_infill_pattern.value,
                                                                            candidate.region->region().config().infill_direction.value);
                            } else {
                                // use expansion boundaries as anchors.
                                // Also, use Infill pattern that is neutral for angle determination, since there are no infill lines.
                                bridging_angle = determine_bridging_angle(area_to_be_bridge, to_lines(boundary_plines), InfillPattern::ipLine, 0);
                            }

                            boundary_plines.insert(boundary_plines.end(), anchors.begin(), anchors.end());
                            if (!lightning_area.empty() && !intersection(area_to_be_bridge, lightning_area).empty()) {
                                boundary_plines = intersection_pl(boundary_plines, expand(area_to_be_bridge, scale_(10)));
                            }
                            Polygons bridging_area = construct_anchored_polygon(area_to_be_bridge, to_lines(boundary_plines), flow, bridging_angle);

                            // Check collision with other expanded surfaces
                            {
                                bool     reconstruct       = false;
                                Polygons tmp_expanded_area = expand(bridging_area, 3.0 * flow.scaled_spacing());
                                for (const CandidateSurface &s : expanded_surfaces) {
                                    if (!intersection(s.new_polys, tmp_expanded_area).empty()) {
                                        bridging_angle = s.bridge_angle;
                                        reconstruct    = true;
                                        break;
                                    }
                                }
                                if (reconstruct) {
                                    bridging_area = construct_anchored_polygon(area_to_be_bridge, to_lines(boundary_plines), flow, bridging_angle);
                                }
                            }

                            bridging_area          = opening(bridging_area, flow.scaled_spacing());
                            bridging_area          = closing(bridging_area, flow.scaled_spacing());
                            bridging_area          = intersection(bridging_area, limiting_area);
                            bridging_area          = intersection(bridging_area, total_fill_area);
                            // BBS: substract top area
                            bridging_area          = diff(bridging_area, top_area);
                            // BBS: open and close again to filter some narrow parts
                            bridging_area          = opening(bridging_area, flow.scaled_spacing());
                            bridging_area          = closing(bridging_area, flow.scaled_spacing());
                            expansion_area         = diff(expansion_area, bridging_area);

#ifdef DEBUG_BRIDGE_OVER_INFILL
                            debug_draw(std::to_string(lidx) + "_" + std::to_string(cluster_idx) + "_" + std::to_string(job_idx) + "_" + "_expanded_bridging" +  std::to_string(r),
                                to_lines(layer->lslices), to_lines(boundary_plines), to_lines(candidate.new_polys), to_lines(bridging_area));
#endif

                            expanded_surfaces.push_back(CandidateSurface(candidate.original_surface, candidate.layer_index, bridging_area,
                                candidate.region, bridging_angle));
                        }
                        surfaces_by_layer[lidx].swap(expanded_surfaces);
                        expanded_surfaces.clear();
                    }
                }
        });

    BOOST_LOG_TRIVIAL(info) << "Bridge over infill - Directions and expanded surfaces computed" << log_memory_info();

    tbb::parallel_for(tbb::blocked_range<size_t>(0, this->layers().size()), [po = this, &surfaces_by_layer](tbb::blocked_range<size_t> r) {
        for (size_t lidx = r.begin(); lidx < r.end(); lidx++) {
            // 如果既不需要生成桥接，也不是桥接的下一层，不处理
            if (surfaces_by_layer.find(lidx) == surfaces_by_layer.end() && surfaces_by_layer.find(lidx + 1) == surfaces_by_layer.end())
                continue;
            Layer *layer = po->get_layer(lidx);

            Polygons cut_from_infill{}; // 桥接区域
            if (surfaces_by_layer.find(lidx) != surfaces_by_layer.end()) {
                for (const auto &surface : surfaces_by_layer.at(lidx)) {
                    cut_from_infill.insert(cut_from_infill.end(), surface.new_polys.begin(), surface.new_polys.end());
                }
            }

            Polygons additional_ensuring_areas{}; // 下一层为上一层桥接需要生成的区域
            if (surfaces_by_layer.find(lidx + 1) != surfaces_by_layer.end()) {
                for (const auto &surface : surfaces_by_layer.at(lidx + 1)) {
                    auto additional_area = diff(surface.new_polys,
                        shrink(surface.new_polys, surface.region->flow(frSolidInfill).scaled_spacing()));
                    additional_ensuring_areas.insert(additional_ensuring_areas.end(), additional_area.begin(), additional_area.end());
                }
            }

            for (LayerRegion *region : layer->regions()) {
                Surfaces new_surfaces;

                Polygons near_perimeters = to_polygons(union_safety_offset_ex(to_polygons(region->fill_surfaces.surfaces))); // 填充区域中，紧靠着外墙的区域
                near_perimeters          = diff(near_perimeters, shrink(near_perimeters, region->flow(frSolidInfill).scaled_spacing()));
                ExPolygons additional_ensuring = intersection_ex(additional_ensuring_areas, near_perimeters); // 紧靠着外墙，能够给上一层的桥接提供支撑的区域

                SurfacesPtr internal_infills = region->fill_surfaces.filter_by_type(stInternal);
                ExPolygons new_internal_infills = diff_ex(internal_infills, cut_from_infill); // 新的稀疏填充区域，去掉生成的桥接区域
                new_internal_infills            = diff_ex(new_internal_infills, additional_ensuring);
                for (const ExPolygon &ep : new_internal_infills) {
                    new_surfaces.emplace_back(stInternal, ep);
                }

                SurfacesPtr internal_solids = region->fill_surfaces.filter_by_type(stInternalSolid);
                if (surfaces_by_layer.find(lidx) != surfaces_by_layer.end()) {
                    for (const CandidateSurface &cs : surfaces_by_layer.at(lidx)) {
                        for (const Surface *surface : internal_solids) {
                            if (cs.original_surface == surface) {
                                Surface tmp{*surface, {}};
                                tmp.surface_type = stInternalBridge;
                                tmp.bridge_angle = cs.bridge_angle;
                                for (const ExPolygon &ep : intersection_ex(union_ex(cs.new_polys),region->fill_expolygons)) {
                                    new_surfaces.emplace_back(tmp, ep);
                                }
                                break;
                            }
                        }
                    }
                }

                ExPolygons new_internal_solids = to_expolygons(internal_solids);
                new_internal_solids.insert(new_internal_solids.end(), additional_ensuring.begin(), additional_ensuring.end());
                new_internal_solids = diff_ex(new_internal_solids, cut_from_infill);
                new_internal_solids = union_safety_offset_ex(new_internal_solids);
                for (const ExPolygon &ep : new_internal_solids) {
                    new_surfaces.emplace_back(stInternalSolid, ep);
                }

#ifdef DEBUG_BRIDGE_OVER_INFILL
                debug_draw("Aensuring_" + std::to_string(reinterpret_cast<uint64_t>(&region)), to_polylines(additional_ensuring),
                    to_polylines(near_perimeters), to_polylines(to_polygons(internal_infills)),
                    to_polylines(to_polygons(internal_solids)));
                debug_draw("Aensuring_" + std::to_string(reinterpret_cast<uint64_t>(&region)) + "_new", to_polylines(additional_ensuring),
                    to_polylines(near_perimeters), to_polylines(to_polygons(new_internal_infills)),
                    to_polylines(to_polygons(new_internal_solids)));
#endif

                region->fill_surfaces.remove_types({stInternalSolid, stInternal});
                region->fill_surfaces.append(new_surfaces);
            }
        }
});

    BOOST_LOG_TRIVIAL(info) << "Bridge over infill - End" << log_memory_info();

} // void PrintObject::bridge_over_infill()

static void clamp_exturder_to_default(ConfigOptionInt &opt, size_t num_extruders)
{
    if (opt.value > (int) num_extruders)
        // assign the default extruder
        opt.value = 1;
}
static void clamp_exturder_to_default_protect0(ConfigOptionInt &opt, size_t num_extruders)
{
    if (opt.value > (int) num_extruders)
        // assign the default extruder
        opt.value = 1;
    else if (opt.value < 1)
        // assign the default extruder
        opt.value = 1;
}

PrintObjectConfig PrintObject::object_config_from_model_object(const PrintObjectConfig &default_object_config, const ModelObject &object, size_t num_extruders, std::vector<int>& variant_index)
{
    PrintObjectConfig config = default_object_config;
    {
        DynamicPrintConfig src_normalized(object.config.get());
        src_normalized.normalize_fdm();
        update_static_print_config_from_dynamic(config, src_normalized, variant_index, print_options_with_variant, 1);
    }
    // Clamp invalid extruders to the default extruder (with index 1).
    clamp_exturder_to_default(config.support_filament,           num_extruders);
    clamp_exturder_to_default(config.support_interface_filament, num_extruders);
    return config;
}

const std::string                                                    key_extruder { "extruder" };
static constexpr const std::initializer_list<const std::string_view> keys_extruders { "sparse_infill_filament"sv, "solid_infill_filament"sv, "wall_filament"sv };

static void apply_to_print_region_config(PrintRegionConfig &out, const DynamicPrintConfig &in, std::vector<int>& variant_index)
{
    // 1) Copy the "extruder key to sparse_infill_filament and wall_filament.
    auto *opt_extruder = in.opt<ConfigOptionInt>(key_extruder);
    if (opt_extruder)
        if (int extruder = opt_extruder->value; extruder != 0) {
            // Not a default extruder.
            out.sparse_infill_filament      .value = extruder;
            out.solid_infill_filament.value = extruder;
            out.wall_filament   .value = extruder;
        }
    // 2) Copy the rest of the values.
    for (auto it = in.cbegin(); it != in.cend(); ++ it)
        if (it->first != key_extruder)
            if (ConfigOption* my_opt = out.option(it->first, false); my_opt != nullptr) {
                if (one_of(it->first, keys_extruders)) {
                    // Ignore "default" extruders.
                    // BBS: 2025-10-20 skip these filament settings, they will be processed out of this func
                    //int extruder = static_cast<const ConfigOptionInt*>(it->second.get())->value;
                    //if (extruder > 0)
                    //    my_opt->setInt(extruder);
                } else {
                    if (*my_opt != *(it->second)) {
                        if (my_opt->is_scalar() || variant_index.empty() || (print_options_with_variant.find(it->first) == print_options_with_variant.end()))
                            my_opt->set(it->second.get());
                            //my_opt->set(it->second.get());
                        else {
                            ConfigOptionVectorBase* opt_vec_src = static_cast<ConfigOptionVectorBase*>(my_opt);
                            const ConfigOptionVectorBase* opt_vec_dest = static_cast<const ConfigOptionVectorBase*>(it->second.get());
                            opt_vec_src->set_to_index(opt_vec_dest, variant_index, 1);
                        }
                    }
                }
            }
}

// Internal infill keys a modifier may set that should be dropped when modifier_ignore_infill is on.
static const char *s_modifier_ignored_infill_keys[] = {
    "sparse_infill_density",
    "sparse_infill_pattern",
    "conformal_infill",
    "conformal_stagger",
    "conformal_link_keep_layers",
    "conformal_link_flip_layers",
    "conformal_pole",
    "conformal_ray_count",
    "conformal_hub_radius",
    "infill_direction",
    "symmetric_infill_y_axis",
    "infill_shift_step",
    "sparse_infill_lattice_angle_1",
    "sparse_infill_lattice_angle_2",
    "infill_rotate_step",
    "skeleton_infill_density",
    "skin_infill_density",
    "fill_multiline",
    "infill_lock_depth",
    "skin_infill_depth",
    "locked_skin_infill_pattern",
    "locked_skeleton_infill_pattern",
    "sparse_infill_filament",
    "sparse_infill_line_width",
    "skin_infill_line_width",
    "skeleton_infill_line_width",
    "infill_wall_overlap",
    "sparse_infill_speed",
    "infill_combination",
    "minimum_sparse_infill_area",
    "internal_solid_infill_pattern",
    "internal_solid_infill_line_width",
    "internal_solid_infill_speed",
    "solid_infill_filament",
    "sparse_infill_anchor",
    "sparse_infill_anchor_max",
    "detect_narrow_internal_solid_infill",
    "embedding_wall_into_infill",
    "infill_instead_top_bottom_surfaces",
    "bridge_angle",
};

static bool volume_ignores_modifier_infill(const ModelVolume &volume)
{
    if (!volume.is_modifier())
        return false;
    const auto *opt = volume.config.get().option<ConfigOptionBool>("modifier_ignore_infill");
    return opt != nullptr && opt->value;
}

static void restore_parent_internal_infill(PrintRegionConfig &config, const PrintRegionConfig &parent)
{
    for (const char *key : s_modifier_ignored_infill_keys) {
        const ConfigOption *src = parent.option(key);
        ConfigOption       *dst = config.option(key, false);
        if (src != nullptr && dst != nullptr)
            dst->set(src);
    }
}

PrintRegionConfig region_config_from_model_volume(const PrintRegionConfig &default_or_parent_region_config, const DynamicPrintConfig *layer_range_config, const ModelVolume &volume, size_t num_extruders, std::vector<int>& variant_index)
{
    PrintRegionConfig config = default_or_parent_region_config;
    if (volume.is_model_part()) {
        // default_or_parent_region_config contains the Print's PrintRegionConfig.
        // Override with ModelObject's PrintRegionConfig values.
        apply_to_print_region_config(config, volume.get_object()->config.get(), variant_index);
    } else {
        // default_or_parent_region_config contains parent PrintRegion config, which already contains ModelVolume's config.
    }
    const bool ignore_infill = volume_ignores_modifier_infill(volume);
    apply_to_print_region_config(config, volume.config.get(), variant_index);
    if (! volume.material_id().empty())
        apply_to_print_region_config(config, volume.material()->config.get(), variant_index);
    if (layer_range_config != nullptr) {
        // Not applicable to modifiers.
        assert(volume.is_model_part());
        apply_to_print_region_config(config, *layer_range_config, variant_index);
    }
    if (ignore_infill)
        restore_parent_internal_infill(config, default_or_parent_region_config);
    // Do not inherit this flag onto child modifiers; it only describes this volume.
    config.modifier_ignore_infill.value = ignore_infill;

    {//over write the seprate filament for features config
        auto resolve_filament_value = [&](const std::string &key, int default_or_parent, int previous_value) -> int {
            int   filament_value_temp = 0;
            bool  has_local_value     = false;
            auto *opt_vol             = volume.config.get().opt<ConfigOptionInt>(key);
            auto *opt_obj             = volume.get_object()->config.get().opt<ConfigOptionInt>(key);
            if (opt_vol) // deside use which value
                filament_value_temp = opt_vol->value;
            else if (opt_obj)
                filament_value_temp = opt_obj->value;
            has_local_value = opt_vol || opt_obj;

            if (layer_range_config != nullptr && volume.is_model_part()) {
                auto *opt = layer_range_config->opt<ConfigOptionInt>(key);
                if (opt) filament_value_temp = opt->value;
                has_local_value = has_local_value || opt;
                // Filament-by-height selects the whole band, above the global feature filament.
                if (auto *opt_extruder = layer_range_config->opt<ConfigOptionInt>("extruder"); opt_extruder && opt_extruder->value > 0)
                    has_local_value = true;
            }

            if (filament_value_temp > 0)
                return filament_value_temp;
            // Nothing set locally: a global feature filament overrides the part extruder.
            if (!has_local_value && volume.is_model_part() && default_or_parent > 0)
                return default_or_parent;
            return previous_value;
        };
        config.wall_filament.value          = resolve_filament_value("wall_filament", default_or_parent_region_config.wall_filament.value, config.wall_filament.value);
        if (!ignore_infill) {
            config.sparse_infill_filament.value = resolve_filament_value("sparse_infill_filament", default_or_parent_region_config.sparse_infill_filament.value,
                                                                         config.sparse_infill_filament.value);
            config.solid_infill_filament.value  = resolve_filament_value("solid_infill_filament", default_or_parent_region_config.solid_infill_filament.value,
                                                                         config.solid_infill_filament.value);
        }
    }

    // Clamp invalid extruders to the default extruder (with index 1).
    clamp_exturder_to_default_protect0(config.sparse_infill_filament, num_extruders);
    clamp_exturder_to_default_protect0(config.wall_filament, num_extruders);
    clamp_exturder_to_default_protect0(config.solid_infill_filament, num_extruders);
    if (config.sparse_infill_density.value < 0.00011f)
        // Switch of infill for very low infill rates, also avoid division by zero in infill generator for these very low rates.
        // See GH issue #5910.
        config.sparse_infill_density.value = 0;
    else
        config.sparse_infill_density.value = std::min(config.sparse_infill_density.value, 100.);
    // Very small fuzzy_skin_point_distance value may cause infinite recursion, and a near-zero thickness doesn't generate anything anyway.
    // Check here even if `fuzzy_skin == None` because that setting allows painting and the type may get reset later (but `Disabled_fuzzy` will not).
    if (config.fuzzy_skin.value != FuzzySkinType::Disabled_fuzzy && (config.fuzzy_skin_point_distance.value < 0.01 || config.fuzzy_skin_thickness.value < 0.001))
        config.fuzzy_skin.value = FuzzySkinType::Disabled_fuzzy;
    return config;
}

struct POProfiler
{
    uint32_t duration1;
    uint32_t duration2;
};

void PrintObject::generate_support_preview()
{
    POProfiler profiler;

    boost::posix_time::ptime ts1 = boost::posix_time::microsec_clock::local_time();
    this->slice();
    boost::posix_time::ptime ts2 = boost::posix_time::microsec_clock::local_time();
    profiler.duration1 = (ts2 - ts1).total_milliseconds();

    this->generate_support_material();
    boost::posix_time::ptime ts3 = boost::posix_time::microsec_clock::local_time();
    profiler.duration2 = (ts3 - ts2).total_milliseconds();
}

void PrintObject::update_slicing_parameters()
{
    if (m_slicing_params.valid)
        return;
    m_slicing_params = this->compute_slicing_parameters(this->print()->single_diameter_layering_nozzle());
}

SlicingParameters PrintObject::compute_slicing_parameters(double single_diameter_layering_nozzle) const
{
    const DynamicPrintConfig &full_config = this->print()->full_print_config();
    const int subdivision = layer_grid_subdivision(full_config);
    // BBS multi-diameter: a plate printing with one diameter only stacks that nozzle's band of grid steps into
    // one sliced layer, which is the height the merge would have given every band.
    int multiplier = 1;
    if (single_diameter_layering_nozzle > EPSILON)
        multiplier = int(adaptive_layer_band_size(single_diameter_layering_nozzle, layer_grid_nozzle_diameter(full_config),
                                                  m_config.layer_height.value / double(std::max(1, subdivision))));
    return SlicingParameters::create_from_config(
        this->print()->config(), m_config, this->model_object()->bounding_box().max.z(), this->object_extruders(),
        subdivision, multiplier, single_diameter_layering_nozzle);
}

SlicingParameters PrintObject::slicing_parameters(const DynamicPrintConfig& full_config, const ModelObject& model_object, float object_max_z, std::vector<int> variant_index)
{
	PrintConfig         print_config;
	PrintObjectConfig   object_config;
	PrintRegionConfig   default_region_config;
	print_config.apply(full_config, true);
	object_config.apply(full_config, true);
	default_region_config.apply(full_config, true);
    // BBS
	size_t              filament_extruders = print_config.filament_diameter.size();
	object_config = object_config_from_model_object(object_config, model_object, filament_extruders, variant_index);

	std::vector<unsigned int> object_extruders;
	for (const ModelVolume* model_volume : model_object.volumes)
		if (model_volume->is_model_part()) {
			PrintRegion::collect_object_printing_extruders(
				print_config,
				region_config_from_model_volume(default_region_config, nullptr, *model_volume, filament_extruders, variant_index),
                object_config.brim_type != btNoBrim && object_config.brim_width > 0.,
				object_extruders);
			for (const std::pair<const t_layer_height_range, ModelConfig> &range_and_config : model_object.layer_config_ranges)
				if (range_and_config.second.has("wall_filament") ||
					range_and_config.second.has("sparse_infill_filament") ||
					range_and_config.second.has("solid_infill_filament"))
					PrintRegion::collect_object_printing_extruders(
						print_config,
						region_config_from_model_volume(default_region_config, &range_and_config.second.get(), *model_volume, filament_extruders, variant_index),
                        object_config.brim_type != btNoBrim && object_config.brim_width > 0.,
						object_extruders);
		}
    sort_remove_duplicates(object_extruders);
    //FIXME add painting extruders

    if (object_max_z <= 0.f)
        object_max_z = (float)model_object.raw_bounding_box().size().z();
    // The interactive variable-layer editor works in the authored preset's height domain.
    // Conversion to the finer object grid is applied by the PrintObject slicing parameters.
    return SlicingParameters::create_from_config(print_config, object_config, object_max_z, object_extruders);
}

// returns 0-based indices of extruders used to print the object (without brim, support and other helper extrusions)
std::vector<unsigned int> PrintObject::object_extruders() const
{
    std::vector<unsigned int> extruders;
    extruders.reserve(this->all_regions().size() * 3);
#if 0
    for (const PrintRegion &region : this->all_regions())
        region.collect_object_printing_extruders(*this->print(), extruders);
#else
    const ModelObject* mo = this->model_object();
    const GlobalFeatureFilaments global_features = GlobalFeatureFilaments::from_config(this->print()->default_region_config(), this->print()->config().filament_diameter.size());
    for (const ModelVolume* mv : mo->volumes) {
        std::vector<int> volume_extruders = mv->get_extruders(global_features);
        for (int extruder : volume_extruders) {
            assert(extruder > 0);
            extruders.push_back(extruder - 1);
        }
    }
#endif
    sort_remove_duplicates(extruders);
    return extruders;
}

bool PrintObject::update_layer_height_profile(const ModelObject &model_object, const SlicingParameters &slicing_parameters, std::vector<coordf_t> &layer_height_profile, bool &out_nozzle_range_reset)
{
    bool updated = false;
    out_nozzle_range_reset = false;

    if (layer_height_profile.empty()) {
        // use the constructor because the assignement is crashing on ASAN OsX
        layer_height_profile = std::vector<coordf_t>(model_object.layer_height_profile.get());
        for (size_t i = 1; i < layer_height_profile.size(); i += 2)
            layer_height_profile[i] = slicing_parameters.to_base_layer_height(layer_height_profile[i]);
//        layer_height_profile = model_object.layer_height_profile;
        updated = true;
    }

    // Verify the layer_height_profile.
    if (!layer_height_profile.empty() &&
        // Must not be of even length.
        ((layer_height_profile.size() & 1) != 0 ||
            // Last entry must be at the top of the object.
            std::abs(layer_height_profile[layer_height_profile.size() - 2] - slicing_parameters.object_print_z_max + slicing_parameters.object_print_z_min) > 1e-3))
        layer_height_profile.clear();

    // Verify that all layer height values are within the allowed range for the current nozzle.
    if (!layer_height_profile.empty()) {
        for (size_t i = 1; i < layer_height_profile.size(); i += 2) {
            if (layer_height_profile[i] < slicing_parameters.min_layer_height - EPSILON ||
                layer_height_profile[i] > slicing_parameters.max_layer_height + EPSILON) {
                out_nozzle_range_reset = true;
                layer_height_profile.clear();
                break;
            }
        }
    }

    bool not_match_flag = !slicing_parameters.has_raft(); // if there is raft layer_height_profile[1] could also be adaptive
    not_match_flag &= !layer_height_profile.empty() && (layer_height_profile[1] != slicing_parameters.first_object_layer_height);
    if (layer_height_profile.empty() || not_match_flag) {
        //layer_height_profile = layer_height_profile_adaptive(slicing_parameters, model_object.layer_config_ranges, model_object.volumes);
        layer_height_profile = layer_height_profile_from_ranges(slicing_parameters, model_object.layer_config_ranges);
        updated = true;
    }

    return updated;
}
//BBS:
void PrintObject::get_certain_layers(float start, float end, std::vector<LayerPtrs> &out, std::vector<BoundingBox> &boundingbox_objects)
{
    BoundingBox temp;
    LayerPtrs   out_temp;
    for (const auto &layer : layers()) {
        if (layer->print_z < start) continue;

        if (layer->print_z > end + EPSILON) break;
        temp.merge(layer->loverhangs_bbox);
        out_temp.emplace_back(layer);
    }
    boundingbox_objects.emplace_back(std::move(temp));
    out.emplace_back(std::move(out_temp));
};

std::vector<Point> PrintObject::get_instances_shift_without_plate_offset() const
{
    std::vector<Point> out;
    out.reserve(m_instances.size());
    for (const auto& instance : m_instances)
        out.push_back(instance.shift_without_plate_offset());

    return out;
}

// Only active if config->infill_only_where_needed. This step trims the sparse infill,
// so it acts as an internal support. It maintains all other infill types intact.
// Here the internal surfaces and perimeters have to be supported by the sparse infill.
//FIXME The surfaces are supported by a sparse infill, but the sparse infill is only as large as the area to support.
// Likely the sparse infill will not be anchored correctly, so it will not work as intended.
// Also one wishes the perimeters to be supported by a full infill.
// Idempotence of this method is guaranteed by the fact that we don't remove things from
// fill_surfaces but we only turn them into VOID surfaces, thus preserving the boundaries.
void PrintObject::clip_fill_surfaces()
{
    if (! PrintObject::infill_only_where_needed)
        return;
    bool has_infill = false;
    for (size_t i = 0; i < this->num_printing_regions(); ++ i)
        if (this->printing_region(i).config().sparse_infill_density > 0) {
            has_infill = true;
            break;
        }
    if (! has_infill)
        return;

    // We only want infill under ceilings; this is almost like an
    // internal support material.
    // Proceed top-down, skipping the bottom layer.
    Polygons upper_internal;
    for (int layer_id = int(m_layers.size()) - 1; layer_id > 0; -- layer_id) {
        Layer *layer       = m_layers[layer_id];
        Layer *lower_layer = m_layers[layer_id - 1];
        // Detect things that we need to support.
        // Cummulative fill surfaces.
        Polygons fill_surfaces;
        // Solid surfaces to be supported.
        Polygons overhangs;
        for (const LayerRegion *layerm : layer->m_regions)
            for (const Surface &surface : layerm->fill_surfaces.surfaces) {
                Polygons polygons = to_polygons(surface.expolygon);
                if (surface.is_solid())
                    polygons_append(overhangs, polygons);
                polygons_append(fill_surfaces, std::move(polygons));
            }
        Polygons lower_layer_fill_surfaces;
        Polygons lower_layer_internal_surfaces;
        for (const LayerRegion *layerm : lower_layer->m_regions)
            for (const Surface &surface : layerm->fill_surfaces.surfaces) {
                Polygons polygons = to_polygons(surface.expolygon);
                if (surface.surface_type == stInternal || surface.surface_type == stInternalVoid)
                    polygons_append(lower_layer_internal_surfaces, polygons);
                polygons_append(lower_layer_fill_surfaces, std::move(polygons));
            }
        // We also need to support perimeters when there's at least one full unsupported loop
        {
            // Get perimeters area as the difference between slices and fill_surfaces
            // Only consider the area that is not supported by lower perimeters
            Polygons perimeters = intersection(diff(layer->lslices, fill_surfaces), lower_layer_fill_surfaces);
            // Only consider perimeter areas that are at least one extrusion width thick.
            //FIXME Offset2 eats out from both sides, while the perimeters are create outside in.
            //Should the pw not be half of the current value?
            float pw = FLT_MAX;
            for (const LayerRegion *layerm : layer->m_regions)
                pw = std::min(pw, (float)layerm->flow(frPerimeter).scaled_width());
            // Append such thick perimeters to the areas that need support
            polygons_append(overhangs, opening(perimeters, pw));
        }
        // Merge the new overhangs, find new internal infill.
        polygons_append(upper_internal, std::move(overhangs));
        static constexpr const auto closing_radius = scaled<float>(2.f);
        upper_internal = intersection(
            // Regularize the overhang regions, so that the infill areas will not become excessively jagged.
            smooth_outward(
                closing(upper_internal, closing_radius, ClipperLib::jtSquare, 0.),
                scaled<coord_t>(0.1)),
            lower_layer_internal_surfaces);
        // Apply new internal infill to regions.
        for (LayerRegion *layerm : lower_layer->m_regions) {
            if (layerm->region().config().sparse_infill_density.value == 0)
                continue;
            Polygons internal;
            for (Surface &surface : layerm->fill_surfaces.surfaces)
                if (surface.surface_type == stInternal || surface.surface_type == stInternalVoid)
                    polygons_append(internal, std::move(surface.expolygon));
            layerm->fill_surfaces.remove_types({ stInternal, stInternalVoid });
            layerm->fill_surfaces.append(intersection_ex(internal, upper_internal, ApplySafetyOffset::Yes), stInternal);
            layerm->fill_surfaces.append(diff_ex        (internal, upper_internal, ApplySafetyOffset::Yes), stInternalVoid);
            // If there are voids it means that our internal infill is not adjacent to
            // perimeters. In this case it would be nice to add a loop around infill to
            // make it more robust and nicer. TODO.
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
            layerm->export_region_fill_surfaces_to_svg_debug("6_clip_fill_surfaces");
#endif
        }
        m_print->throw_if_canceled();
    }
}

void PrintObject::discover_horizontal_shells()
{
    BOOST_LOG_TRIVIAL(trace);

    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id) {
        for (size_t i = 0; i < m_layers.size(); ++i) {
            m_print->throw_if_canceled();
            Layer* layer = m_layers[i];
            LayerRegion* layerm = layer->regions()[region_id];
            const PrintRegionConfig& region_config = layerm->region().config();
            const EffectiveShellParameters top_shell = effective_top_shell_parameters(
                *this, region_config, m_slicing_params.layer_height);
            const EffectiveShellParameters bottom_shell = effective_bottom_shell_parameters(
                *this, region_config, m_slicing_params.layer_height);
            // If ensure_vertical_shell_thickness, then the rest has already been performed by discover_vertical_shells().
            if (region_config.ensure_vertical_shell_thickness.value!=EnsureVerticalThicknessLevel::evtDisabled)
                continue;

            coordf_t print_z = layer->print_z;
            coordf_t bottom_z = layer->bottom_z();
            for (size_t idx_surface_type = 0; idx_surface_type < 3; ++idx_surface_type) {
                m_print->throw_if_canceled();
                SurfaceType type = (idx_surface_type == 0) ? stTop : (idx_surface_type == 1) ? stBottom : stBottomBridge;
                int num_solid_layers = (type == stTop) ? top_shell.layers : bottom_shell.layers;
                if (num_solid_layers == 0)
                    continue;
                // Find slices of current type for current layer.
                // Use slices instead of fill_surfaces, because they also include the perimeter area,
                // which needs to be propagated in shells; we need to grow slices like we did for
                // fill_surfaces though. Using both ungrown slices and grown fill_surfaces will
                // not work in some situations, as there won't be any grown region in the perimeter
                // area (this was seen in a model where the top layer had one extra perimeter, thus
                // its fill_surfaces were thinner than the lower layer's infill), however it's the best
                // solution so far. Growing the external slices by EXTERNAL_INFILL_MARGIN will put
                // too much solid infill inside nearly-vertical slopes.

                // Surfaces including the area of perimeters. Everything, that is visible from the top / bottom
                // (not covered by a layer above / below).
                // This does not contain the areas covered by perimeters!
                Polygons solid;
                for (const Surface& surface : layerm->slices.surfaces)
                    if (surface.surface_type == type)
                        polygons_append(solid, to_polygons(surface.expolygon));
                // Infill areas (slices without the perimeters).
                for (const Surface& surface : layerm->fill_surfaces.surfaces)
                    if (surface.surface_type == type)
                        polygons_append(solid, to_polygons(surface.expolygon));
                if (solid.empty())
                    continue;
                //                Slic3r::debugf "Layer %d has %s surfaces\n", $i, ($type == stTop) ? 'top' : 'bottom';

                                // Scatter top / bottom regions to other layers. Scattering process is inherently serial, it is difficult to parallelize without locking.
                for (int n = (type == stTop) ? int(i) - 1 : int(i) + 1;
                    (type == stTop) ?
                    (n >= 0 && (int(i) - n < num_solid_layers ||
                        print_z - m_layers[n]->print_z < top_shell.thickness - EPSILON)) :
                    (n < int(m_layers.size()) && (n - int(i) < num_solid_layers ||
                        m_layers[n]->bottom_z() - bottom_z < bottom_shell.thickness - EPSILON));
                    (type == stTop) ? --n : ++n)
                {
                    //                    Slic3r::debugf "  looking for neighbors on layer %d...\n", $n;
                                        // Reference to the lower layer of a TOP surface, or an upper layer of a BOTTOM surface.
                    LayerRegion* neighbor_layerm = m_layers[n]->regions()[region_id];

                    // find intersection between neighbor and current layer's surfaces
                    // intersections have contours and holes
                    // we update $solid so that we limit the next neighbor layer to the areas that were
                    // found on this one - in other words, solid shells on one layer (for a given external surface)
                    // are always a subset of the shells found on the previous shell layer
                    // this approach allows for DWIM in hollow sloping vases, where we want bottom
                    // shells to be generated in the base but not in the walls (where there are many
                    // narrow bottom surfaces): reassigning $solid will consider the 'shadow' of the
                    // upper perimeter as an obstacle and shell will not be propagated to more upper layers
                    //FIXME How does it work for stInternalBRIDGE? This is set for sparse infill. Likely this does not work.
                    Polygons new_internal_solid;
                    {
                        Polygons internal;
                        for (const Surface& surface : neighbor_layerm->fill_surfaces.surfaces)
                            if (surface.surface_type == stInternal || surface.surface_type == stInternalSolid)
                                polygons_append(internal, to_polygons(surface.expolygon));
                        new_internal_solid = intersection(solid, internal, ApplySafetyOffset::Yes);
                    }
                    if (new_internal_solid.empty()) {
                        // No internal solid needed on this layer. In order to decide whether to continue
                        // searching on the next neighbor (thus enforcing the configured number of solid
                        // layers, use different strategies according to configured infill density:
                        if (region_config.sparse_infill_density.value == 0 || region_config.ensure_vertical_shell_thickness.value == EnsureVerticalThicknessLevel::evtDisabled) {
                            // If user expects the object to be void (for example a hollow sloping vase),
                            // don't continue the search. In this case, we only generate the external solid
                            // shell if the object would otherwise show a hole (gap between perimeters of
                            // the two layers), and internal solid shells are a subset of the shells found
                            // on each previous layer.
                            goto EXTERNAL;
                        }
                        else {
                            // If we have internal infill, we can generate internal solid shells freely.
                            continue;
                        }
                    }

                    if (region_config.sparse_infill_density.value == 0) {
                        // if we're printing a hollow object we discard any solid shell thinner
                        // than a perimeter width, since it's probably just crossing a sloping wall
                        // and it's not wanted in a hollow print even if it would make sense when
                        // obeying the solid shell count option strictly (DWIM!)
                        float margin = float(neighbor_layerm->flow(frExternalPerimeter).scaled_width());
                        Polygons too_narrow = diff(
                            new_internal_solid,
                            opening(new_internal_solid, margin, margin + ClipperSafetyOffset, jtMiter, 5));
                        // Trim the regularized region by the original region.
                        if (!too_narrow.empty())
                            new_internal_solid = solid = diff(new_internal_solid, too_narrow);
                    }

                    // make sure the new internal solid is wide enough, as it might get collapsed
                    // when spacing is added in Fill.pm
                    {
                        //FIXME Vojtech: Disable this and you will be sorry.
                        float margin = 3.f * layerm->flow(frSolidInfill).scaled_width(); // require at least this size
                        // we use a higher miterLimit here to handle areas with acute angles
                        // in those cases, the default miterLimit would cut the corner and we'd
                        // get a triangle in $too_narrow; if we grow it below then the shell
                        // would have a different shape from the external surface and we'd still
                        // have the same angle, so the next shell would be grown even more and so on.
                        Polygons too_narrow = diff(
                            new_internal_solid,
                            opening(new_internal_solid, margin, margin + ClipperSafetyOffset, ClipperLib::jtMiter, 5));
                        if (!too_narrow.empty()) {
                            // grow the collapsing parts and add the extra area to  the neighbor layer
                            // as well as to our original surfaces so that we support this
                            // additional area in the next shell too
                            // make sure our grown surfaces don't exceed the fill area
                            Polygons internal;
                            for (const Surface& surface : neighbor_layerm->fill_surfaces.surfaces)
                                if (surface.is_internal() && !surface.is_bridge())
                                    polygons_append(internal, to_polygons(surface.expolygon));
                            polygons_append(new_internal_solid,
                                intersection(
                                    expand(too_narrow, +margin),
                                    // Discard bridges as they are grown for anchoring and we can't
                                    // remove such anchors. (This may happen when a bridge is being
                                    // anchored onto a wall where little space remains after the bridge
                                    // is grown, and that little space is an internal solid shell so
                                    // it triggers this too_narrow logic.)
                                    internal));
                            // solid = new_internal_solid;
                        }
                    }

                    // internal-solid are the union of the existing internal-solid surfaces
                    // and new ones
                    SurfaceCollection backup = std::move(neighbor_layerm->fill_surfaces);
                    polygons_append(new_internal_solid, to_polygons(backup.filter_by_type(stInternalSolid)));
                    ExPolygons internal_solid = union_ex(new_internal_solid);
                    // assign new internal-solid surfaces to layer
                    neighbor_layerm->fill_surfaces.set(internal_solid, stInternalSolid);
                    // subtract intersections from layer surfaces to get resulting internal surfaces
                    Polygons polygons_internal = to_polygons(std::move(internal_solid));
                    ExPolygons internal = diff_ex(backup.filter_by_type(stInternal), polygons_internal, ApplySafetyOffset::Yes);
                    // assign resulting internal surfaces to layer
                    neighbor_layerm->fill_surfaces.append(internal, stInternal);
                    polygons_append(polygons_internal, to_polygons(std::move(internal)));
                    // assign top and bottom surfaces to layer
                    backup.keep_types({ stTop, stBottom, stBottomBridge });
                    std::vector<SurfacesPtr> top_bottom_groups;
                    backup.group(&top_bottom_groups);
                    for (SurfacesPtr& group : top_bottom_groups)
                        neighbor_layerm->fill_surfaces.append(
                            diff_ex(group, polygons_internal),
                            // Use an existing surface as a template, it carries the bridge angle etc.
                            *group.front());
                }
            EXTERNAL:;
            } // foreach type (stTop, stBottom, stBottomBridge)
        } // for each layer
    } // for each region

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id) {
        for (const Layer* layer : m_layers) {
            const LayerRegion* layerm = layer->m_regions[region_id];
            layerm->export_region_slices_to_svg_debug("5_discover_horizontal_shells");
            layerm->export_region_fill_surfaces_to_svg_debug("5_discover_horizontal_shells");
        } // for each layer
    } // for each region
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
}

// The diameter every band size is measured against: the nozzle the layer grid resolves, i.e. the 0.2 in
// multi-diameter mode, whatever nozzles are mounted or used. SlicingParameters divided the preset's
// layer height down to that nozzle's step, so each nozzle covers diameter/reference of those steps. The
// reference is deliberately not the preset diameter (the preset may be a coarse one) and not the finest
// nozzle in use either: the latter would rescale the grid whenever the finest one is absent, so a 0.4 + 0.8
// plate would call 0.4 the reference and leave 0.4 printing single steps of a grid built for the 0.2.
static double band_reference_nozzle(const PrintObject &object)
{
    const double primary = layer_grid_nozzle_diameter(object.print()->full_print_config());
    if (primary > EPSILON)
        return primary;
    // No printer preset to read the variant from. Fall back to the finest nozzle laying material down.
    const PrintConfig &print_config = object.print()->config();
    double             min_nozzle   = 0.;
    for (size_t region_id = 0; region_id < object.num_printing_regions(); ++ region_id) {
        bool prints_anything = false;
        for (const Layer *layer : object.layers())
            if (! layer->get_region(int(region_id))->raw_slices.empty()) {
                prints_anything = true;
                break;
            }
        if (! prints_anything)
            continue;
        const double nozzle = nozzle_diameter_for_filament(
            print_config, (unsigned int) object.printing_region(region_id).config().wall_filament.value);
        if (nozzle > EPSILON && (min_nozzle <= EPSILON || nozzle < min_nozzle))
            min_nozzle = nozzle;
    }
    return min_nozzle;
}

// Decide which layers each wall region keeps. The object is sliced at the layer height of the smallest
// nozzle of the print; a larger nozzle then keeps one contour out of every band and drops the ones below
// it, instead of merging them into it. Bands are a global grid shared by every region using the same
// diameter, so two such regions never straddle each other's band boundaries.
void PrintObject::compute_adaptive_merge_groups()
{
    for (Layer *layer : m_layers)
        for (LayerRegion *layerm : layer->regions())
            layerm->adaptive_merge_patches.clear();

    // Only meaningful when different nozzle diameters are present.
    if (! this->print()->uses_multiple_nozzle_diameters())
        return;

    const PrintConfig &print_config = this->print()->config();
    const size_t       region_count = this->num_printing_regions();
    const size_t       layer_count  = m_layers.size();
    if (region_count == 0 || layer_count < 2)
        return;

    std::vector<double> layer_heights;
    layer_heights.reserve(layer_count);
    for (const Layer *layer : m_layers)
        layer_heights.emplace_back(layer->height);

    std::vector<double> region_nozzles(region_count, 0.);
    for (size_t region_id = 0; region_id < region_count; ++ region_id) {
        const PrintRegionConfig &config = this->printing_region(region_id).config();
        const unsigned int filament = (unsigned int) config.wall_filament.value;
        region_nozzles[region_id] = nozzle_diameter_for_filament(print_config, filament);
    }
    const double min_nozzle = band_reference_nozzle(*this);
    if (min_nozzle <= EPSILON)
        return;

    // A colour transition changes ownership inside a band, not the nozzle's Z ladder. The normalization
    // pass has already aligned painted ownership to each band's anchor contour.
    const std::vector<uint8_t> band_tops;

    BOOST_LOG_TRIVIAL(debug) << "[adaptive_merge] layers=" << layer_count << " min_nozzle=" << min_nozzle
                             << " band_tops=none (fixed nozzle ladders)";

    // Plan every region on the fixed ladder shared by its nozzle diameter, then arbitrate the resulting
    // slabs from bottom to top. Colours never create a new ladder phase.
    struct MergeEvent {
        size_t                   region_id;
        double                   nozzle;
        AdaptiveLayerHeightRange range;
        AdaptiveLayerBand        band;
        size_t                    source_bottom_idx;
        bool                      merged;
    };
    std::vector<MergeEvent> events;
    auto span_height = [&layer_heights](size_t bottom_idx, size_t top_idx) {
        double height = 0.;
        for (size_t layer_idx = bottom_idx; layer_idx <= top_idx; ++ layer_idx)
            height += layer_heights[layer_idx];
        return height;
    };
    // Keep the previous slab's floor and the tail's top fixed. Borrow only enough steps from the
    // previous slab to make the tail printable, e.g. 0.5 + 0.1 becomes 0.4 + 0.2 for a 0.8 nozzle.
    // Callers try this only once growing a neighbour inside the preferred window has failed.
    // A slab resting on the bed belongs to the first band, whose height is fixed; it never donates steps.
    auto split_before_tail = [&](size_t bottom_idx, size_t previous_top_idx, size_t tail_top_idx,
                                 const AdaptiveLayerHeightRange &range, size_t &split_top_idx) {
        if (bottom_idx == 0 || bottom_idx >= previous_top_idx || previous_top_idx >= tail_top_idx)
            return false;
        double tail_height = span_height(previous_top_idx + 1, tail_top_idx);
        if (tail_height >= range.min_height - EPSILON)
            return false;
        size_t tail_bottom_idx = previous_top_idx + 1;
        while (tail_bottom_idx > bottom_idx && tail_height < range.min_height - EPSILON)
            tail_height += layer_heights[-- tail_bottom_idx];
        if (tail_bottom_idx == bottom_idx || ! range.contains(tail_height) ||
            ! range.contains(span_height(bottom_idx, tail_bottom_idx - 1)))
            return false;
        split_top_idx = tail_bottom_idx - 1;
        return true;
    };
    // A run of steps a region owns that no band of its nozzle could take. It is carried to the end of
    // planning as an explicit record instead of being suppressed right away, because absorbing it needs to
    // know the space the other nozzles occupy. Recovering it afterwards by subtracting the planned
    // extrusions from the raw slices would also report the hairline by which every step inside a band
    // differs from that band's anchor contour, and those hairlines are not holes.
    struct PendingOrphan {
        size_t                   region_id;
        AdaptiveLayerHeightRange range;
        // The relaxed ceiling, 1.0D. Passing the preferred 0.7D is the last resort before giving the run away.
        double                   max_height;
        size_t                   bottom_idx;
        size_t                   top_idx;
        // "no-band" - no band of this nozzle covers the step. "too-thin" - a band covers it, but everything
        // under the step is occupied and the clear span left is thinner than this nozzle can extrude.
        const char              *source;
        // What was lost. Recovery takes back only the column over it, so the rest of the slab that takes it
        // keeps its own span.
        ExPolygons               area;
        // A too-thin bottom face resting on nothing. It is dropped rather than absorbed: the slab above keeps
        // its own floor, and the step above the face is typed as the bridge over it instead.
        bool                     floating { false };
    };
    std::vector<PendingOrphan> pending_orphans;
    for (size_t region_id = 0; region_id < region_count; ++ region_id) {
        const double nozzle = region_nozzles[region_id];
        if (nozzle <= EPSILON)
            continue;
        const AdaptiveLayerHeightRange range = adaptive_layer_height_range(nozzle);
        const size_t band_size = adaptive_layer_band_size(nozzle, min_nozzle, m_slicing_params.layer_height);

        std::vector<unsigned char> has_material(layer_count, 0);
        for (size_t layer_idx = 0; layer_idx < layer_count; ++ layer_idx)
            has_material[layer_idx] = ! m_layers[layer_idx]->get_region(region_id)->raw_slices.empty();

        if (band_size < 2) {
            // This nozzle extrudes the slicing grid itself, so it prints every layer exactly as sliced.
            for (size_t layer_idx = 0; layer_idx < layer_count; ++ layer_idx)
                if (has_material[layer_idx])
                    events.push_back({ region_id, nozzle, range,
                                       { layer_idx, layer_idx, layer_heights[layer_idx] },
                                       layer_idx, false });
            BOOST_LOG_TRIVIAL(debug) << "[adaptive_merge] region=" << region_id << " nozzle=" << nozzle
                                     << " band_size=1 -> prints every layer";
            continue;
        }

        // One diameter owns one ladder. Region/colour start layers do not create a new phase.
        const std::vector<AdaptiveLayerBand> bands =
            build_adaptive_layer_bands(layer_heights, band_size, range, band_tops);

        std::vector<AdaptiveLayerBand> live_bands;
        std::vector<unsigned char>     covered(layer_count, 0);
        for (const AdaptiveLayerBand &band : bands) {
            AdaptiveLayerBand live = band;
            // The first band is a snapshot of the object base and keeps its grid top whatever it holds.
            if (band.bottom_idx != 0 &&
                ! pull_adaptive_band_top_to_material(live, layer_heights, range, has_material))
                continue;
            for (size_t layer_idx = live.bottom_idx; layer_idx <= live.top_idx; ++ layer_idx)
                covered[layer_idx] = 1;
            live_bands.emplace_back(live);
        }

        // Layers this region owns that no band covers: a colour tail ending inside a band, or a step the
        // ladder had to bridge with a single-layer group. A run the nozzle can extrude gets bands of its own.
        // Printing a thinner one as sliced is what put a 0.1 mm extrusion under a 0.6 nozzle, so it is folded
        // into a neighbouring band while that band's height stays printable, and otherwise recorded as an
        // orphan for the resolution pass below.
        std::string orphans;
        for (size_t layer_idx = 0; layer_idx < layer_count; ) {
            if (covered[layer_idx] || ! has_material[layer_idx]) {
                ++ layer_idx;
                continue;
            }
            size_t run_top = layer_idx;
            while (run_top + 1 < layer_count && ! covered[run_top + 1] && has_material[run_top + 1])
                ++ run_top;
            orphans += "[" + std::to_string(layer_idx) + "," + std::to_string(run_top) + "]";

            // A run this nozzle can extrude on its own is not thin, it is just off the ladder: 0.1 under a 0.4
            // nozzle. It is cut into printable bands of its own, which then go through arbitration like every
            // other band. Only a run thinner than one extrusion is left to the repairs below.
            std::vector<std::pair<size_t, size_t>> own_bands;
            if (split_adaptive_layer_span(layer_heights, layer_idx, run_top, band_size, range, own_bands)) {
                for (const std::pair<size_t, size_t> &own : own_bands)
                    live_bands.push_back({ own.first, own.second, span_height(own.first, own.second) });
                for (size_t idx = layer_idx; idx <= run_top; ++ idx)
                    covered[idx] = 1;
                layer_idx = run_top + 1;
                continue;
            }

            AdaptiveLayerBand *below = nullptr;
            AdaptiveLayerBand *above = nullptr;
            for (AdaptiveLayerBand &band : live_bands) {
                if (layer_idx > 0 && band.top_idx == layer_idx - 1)
                    below = &band;
                if (band.bottom_idx == run_top + 1)
                    above = &band;
            }
            // Growing the band above pulls its floor down; growing the one below lifts this colour's top face
            // by the run. Either keeps the material, so prefer them over giving the run away. The band above
            // goes first: it keeps its group top, so no new print Z appears off the ladder.
            auto absorb = [&](double ceiling) {
                if (above != nullptr) {
                    const double height = span_height(layer_idx, above->top_idx);
                    if (height >= range.min_height - EPSILON && height <= ceiling + EPSILON) {
                        above->bottom_idx    = layer_idx;
                        above->merged_height = height;
                        return true;
                    }
                }
                if (below != nullptr) {
                    const double height = span_height(below->bottom_idx, run_top);
                    if (height >= range.min_height - EPSILON && height <= ceiling + EPSILON) {
                        below->top_idx       = run_top;
                        below->merged_height = height;
                        return true;
                    }
                }
                return false;
            };
            // Re-cut the band below and the run into two slabs inside the window, keeping both outer faces.
            auto split = [&]() {
                size_t split_top_idx;
                if (below == nullptr ||
                    ! split_before_tail(below->bottom_idx, below->top_idx, run_top, range, split_top_idx))
                    return false;
                below->top_idx       = split_top_idx;
                below->merged_height = span_height(below->bottom_idx, split_top_idx);
                // Invalidates below and above, so nothing may follow a successful split.
                live_bands.push_back({ split_top_idx + 1, run_top, span_height(split_top_idx + 1, run_top) });
                return true;
            };
            // Heights inside the preferred window come first, and a whole slab before a cut one: both neighbours
            // are offered the preferred ceiling, then the run may be re-cut with the band below, and only then
            // may a neighbour grow to the relaxed ceiling. Same order as the orphan resolution below.
            if (absorb(range.max_height) || split() || absorb(nozzle))
                for (size_t idx = layer_idx; idx <= run_top; ++ idx)
                    covered[idx] = 1;
            else {
                Polygons run_area;
                for (size_t idx = layer_idx; idx <= run_top; ++ idx)
                    polygons_append(run_area, to_polygons(m_layers[idx]->get_region(region_id)->raw_slices));
                pending_orphans.push_back({ region_id, range, nozzle, layer_idx, run_top, "no-band",
                                            union_ex(run_area) });
            }
            layer_idx = run_top + 1;
        }

        std::string span;
        for (const AdaptiveLayerBand &band : live_bands) {
            span += "[" + std::to_string(band.bottom_idx) + "," + std::to_string(band.top_idx) + "]";
            events.push_back({ region_id, nozzle, range, band, band.bottom_idx, true });
        }

        BOOST_LOG_TRIVIAL(debug) << "[adaptive_merge] region=" << region_id << " nozzle=" << nozzle
                                 << " band_size=" << band_size
                                 << " range=[" << range.min_height << "," << range.max_height << "]"
                                 << " bands=" << live_bands.size() << " " << span
                                 << " orphans=" << (orphans.empty() ? "none" : orphans);
    }
    // Bottom to top for merged slabs. Outside the layer-0 reservation of a first band, fixed-grid events are
    // registered as immutable occupancy before this arbitration, so a coarse band cannot claim a layer that
    // a fine nozzle will extrude later. Where two merged ladders start together the coarser one still wins.
    std::stable_sort(events.begin(), events.end(), [](const MergeEvent &lhs, const MergeEvent &rhs) {
        if (lhs.band.bottom_idx != rhs.band.bottom_idx)
            return lhs.band.bottom_idx < rhs.band.bottom_idx;
        if (std::abs(lhs.nozzle - rhs.nozzle) > EPSILON)
            return lhs.nozzle > rhs.nozzle;
        if (lhs.band.top_idx != rhs.band.top_idx)
            return lhs.band.top_idx < rhs.band.top_idx;
        return lhs.region_id < rhs.region_id;
    });

    // Physical occupancy on the fine slicing grid. A merged patch occupies every step of its Z span even
    // though its G-code is emitted only on the group-top layer.
    std::vector<ExPolygons> occupied(layer_count);
    auto occupy = [&occupied](size_t layer_idx, const ExPolygons &area) {
        if (area.empty())
            return;
        Polygons combined = to_polygons(occupied[layer_idx]);
        polygons_append(combined, to_polygons(area));
        occupied[layer_idx] = union_ex(combined);
    };
    struct FirstBandClaim {
        size_t     region_id;
        size_t     top_idx;
        ExPolygons area;
    };
    std::vector<FirstBandClaim> first_band_claims;
    // Resolved slabs, kept in planning order until the orphan resolution below can no longer change them.
    struct PlannedPatch {
        size_t             region_id;
        // The band's planned floor. Marker copies start here even when the patch itself was clipped higher.
        size_t             store_bottom_idx;
        AdaptiveMergePatch patch;
    };
    std::vector<PlannedPatch> planned;

    // The first coarse band is anchored to its true layer-0 contour, which owns that footprint throughout
    // the band. Reserve it before registering fixed-grid regions; later coarse bands still yield to those
    // immutable fine-nozzle extrusions.
    std::vector<ExPolygons> first_band_reservations(layer_count);
    for (const MergeEvent &event : events) {
        if (! event.merged || event.band.bottom_idx != 0)
            continue;
        const ExPolygons &anchor = m_layers.front()->get_region(event.region_id)->raw_slices;
        if (anchor.empty())
            continue;
        for (size_t layer_idx = 0; layer_idx <= event.band.top_idx; ++ layer_idx) {
            Polygons combined = to_polygons(first_band_reservations[layer_idx]);
            polygons_append(combined, to_polygons(anchor));
            first_band_reservations[layer_idx] = union_ex(combined);
        }
    }

    // Fixed-grid regions are emitted exactly as sliced and have no later clipping pass. Register their
    // unreserved footprint first, including events whose layer lies inside a non-first merged band's span.
    for (const MergeEvent &event : events) {
        if (event.merged)
            continue;
        ExPolygons area = m_layers[event.band.top_idx]->get_region(event.region_id)->raw_slices;
        if (! first_band_reservations[event.band.top_idx].empty())
            area = diff_ex(area, first_band_reservations[event.band.top_idx]);
        occupy(event.band.top_idx, area);
    }

    for (const MergeEvent &event : events) {
        m_print->throw_if_canceled();
        const size_t region_id = event.region_id;
        if (! event.merged)
            continue;

        const AdaptiveLayerBand &band       = event.band;
        const bool               first_band = band.bottom_idx == 0;
        // Planning already resolved the top against the material this region owns.
        const AdaptiveLayerBand &live = band;
        if (first_band && m_layers.front()->get_region(region_id)->raw_slices.empty()) {
            AdaptiveMergePatch patch;
            patch.bottom_idx    = live.bottom_idx;
            patch.top_idx       = live.top_idx;
            patch.merged_height = live.merged_height;
            patch.set_area({});
            for (size_t layer_idx = live.bottom_idx; layer_idx <= live.top_idx; ++ layer_idx) {
                AdaptiveMergePatch stored = patch;
                stored.group_top = layer_idx == live.top_idx;
                m_layers[layer_idx]->get_region(region_id)->adaptive_merge_patches.emplace_back(std::move(stored));
            }
            continue;
        }
        // The first band is a snapshot of the true first layer. Later bands use their top contour.
        const size_t anchor_idx  = first_band ? 0 : live.top_idx;
        const ExPolygons  anchor = m_layers[anchor_idx]->get_region(region_id)->raw_slices;
        const double top_area    = ::Slic3r::area(anchor) * SCALING_FACTOR * SCALING_FACTOR;

        // A band shows one contour over its whole Z span, so a volume whose top face falls inside the band
        // is not in that contour at all: on the steps it does occupy it would be neither walled nor filled.
        // Give what ends inside the band its own group top, on the highest step where it still exists.
        //
        // The fringe by which a shrinking shape - a cone, a dome - exceeds the contour above it is not such a
        // volume: it cannot hold a bead, and the ladder has always accepted it as inherent underfill. Only
        // what this nozzle could really lay a bead into is worth a ceiling of its own.
        //
        // The first band is excluded. Its anchor is the object base by design, and pulling side-wall
        // material up into it is exactly what that choice exists to prevent.
        struct Slab {
            size_t     top_idx;
            ExPolygons area;
        };
        std::vector<Slab> slabs { { live.top_idx, anchor } };
        if (! first_band) {
            Polygons inside;
            for (size_t layer_idx = live.bottom_idx; layer_idx < live.top_idx; ++ layer_idx)
                polygons_append(inside, to_polygons(m_layers[layer_idx]->get_region(region_id)->raw_slices));
            ExPolygons ends_inside =
                adaptive_printable_core(diff_ex(union_ex(inside), anchor), event.nozzle);
            // Top down, so each piece is claimed by the highest step it still exists on.
            for (size_t layer_idx = live.top_idx; layer_idx-- > live.bottom_idx && ! ends_inside.empty();) {
                ExPolygons here = adaptive_printable_core(
                    intersection_ex(ends_inside, m_layers[layer_idx]->get_region(region_id)->raw_slices),
                    event.nozzle);
                if (here.empty())
                    continue;
                ends_inside = diff_ex(ends_inside, here);
                slabs.push_back({ layer_idx, std::move(here) });
            }
        }

        std::vector<AdaptiveMergePatch> patches;
        // Too-thin orphans recorded for this band, kept to decide whether the empty marker is still needed.
        struct BandOrphan {
            size_t     bottom_idx;
            size_t     top_idx;
            ExPolygons area;
        };
        std::vector<BandOrphan> band_orphans;
        for (const Slab &slab : slabs) {
            ExPolygons area = occupied[slab.top_idx].empty() ?
                slab.area : diff_ex(slab.area, occupied[slab.top_idx]);

            // Partition the XY footprint by the highest occupied step underneath it. Every piece keeps this
            // slab's group top, but starts immediately above its own local obstruction. This avoids both
            // overlap and the gap that would result from raising the complete patch for a partial conflict.
            ExPolygons assigned;
            // The first step whose clear span is too thin to extrude. From there up this slab can place
            // nothing, so whatever footprint is still unassigned has lost its owner.
            size_t     thin_bottom_idx = slab.top_idx + 1;
            // A merged extrusion is laid onto whatever is below its floor, so it cannot reach down into a step
            // that holds no object material under its footprint: a colour starting on a bridge in the middle
            // of a band would otherwise hang below the real bottom face. The first band rests on the bed.
            // Fringes too narrow to hold a bead of this nozzle are the usual underfill of a sloped wall.
            //
            // Only a face too thin for this nozzle is held off, and then dropped as an orphan. A face that could be
            // extruded on its own is not cut out of the slab: a patch of its own would wall it off from the
            // material it rests against, so it hangs from the band floor with the rest of the contour.
            std::vector<ExPolygons> unsupported(slab.top_idx + 1);
            if (! first_band && ! area.empty()) {
                Polygons thin_faces;
                for (size_t layer_idx = live.bottom_idx; layer_idx < slab.top_idx; ++ layer_idx) {
                    unsupported[layer_idx] = adaptive_printable_pieces(
                        diff_ex(area, m_layers[layer_idx]->lslices), event.nozzle);
                    // Missing on this step, so it starts on the step above at the earliest.
                    if (span_height(layer_idx + 1, slab.top_idx) < event.range.min_height - EPSILON)
                        polygons_append(thin_faces, to_polygons(unsupported[layer_idx]));
                }
                const ExPolygons thin_area = union_ex(thin_faces);
                for (size_t layer_idx = live.bottom_idx; layer_idx < slab.top_idx; ++ layer_idx)
                    unsupported[layer_idx] = thin_area.empty() ? ExPolygons{} : adaptive_printable_pieces(
                        intersection_ex(unsupported[layer_idx], thin_area), event.nozzle);
            }
            for (size_t bottom_idx = live.bottom_idx; bottom_idx <= slab.top_idx && ! area.empty(); ++ bottom_idx) {
                const double height = span_height(bottom_idx, slab.top_idx);
                // A band that absorbed an orphan may exceed the preferred 0.7D ceiling. Keep the same 1.0D
                // ceiling used when it was accepted; otherwise it is turned into an empty patch here.
                if (height > event.nozzle + EPSILON)
                    continue;
                if (height < event.range.min_height - EPSILON) {
                    thin_bottom_idx = bottom_idx;
                    break;
                }

                Polygons blockers;
                for (size_t layer_idx = bottom_idx; layer_idx < slab.top_idx; ++ layer_idx) {
                    polygons_append(blockers, to_polygons(occupied[layer_idx]));
                    polygons_append(blockers, to_polygons(unsupported[layer_idx]));
                }
                ExPolygons clear = blockers.empty() ? area : diff_ex(area, union_ex(blockers));
                if (! assigned.empty())
                    clear = diff_ex(clear, assigned);
                if (clear.empty())
                    continue;

                AdaptiveMergePatch patch;
                patch.bottom_idx    = bottom_idx;
                patch.top_idx       = slab.top_idx;
                patch.merged_height = height;
                patch.set_area(std::move(clear));
                Polygons combined = to_polygons(assigned);
                polygons_append(combined, to_polygons(patch.area()));
                assigned = union_ex(combined);
                patches.emplace_back(std::move(patch));
            }

            // Steps the partition had to give up on are the second way a step loses its owner: a slab does
            // cover them, but everything underneath is occupied and the clear span left is below this
            // nozzle's minimum. Recorded for the same reason as a step no band could take - the alternative
            // is rediscovering it later from the raw slices, where it cannot be told from a fringe.
            const ExPolygons unassigned = diff_ex(area, assigned);
            if (thin_bottom_idx <= slab.top_idx && ! unassigned.empty()) {
                const bool floating = thin_bottom_idx > 0 && ! adaptive_printable_pieces(
                    diff_ex(unassigned, m_layers[thin_bottom_idx - 1]->lslices), event.nozzle).empty();
                pending_orphans.push_back({ region_id, event.range, event.nozzle,
                                            thin_bottom_idx, slab.top_idx, "too-thin", unassigned, floating });
                band_orphans.push_back({ thin_bottom_idx, slab.top_idx, unassigned });
            }

            BOOST_LOG_TRIVIAL(debug) << "[adaptive_merge] region=" << region_id
                                     << " band=[" << band.bottom_idx << "," << band.top_idx << "]"
                                     << " slab=[" << live.bottom_idx << "," << slab.top_idx << "]"
                                     << (slab.top_idx == live.top_idx ? " anchor" : " local_top")
                                     << " raw_area="
                                     << ::Slic3r::area(slab.area) * SCALING_FACTOR * SCALING_FACTOR
                                     << " after_occupied="
                                     << ::Slic3r::area(area) * SCALING_FACTOR * SCALING_FACTOR
                                     << " unassigned="
                                     << ::Slic3r::area(unassigned) * SCALING_FACTOR * SCALING_FACTOR
                                     << " thin_from=" << (thin_bottom_idx > slab.top_idx ?
                                                          std::string("none") : std::to_string(thin_bottom_idx))
                                     << (area.empty() ? " DROPPED" : " KEPT");
        }

        // The empty marker suppresses the whole region over the band. It can be left out only when every piece
        // of material the region holds inside the band belongs to one of its too-thin orphans: orphan resolution
        // then absorbs or suppresses all of it, while the marker would void the orphan's own fill (a bridge,
        // say) before the slab absorbing it could lift it. Anything else - material under another nozzle's
        // occupancy, a fringe no slab took - would otherwise fall back to the base layer height.
        bool orphans_own_band = ! band_orphans.empty();
        for (size_t layer_idx = live.bottom_idx; layer_idx <= live.top_idx && orphans_own_band; ++ layer_idx) {
            const ExPolygons &raw = m_layers[layer_idx]->get_region(region_id)->raw_slices;
            if (raw.empty())
                continue;
            Polygons owned;
            for (const BandOrphan &orphan : band_orphans)
                if (orphan.bottom_idx <= layer_idx && layer_idx <= orphan.top_idx)
                    polygons_append(owned, to_polygons(orphan.area));
            // Only numerical slivers between the two polygonisations may be left over.
            orphans_own_band = ! owned.empty() &&
                opening_ex(diff_ex(raw, union_ex(owned)), float(scale_(0.01))).empty();
        }
        if (patches.empty() && ! orphans_own_band) {
            AdaptiveMergePatch patch;
            patch.bottom_idx    = live.bottom_idx;
            patch.top_idx       = live.top_idx;
            patch.merged_height = live.merged_height;
            patch.set_area({});
            patches.emplace_back(std::move(patch));
        }

        BOOST_LOG_TRIVIAL(debug) << "[adaptive_merge] region=" << region_id
                                 << " band=[" << band.bottom_idx << "," << band.top_idx << "]"
                                 << " live=[" << live.bottom_idx << "," << live.top_idx << "]"
                                 << " top_raw_area=" << top_area
                                 << " slabs=" << slabs.size()
                                 << " patches=" << patches.size();

        for (AdaptiveMergePatch &patch : patches) {
            for (size_t layer_idx = patch.bottom_idx; layer_idx <= patch.top_idx; ++ layer_idx)
                occupy(layer_idx, patch.area());

            // Held back until orphan resolution below has had its say: it may still drop a slab's floor,
            // and the marker layers under a locally clipped patch have to grow with it.
            planned.push_back({ region_id, event.source_bottom_idx, std::move(patch) });
        }
    }

    // Resolve the runs recorded while planning, in the same order as a run is resolved during planning:
    // heights inside the preferred window come first, a whole slab before a cut one, and the relaxed 1.0D
    // ceiling is the last resort before giving the run away.
    //   1. the slab above lowers its floor over the run, within 0.7D;
    //   2. the slab below lifts its top over the run, within 0.7D;
    //   3. the run is re-cut with the slab below into two slabs inside the window;
    //   4. as 1. and 2., up to the relaxed ceiling.
    // A floating face skips all of them: the slab above keeps its floor and bridges over it.
    //
    // A run nobody can absorb keeps its empty group top. That marker is the established "nothing is
    // extruded here": it clears the perimeters and makes combine_adaptive_infill() void the fill, so the
    // step cannot fall back to a base-grid extrusion this nozzle is unable to lay down.
    enum class Recovery { FromAbove, FromBelow, Split };
    for (const PendingOrphan &orphan : pending_orphans) {
        m_print->throw_if_canceled();
        ExPolygons remaining = orphan.area;

        // Take the run back column by column into a slab of its region touching it. Only the column over the
        // lost footprint changes; the rest of that slab keeps its own span.
        //   FromAbove - the slab above lowers the column's floor to the run's bottom, keeping its group top.
        //   FromBelow - the slab below lifts the column's top to the run's top, keeping its floor.
        //   Split     - the column is re-cut with the slab below into two slabs inside the window, keeping
        //               the slab's floor and the run's top.
        // A slab resting on the bed belongs to the first band, whose height is fixed; it is never lifted or cut.
        auto recover = [&](Recovery mode, double ceiling) {
            const size_t planned_count = planned.size();
            for (size_t planned_idx = 0; planned_idx < planned_count && ! remaining.empty(); ++ planned_idx) {
                PlannedPatch &donor = planned[planned_idx];
                if (donor.region_id != orphan.region_id || donor.patch.area().empty())
                    continue;
                if (mode == Recovery::FromAbove ? donor.patch.bottom_idx != orphan.top_idx + 1 :
                        donor.patch.bottom_idx == 0 || donor.patch.top_idx + 1 != orphan.bottom_idx)
                    continue;
                size_t split_top_idx = 0;
                if (mode == Recovery::Split) {
                    if (! split_before_tail(donor.patch.bottom_idx, donor.patch.top_idx, orphan.top_idx,
                                            orphan.range, split_top_idx))
                        continue;
                } else if (mode == Recovery::FromAbove ?
                               span_height(orphan.bottom_idx, donor.patch.top_idx) > ceiling + EPSILON :
                               span_height(donor.patch.bottom_idx, orphan.top_idx) > ceiling + EPSILON)
                    continue;

                m_print->throw_if_canceled();
                ExPolygons recovered = intersection_ex(donor.patch.area(), remaining);
                // The column has to exist on every step from where a new group top starts: the run's bottom, or
                // the split when cut, where the lower slab now ends and shows its contour. Below that the donor
                // already holds it. Only the run is checked against other occupancy: the donor owns its own steps.
                // A nozzle cannot dip below an extrusion standing on the run, so a conflict only removes its own
                // part of the column.
                const size_t check_from = mode == Recovery::Split ? split_top_idx : orphan.bottom_idx;
                for (size_t idx = check_from; idx <= orphan.top_idx && ! recovered.empty(); ++ idx) {
                    recovered = intersection_ex(recovered, m_layers[idx]->get_region(orphan.region_id)->raw_slices);
                    if (idx >= orphan.bottom_idx && ! occupied[idx].empty())
                        recovered = diff_ex(recovered, occupied[idx]);
                }
                recovered = adaptive_printable_pieces(std::move(recovered), region_nozzles[orphan.region_id]);
                if (recovered.empty())
                    continue;

                std::vector<PlannedPatch> pieces;
                if (mode == Recovery::Split) {
                    PlannedPatch lower = donor;
                    lower.patch.top_idx       = split_top_idx;
                    lower.patch.merged_height = span_height(lower.patch.bottom_idx, split_top_idx);
                    lower.patch.set_area(recovered);
                    pieces.emplace_back(std::move(lower));
                }
                PlannedPatch column = donor;
                if (mode == Recovery::FromAbove)
                    column.patch.bottom_idx = orphan.bottom_idx;
                else {
                    if (mode == Recovery::Split) {
                        column.store_bottom_idx = split_top_idx + 1;
                        column.patch.bottom_idx = split_top_idx + 1;
                    }
                    column.patch.top_idx = orphan.top_idx;
                }
                column.patch.merged_height = span_height(column.patch.bottom_idx, column.patch.top_idx);
                column.patch.set_area(recovered);
                pieces.emplace_back(std::move(column));

                ExPolygons unchanged = diff_ex(donor.patch.area(), recovered);
                remaining = diff_ex(remaining, recovered);
                // Occupancy outside the run is unchanged; only the run is filled in.
                for (size_t idx = orphan.bottom_idx; idx <= orphan.top_idx; ++ idx)
                    occupy(idx, recovered);
                const AdaptiveMergePatch &taken = pieces.back().patch;
                if (mode == Recovery::Split)
                    BOOST_LOG_TRIVIAL(debug) << "[adaptive_merge] orphan region=" << orphan.region_id
                                             << " [" << orphan.bottom_idx << "," << orphan.top_idx << "]"
                                             << " " << orphan.source << " rebalanced from below, split=" << split_top_idx
                                             << " h=" << pieces.front().patch.merged_height << "+" << taken.merged_height;
                else
                    BOOST_LOG_TRIVIAL(debug) << "[adaptive_merge] orphan region=" << orphan.region_id
                                             << " [" << orphan.bottom_idx << "," << orphan.top_idx << "]"
                                             << " " << orphan.source
                                             << (mode == Recovery::FromAbove ? " absorbed from above into [" :
                                                                               " lifted from below into [")
                                             << taken.bottom_idx << "," << taken.top_idx << "] h=" << taken.merged_height
                                             << (taken.merged_height > orphan.range.max_height + EPSILON ?
                                                 " RELAXED_MAX" : "");
                // The donor itself is reused for the first piece, or keeps what the column left of it.
                // Do not use donor after appending: growing planned may invalidate the reference.
                size_t first_new = 0;
                if (unchanged.empty())
                    donor = std::move(pieces[first_new ++]);
                else
                    donor.patch.set_area(std::move(unchanged));
                for (size_t idx = first_new; idx < pieces.size(); ++ idx)
                    planned.emplace_back(std::move(pieces[idx]));
            }
            return remaining.empty();
        };

        if (! orphan.floating &&
            (recover(Recovery::FromAbove, orphan.range.max_height) ||
             recover(Recovery::FromBelow, orphan.range.max_height) ||
             recover(Recovery::Split, orphan.range.max_height) ||
             recover(Recovery::FromAbove, orphan.max_height) ||
             recover(Recovery::FromBelow, orphan.max_height)))
            continue;

        for (size_t idx = orphan.bottom_idx; idx <= orphan.top_idx; ++ idx) {
            AdaptiveMergePatch patch;
            patch.bottom_idx    = idx;
            patch.top_idx       = idx;
            patch.group_top     = true;
            patch.merged_height = layer_heights[idx];
            patch.set_area({});
            patch.set_suppressed_area(intersection_ex(
                remaining, m_layers[idx]->get_region(orphan.region_id)->raw_slices));
            m_layers[idx]->get_region(orphan.region_id)->adaptive_merge_patches.emplace_back(std::move(patch));
        }
        BOOST_LOG_TRIVIAL(debug) << "[adaptive_merge] orphan region=" << orphan.region_id
                                 << " [" << orphan.bottom_idx << "," << orphan.top_idx << "]"
                                 << " " << orphan.source << (orphan.floating ? " floating" : "") << " suppressed";
    }

    for (const PlannedPatch &planned_patch : planned) {
        // Orphan recovery may lower a patch after the initial partition. Build claims from the final span,
        // but only for patches whose source band and resolved floor are both truly anchored at layer 0.
        if (planned_patch.store_bottom_idx == 0 && planned_patch.patch.bottom_idx == 0 &&
            ! planned_patch.patch.area().empty())
            first_band_claims.push_back(
                { planned_patch.region_id, planned_patch.patch.top_idx, planned_patch.patch.area() });

        // Marker copies reach down to the band's planned floor as well as to the absorbed one: every step
        // the slab stands on is swallowed and must not fall back to a thin-layer toolpath.
        const size_t store_bottom = std::min(planned_patch.store_bottom_idx, planned_patch.patch.bottom_idx);
        for (size_t layer_idx = store_bottom; layer_idx <= planned_patch.patch.top_idx; ++ layer_idx) {
            AdaptiveMergePatch stored = planned_patch.patch;
            stored.group_top = layer_idx == planned_patch.patch.top_idx;
            m_layers[layer_idx]->get_region(planned_patch.region_id)
                ->adaptive_merge_patches.emplace_back(std::move(stored));
        }
    }

    // The layer-0 snapshot owns its XY footprint through its group top against fixed-grid regions. Merged
    // regions were already partitioned through patch.area(); trimming their source slices here would corrupt
    // the layer-0 contour from which their own first-band walls are generated.
    for (const FirstBandClaim &claim : first_band_claims)
        for (size_t layer_idx = 0; layer_idx <= claim.top_idx; ++ layer_idx)
            for (size_t region_id = 0; region_id < region_count; ++ region_id) {
                if (region_id == claim.region_id)
                    continue;
                LayerRegion *region = m_layers[layer_idx]->get_region(region_id);
                if (! region->adaptive_merge_patches.empty())
                    continue;
                Surfaces kept;
                for (const Surface &surface : region->slices.surfaces)
                    for (ExPolygon &expolygon : diff_ex(ExPolygons{ surface.expolygon }, claim.area))
                        kept.emplace_back(surface, std::move(expolygon));
                region->slices.set(std::move(kept));
            }
}

// Real overhangs of a merged span are already typed on the physical layers by detect_surfaces_type()
// and bridge_over_infill(). Only those surfaces are lifted to the group top, so nothing here can invent a
// bridge the unmerged slice did not have.
//
// Every layer of the span is voided by combine_adaptive_infill(), so the merged extrusion rests on the
// layer below the span, not on anything the span used to hold. Sparse fill inside the span is therefore no
// reason to drop a bridge - it is the very thing that gets taken away, which is why a shell that starts
// mid-band needs its internal bridge more than the unmerged slice did, not less.
static Surfaces collect_span_bridges(const LayerPtrs  &layers,
                                     size_t            region_id,
                                     size_t            bottom_idx,
                                     size_t            top_idx,
                                     const ExPolygons &merge_area)
{
    Surfaces out;
    Polygons claimed;

    auto claim = [&out, &claimed](const Surface &templ, ExPolygons &&area) {
        if (! claimed.empty())
            area = diff_ex(area, union_ex(claimed));
        for (ExPolygon &expolygon : area) {
            out.emplace_back(templ, std::move(expolygon));
            polygons_append(claimed, to_polygons(out.back().expolygon));
        }
    };

    for (size_t layer_idx = bottom_idx; layer_idx <= top_idx; ++ layer_idx)
        for (const Surface &surface : layers[layer_idx]->get_region(region_id)->fill_surfaces.surfaces)
            if (surface.is_bridge())
                claim(surface, intersection_ex(ExPolygons{ surface.expolygon }, merge_area));
    return out;
}

// Move the fill of a merge span onto its group-top layer. Fill using the wall nozzle follows the wall
// patches; fill using another nozzle diameter is planned independently with its own height window.
void PrintObject::combine_adaptive_infill()
{
    const PrintConfig &print_config = this->print()->config();

    if (! this->print()->uses_multiple_nozzle_diameters())
        return;

    const size_t region_count = this->num_printing_regions();
    const size_t layer_count = m_layers.size();

    std::vector<double> layer_heights;
    layer_heights.reserve(layer_count);
    for (const Layer *layer : m_layers)
        layer_heights.emplace_back(layer->height);

    // Measured against the same reference compute_adaptive_merge_groups() uses, so a fill band and a wall
    // band of one diameter always cover the same steps.
    const double min_nozzle = band_reference_nozzle(*this);
    if (min_nozzle <= EPSILON)
        return;

    // Fill follows the same fixed nozzle ladder as walls. Colour transitions do not split either grid.
    const std::vector<uint8_t> band_tops;

    // The merged extrusion of the group-top layer already fills this volume, so the fill of the layers below
    // must disappear. Surfaces are rebuilt one by one to keep the template (bridge angle, thickness) of the
    // parts that stay outside the area.
    auto void_fill = [this](LayerRegion *layerm, const ExPolygons &area) {
        if (area.empty())
            return;
        const BoundingBox area_bbox = get_extents(area);
        Surfaces   kept;
        ExPolygons voided;
        kept.reserve(layerm->fill_surfaces.surfaces.size());
        for (const Surface &surface : layerm->fill_surfaces.surfaces) {
            m_print->throw_if_canceled();
            // Splitting a void surface again leaves it void; it only multiplies the surfaces every later
            // call has to clip.
            const BoundingBox surface_bbox = get_extents(surface.expolygon);
            if (surface.surface_type == stInternalVoid || ! surface_bbox.overlap(area_bbox)) {
                kept.emplace_back(surface);
                continue;
            }
            // The area may cover a whole layer while a surface is small; clipping it to the surface's
            // bounding box first keeps the result and avoids feeding every point of it to each operation.
            const Polygons clip = ClipperUtils::clip_clipper_polygons_with_subject_bbox(
                area, surface_bbox.inflated(SCALED_EPSILON));
            if (clip.empty()) {
                kept.emplace_back(surface);
                continue;
            }
            for (ExPolygon &expolygon : diff_ex(ExPolygons{ surface.expolygon }, clip))
                kept.emplace_back(surface, std::move(expolygon));
            expolygons_append(voided, intersection_ex(ExPolygons{ surface.expolygon }, clip));
        }
        if (voided.empty())
            return;
        layerm->fill_surfaces.set(std::move(kept));
        layerm->fill_surfaces.append(union_ex(to_polygons(voided)), stInternalVoid);
    };
    // A layer whose whole region is swallowed extrudes nothing of it, so every fill surface turns void at
    // once. Fill and G-code export skip only stInternalVoid, and a merged-out layer must have no extrusions.
    auto void_whole_fill = [](LayerRegion *layerm) {
        if (layerm->fill_surfaces.empty())
            return;
        layerm->fill_surfaces.set(union_ex(to_polygons(layerm->fill_surfaces.surfaces)), stInternalVoid);
    };

    // The merged extrusion covers the whole Z span, so a solid requirement of any layer inside the span has
    // to be honored on the group top. Only the footprint that asks for solid is promoted: promoting the
    // complete merge area would turn every sparse column of the span into solid fill.
    auto span_solid_area = [](const Polygons &solid, const ExPolygons &merge_area) {
        return solid.empty() ? ExPolygons{} : intersection_ex(merge_area, union_ex(solid));
    };

    // Surface types were detected on the fine slicing grid, but a merged extrusion is exposed according
    // to the footprint of the next physical slab. A patch stored on a swallowed layer still describes
    // that slab because all copies across its span share the group-top geometry.
    const bool spiral_mode      = print_config.spiral_mode.value;
    const bool interface_shells = ! spiral_mode && m_config.interface_shells.value;
    std::unordered_map<size_t, ExPolygons> extrusion_footprint_cache;
    auto extrusion_footprint = [this, interface_shells, region_count, &extrusion_footprint_cache](
                                  size_t layer_idx, size_t region_id) -> const ExPolygons& {
        static const ExPolygons empty;
        if (layer_idx >= m_layers.size())
            return empty;

        const size_t cache_region = interface_shells ? region_id : 0;
        const size_t cache_key    = layer_idx * (interface_shells ? region_count : 1) + cache_region;
        if (auto cached = extrusion_footprint_cache.find(cache_key); cached != extrusion_footprint_cache.end())
            return cached->second;

        m_print->throw_if_canceled();
        Polygons footprint;
        const size_t first_region = interface_shells ? region_id : 0;
        const size_t last_region  = interface_shells ? region_id + 1 : region_count;
        for (size_t current_region = first_region; current_region < last_region; ++ current_region) {
            const LayerRegion *layerm = m_layers[layer_idx]->get_region(current_region);
            if (layerm->adaptive_merge_patches.empty()) {
                polygons_append(footprint, to_polygons(layerm->slices.surfaces));
            } else {
                for (const AdaptiveMergePatch &patch : layerm->adaptive_merge_patches)
                    polygons_append(footprint, to_polygons(patch.area()));
            }
        }
        return extrusion_footprint_cache.emplace(cache_key, union_ex(footprint)).first->second;
    };

    // deferred_void: when set, the swallowed-layer voids are collected there per layer instead of applied now.
    auto apply_merged_fill = [&void_fill, &span_solid_area, &extrusion_footprint, spiral_mode, this](
                                 size_t region_id, size_t bottom_idx, size_t top_idx, double merged_height,
                                 const ExPolygons &merge_area,
                                 bool includes_sparse, bool includes_solid, bool void_lower_layers,
                                 std::vector<Polygons> *deferred_void) {
        Polygons solid;
        if (includes_solid)
            for (size_t layer_idx = bottom_idx; layer_idx <= top_idx; ++layer_idx)
                for (const Surface &surface : m_layers[layer_idx]->get_region(region_id)->fill_surfaces.surfaces)
                    // Bridges are solids for fill flow, but they are promoted on their own by
                    // collect_span_bridges(); keep them out of the solid footprint.
                    if (surface.is_solid() && ! surface.is_bridge())
                        polygons_append(solid, to_polygons(surface.expolygon));
        const ExPolygons solid_area = span_solid_area(solid, merge_area);

        Surfaces bottom_bridges;
        if (includes_solid)
            bottom_bridges = collect_span_bridges(m_layers, region_id, bottom_idx, top_idx, merge_area);
        Polygons object_bottom;
        if (includes_solid && bottom_idx == 0)
            for (const Surface &surface : m_layers.front()->get_region(region_id)->fill_surfaces.surfaces)
                if (surface.is_bottom() && ! surface.is_bridge())
                    polygons_append(object_bottom, to_polygons(surface.expolygon));
        const ExPolygons object_bottom_area = object_bottom.empty() ?
            ExPolygons{} : intersection_ex(merge_area, union_ex(object_bottom));

        if (void_lower_layers)
            for (size_t layer_idx = bottom_idx; layer_idx < top_idx; ++layer_idx) {
                if (deferred_void)
                    polygons_append((*deferred_void)[layer_idx], to_polygons(merge_area));
                else
                    void_fill(m_layers[layer_idx]->get_region(region_id), merge_area);
            }

        LayerRegion *layerm = m_layers[top_idx]->get_region(region_id);
        // The fine layer above may be swallowed by a later coarse-nozzle slab. Compare against that
        // slab's actual footprint, otherwise an exposed group top keeps the stale internal-solid role.
        const PrintRegionConfig &region_config = layerm->region().config();
        const bool detect_top = spiral_mode || region_config.top_shell_layers.value > 0;
        ExPolygons top_area;
        if (includes_solid && detect_top) {
            top_area = opening_ex(
                diff_ex(solid_area, extrusion_footprint(top_idx + 1, region_id), ApplySafetyOffset::Yes),
                layerm->flow(frExternalPerimeter).scaled_width() / 10.f);
            // The group top keeps the top surfaces typed on the fine grid. process_external_surfaces() grew
            // them into the solid under the material above, so the top pattern reaches the walls there.
            // The slab footprint above only adds faces the fine grid could not see; it must not shrink them.
            Polygons top_polygons = to_polygons(top_area);
            for (const Surface &surface : layerm->fill_surfaces.surfaces)
                if (surface.surface_type == stTop)
                    polygons_append(top_polygons, to_polygons(surface.expolygon));
            top_area = union_ex(top_polygons);
            if (! object_bottom_area.empty())
                top_area = diff_ex(top_area, object_bottom_area);
        }

        Surfaces   kept;
        ExPolygons covered;
        const BoundingBox merge_bbox = get_extents(merge_area);
        kept.reserve(layerm->fill_surfaces.surfaces.size());
        for (const Surface &surface : layerm->fill_surfaces.surfaces) {
            m_print->throw_if_canceled();
            const BoundingBox surface_bbox = get_extents(surface.expolygon);
            const Polygons    clip         = surface_bbox.overlap(merge_bbox) ?
                ClipperUtils::clip_clipper_polygons_with_subject_bbox(merge_area, surface_bbox.inflated(SCALED_EPSILON)) :
                Polygons{};
            if (clip.empty()) {
                kept.emplace_back(surface);
                continue;
            }
            for (ExPolygon &expolygon : diff_ex(ExPolygons{ surface.expolygon }, clip))
                kept.emplace_back(surface, std::move(expolygon));
            expolygons_append(covered, intersection_ex(ExPolygons{ surface.expolygon }, clip));
        }
        if (covered.empty()) {
            if (bottom_idx != 0)
                return;
            // The first-band wall may consume the entire narrow slice. Fall back to the cavity left by
            // perimeter generation, not the full slice, or infill will overlap the merged wall.
            covered = intersection_ex(merge_area, layerm->fill_expolygons);
            if (covered.empty())
                return;
        } else {
            covered = union_ex(to_polygons(covered));
        }

        // A merged extrusion is supported below the group bottom. Preserve bridge surfaces from that
        // layer on the group top; otherwise the retained contours inside the span hide the void below.
        // The visible face is the exception: an internal bridge exists only to carry the solid shell
        // above it, and the group top is that shell. Merging both into one extrusion is the same as
        // slicing the shell at the merged height, which is a plain top surface over sparse infill.
        Surfaces promoted_bridges;
        Polygons bridge_claimed;
        for (const Surface &surface : bottom_bridges) {
            ExPolygons bridge_area = intersection_ex(ExPolygons{ surface.expolygon }, covered);
            if (! top_area.empty())
                bridge_area = diff_ex(bridge_area, top_area);
            if (! bridge_claimed.empty())
                bridge_area = diff_ex(bridge_area, union_ex(bridge_claimed));
            for (ExPolygon &expolygon : bridge_area) {
                promoted_bridges.emplace_back(surface, std::move(expolygon));
                Surface &promoted = promoted_bridges.back();
                promoted.thickness = merged_height;
                promoted.thickness_layers = (unsigned short)(top_idx - bottom_idx + 1);
                polygons_append(bridge_claimed, to_polygons(promoted.expolygon));
            }
        }
        ExPolygons remaining = diff_ex(covered, union_ex(bridge_claimed));

        // A first band has one extrusion representing the complete slab. Bottom contact wins where its
        // top and bottom faces overlap, matching detect_surfaces_type().
        ExPolygons top_fill = intersection_ex(remaining, top_area);
        ExPolygons bottom_fill = diff_ex(intersection_ex(remaining, object_bottom_area), top_fill);
        Polygons visible_fill = to_polygons(bottom_fill);
        polygons_append(visible_fill, to_polygons(top_fill));
        ExPolygons solid_fill = diff_ex(intersection_ex(remaining, solid_area), union_ex(visible_fill));
        Polygons claimed = bridge_claimed;
        polygons_append(claimed, to_polygons(bottom_fill));
        polygons_append(claimed, to_polygons(top_fill));
        polygons_append(claimed, to_polygons(solid_fill));
        ExPolygons sparse_fill = includes_sparse ? diff_ex(covered, union_ex(claimed)) : ExPolygons{};

        Surface templ(stInternal, ExPolygon());
        templ.thickness = merged_height;
        templ.thickness_layers = (unsigned short)(top_idx - bottom_idx + 1);

        layerm->fill_surfaces.set(std::move(kept));
        layerm->fill_surfaces.append(std::move(promoted_bridges));
        if (! bottom_fill.empty()) {
            templ.surface_type = stBottom;
            layerm->fill_surfaces.append(std::move(bottom_fill), templ);
        }
        if (! top_fill.empty()) {
            templ.surface_type = stTop;
            layerm->fill_surfaces.append(std::move(top_fill), templ);
        }
        if (! solid_fill.empty()) {
            templ.surface_type = stInternalSolid;
            layerm->fill_surfaces.append(std::move(solid_fill), templ);
        }
        if (! sparse_fill.empty()) {
            templ.surface_type = stInternal;
            layerm->fill_surfaces.append(std::move(sparse_fill), templ);
        }
    };

    for (size_t region_id = 0; region_id < region_count; ++region_id) {
        m_print->throw_if_canceled();
        const PrintRegionConfig &config = this->printing_region(region_id).config();
        const unsigned int wall_filament   = (unsigned int) config.wall_filament.value;
        const unsigned int sparse_filament = (unsigned int) config.sparse_infill_filament.value;
        const unsigned int solid_filament  = (unsigned int) config.solid_infill_filament.value;

        const double wall_nozzle   = nozzle_diameter_for_filament(print_config, wall_filament);
        const double sparse_nozzle = nozzle_diameter_for_filament(print_config, sparse_filament);
        const double solid_nozzle  = nozzle_diameter_for_filament(print_config, solid_filament);
        // Explicit infill combination owns the sparse-infill Z planning. Leave sparse surfaces on the
        // base-layer grid here so combine_infill() can merge them up to their own nozzle diameter.
        const bool sparse_follows_adaptive_bands = ! config.infill_combination.value;
        const bool sparse_uses_wall_nozzle = sparse_follows_adaptive_bands &&
                                             std::abs(sparse_nozzle - wall_nozzle) <= EPSILON;
        const bool solid_uses_wall_nozzle  = std::abs(solid_nozzle - wall_nozzle) <= EPSILON;

        // Reuse each wall group-top patch as the XY/Z range for fill roles that share the wall nozzle.
        // inherit_wall_column: sparse and solid both use that nozzle, so lower layers inherit the whole
        // wall column (they may already be void after the wall merge) and leftover fill inside the column
        // is voided so it cannot overlap the thick wall. Otherwise only the matching roles are collected.
        auto merge_fill_along_wall_patches = [&](bool includes_sparse, bool includes_solid, bool inherit_wall_column) {
            for (size_t layer_idx = 0; layer_idx < layer_count; ++layer_idx) {
                for (const AdaptiveMergePatch &wall_patch :
                     m_layers[layer_idx]->get_region(region_id)->adaptive_merge_patches) {
                    if (! wall_patch.group_top)
                        continue;
                    m_print->throw_if_canceled();

                    if (wall_patch.area().empty()) {
                        // A local orphan suppresses only the XY footprint that failed planning. Legacy empty
                        // markers without such a footprint still mean that the complete region is suppressed.
                        const ExPolygons &suppressed_patch_area = wall_patch.suppressed_area();
                        const bool        has_suppressed_area   = wall_patch.has_suppressed_area();
                        // An explicitly empty local footprint means this orphan has no geometry on the
                        // current layer. It is not the legacy whole-region marker represented by "unset".
                        if (has_suppressed_area && suppressed_patch_area.empty())
                            continue;
                        for (size_t idx = wall_patch.bottom_idx; idx <= wall_patch.top_idx; ++ idx) {
                            LayerRegion *suppressed = m_layers[idx]->get_region(region_id);
                            if (inherit_wall_column) {
                                if (has_suppressed_area)
                                    void_fill(suppressed, suppressed_patch_area);
                                else
                                    void_whole_fill(suppressed);
                                continue;
                            }
                            Polygons same_nozzle_fill;
                            for (const Surface &surface : suppressed->fill_surfaces.surfaces) {
                                const bool sparse_surface = surface.surface_type == stInternal;
                                if ((includes_sparse && sparse_surface) || (includes_solid && surface.is_solid()))
                                    polygons_append(same_nozzle_fill, to_polygons(surface.expolygon));
                            }
                            if (! same_nozzle_fill.empty()) {
                                ExPolygons area = union_ex(same_nozzle_fill);
                                if (has_suppressed_area)
                                    area = intersection_ex(area, suppressed_patch_area);
                                void_fill(suppressed, area);
                            }
                        }
                        continue;
                    }

                    // The fill follows the wall band, it is not planned again: the wall is extruded on the
                    // group top and nowhere else, so its fill has to land on exactly that layer. Like the
                    // wall, the first band keeps the true first layer; later bands keep their top layer.
                    const LayerRegion *layerm =
                        m_layers[wall_patch.bottom_idx == 0 ? 0 : wall_patch.top_idx]->get_region(region_id);
                    ExPolygons         fill_area;
                    if (inherit_wall_column) {
                        // Both fill roles use the wall nozzle, so the fill inherits the whole wall column.
                        Polygons blocked;
                        for (const Surface &surface : layerm->fill_surfaces.surfaces)
                            if ((surface.is_bottom() && wall_patch.bottom_idx != 0) ||
                                surface.surface_type == stInternalVoid)
                                polygons_append(blocked, to_polygons(surface.expolygon));
                        fill_area = diff_ex(wall_patch.area(), union_ex(blocked));
                    } else {
                        Polygons eligible;
                        for (const Surface &surface : layerm->fill_surfaces.surfaces) {
                            if ((surface.is_bottom() && wall_patch.bottom_idx != 0) ||
                                surface.surface_type == stInternalVoid)
                                continue;
                            const bool sparse_surface = surface.surface_type == stInternal;
                            if ((includes_sparse && sparse_surface) || (includes_solid && surface.is_solid()))
                                polygons_append(eligible, to_polygons(surface.expolygon));
                        }
                        fill_area = intersection_ex(union_ex(eligible), wall_patch.area());
                    }
                    // With the whole column inherited, the swallowed-layer pass below voids at least the
                    // patch area on every lower layer, which covers fill_area; voiding it here is redundant.
                    if (! fill_area.empty())
                        apply_merged_fill(region_id, wall_patch.bottom_idx, wall_patch.top_idx,
                                          wall_patch.merged_height, fill_area, includes_sparse, includes_solid,
                                          ! inherit_wall_column, nullptr);
                    for (size_t idx = wall_patch.bottom_idx; idx < wall_patch.top_idx; ++idx) {
                        LayerRegion *swallowed = m_layers[idx]->get_region(region_id);
                        // A shape that closes inside the band - the top face of a step, say - has no
                        // counterpart on the group top, so the patch area never reaches it and it is
                        // left behind alone on its own layer, extruded at the base layer height. This
                        // swallowed layer has no walls to go with it: Layer::make_perimeters() drops
                        // those for the whole region, not just inside the patch. Drop every fill role
                        // sharing that nozzle across the region while preserving roles assigned to
                        // another nozzle.
                        //
                        // Local Z repair may grow one slab down over the group top of the band below it.
                        // That layer does extrude its own walls, so only the footprint this slab covers is
                        // swallowed there - clearing the whole region would delete the fill that group top
                        // owns, leaving walls around an empty layer.
                        const bool extrudes_on_its_own = std::any_of(
                            swallowed->adaptive_merge_patches.begin(), swallowed->adaptive_merge_patches.end(),
                            [](const AdaptiveMergePatch &patch) {
                                return patch.group_top && ! patch.area().empty();
                            });
                        if (inherit_wall_column) {
                            if (extrudes_on_its_own)
                                void_fill(swallowed, wall_patch.area());
                            else
                                void_whole_fill(swallowed);
                        } else {
                            Polygons same_nozzle_fill;
                            for (const Surface &surface : swallowed->fill_surfaces.surfaces) {
                                const bool sparse_surface = surface.surface_type == stInternal;
                                if ((includes_sparse && sparse_surface) || (includes_solid && surface.is_solid()))
                                    polygons_append(same_nozzle_fill, to_polygons(surface.expolygon));
                            }
                            if (! same_nozzle_fill.empty()) {
                                ExPolygons area = union_ex(same_nozzle_fill);
                                if (extrudes_on_its_own)
                                    area = intersection_ex(area, wall_patch.area());
                                if (! area.empty())
                                    void_fill(swallowed, area);
                            }
                        }
                    }
                }
            }
        };

        // Three mutually exclusive cases when sparse fill follows adaptive bands:
        //   both roles share the wall nozzle  -> one pass along wall patches, inherit the whole column
        //   exactly one role shares it        -> that role follows wall patches; the other is planned below
        //   neither shares it                 -> both roles are planned independently
        // With explicit infill combination, sparse fill is excluded from all three and handled later.
        if (sparse_uses_wall_nozzle && solid_uses_wall_nozzle) {
            merge_fill_along_wall_patches(true, true, true);
        } else {
            if (sparse_uses_wall_nozzle || solid_uses_wall_nozzle)
                merge_fill_along_wall_patches(sparse_uses_wall_nozzle, solid_uses_wall_nozzle, false);

            struct FillGroup {
                unsigned int filament;
                bool         sparse;
                bool         solid;
            };
            std::vector<FillGroup> fill_groups;
            if (sparse_follows_adaptive_bands && ! sparse_uses_wall_nozzle)
                fill_groups.push_back({ sparse_filament, true, false });
            if (! solid_uses_wall_nozzle) {
                auto group = std::find_if(fill_groups.begin(), fill_groups.end(),
                    [solid_filament](const FillGroup &candidate) { return candidate.filament == solid_filament; });
                if (group == fill_groups.end())
                    fill_groups.push_back({ solid_filament, false, true });
                else
                    group->solid = true;
            }

            // Fill using a nozzle diameter different from the wall gets its own band grid, anchored like
            // every other one. Sparse and solid surfaces share a grid only when they use the same filament;
            // otherwise each tool is decimated on its own.
            for (const FillGroup &group : fill_groups) {
                const double nozzle = nozzle_diameter_for_filament(print_config, group.filament);
                const AdaptiveLayerHeightRange       range     = adaptive_layer_height_range(nozzle);
                const size_t                         band_size = adaptive_layer_band_size(nozzle, min_nozzle, m_slicing_params.layer_height);

                std::vector<ExPolygons> mergeable(layer_count);
                for (size_t layer_idx = 0; layer_idx < layer_count; ++layer_idx) {
                    Polygons eligible;
                    for (const Surface &surface : m_layers[layer_idx]->get_region(region_id)->fill_surfaces.surfaces) {
                        if ((surface.is_bottom() && layer_idx != 0) ||
                            surface.surface_type == stInternalVoid)
                            continue;
                        const bool sparse_surface = surface.surface_type == stInternal;
                        if ((group.sparse && sparse_surface) || (group.solid && surface.is_solid()))
                            polygons_append(eligible, to_polygons(surface.expolygon));
                    }
                    mergeable[layer_idx] = union_ex(eligible);
                }

                const std::vector<AdaptiveLayerBand> bands =
                    build_adaptive_layer_bands(layer_heights, band_size, range, band_tops);
                if (bands.empty())
                    continue;

                // Per-XY bookkeeping drives the residual repair below: it has to know which footprint of a
                // layer is already spoken for.
                std::vector<ExPolygons> carried(layer_count);
                auto mark_carried = [&carried](size_t layer_idx, const ExPolygons &area) {
                    if (area.empty())
                        return;
                    Polygons combined = to_polygons(carried[layer_idx]);
                    polygons_append(combined, to_polygons(area));
                    carried[layer_idx] = union_ex(combined);
                };
                struct PlannedFillPatch {
                    size_t     bottom_idx;
                    size_t     top_idx;
                    double     merged_height;
                    ExPolygons area;
                };
                std::vector<PlannedFillPatch> planned_fill;
                for (const AdaptiveLayerBand &band : bands) {
                    const bool first_band = band.bottom_idx == 0;
                    if (first_band && mergeable.front().empty()) {
                        for (size_t layer_idx = band.bottom_idx; layer_idx <= band.top_idx; ++ layer_idx) {
                            LayerRegion *suppressed = m_layers[layer_idx]->get_region(region_id);
                            Polygons same_nozzle_fill;
                            for (const Surface &surface : suppressed->fill_surfaces.surfaces) {
                                const bool sparse_surface = surface.surface_type == stInternal;
                                if ((group.sparse && sparse_surface) || (group.solid && surface.is_solid()))
                                    polygons_append(same_nozzle_fill, to_polygons(surface.expolygon));
                            }
                            if (! same_nozzle_fill.empty()) {
                                ExPolygons suppressed_area = union_ex(same_nozzle_fill);
                                void_fill(suppressed, suppressed_area);
                                mark_carried(layer_idx, suppressed_area);
                            }
                        }
                        continue;
                    }
                    AdaptiveLayerBand live = band;
                    std::vector<unsigned char> has_material(band.top_idx + 1, 0);
                    for (size_t layer_idx = band.bottom_idx; layer_idx <= band.top_idx; ++ layer_idx)
                        has_material[layer_idx] = ! mergeable[layer_idx].empty();
                    if (! first_band && ! pull_adaptive_band_top_to_material(live, layer_heights, range, has_material))
                        continue;
                    ExPolygons area = mergeable[first_band ? 0 : live.top_idx];
                    if (area.empty())
                        continue;
                    // The wall of this region is still extruded on every layer of the band, so a bottom
                    // shell inside it is a real floor and must survive. Voiding it would drop the surface
                    // the merged fill is laid onto.
                    Polygons blocked;
                    for (size_t layer_idx = live.bottom_idx; layer_idx < live.top_idx; ++ layer_idx)
                        for (const Surface &surface : m_layers[layer_idx]->get_region(region_id)->fill_surfaces.surfaces)
                            if (surface.is_bottom() && live.bottom_idx != 0)
                                polygons_append(blocked, to_polygons(surface.expolygon));
                    if (! blocked.empty())
                        area = diff_ex(area, union_ex(blocked));
                    if (area.empty())
                        continue;

                    for (size_t layer_idx = live.bottom_idx; layer_idx <= live.top_idx; ++ layer_idx)
                        mark_carried(layer_idx, area);
                    planned_fill.push_back(
                        { live.bottom_idx, live.top_idx, live.merged_height, std::move(area) });
                }

                std::vector<ExPolygons> remainder(layer_count);
                for (size_t layer_idx = 0; layer_idx < layer_count; ++ layer_idx) {
                    if (mergeable[layer_idx].empty())
                        continue;
                    remainder[layer_idx] = carried[layer_idx].empty() ?
                        mergeable[layer_idx] : diff_ex(mergeable[layer_idx], carried[layer_idx]);
                }

                // Residuals first form their own shortest band inside the preferred 0.2D..0.7D window.
                // Only what is still too thin after this pass may use the relaxed neighboring-band repair.
                for (size_t top_idx = layer_count; top_idx-- > 0;) {
                    while (! remainder[top_idx].empty()) {
                        ExPolygons common = remainder[top_idx];
                        double     height = 0.;
                        size_t     bottom_idx = top_idx;
                        bool       found = false;
                        for (size_t candidate = top_idx + 1; candidate-- > 0;) {
                            height += layer_heights[candidate];
                            if (height > range.max_height + EPSILON)
                                break;
                            if (candidate != top_idx)
                                common = intersection_ex(common, remainder[candidate]);
                            if (common.empty())
                                break;
                            if (range.contains(height)) {
                                bottom_idx = candidate;
                                found = true;
                                break;
                            }
                        }
                        if (! found)
                            break;
                        // Slivers left by Clipper rounding stay in the remainder and are voided below.
                        common = adaptive_printable_pieces(std::move(common), nozzle);
                        if (common.empty())
                            break;

                        // Commit only after diff_ex() really removed the patch. A patch left in the remainder
                        // would be voided again below, wiping the merged fill just written on its group top.
                        // Without progress nothing is committed: the remainder stays as is for the repair
                        // passes, and whatever they cannot take is voided at the end.
                        std::vector<ExPolygons> shrunk;
                        shrunk.reserve(top_idx - bottom_idx + 1);
                        for (size_t layer_idx = bottom_idx; layer_idx <= top_idx; ++ layer_idx)
                            shrunk.emplace_back(diff_ex(remainder[layer_idx], common));
                        if (area(remainder[top_idx]) - area(shrunk.back()) < 0.9 * area(common))
                            break;

                        for (size_t layer_idx = bottom_idx; layer_idx <= top_idx; ++ layer_idx) {
                            mark_carried(layer_idx, common);
                            remainder[layer_idx] = std::move(shrunk[layer_idx - bottom_idx]);
                        }
                        planned_fill.push_back({ bottom_idx, top_idx, height, std::move(common) });
                        BOOST_LOG_TRIVIAL(debug) << "[adaptive_merge] fill remainder region=" << region_id
                                                 << " band=[" << bottom_idx << "," << top_idx << "]"
                                                 << " h=" << height;
                    }
                }

                // Residual-only relaxation: first give the remaining footprint to the slab directly below
                // it. This keeps the new group top on a layer where the residual geometry really exists.
                // Prefer the normal <= 0.7D plan above; only this repair may grow as high as 1.0D.
                for (size_t layer_idx = 0; layer_idx < layer_count; ++ layer_idx) {
                    if (remainder[layer_idx].empty())
                        continue;
                    std::vector<PlannedFillPatch> grown_fill;
                    const size_t planned_count = planned_fill.size();
                    for (size_t planned_idx = 0; planned_idx < planned_count; ++ planned_idx) {
                        if (remainder[layer_idx].empty())
                            break;
                        PlannedFillPatch &patch = planned_fill[planned_idx];
                        if (layer_idx == 0 || patch.top_idx != layer_idx - 1 || patch.area.empty())
                            continue;
                        const double height = patch.merged_height + layer_heights[layer_idx];
                        if (height < range.min_height - EPSILON || height > nozzle + EPSILON)
                            continue;

                        ExPolygons extended_area = intersection_ex(patch.area, remainder[layer_idx]);
                        if (extended_area.empty())
                            continue;
                        ExPolygons remaining_area = diff_ex(patch.area, extended_area);
                        const bool split = ! remaining_area.empty();
                        if (! split) {
                            patch.top_idx       = layer_idx;
                            patch.merged_height = height;
                        } else {
                            grown_fill.push_back(
                                { patch.bottom_idx, layer_idx, height, std::move(extended_area) });
                            patch.area = std::move(remaining_area);
                        }
                        const ExPolygons &claimed_area =
                            split ? grown_fill.back().area : patch.area;
                        mark_carried(layer_idx, claimed_area);
                        remainder[layer_idx] = diff_ex(remainder[layer_idx], claimed_area);
                        BOOST_LOG_TRIVIAL(debug) << "[adaptive_merge] fill repair-up region=" << region_id
                                                 << " patch=[" << patch.bottom_idx << "," << layer_idx << "]"
                                                 << " h=" << height
                                                 << (split ? " split" : " whole")
                                                 << (height > range.max_height + EPSILON ?
                                                     " RELAXED_MAX" : "");
                    }
                    for (PlannedFillPatch &grown : grown_fill)
                        planned_fill.emplace_back(std::move(grown));
                }

                // Anything not accepted from below gets one more chance to extend the slab directly above
                // it downwards, under the same residual-only 1.0D ceiling.
                for (size_t layer_idx = layer_count; layer_idx-- > 0;) {
                    if (remainder[layer_idx].empty())
                        continue;
                    std::vector<PlannedFillPatch> grown_fill;
                    const size_t planned_count = planned_fill.size();
                    for (size_t planned_idx = 0; planned_idx < planned_count; ++ planned_idx) {
                        if (remainder[layer_idx].empty())
                            break;
                        PlannedFillPatch &patch = planned_fill[planned_idx];
                        if (patch.bottom_idx != layer_idx + 1 || patch.area.empty())
                            continue;
                        const double height = patch.merged_height + layer_heights[layer_idx];
                        if (height < range.min_height - EPSILON || height > nozzle + EPSILON)
                            continue;

                        ExPolygons extended_area = intersection_ex(patch.area, remainder[layer_idx]);
                        if (extended_area.empty())
                            continue;
                        ExPolygons remaining_area = diff_ex(patch.area, extended_area);
                        const bool split = ! remaining_area.empty();
                        if (! split) {
                            patch.bottom_idx    = layer_idx;
                            patch.merged_height = height;
                        } else {
                            grown_fill.push_back(
                                { layer_idx, patch.top_idx, height, std::move(extended_area) });
                            patch.area = std::move(remaining_area);
                        }
                        const ExPolygons &claimed_area =
                            split ? grown_fill.back().area : patch.area;
                        mark_carried(layer_idx, claimed_area);
                        remainder[layer_idx] = diff_ex(remainder[layer_idx], claimed_area);
                        BOOST_LOG_TRIVIAL(debug) << "[adaptive_merge] fill repair region=" << region_id
                                                 << " patch=[" << layer_idx << "," << patch.top_idx << "]"
                                                 << " h=" << height
                                                 << (split ? " split" : " whole")
                                                 << (height > range.max_height + EPSILON ?
                                                     " RELAXED_MAX" : "");
                    }
                    for (PlannedFillPatch &grown : grown_fill)
                        planned_fill.emplace_back(std::move(grown));
                }

                // Planned patches never overlap on a layer (see carried above), and apply_merged_fill() only
                // reads a layer inside its own merge area, so the swallowed-layer voids of every patch can be
                // collected and applied once per layer instead of once per patch.
                std::vector<Polygons> deferred_void(layer_count);
                for (const PlannedFillPatch &patch : planned_fill)
                    apply_merged_fill(region_id, patch.bottom_idx, patch.top_idx, patch.merged_height,
                                      patch.area, group.sparse, group.solid, true, &deferred_void);

                // A footprint that exists for less than this nozzle's minimum printable height cannot be
                // assigned to any neighboring band. This branch plans fill whose nozzle differs from the
                // wall nozzle, so another XY patch touching the layer does not make this remainder legal:
                // leaving it behind would extrude it at the base layer height.
                for (size_t layer_idx = 0; layer_idx < layer_count; ++ layer_idx) {
                    if (! remainder[layer_idx].empty())
                        polygons_append(deferred_void[layer_idx], to_polygons(remainder[layer_idx]));
                    if (! deferred_void[layer_idx].empty())
                        void_fill(m_layers[layer_idx]->get_region(region_id), union_ex(deferred_void[layer_idx]));
                }
            }
        }
    }
}

// combine fill surfaces across layers to honor the "infill every N layers" option
// Idempotence of this method is guaranteed by the fact that we don't remove things from
// fill_surfaces but we only turn them into VOID surfaces, thus preserving the boundaries.
void PrintObject::combine_infill()
{
    // Work on each region separately.
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        const PrintRegion &region = this->printing_region(region_id);
        //BBS
        const bool enable_combine_infill = region.config().infill_combination.value;
        if (enable_combine_infill == false || region.config().sparse_infill_density == 0.)
            continue;
        // Limit the number of combined layers to the sparse-infill nozzle diameter. Solid fill is not
        // combined here and must not constrain the sparse-infill band size.
        //FIXME limit the layer height to max_layer_height
        const double nozzle_diameter =
            nozzle_diameter_for_filament(this->print()->config(), region.config().sparse_infill_filament.value);
        // define the combinations
        std::vector<size_t> combine(m_layers.size(), 0);
        {
            double current_height = 0.;
            size_t num_layers = 0;
            for (size_t layer_idx = 0; layer_idx < m_layers.size(); ++ layer_idx) {
                m_print->throw_if_canceled();
                const Layer *layer = m_layers[layer_idx];
                if (layer->id() == 0)
                    // Skip first print layer (which may not be first layer in array because of raft).
                    continue;
                // Check whether the combination of this layer with the lower layers' buffer
                // would exceed max layer height or max combined layer count.
                // BBS: automatically calculate how many layers should be combined
                if (current_height + layer->height > nozzle_diameter + EPSILON) {
                    // Append combination to lower layer.
                    combine[layer_idx - 1] = num_layers;
                    current_height = 0.;
                    num_layers = 0;
                }
                current_height += layer->height;
                ++ num_layers;
            }

            // Append lower layers (if any) to uppermost layer.
            combine[m_layers.size() - 1] = num_layers;
        }

        // loop through layers to which we have assigned layers to combine
        for (size_t layer_idx = 0; layer_idx < m_layers.size(); ++ layer_idx) {
            m_print->throw_if_canceled();
            size_t num_layers = combine[layer_idx];
			if (num_layers <= 1)
                continue;
            // Get all the LayerRegion objects to be combined.
            std::vector<LayerRegion*> layerms;
            layerms.reserve(num_layers);
			for (size_t i = layer_idx + 1 - num_layers; i <= layer_idx; ++ i)
                layerms.emplace_back(m_layers[i]->regions()[region_id]);
            // We need to perform a multi-layer intersection, so let's split it in pairs.
            // Initialize the intersection with the candidates of the lowest layer.
            ExPolygons intersection = to_expolygons(layerms.front()->fill_surfaces.filter_by_type(stInternal));
            // Start looping from the second layer and intersect the current intersection with it.
            for (size_t i = 1; i < layerms.size(); ++ i)
                intersection = intersection_ex(layerms[i]->fill_surfaces.filter_by_type(stInternal), intersection);
            double area_threshold = layerms.front()->infill_area_threshold();
            if (! intersection.empty() && area_threshold > 0.)
                intersection.erase(std::remove_if(intersection.begin(), intersection.end(),
                    [area_threshold](const ExPolygon &expoly) { return expoly.area() <= area_threshold; }),
                    intersection.end());
            if (intersection.empty())
                continue;
//            Slic3r::debugf "  combining %d %s regions from layers %d-%d\n",
//                scalar(@$intersection),
//                ($type == stInternal ? 'internal' : 'internal-solid'),
//                $layer_idx-($every-1), $layer_idx;
            // intersection now contains the regions that can be combined across the full amount of layers,
            // so let's remove those areas from all layers.
            Polygons intersection_with_clearance;
            intersection_with_clearance.reserve(intersection.size());
            float clearance_offset =
                0.5f * layerms.back()->flow(frPerimeter).scaled_width() +
             // Because fill areas for rectilinear and honeycomb are grown
             // later to overlap perimeters, we need to counteract that too.
                ((region.config().sparse_infill_pattern == ipRectilinear   ||
                  region.config().sparse_infill_pattern == ipMonotonic     ||
                  region.config().sparse_infill_pattern == ipGrid          ||
                  region.config().sparse_infill_pattern == ip2DLattice     ||
                  region.config().sparse_infill_pattern == ipLine          ||
                  region.config().sparse_infill_pattern == ipHoneycomb) ? 1.5f : 0.5f) *
                    layerms.back()->flow(frSolidInfill).scaled_width();
            for (ExPolygon &expoly : intersection)
                polygons_append(intersection_with_clearance, offset(expoly, clearance_offset));
            for (LayerRegion *layerm : layerms) {
                Polygons internal = to_polygons(std::move(layerm->fill_surfaces.filter_by_type(stInternal)));
                layerm->fill_surfaces.remove_type(stInternal);
                layerm->fill_surfaces.append(diff_ex(internal, intersection_with_clearance), stInternal);
                if (layerm == layerms.back()) {
                    // Apply surfaces back with adjusted depth to the uppermost layer.
                    Surface templ(stInternal, ExPolygon());
                    templ.thickness = 0.;
                    for (LayerRegion *layerm2 : layerms)
                        templ.thickness += layerm2->layer()->height;
                    templ.thickness_layers = (unsigned short)layerms.size();
                    layerm->fill_surfaces.append(intersection, templ);
                } else {
                    // Save void surfaces.
                    layerm->fill_surfaces.append(
                        intersection_ex(internal, intersection_with_clearance),
                        stInternalVoid);
                }
            }
        }
    }
}

// A top island that an erosion of this radius wipes out is too narrow to justify a sub-top band
// underneath it. Eroding rather than measuring the area also rejects long thin slivers, whose area
// can be large while no part of them is wide enough to matter.
static constexpr double SUB_TOP_MIN_TOP_EROSION_MM = 1.5;

void PrintObject::discover_sub_top_surfaces()
{
    BOOST_LOG_TRIVIAL(trace);
    if (m_layers.size() < 2)
        return;

    auto process_layer = [this](size_t idx_layer) {
        m_print->throw_if_canceled();
        const Layer *upper = m_layers[idx_layer + 1];
        Layer       *layer = m_layers[idx_layer];

        ExPolygons top_mask;
        for (const LayerRegion *upper_region : upper->regions())
            for (const Surface &s : upper_region->fill_surfaces.surfaces)
                if (s.surface_type == stTop)
                    top_mask.emplace_back(s.expolygon);
        if (top_mask.empty())
            return;

        const auto    erosion    = float(scale_(SUB_TOP_MIN_TOP_EROSION_MM));
        const coord_t min_extent = coord_t(scale_(2. * SUB_TOP_MIN_TOP_EROSION_MM));
        ExPolygons    large_tops;
        for (ExPolygon &top : union_ex(top_mask)) {
            // An island narrower than the erosion diameter cannot survive it, and its extents are a
            // linear scan against a boolean, so check them before paying for the offset.
            const Point extents = get_extents(top).size();
            if (extents.x() < min_extent || extents.y() < min_extent)
                continue;
            if (! offset_ex(top, - erosion).empty())
                large_tops.emplace_back(std::move(top));
        }
        if (large_tops.empty())
            return;
        top_mask = std::move(large_tops);

        // The vertex cull below runs once per island of every region, and it walks the points of
        // every mask polygon. Caching the extents here lets a polygon whose box misses the island
        // be dropped without touching its points at all.
        std::vector<BoundingBox> top_extents;
        top_extents.reserve(top_mask.size());
        for (const ExPolygon &top : top_mask)
            top_extents.emplace_back(get_extents(top));

        for (LayerRegion *layerm : layer->m_regions) {
            const float min_width = float(layerm->flow(frSolidInfill).scaled_spacing());
            // A claimed island keeps its geometry and every other field, only its type changes, so
            // retype in place rather than rebuilding the collection.
            for (Surface &surface : layerm->fill_surfaces.surfaces) {
                if (surface.surface_type != stInternalSolid)
                    continue;
                // Trim the mask to the island first, so one island does not have to face the whole
                // layer's top geometry. Opening then drops what only grazes the mask: an island is
                // claimed on real overlap, not on a shared edge.
                const BoundingBox island_bbox = get_extents(surface.expolygon).inflated(SCALED_EPSILON);
                Polygons          local_mask;
                for (size_t i = 0; i < top_mask.size(); ++ i)
                    if (top_extents[i].overlap(island_bbox))
                        append(local_mask, ClipperUtils::clip_clipper_polygons_with_subject_bbox(top_mask[i], island_bbox));
                if (! local_mask.empty() &&
                    ! opening_ex(intersection_ex(surface.expolygon, local_mask, ApplySafetyOffset::Yes), 0.5f * min_width).empty())
                    surface.surface_type = stSubTop;
            }
        }
    };

    // A layer reads the tops of the layer above and retypes its own fill surfaces, so neighbours
    // must not run together. Splitting the sweep by parity keeps every pair of concurrent layers
    // two apart, which leaves the read and write sets disjoint.
    const size_t num_to_process = m_layers.size() - 1;
    for (size_t parity = 0; parity < 2; ++ parity) {
        if (num_to_process <= parity)
            continue;
        const size_t count = (num_to_process - parity + 1) / 2;
        tbb::parallel_for(tbb::blocked_range<size_t>(0, count),
            [&process_layer, parity](const tbb::blocked_range<size_t> &range) {
                for (size_t k = range.begin(); k < range.end(); ++ k)
                    process_layer(parity + 2 * k);
            });
    }
}

void PrintObject::_generate_support_material()
{
    const long long support_begin_time = Slic3r::Utils::get_current_milliseconds_time_monotonic();
    const long long child_time_before =
        m_support_stage_times.detect + m_support_stage_times.interface_generate + m_support_stage_times.toolpath_generate;

    if (is_tree(m_config.support_type.value)) {
        TreeSupport tree_support(*this, m_slicing_params);
        tree_support.throw_on_cancel = [this]() { this->throw_if_canceled(); };
        tree_support.generate();
    }
    else {
        PrintObjectSupportMaterial support_material(this, m_slicing_params);
        support_material.generate(*this);
    }

    const long long support_elapsed =
        Slic3r::Utils::get_current_milliseconds_time_monotonic() - support_begin_time;
    const long long child_time_after =
        m_support_stage_times.detect + m_support_stage_times.interface_generate + m_support_stage_times.toolpath_generate;
    const long long generate_time = std::max(0LL, support_elapsed - (child_time_after - child_time_before));
    if (is_tree(m_config.support_type.value))
        m_support_stage_times.tree_generate += generate_time;
    else
        m_support_stage_times.normal_generate += generate_time;
}

// Overhang islands below this area are detection noise, or slivers a single branch tip covers anyway.
// Reporting them would bury the cases the user can actually act on.
static constexpr double MIN_REPORTED_OVERHANG_AREA_MM2 = 3.;
// A tree branch touches a flat overhang at a few points only, so an island counts as unsupported when
// there is essentially nothing beneath it, not when the coverage is merely partial.
static constexpr double MIN_SUPPORTED_OVERHANG_RATIO = 0.05;
// How much uncovered overhang has to pile up across the object before the user is worth interrupting.
static constexpr double MIN_TOTAL_UNCOVERED_AREA_MM2 = 20.;

// Only the parts of a support layer that can reach an overhang island are worth unioning: one tree
// support layer of a tall object holds hundreds of branch cross sections, while the islands tested
// against it cover a small part of the plate. Discarding the rest by bounding box before the union
// keeps both the Clipper input and the cached result proportional to the overhangs.
ExPolygons PrintObject::collected_support_areas(const SupportLayer *support_layer, const BoundingBox &roi) const
{
    if (! roi.defined)
        return {};

    auto append_overlapping = [&roi](ExPolygons &dst, const ExPolygons &src) {
        for (const ExPolygon &expoly : src) {
            const BoundingBox bbox = get_extents(expoly);
            if (bbox.defined && roi.overlap(bbox))
                dst.emplace_back(expoly);
        }
    };

    // Always union every generated support area that still has geometry.
    const bool has_areas = ! support_layer->support_islands.empty() || ! support_layer->base_areas.empty() ||
                           ! support_layer->roof_areas.empty() || ! support_layer->roof_1st_layer.empty() ||
                           ! support_layer->floor_areas.empty();
    if (has_areas) {
        ExPolygons areas;
        append_overlapping(areas, support_layer->support_islands);
        append_overlapping(areas, support_layer->base_areas);
        append_overlapping(areas, support_layer->roof_areas);
        append_overlapping(areas, support_layer->roof_1st_layer);
        append_overlapping(areas, support_layer->floor_areas);
        // Empty here means "no support near roi", not "no support at all", so the extrusion
        // fallback below must not take over.
        return areas.empty() ? ExPolygons{} : union_ex(areas);
    }

    // No area geometry at all: the generator left only extrusions behind. Rebuilding polygons from
    // them is expensive, hence the fallback position, and the bounding box filter only pays off
    // afterwards because the spacing coverage is computed for the whole collection at once.
    if (support_layer->support_fills.empty())
        return {};
    // Cheap rejection before rebuilding polygons: walk the fill paths once and bail out when their
    // bounding box cannot touch the roi. polygons_covered_by_spacing() has to offset+union the whole
    // collection, which only pays off when some part of it is actually visible. The coverage offsets
    // every path by half its spacing plus epsilon, so inflate the reject box by the largest such
    // margin before comparing: the bare centerline box would discard fills whose inflated coverage
    // still reaches the roi. get_extents<true> keeps a degenerate (single vertical/horizontal line)
    // fill defined, instead of mistaking it for "no support".
    Points       fill_points;
    support_layer->support_fills.collect_points(fill_points);
    coordf_t     max_half_spacing = 0;
    auto         path_half_spacing = [](const ExtrusionPath &path) {
        const Flow flow = is_bridge(path.role()) ? Flow::bridging_flow(path.width, 0.f)
                                                 : Flow(path.width, path.height, 0.f);
        return coordf_t(0.5f * float(flow.scaled_spacing()));
    };
    std::function<void(const ExtrusionEntity *)> walk_fill_spacing = [&](const ExtrusionEntity *entity) {
        if (entity->is_collection())
            for (const ExtrusionEntity *child : static_cast<const ExtrusionEntityCollection *>(entity)->entities)
                walk_fill_spacing(child);
        else if (auto *path = dynamic_cast<const ExtrusionPath *>(entity))
            max_half_spacing = std::max(max_half_spacing, path_half_spacing(*path));
        else if (auto *multi = dynamic_cast<const ExtrusionMultiPath *>(entity))
            for (const ExtrusionPath &path : multi->paths)
                max_half_spacing = std::max(max_half_spacing, path_half_spacing(path));
        else if (auto *loop = dynamic_cast<const ExtrusionLoop *>(entity))
            for (const ExtrusionPath &path : loop->paths)
                max_half_spacing = std::max(max_half_spacing, path_half_spacing(path));
    };
    walk_fill_spacing(&support_layer->support_fills);
    BoundingBox fill_bbox = get_extents<true>(fill_points);
    fill_bbox.offset(max_half_spacing + SCALED_EPSILON);
    if (! fill_bbox.defined || ! roi.overlap(fill_bbox))
        return {};
    const ExPolygons fills = union_ex(support_layer->support_fills.polygons_covered_by_spacing(float(SCALED_EPSILON)));
    ExPolygons       areas;
    append_overlapping(areas, fills);
    return areas;
}

// Supports can legitimately fail to grow, most visibly when "Support on build plate only" leaves the
// branches nowhere to land. Slicing still succeeds and nothing tells the user, so compare what the
// generator meant to cover against what it produced and report the difference.
// layer->loverhangs_with_type is the "meant to cover" set: detect_overhangs() has already dropped
// bridgeable regions, painted blockers and everything support_critical_regions_only excludes.
// Ordinary small overhangs are dropped there too, while painted enforcers remain reportable. The set
// is only refreshed by the tree generators, hence the is_tree() gate at the call site.
void PrintObject::warn_uncovered_overhangs()
{
    // draw_circles() marks the support layers it left empty with a zero print_z and height, which would
    // break a search over print_z, so index only the layers that carry geometry.
    std::vector<coordf_t> support_zs;
    std::vector<size_t>   support_layer_indices;
    support_zs.reserve(m_support_layers.size());
    support_layer_indices.reserve(m_support_layers.size());
    for (size_t support_layer_idx = 0; support_layer_idx < m_support_layers.size(); ++ support_layer_idx) {
        const SupportLayer *support_layer = m_support_layers[support_layer_idx];
        if (support_layer->height < EPSILON)
            continue;
        support_zs.emplace_back(support_layer->print_z);
        support_layer_indices.emplace_back(support_layer_idx);
    }
    assert(std::is_sorted(support_zs.begin(), support_zs.end()));
    m_print->throw_if_canceled();

    // Multi-diameter top gaps follow the nozzle printing the overhang. Use the largest one here so
    // every region gets a search window wide enough for its gap and printable layer height.
    coordf_t search_top_z           = m_slicing_params.gap_support_object;
    coordf_t search_max_layer_height = std::max(m_slicing_params.max_suport_layer_height,
                                                 m_slicing_params.layer_height);
    coordf_t search_min_layer_height = m_slicing_params.min_layer_height;
    if (m_print->uses_multiple_nozzle_diameters()) {
        const PrintConfig &print_config = m_print->config();
        double             max_nozzle   = 0.;
        for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
            const PrintRegionConfig &config = this->printing_region(region_id).config();
            max_nozzle = std::max({max_nozzle,
                double(nozzle_diameter_for_filament(print_config, config.wall_filament.value)),
                double(nozzle_diameter_for_filament(print_config, config.solid_infill_filament.value))});
        }
        if (max_nozzle > EPSILON) {
            if (search_top_z > EPSILON)
                search_top_z = support_top_z_for_nozzle(max_nozzle);
            const AdaptiveLayerHeightRange height_range = adaptive_layer_height_range(max_nozzle);
            search_max_layer_height = height_range.max_height;
            search_min_layer_height = height_range.min_height;
        }
    }
    const coordf_t search_depth = search_top_z + search_max_layer_height + search_min_layer_height;
    const float    coverage_offset = float(scale_(
        resolved_support_object_xy_distance(m_print->config(), m_config)));

    // One overhang island worth reporting, with everything the passes below need from it.
    struct Island {
        ExPolygons  coverage_area;
        double      area_mm2;
        // Inflated by the support xy distance. Used only to reject support branches that cannot touch it.
        BoundingBox bbox;
    };
    struct LayerWork {
        std::vector<Island> islands;
        // Union of the island bounding boxes: the only part of the support that can cover this layer.
        BoundingBox         roi;
        // Range into support_zs of the support layers sitting close enough below to count.
        size_t              support_begin  = 0;
        size_t              support_end    = 0;
        double              uncovered_area = 0.;
        size_t              num_uncovered  = 0;
    };
    std::vector<LayerWork> work(m_layers.size());

    // Pass 1: pick the islands worth reporting and the support window each layer has to look at.
    // This only walks geometry that already exists, and it lets the two passes below skip the layers -
    // usually the vast majority - with nothing left to check.
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layers.size()),
        [this, &support_zs, search_depth, coverage_offset, &work](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                m_print->throw_if_canceled();
                const Layer *layer = m_layers[layer_idx];
                if (layer->loverhangs_with_type.empty())
                    continue;

                LayerWork &w = work[layer_idx];
                struct Candidate {
                    const ExPolygon *island;
                    BoundingBox      bbox;
                    double           area_mm2;
                };
                std::vector<Candidate> candidate_islands;
                BoundingBox           candidate_bbox;
                candidate_islands.reserve(layer->loverhangs_with_type.size());
                for (const auto &island_with_type : layer->loverhangs_with_type) {
                    const ExPolygon &island = island_with_type.first;
                    // A difference can only reduce the area, so these islands cannot become reportable.
                    const double island_area = area(island) * SCALING_FACTOR * SCALING_FACTOR;
                    if (island_area < MIN_REPORTED_OVERHANG_AREA_MM2)
                        continue;
                    const BoundingBox bbox = get_extents(island);
                    if (bbox.defined) {
                        candidate_islands.push_back({ &island, bbox, island_area });
                        candidate_bbox.merge(bbox);
                    }
                }
                if (candidate_islands.empty())
                    continue;

                // Reuse the full search depth intentionally: after the top gap, the maximum layer height
                // covers the quantized bottom clearance and the minimum leaves room to extrude support.
                // Model material in this span leaves no usable Z room and is removed from the requirement.
                Polygons model_below;
                const coordf_t bottom_z = layer->bottom_z();
                const coordf_t lowest_z = bottom_z - search_depth;
                const BoundingBox model_bbox = candidate_bbox.inflated(SCALED_EPSILON);
                for (size_t model_layer_idx = layer_idx; model_layer_idx-- > 0;) {
                    const Layer *model_layer = m_layers[model_layer_idx];
                    if (model_layer->print_z < lowest_z - EPSILON)
                        break;
                    append(model_below, ClipperUtils::clip_clipper_polygons_with_subject_bbox(
                        model_layer->lslices, model_bbox));
                }

                auto add_island = [&w, coverage_offset](const ExPolygon &island, double island_area) {
                    if (island_area < MIN_REPORTED_OVERHANG_AREA_MM2)
                        return;
                    ExPolygons coverage_area = coverage_offset > SCALED_EPSILON ?
                        offset_ex(island, coverage_offset) : ExPolygons{island};
                    const BoundingBox coverage_bbox = get_extents(coverage_area);
                    if (! coverage_bbox.defined)
                        return;
                    w.islands.push_back({ std::move(coverage_area), island_area, coverage_bbox });
                    w.roi.merge(coverage_bbox);
                };
                w.islands.reserve(candidate_islands.size());
                for (const Candidate &candidate : candidate_islands) {
                    const Polygons model_near = model_below.empty() ? Polygons{} :
                        ClipperUtils::clip_clipper_polygons_with_subject_bbox(
                            model_below, candidate.bbox.inflated(SCALED_EPSILON));
                    if (model_near.empty())
                        add_island(*candidate.island, candidate.area_mm2);
                    else
                        for (const ExPolygon &island : diff_ex(*candidate.island, model_near))
                            add_island(island, area(island) * SCALING_FACTOR * SCALING_FACTOR);
                }
                if (w.islands.empty())
                    continue;

                const auto     it_end   = std::upper_bound(support_zs.begin(), support_zs.end(), bottom_z + EPSILON);
                const auto     it_begin = std::lower_bound(support_zs.begin(), it_end, bottom_z - search_depth);
                w.support_begin = it_begin - support_zs.begin();
                w.support_end   = it_end - support_zs.begin();
            }
        });
    m_print->throw_if_canceled();

    // Pass 2: consecutive model layers share most of their support window, so union each support layer
    // once instead of once per model layer, and only the part overlapping the islands that will query
    // it. Both the work and the extra memory then scale with the overhangs, not with the support.
    std::vector<int>         cache_slot(support_zs.size(), -1);
    std::vector<size_t>      cached_support_zs;
    std::vector<BoundingBox> cached_rois;
    for (const LayerWork &w : work) {
        if (w.islands.empty())
            continue;
        for (size_t i = w.support_begin; i < w.support_end; ++ i) {
            if (cache_slot[i] < 0) {
                cache_slot[i] = int(cached_support_zs.size());
                cached_support_zs.emplace_back(i);
                cached_rois.emplace_back();
            }
            cached_rois[cache_slot[i]].merge(w.roi);
        }
    }

    std::vector<ExPolygons>               support_cache(cached_support_zs.size());
    std::vector<std::vector<BoundingBox>> support_cache_bboxes(cached_support_zs.size());
    std::vector<BoundingBox>              support_cache_extents(cached_support_zs.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, cached_support_zs.size()),
        [this, &support_layer_indices, &cached_support_zs, &cached_rois, &support_cache,
         &support_cache_bboxes, &support_cache_extents](const tbb::blocked_range<size_t> &range) {
            for (size_t i = range.begin(); i < range.end(); ++ i) {
                m_print->throw_if_canceled();
                const size_t support_layer_idx = support_layer_indices[cached_support_zs[i]];
                ExPolygons &cached = support_cache[i];
                cached = this->collected_support_areas(m_support_layers[support_layer_idx], cached_rois[i]);
                support_cache_bboxes[i].reserve(cached.size());
                for (const ExPolygon &expoly : cached) {
                    const BoundingBox bbox = get_extents(expoly);
                    support_cache_bboxes[i].emplace_back(bbox);
                    support_cache_extents[i].merge(bbox);
                }
            }
        });
    m_print->throw_if_canceled();

    // Pass 3: the coverage test itself.
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layers.size()),
        [this, &work, &cache_slot, &support_cache, &support_cache_bboxes, &support_cache_extents](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                m_print->throw_if_canceled();
                LayerWork &w = work[layer_idx];
                if (w.islands.empty())
                    continue;

                // Each cached layer is already unioned and already clipped to this layer's islands, so
                // a lone one is used as is and only several of them still have to be merged.
                const ExPolygons *support_below    = nullptr;
                ExPolygons        support_merged;
                size_t            num_nonempty     = 0;
                size_t            single_cache_idx = 0;
                for (size_t i = w.support_begin; i < w.support_end; ++ i) {
                    const ExPolygons &cached = support_cache[cache_slot[i]];
                    if (cached.empty())
                        continue;
                    ++ num_nonempty;
                    support_below = &cached;
                    single_cache_idx = i;
                }
                if (num_nonempty > 1) {
                    // Only the layers that actually carry geometry contribute to the merged set, which
                    // keeps the union input proportional to the overhangs.
                    for (size_t i = w.support_begin; i < w.support_end; ++ i) {
                        const ExPolygons &cached = support_cache[cache_slot[i]];
                        if (! cached.empty())
                            append(support_merged, cached);
                    }
                    support_merged = union_ex(support_merged);
                    support_below  = &support_merged;
                }

                // Per-branch bounding boxes for the clipping step below. A single cached layer already
                // has them computed by Pass 2; only a merged set has to be re-measured here.
                const std::vector<BoundingBox> *support_bboxes = nullptr;
                std::vector<BoundingBox>        merged_bboxes;
                if (support_below != nullptr) {
                    if (num_nonempty == 1) {
                        support_bboxes = &support_cache_bboxes[cache_slot[single_cache_idx]];
                    } else {
                        merged_bboxes.reserve(support_below->size());
                        for (const ExPolygon &expoly : *support_below)
                            merged_bboxes.emplace_back(get_extents(expoly));
                        support_bboxes = &merged_bboxes;
                    }
                }

                for (const Island &island : w.islands) {
                    double covered_area = 0.;
                    if (support_below != nullptr) {
                        // Whole-cache rejection: a single supporting layer whose overall bounds cannot
                        // reach this island has nothing left to clip.
                        bool may_touch = true;
                        if (num_nonempty == 1) {
                            const BoundingBox &extent = support_cache_extents[cache_slot[single_cache_idx]];
                            may_touch = extent.defined && island.bbox.defined && extent.overlap(island.bbox);
                        }
                        if (may_touch) {
                            // Clipper's cost grows faster than linearly in the edge count, so hand it only
                            // the branches whose bounding box can touch this island.
                            ExPolygons clip;
                            for (size_t i = 0; i < support_below->size(); ++ i)
                                if ((*support_bboxes)[i].defined && island.bbox.overlap((*support_bboxes)[i]))
                                    clip.emplace_back((*support_below)[i]);
                            if (! clip.empty())
                                covered_area = area(intersection_ex(island.coverage_area, clip)) * SCALING_FACTOR * SCALING_FACTOR;
                        }
                    }
                    if (covered_area < MIN_SUPPORTED_OVERHANG_RATIO * island.area_mm2) {
                        w.uncovered_area += island.area_mm2;
                        ++ w.num_uncovered;
                    }
                }
            }
        });

    double   total_uncovered_area = 0.;
    size_t   total_uncovered      = 0;
    size_t   total_significant    = 0;
    coordf_t lowest_z             = 0.;
    for (size_t layer_idx = 0; layer_idx < work.size(); ++ layer_idx) {
        const LayerWork &w = work[layer_idx];
        total_significant += w.islands.size();
        if (w.num_uncovered == 0)
            continue;
        if (total_uncovered == 0)
            lowest_z = m_layers[layer_idx]->print_z;
        total_uncovered_area += w.uncovered_area;
        total_uncovered      += w.num_uncovered;
    }

    if (total_uncovered > 0)
        BOOST_LOG_TRIVIAL(info) << "Uncovered overhang check on object " << this->model_object()->name << ": "
                                << total_uncovered << " of " << total_significant << " region(s) uncovered, "
                                << total_uncovered_area << " mm2, lowest_z=" << lowest_z;

    if (total_uncovered_area < MIN_TOTAL_UNCOVERED_AREA_MM2)
        return;

    // Name build-plate-only only when the resolved style still walks drop_nodes.
    // resolve_support_style() matches SupportParameters, including Default / Grid / Snug.
    const PrintConfig &print_config = this->print()->config();
    const SupportMaterialStyle resolved_style =
        resolve_support_style(m_config.support_style.value, m_config.support_type.value,
                              this->has_variable_layer_heights, m_slicing_params.soluble_interface,
                              config_flag_or_false(print_config, "heat_preserve_mode"),
                              config_flag_or_false(print_config, "enable_support_ring"),
                              this->print()->uses_multiple_nozzle_diameters());
    const bool classic_tree_drop = resolved_style != smsTreeOrganic;
    std::string warning_message;
    PrintStateBase::SlicingNotificationType warning_id = PrintStateBase::SlicingSupportIncomplete;
    if (m_config.support_on_build_plate_only.value && classic_tree_drop) {
        warning_id = PrintStateBase::SlicingSupportIncompleteOnBuildPlate;
        warning_message = Slic3r::format(
            _u8L("Unsupported overhangs were detected.\n"
                 "Model \"%1%\" has %2% overhang region(s). Because \"Support on build plate only\" is enabled, "
                 "supports cannot grow down to the build plate. Try turning this option off."),
            this->model_object()->name, total_uncovered);
    } else
        warning_message = Slic3r::format(
            _u8L("Unsupported overhangs were detected.\n"
                 "Model \"%1%\" has %2% overhang region(s) that could not get support. "
                 "Try adjusting the support settings or re-orienting the object."),
            this->model_object()->name, total_uncovered);
    this->active_step_add_warning(PrintStateBase::WarningLevel::NON_CRITICAL, warning_message, warning_id);
}

// BBS
#define SUPPORT_SURFACES_OFFSET_PARAMETERS ClipperLib::jtSquare, 0.
#define SUPPORT_MATERIAL_MARGIN 1.2
template<typename PolysType>
void PrintObject::remove_bridges_from_contacts(
    const Layer* lower_layer,
    const Layer* current_layer,
    float extrusion_width,
    PolysType* overhang_regions,
    float max_bridge_length,
    bool break_bridge,
    std::vector<std::pair<ExPolygon, int>>* overhang_regions_with_type)
{
    // Extrusion width accounts for the roundings of the extrudates.
    // It is the maximum widh of the extrudate.
    float fw = extrusion_width;
    Lines overhang_perimeters = to_lines(*overhang_regions);
    auto layer_regions = current_layer->regions();
    Polygons lower_layer_polygons = to_polygons(lower_layer->lslices);
    const PrintObjectConfig& object_config = current_layer->object()->config();

    Polygons all_bridges;
    for (LayerRegion* layerm : layer_regions)
    {
        Polygons bridges;
        // Surface supporting this layer, expanded by 0.5 * nozzle_diameter, as we consider this kind of overhang to be sufficiently supported.
        Polygons lower_grown_slices = offset(lower_layer_polygons,
            //FIXME to mimic the decision in the perimeter generator, we should use half the external perimeter width.
            0.5f * fw, SUPPORT_SURFACES_OFFSET_PARAMETERS);
        Polylines overhang_perimeters = diff_pl(layerm->perimeters.as_polylines(), lower_grown_slices);
        // only consider straight overhangs
            // only consider overhangs having endpoints inside layer's slices
            // convert bridging polylines into polygons by inflating them with their thickness
            // since we're dealing with bridges, we can't assume width is larger than spacing,
            // so we take the largest value and also apply safety offset to be ensure no gaps
            // are left in between
        Flow bridge_flow = layerm->bridging_flow(frPerimeter, object_config.thick_bridges);
        float w = float(std::max(bridge_flow.scaled_width(), bridge_flow.scaled_spacing()));
        for (Polyline& polyline : overhang_perimeters)
            if (polyline.is_straight()) {
                // This is a bridge
                polyline.extend_start(fw);
                polyline.extend_end(fw);
                // Is the straight perimeter segment supported at both sides?
                Point pts[2] = { polyline.first_point(), polyline.last_point() };
                bool  supported[2] = { false, false };
                for (size_t i = 0; i < lower_layer->lslices.size() && !(supported[0] && supported[1]); ++i)
                    for (int j = 0; j < 2; ++j)
                        if (!supported[j] && lower_layer->lslices_bboxes[i].contains(pts[j]) && lower_layer->lslices[i].contains(pts[j]))
                            supported[j] = true;
                if (supported[0] && supported[1]) {
                    Polylines lines;
                    if (polyline.length() > max_bridge_length + 10) {
                        if (break_bridge) {
                            // equally divide the polyline
                            float len = polyline.length() / ceil(polyline.length() / max_bridge_length);
                            lines = polyline.equally_spaced_lines(len);
                            for (auto& line : lines) {
                                if (line.is_valid())
                                    line.clip_start(fw);
                                if (line.is_valid())
                                    line.clip_end(fw);
                            }
                        }
                    }
                    else
                        lines.push_back(polyline);
                    // Offset a polyline into a thick line.
                    polygons_append(bridges, offset(lines, 0.5f * w + 10.f));
                }
            }
        bridges = union_(bridges);

        // remove the entire bridges and only support the unsupported edges
        //FIXME the brided regions are already collected as layerm->bridged. Use it?
        for (const Surface& surface : layerm->fill_surfaces.surfaces)
            if (surface.surface_type == stBottomBridge && surface.bridge_angle != -1) {
                auto bbox      = get_extents(surface.expolygon);
                auto bbox_size = bbox.size();
                if (bbox_size[0] < max_bridge_length && bbox_size[1] < max_bridge_length)
                    polygons_append(bridges, surface.expolygon);
                else {
                    if (break_bridge) {
                        Polygons holes;
                        coord_t  x0 = bbox.min.x();
                        coord_t  x1 = bbox.max.x();
                        coord_t  y0 = bbox.min.y();
                        coord_t  y1 = bbox.max.y();
                        const int grid_lw = int(w/2); // grid line width

                        Vec2f bridge_direction{ cos(surface.bridge_angle),sin(surface.bridge_angle) };
                        if (fabs(bridge_direction(0)) > fabs(bridge_direction(1)))
                        {   // cut bridge along x-axis if bridge direction is aligned to x-axis more than to y-axis
                            // Note: surface.bridge_angle may be pi, so we can't compare it to 0 & pi/2.
                            int step = bbox_size(0) / ceil(bbox_size(0) / max_bridge_length);
                            for (int x = x0 + step; x < x1; x += step) {
                                Polygon poly;
                                poly.points = {Point(x - grid_lw, y0), Point(x + grid_lw, y0), Point(x + grid_lw, y1), Point(x - grid_lw, y1)};
                                holes.emplace_back(poly);
                            }
                        } else {
                            int step = bbox_size(1) / ceil(bbox_size(1) / max_bridge_length);
                            for (int y = y0 + step; y < y1; y += step) {
                                Polygon poly;
                                poly.points = {Point(x0, y - grid_lw), Point(x0, y + grid_lw), Point(x1, y + grid_lw), Point(x1, y - grid_lw)};
                                holes.emplace_back(poly);
                            }
                        }
                        auto expoly = diff_ex(surface.expolygon, holes);
                        polygons_append(bridges, expoly);
                    }
                }
            }
        //FIXME add the gap filled areas. Extrude the gaps with a bridge flow?
        // Remove the unsupported ends of the bridges from the bridged areas.
        //FIXME add supports at regular intervals to support long bridges!
        bridges = diff(bridges,
            // Offset unsupported edges into polygons.
            offset(layerm->unsupported_bridge_edges, scale_(SUPPORT_MATERIAL_MARGIN), SUPPORT_SURFACES_OFFSET_PARAMETERS));
        append(all_bridges, bridges);
    }
    if (typeid(overhang_regions) == typeid(ExPolygons*)) {
        *(ExPolygons*)overhang_regions = diff_ex(*overhang_regions, all_bridges, ApplySafetyOffset::Yes);
    }
    else if (typeid(overhang_regions) == typeid(Polygons*)) {
        *(Polygons*)overhang_regions = diff(*overhang_regions, all_bridges, ApplySafetyOffset::Yes);
    }

    // Keep an optional typed overhang list in sync by diffing against the same bridge set.
    // Skip items whose bbox doesn't overlap the bridges bbox to avoid unnecessary Clipper calls.
    if (overhang_regions_with_type && !overhang_regions_with_type->empty() && !all_bridges.empty()) {
        BoundingBox bridges_bbox = get_extents(all_bridges);
        std::vector<std::pair<ExPolygon, int>> kept;
        kept.reserve(overhang_regions_with_type->size());
        bool any_trimmed = false;
        for (auto &overhang_part : *overhang_regions_with_type) {
            BoundingBox part_bbox = get_extents(overhang_part.first);
            if (!bridges_bbox.overlap(part_bbox)) {
                kept.emplace_back(std::move(overhang_part.first), overhang_part.second);
                continue;
            }
            ExPolygons remaining = diff_ex({overhang_part.first}, all_bridges, ApplySafetyOffset::Yes);
            any_trimmed = true;
            for (auto &expoly : remaining)
                kept.emplace_back(std::move(expoly), overhang_part.second);
        }
        if (any_trimmed)
            *overhang_regions_with_type = std::move(kept);
    }
}

template void PrintObject::remove_bridges_from_contacts<ExPolygons>(
    const Layer* lower_layer,
    const Layer* current_layer,
    float extrusion_width,
    ExPolygons* overhang_regions,
    float max_bridge_length, bool break_bridge,
    std::vector<std::pair<ExPolygon, int>>* overhang_regions_with_type);
template void PrintObject::remove_bridges_from_contacts<Polygons>(
    const Layer* lower_layer,
    const Layer* current_layer,
    float extrusion_width,
    Polygons* overhang_regions,
    float max_bridge_length, bool break_bridge,
    std::vector<std::pair<ExPolygon, int>>* overhang_regions_with_type);


SupportNecessaryType PrintObject::is_support_necessary()
{
    const double cantilevel_dist_thresh = scale_(6);

    TreeSupport tree_support(*this, m_slicing_params);
    tree_support.support_type = SupportType::stTreeAuto; // need to set support type to fully utilize the power of feature detection
    tree_support.detect_overhangs(true);
    this->clear_support_layers();
    if (tree_support.has_sharp_tails)
        return SharpTail;
    else if (tree_support.has_cantilever && tree_support.max_cantilever_dist > cantilevel_dist_thresh)
        return Cantilever;

    return NoNeedSupp;
}

static void project_triangles_to_slabs(ConstLayerPtrsAdaptor layers, const indexed_triangle_set &custom_facets, const Transform3f &tr, bool seam, std::vector<Polygons> &out)
{
    if (custom_facets.indices.empty())
        return;

    const float tr_det_sign = (tr.matrix().determinant() > 0. ? 1.f : -1.f);

    // The projection will be at most a pentagon. Let's minimize heap
    // reallocations by saving in in the following struct.
    // Points are used so that scaling can be done in parallel
    // and they can be moved from to create an ExPolygon later.
    struct LightPolygon {
        LightPolygon() { pts.reserve(5); }
        LightPolygon(const std::array<Vec2f, 3>& tri) {
            pts.reserve(3);
            pts.emplace_back(scaled<coord_t>(tri.front()));
            pts.emplace_back(scaled<coord_t>(tri[1]));
            pts.emplace_back(scaled<coord_t>(tri.back()));
        }

        Points pts;

        void add(const Vec2f& pt) {
            pts.emplace_back(scaled<coord_t>(pt));
            assert(pts.size() <= 5);
        }
    };

    // Structure to collect projected polygons. One element for each triangle.
    // Saves vector of polygons and layer_id of the first one.
    struct TriangleProjections {
        size_t first_layer_id;
        std::vector<LightPolygon> polygons;
    };

    // Vector to collect resulting projections from each triangle.
    std::vector<TriangleProjections> projections_of_triangles(custom_facets.indices.size());

    // Iterate over all triangles.
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, custom_facets.indices.size()),
        [&custom_facets, &tr, tr_det_sign, seam, layers, &projections_of_triangles](const tbb::blocked_range<size_t>& range) {
        for (size_t idx = range.begin(); idx < range.end(); ++ idx) {

        std::array<Vec3f, 3> facet;

        // Transform the triangle into worlds coords.
        for (int i=0; i<3; ++i)
            facet[i] = tr * custom_facets.vertices[custom_facets.indices[idx](i)];

        // Ignore triangles with upward-pointing normal. Don't forget about mirroring.
        float z_comp = (facet[1]-facet[0]).cross(facet[2]-facet[0]).z();
        if (! seam && tr_det_sign * z_comp > 0.)
            continue;

        // The algorithm does not process vertical triangles, but it should for seam.
        // In that case, tilt the triangle a bit so the projection does not degenerate.
        if (seam && z_comp == 0.f)
            facet[0].x() += float(EPSILON);

        // Sort the three vertices according to z-coordinate.
        std::sort(facet.begin(), facet.end(),
                  [](const Vec3f& pt1, const Vec3f&pt2) {
                      return pt1.z() < pt2.z();
                  });

        std::array<Vec2f, 3> trianglef;
        for (int i=0; i<3; ++i)
            trianglef[i] = to_2d(facet[i]);

        // Find lowest slice not below the triangle.
        auto it = std::lower_bound(layers.begin(), layers.end(), facet[0].z()+EPSILON,
                      [](const Layer* l1, float z) {
                           return l1->slice_z < z;
                      });

        // Count how many projections will be generated for this triangle
        // and allocate respective amount in projections_of_triangles.
        size_t first_layer_id = projections_of_triangles[idx].first_layer_id = it - layers.begin();
        size_t last_layer_id  = first_layer_id;
        // The cast in the condition below is important. The comparison must
        // be an exact opposite of the one lower in the code where
        // the polygons are appended. And that one is on floats.
        while (last_layer_id + 1 < layers.size()
            && float(layers[last_layer_id]->slice_z) <= facet[2].z())
            ++last_layer_id;

        if (first_layer_id == last_layer_id) {
            // The triangle fits just a single slab, just project it. This also avoids division by zero for horizontal triangles.
            float dz = facet[2].z() - facet[0].z();
            assert(dz >= 0);
            // The face is nearly horizontal and it crosses the slicing plane at first_layer_id - 1.
            // Rather add this face to both the planes.
            bool add_below = dz < float(2. * EPSILON) && first_layer_id > 0 && layers[first_layer_id - 1]->slice_z > facet[0].z() - EPSILON;
            projections_of_triangles[idx].polygons.reserve(add_below ? 2 : 1);
            projections_of_triangles[idx].polygons.emplace_back(trianglef);
            if (add_below) {
                -- projections_of_triangles[idx].first_layer_id;
                projections_of_triangles[idx].polygons.emplace_back(trianglef);
            }
            continue;
        }

        projections_of_triangles[idx].polygons.resize(last_layer_id - first_layer_id + 1);

        // Calculate how to move points on triangle sides per unit z increment.
        Vec2f ta(trianglef[1] - trianglef[0]);
        Vec2f tb(trianglef[2] - trianglef[0]);
        ta *= 1.f/(facet[1].z() - facet[0].z());
        tb *= 1.f/(facet[2].z() - facet[0].z());

        // Projection on current slice will be built directly in place.
        LightPolygon* proj = &projections_of_triangles[idx].polygons[0];
        proj->add(trianglef[0]);

        bool passed_first = false;
        bool stop = false;

        // Project a sub-polygon on all slices intersecting the triangle.
        while (it != layers.end()) {
            const float z = float((*it)->slice_z);

            // Projections of triangle sides intersections with slices.
            // a moves along one side, b tracks the other.
            Vec2f a;
            Vec2f b;

            // If the middle vertex was already passed, append the vertex
            // and use ta for tracking the remaining side.
            if (z > facet[1].z() && ! passed_first) {
                proj->add(trianglef[1]);
                ta = trianglef[2]-trianglef[1];
                ta *= 1.f/(facet[2].z() - facet[1].z());
                passed_first = true;
            }

            // This slice is above the triangle already.
            if (z > facet[2].z() || it+1 == layers.end()) {
                proj->add(trianglef[2]);
                stop = true;
            }
            else {
                // Move a, b along the side it currently tracks to get
                // projected intersection with current slice.
                a = passed_first ? (trianglef[1]+ta*(z-facet[1].z()))
                                 : (trianglef[0]+ta*(z-facet[0].z()));
                b = trianglef[0]+tb*(z-facet[0].z());
                proj->add(a);
                proj->add(b);
            }

           if (stop)
                break;

            // Advance to the next layer.
            ++it;
            ++proj;
            assert(proj <= &projections_of_triangles[idx].polygons.back() );

            // a, b are first two points of the polygon for the next layer.
            proj->add(b);
            proj->add(a);
        }
    }
    }); // end of parallel_for

    // Make sure that the output vector can be used.
    out.resize(layers.size());

    // Now append the collected polygons to respective layers.
    for (auto& trg : projections_of_triangles) {
        int layer_id = int(trg.first_layer_id);
        for (LightPolygon &poly : trg.polygons) {
            if (layer_id >= int(out.size()))
                break; // part of triangle could be projected above top layer
            assert(! poly.pts.empty());
            // The resulting triangles are fed to the Clipper library, which seem to handle flipped triangles well.
//                if (cross2(Vec2d((poly.pts[1] - poly.pts[0]).cast<double>()), Vec2d((poly.pts[2] - poly.pts[1]).cast<double>())) < 0)
//                    std::swap(poly.pts.front(), poly.pts.back());

            out[layer_id].emplace_back(std::move(poly.pts));
            ++layer_id;
        }
    }
}

void PrintObject::project_and_append_custom_facets(
        bool seam, EnforcerBlockerType type, std::vector<Polygons>& out, std::vector<std::pair<Vec3f, Vec3f>>* vertical_points) const
{
    const double shrink_scale = seam ? 1. : this->support_shrinkage_scale();
    for (const ModelVolume* mv : this->model_object()->volumes)
        if (mv->is_model_part()) {
            const indexed_triangle_set custom_facets = seam
                    ? mv->seam_facets.get_facets_strict(*mv, type)
                    : mv->supported_facets.get_facets_strict(*mv, type);//just for support
            if (! custom_facets.indices.empty()) {
                if (seam)
                    project_triangles_to_slabs(this->layers(), custom_facets,
                        (this->trafo_centered() * mv->get_matrix()).cast<float>(),
                        seam, out);
                else {
                    std::vector<Polygons> projected;
                    const size_t          vp_begin = vertical_points ? vertical_points->size() : 0;
                    // Support blockers or enforcers. Project downward facing painted areas upwards to their respective slicing plane.
                    slice_mesh_slabs(custom_facets, zs_from_layers(this->layers()), this->trafo_centered() * mv->get_matrix(), nullptr, &projected, vertical_points, [](){});
                    if (shrink_scale != 1.) {
                        for (Polygons &polys : projected)
                            for (Polygon &poly : polys)
                                poly.scale(shrink_scale);
                        if (vertical_points)
                            for (size_t i = vp_begin; i < vertical_points->size(); ++i) {
                                (*vertical_points)[i].first.x()  *= float(shrink_scale);
                                (*vertical_points)[i].first.y()  *= float(shrink_scale);
                            }
                    }
                    // Merge these projections with the output, layer by layer.
                    assert(! projected.empty());
                    assert(out.empty() || out.size() == projected.size());
                    if (out.empty())
                        out = std::move(projected);
                    else
                        for (size_t i = 0; i < out.size(); ++ i)
                            append(out[i], std::move(projected[i]));
                }
            }
        }
}

const Layer* PrintObject::get_layer_at_printz(coordf_t print_z) const {
    auto it = Slic3r::lower_bound_by_predicate(m_layers.begin(), m_layers.end(), [print_z](const Layer *layer) { return layer->print_z < print_z; });
    return (it == m_layers.end() || (*it)->print_z != print_z) ? nullptr : *it;
}



Layer* PrintObject::get_layer_at_printz(coordf_t print_z) { return const_cast<Layer*>(std::as_const(*this).get_layer_at_printz(print_z)); }



// Get a layer approximately at print_z.
const Layer* PrintObject::get_layer_at_printz(coordf_t print_z, coordf_t epsilon) const {
    coordf_t limit = print_z - epsilon;
    auto it = Slic3r::lower_bound_by_predicate(m_layers.begin(), m_layers.end(), [limit](const Layer *layer) { return layer->print_z < limit; });
    return (it == m_layers.end() || (*it)->print_z > print_z + epsilon) ? nullptr : *it;
}



Layer* PrintObject::get_layer_at_printz(coordf_t print_z, coordf_t epsilon) { return const_cast<Layer*>(std::as_const(*this).get_layer_at_printz(print_z, epsilon)); }

const Layer *PrintObject::get_first_layer_bellow_printz(coordf_t print_z, coordf_t epsilon) const
{
    coordf_t limit = print_z + epsilon;
    auto it = Slic3r::lower_bound_by_predicate(m_layers.begin(), m_layers.end(), [limit](const Layer *layer) { return layer->print_z < limit; });
    return (it == m_layers.begin()) ? nullptr : *(--it);
}
int PrintObject::get_layer_idx_get_printz(coordf_t print_z, coordf_t epsilon) {
    coordf_t limit = print_z + epsilon;
    auto     it    = Slic3r::lower_bound_by_predicate(m_layers.begin(), m_layers.end(), [limit](const Layer *layer) { return layer->print_z < limit; });
    return (it == m_layers.begin()) ? -1 : std::distance(m_layers.begin(), it);
}
// BBS
const Layer* PrintObject::get_layer_at_bottomz(coordf_t bottom_z, coordf_t epsilon) const {
    coordf_t limit_upper = bottom_z + epsilon;
    coordf_t limit_lower = bottom_z - epsilon;

    for (const Layer* layer : m_layers) {
        if (layer->bottom_z() > limit_lower)
            return layer->bottom_z() < limit_upper ? layer : nullptr;
    }

    return nullptr;
}

Layer* PrintObject::get_layer_at_bottomz(coordf_t bottom_z, coordf_t epsilon) { return const_cast<Layer*>(std::as_const(*this).get_layer_at_bottomz(bottom_z, epsilon)); }


} // namespace Slic3r
