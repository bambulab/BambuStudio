#include "Print.hpp"
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "Layer.hpp"

#include <functional>
#include <optional>

#include <boost/log/trivial.hpp>

#include <tbb/concurrent_unordered_set.h>
#include <tbb/parallel_for.h>

namespace Slic3r {

std::vector<std::set<int>> PrintObject::detect_extruder_geometric_unprintables() const
{
    int extruder_size = m_print->config().nozzle_diameter.size();
    if (extruder_size == 1)
        return std::vector<std::set<int>>(1, std::set<int>());

    std::vector<std::set<int>> geometric_unprintables(extruder_size);

    std::vector<double> printable_height_per_extruder = m_print->config().extruder_printable_height.values;
    assert(printable_height_per_extruder.size() == extruder_size);

    for (size_t extruder_id = 0; extruder_id < printable_height_per_extruder.size(); ++extruder_id) {
        double printable_height = printable_height_per_extruder[extruder_id];
        for (size_t layer_idx = 0; layer_idx < m_layers.size(); ++layer_idx) {
            auto layer = m_layers[layer_idx];
            if (layer->print_z <= printable_height)
                continue;
            for (auto layerm : layer->regions()) {
                auto region                 = layerm->region();
                int  wall_filament          = region.config().wall_filament;
                int  solid_infill_filament  = region.config().solid_infill_filament;
                int  sparse_infill_filament = region.config().sparse_infill_filament;

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

    std::vector<tbb::concurrent_unordered_set<int>> tbb_geometric_unprintables(extruder_size);

    std::vector<Polygons>    unprintable_area_in_obj_coord = m_print->get_extruder_unprintable_polygons();
    std::vector<BoundingBox> unprintable_area_bbox;

    for (auto &polys : unprintable_area_in_obj_coord) {
        for (auto &poly : polys)
            poly.translate(-m_instances.front().shift_without_plate_offset());
        unprintable_area_bbox.emplace_back(get_extents(polys));
    }

    tbb::parallel_for(tbb::blocked_range<int>(0, m_layers.size()),
        [this, &tbb_geometric_unprintables, &unprintable_area_in_obj_coord, &unprintable_area_bbox](const tbb::blocked_range<int> &range) {
            for (int j = range.begin(); j < range.end(); ++j) {
                auto layer = m_layers[j];
                for (auto layerm : layer->regions()) {
                    const auto &region                 = layerm->region();
                    int         wall_filament          = region.config().wall_filament;
                    int         solid_infill_filament  = region.config().solid_infill_filament;
                    int         sparse_infill_filament = region.config().sparse_infill_filament;
                    std::optional<ExPolygons> fill_expolys;
                    BoundingBox               fill_bbox;
                    std::optional<ExPolygons> wall_expolys;
                    BoundingBox               wall_bbox;

                    for (size_t idx = 0; idx < unprintable_area_in_obj_coord.size(); ++idx) {
                        bool do_infill_filament_detect =
                            (solid_infill_filament > 0 && tbb_geometric_unprintables[idx].count(solid_infill_filament - 1) == 0) ||
                            (sparse_infill_filament > 0 && tbb_geometric_unprintables[idx].count(sparse_infill_filament - 1) == 0);

                        bool infill_unprintable = !layerm->fills.entities.empty() &&
                            ((solid_infill_filament > 0 && tbb_geometric_unprintables[idx].count(solid_infill_filament - 1) > 0) ||
                             (sparse_infill_filament > 0 && tbb_geometric_unprintables[idx].count(sparse_infill_filament - 1) > 0));

                        if (!layerm->fills.entities.empty() && do_infill_filament_detect) {
                            if (!fill_expolys) {
                                fill_expolys = layerm->fill_expolygons;
                                fill_bbox    = get_extents(*fill_expolys);
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
                            if (infill_unprintable) {
                                tbb_geometric_unprintables[idx].insert(wall_filament - 1);
                                continue;
                            }

                            if (!wall_expolys) {
                                if (!fill_expolys) {
                                    fill_expolys = layerm->fill_expolygons;
                                    fill_bbox    = get_extents(*fill_expolys);
                                }
                                wall_expolys = diff_ex(layerm->raw_slices, *fill_expolys);
                                wall_bbox    = get_extents(*wall_expolys);
                            }

                            if (wall_bbox.overlap(unprintable_area_bbox[idx]) &&
                                !intersection(*wall_expolys, unprintable_area_in_obj_coord[idx]).empty())
                                tbb_geometric_unprintables[idx].insert(wall_filament - 1);
                        }
                    }
                }
            }
        });

    for (size_t idx = 0; idx < extruder_size; ++idx)
        geometric_unprintables[idx].insert(tbb_geometric_unprintables[idx].begin(), tbb_geometric_unprintables[idx].end());

    return geometric_unprintables;
}

std::unordered_map<int, std::unordered_map<int, double>> PrintObject::calc_estimated_filament_print_time() const
{
    auto                     full_print_config     = this->print()->m_ori_full_print_config;
    std::vector<std::string> extruder_variant_list = this->print()->config().printer_extruder_variant.values;

    const auto *opt_filament_variant     = full_print_config.option<ConfigOptionStrings>("filament_extruder_variant");
    const auto *opt_filament_self_idx    = full_print_config.option<ConfigOptionInts>("filament_self_index");
    const auto *opt_filament_max_vol_spd = full_print_config.option<ConfigOptionFloats>("filament_max_volumetric_speed");
    const auto *opt_filament_flow_ratio  = full_print_config.option<ConfigOptionFloats>("filament_flow_ratio");
    if (!opt_filament_variant || !opt_filament_self_idx || !opt_filament_max_vol_spd || !opt_filament_flow_ratio) {
        BOOST_LOG_TRIVIAL(warning) << "calc_estimated_filament_print_time: filament_* config option missing, skip estimation";
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
            auto *collection = dynamic_cast<ExtrusionEntityCollection *>(entity);
            if (collection) {
                for (auto &sub_entity : collection->entities)
                    process_entity(filament, eidx, sub_entity, speed_from_config);
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

    int extruder_num = this->print()->config().nozzle_diameter.size();
    for (size_t idx = 0; idx < m_layers.size(); ++idx) {
        auto layer = m_layers[idx];

        for (auto layerm : layer->regions()) {
            const auto &region = layerm->region();

            std::vector<ExtrusionEntitiesPtr> entity_list{ layerm->fills.entities, layerm->thin_fills.entities, layerm->perimeters.entities };
            for (int eidx = 0; eidx < extruder_num; ++eidx) {
                for (auto &entities : entity_list) {
                    for (auto &entity : entities) {
                        auto role     = entity->role();
                        int  filament = 0;
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

} // namespace Slic3r
