#include "Print.hpp"

#include "GCode.hpp"
#include "I18N.hpp"
#include "Model.hpp"
#include "PrintConfig.hpp"
#include "Support/SupportMaterial.hpp"

#include <limits>

#include <boost/log/trivial.hpp>

#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

// BBS
static StringObjectException layered_print_cleareance_valid(const Print &print, StringObjectException *warning)
{
    std::vector<const PrintInstance *> print_instances_ordered = sort_object_instances_by_model_order(print, true);
    if (print_instances_ordered.size() < 1)
        return {};

    auto print_config = print.config();
    Pointfs excluse_area_points = print_config.bed_exclude_area.values;
    Polygons exclude_polys;
    Polygon exclude_poly;
    const Vec3d print_origin = print.get_plate_origin();
    for (int i = 0; i < excluse_area_points.size(); i++) {
        auto pt = excluse_area_points[i];
        exclude_poly.points.emplace_back(scale_(pt.x() + print_origin.x()), scale_(pt.y() + print_origin.y()));
        if (i % 4 == 3) {
            exclude_polys.push_back(exclude_poly);
            exclude_poly.points.clear();
        }
    }

    Pointfs wrapping_detection_area = print_config.wrapping_exclude_area.values;
    Polygon wrapping_poly;
    for (size_t i = 0; i < wrapping_detection_area.size(); ++i) {
        auto pt = wrapping_detection_area[i];
        wrapping_poly.points.emplace_back(scale_(pt.x() + print_origin.x()), scale_(pt.y() + print_origin.y()));
    }

    std::map<const ModelVolume *, Polygon> map_model_volume_to_convex_hull;
    Polygons convex_hulls_other;
    for (auto &inst : print_instances_ordered) {
        for (const ModelVolume *v : inst->print_object->model_object()->volumes) {
            if (! v->is_model_part()) continue;
            auto it_convex_hull = map_model_volume_to_convex_hull.find(v);
            if (it_convex_hull == map_model_volume_to_convex_hull.end()) {
                auto volume_hull = v->get_convex_hull_2d(Geometry::assemble_transform(Vec3d::Zero(), inst->model_instance->get_rotation(),
                                                                                      inst->model_instance->get_scaling_factor(), inst->model_instance->get_mirror()));
                volume_hull.translate(inst->shift - inst->print_object->center_offset());

                it_convex_hull = map_model_volume_to_convex_hull.emplace_hint(it_convex_hull, v, volume_hull);
            }
            Polygon &convex_hull = it_convex_hull->second;
            Polygons convex_hulls_temp;
            convex_hulls_temp.push_back(convex_hull);
            if (! intersection(exclude_polys, convex_hull).empty()) {
                return {inst->model_instance->get_object()->name + L(" is too close to exclusion area, there may be collisions when printing.") + "\n",
                        inst->model_instance->get_object()};
            }

            if (print_config.enable_wrapping_detection.value && ! intersection(wrapping_poly, convex_hull).empty()) {
                return {inst->model_instance->get_object()->name + L(" is too close to clumping detection area, there may be collisions when printing.") + "\n",
                        inst->model_instance->get_object()};
            }
            convex_hulls_other.emplace_back(convex_hull);
        }
    }

    const PrintConfig &config = print.config();
    int                filaments_count = print.extruders().size();
    int                plate_index     = print.get_plate_index();
    const Vec3d        plate_origin    = print.get_plate_origin();
    float              x               = config.wipe_tower_x.get_at(plate_index) + plate_origin(0);
    float              y               = config.wipe_tower_y.get_at(plate_index) + plate_origin(1);
    float              width           = config.prime_tower_width.value;
    float              a               = config.wipe_tower_rotation_angle.value;

    float depth = print.wipe_tower_data(filaments_count).depth;

    if (config.prime_tower_rib_wall.value)
        width = depth;

    Polygons convex_hulls_temp;
    if (print.has_wipe_tower()) {
        if (! print.is_step_done(psWipeTower)) {
            Polygon wipe_tower_convex_hull;
            wipe_tower_convex_hull.points.emplace_back(scale_(x), scale_(y));
            wipe_tower_convex_hull.points.emplace_back(scale_(x + width), scale_(y));
            wipe_tower_convex_hull.points.emplace_back(scale_(x + width), scale_(y + depth));
            wipe_tower_convex_hull.points.emplace_back(scale_(x), scale_(y + depth));
            wipe_tower_convex_hull.rotate(a);
            convex_hulls_temp.push_back(wipe_tower_convex_hull);
        } else {
            Polygon wipe_tower_polygon;
            if (print.wipe_tower_data().wipe_tower_mesh_data)
                wipe_tower_polygon = print.wipe_tower_data().wipe_tower_mesh_data->bottom;
            wipe_tower_polygon.translate(Point(scale_(x), scale_(y)));
            convex_hulls_temp.push_back(wipe_tower_polygon);
        }
    }
    if (! intersection(convex_hulls_other, convex_hulls_temp).empty()) {
        if (warning) {
            warning->string += L("Prime Tower") + L(" is too close to others, and collisions may be caused.\n");
        }
    }
    if (! intersection(exclude_polys, convex_hulls_temp).empty()) {
        return {L("Prime Tower") + L(" is too close to exclusion area, and collisions will be caused.\n")};
    }
    if (print_config.enable_wrapping_detection.value && ! intersection({wrapping_poly}, convex_hulls_temp).empty()) {
        return {L("Prime Tower") + L(" is too close to clumping detection area, and collisions will be caused.\n")};
    }
    return {};
}

// Precondition: Print::validate() requires the Print::apply() to be called its invocation.
StringObjectException Print::validate(StringObjectException *warning, Polygons *collison_polygons, std::vector<std::pair<Polygon, float>> *height_polygons) const
{
    std::vector<unsigned int> extruders = this->extruders();
    if (extruders.empty())
        return {};

    auto ret = check_multi_filament_valid(*this);
    if (! ret.is_warning) {
        if (! ret.string.empty())
            return ret;
    } else if (warning && warning->string.empty()) {
        *warning = ret;
    }

    if (m_config.print_sequence == PrintSequence::ByObject) {
        auto ret = sequential_print_clearance_valid(*this, collison_polygons, height_polygons);
        if (! ret.string.empty())
            return ret;
    } else {
        for (const PrintObject *print_object : print_objects()) {
            if (print_object->model_object()->is_too_large()) {
                StringObjectException ret;
                ret.string           = (boost::format(L("%1% is too large, there may be collisions when printing.")) % print_object->model_object()->name).str();
                ret.object           = print_object->model_object();
                return ret;
            }
        }
    }

    if (m_config.enable_prime_tower && m_config.prime_tower_with_more_brush && this->is_timelapse_print())
        return {L("Prime tower with more flush is not supported when timelapse smooth is enabled."), nullptr, "prime_tower_with_more_brush"};
    if (this->is_timelapse_print() && m_config.vibration_compensation == vcAuto)
        return {L("Auto vibration compensation is not supported when timelapse smooth is enabled."), nullptr, "vibration_compensation"};

    {
        auto ret = layered_print_cleareance_valid(*this, warning);
        if (! ret.string.empty())
            return ret;
    }

    if (m_config.spiral_mode) {
        for (const PrintObject *print_object : print_objects()) {
            if (print_object->all_regions().size() > 1)
                return {L("Spiral mode only works when an object contains a single material."), nullptr, "spiral_mode"};
        }
    }

    std::vector<std::vector<coordf_t>> layer_height_profiles;
    auto layer_height_profile = [this, &layer_height_profiles](const size_t print_object_idx) -> const std::vector<coordf_t>& {
        const PrintObject &print_object = *m_objects[print_object_idx];
        if (layer_height_profiles.empty())
            layer_height_profiles.assign(m_objects.size(), std::vector<coordf_t>());
        std::vector<coordf_t> &profile = layer_height_profiles[print_object_idx];
        if (profile.empty()) {
            bool nozzle_range_reset = false;
            PrintObject::update_layer_height_profile(*print_object.model_object(), print_object.slicing_parameters(), profile, nozzle_range_reset);
        }
        return profile;
    };

    for (size_t print_object_idx = 0; print_object_idx < m_objects.size(); ++print_object_idx) {
        PrintObject &print_object = *m_objects[print_object_idx];
        print_object.has_variable_layer_heights = false;
        if (print_object.has_support_material() && is_tree(print_object.config().support_type.value) &&
            print_object.model_object()->has_custom_layering()) {
            if (const std::vector<coordf_t> &layers = layer_height_profile(print_object_idx); ! layers.empty())
                if (! check_object_layers_fixed(print_object.slicing_parameters(), layers)) {
                    print_object.has_variable_layer_heights = true;
                    BOOST_LOG_TRIVIAL(warning) << "print_object: " << print_object.model_object()->name
                                               << " has_variable_layer_heights: " << print_object.has_variable_layer_heights;
                    if (print_object.config().support_style.value == smsTreeOrganic) return {L("Variable layer height is not supported with Organic supports.")};
                }
        }
    }

    if (this->has_wipe_tower() && ! m_objects.empty()) {
        double first_nozzle_diam   = m_config.nozzle_diameter.get_at(extruders.front());
        double first_filament_diam = m_config.filament_diameter.get_at(extruders.front());
        for (const auto &extruder_idx : extruders) {
            double nozzle_diam   = m_config.nozzle_diameter.get_at(extruder_idx);
            double filament_diam = m_config.filament_diameter.get_at(extruder_idx);
            if (nozzle_diam - EPSILON > first_nozzle_diam || nozzle_diam + EPSILON < first_nozzle_diam
                || std::abs((filament_diam - first_filament_diam) / first_filament_diam) > 0.1)
                return {L("Different nozzle diameters and different filament diameters is not allowed when prime tower is enabled.")};
        }

        if (! m_config.use_relative_e_distances)
            return {L("The Wipe Tower is currently only supported with the relative extruder addressing (use_relative_e_distances=1).")};
        if (m_config.ooze_prevention)
            return {L("Ooze prevention is currently not supported with the prime tower enabled.")};

        if (m_objects.size() > 1) {
            bool has_custom_layering = std::any_of(m_objects.begin(), m_objects.end(),
                [](const PrintObject *object) { return object->model_object()->has_custom_layering(); });

            const SlicingParameters &slicing_params0 = m_objects.front()->slicing_parameters();
            size_t                   tallest_object_idx = 0;
            for (size_t i = 1; i < m_objects.size(); ++i) {
                const PrintObject       *object         = m_objects[i];
                const SlicingParameters &slicing_params = object->slicing_parameters();
                if (std::abs(slicing_params.first_print_layer_height - slicing_params0.first_print_layer_height) > EPSILON ||
                    std::abs(slicing_params.layer_height - slicing_params0.layer_height) > EPSILON)
                    return {L("The prime tower requires that all objects have the same layer heights"), object, "initial_layer_print_height"};
                if (slicing_params.raft_layers() != slicing_params0.raft_layers())
                    return {L("The prime tower requires that all objects are printed over the same number of raft layers"), object, "raft_layers"};
                if (! equal_layering(slicing_params, slicing_params0))
                    return {L("The prime tower requires that all objects are sliced with the same layer heights."), object};
                if (has_custom_layering) {
                    auto &lh         = layer_height_profile(i);
                    auto &lh_tallest = layer_height_profile(tallest_object_idx);
                    if (*(lh.end() - 2) > *(lh_tallest.end() - 2))
                        tallest_object_idx = i;
                }
            }

            if (has_custom_layering) {
                std::vector<std::vector<coordf_t>> layer_z_series;
                layer_z_series.assign(m_objects.size(), std::vector<coordf_t>());

                for (size_t idx_object = 0; idx_object < m_objects.size(); ++idx_object)
                    layer_z_series[idx_object] = generate_object_layers(m_objects[idx_object]->slicing_parameters(), layer_height_profiles[idx_object], m_objects[idx_object]->config().precise_z_height.value);

                for (size_t idx_object = 0; idx_object < m_objects.size(); ++idx_object) {
                    if (idx_object == tallest_object_idx) continue;
                    size_t         i   = 0;
                    const coordf_t eps = 0.5 * EPSILON;
                    while (i < layer_z_series[idx_object].size() && i < layer_z_series[tallest_object_idx].size()) {
                        if (std::abs(layer_z_series[idx_object][i] - layer_z_series[tallest_object_idx][i]) > eps)
                            return {L("The prime tower is only supported if all objects have the same variable layer height")};
                        ++i;
                    }
                }
            }
        }
    }

    {
        double min_nozzle_diameter = std::numeric_limits<double>::max();
        double max_nozzle_diameter = 0;
        for (unsigned int extruder_id : extruders) {
            double dmr = m_config.nozzle_diameter.get_at(extruder_id);
            min_nozzle_diameter = std::min(min_nozzle_diameter, dmr);
            max_nozzle_diameter = std::max(max_nozzle_diameter, dmr);
        }

        auto validate_extrusion_width = [max_nozzle_diameter](const ConfigBase &config, const char *opt_key, double layer_height, std::string &err_msg) -> bool {
            double extrusion_width_min = config.get_abs_value(opt_key);
            double extrusion_width_max = config.get_abs_value(opt_key);
            if (extrusion_width_min == 0) {
            } else if (extrusion_width_min <= layer_height) {
                err_msg = L("Too small line width");
                return false;
            } else if (extrusion_width_max > max_nozzle_diameter * 2.5) {
                err_msg = L("Too large line width");
                return false;
            }
            return true;
        };
        for (PrintObject *object : m_objects) {
            if (object->has_support_material()) {
            }

            if (! object->has_support() && warning) {
                for (const ModelVolume *mv : object->model_object()->volumes) {
                    bool has_enforcers = mv->is_support_enforcer() ||
                        (mv->is_model_part() && mv->supported_facets.has_facets(*mv, EnforcerBlockerType::ENFORCER));
                    if (has_enforcers) {
                        StringObjectException warningtemp;
                        warningtemp.string = L("Support enforcers are used but support is not enabled. Please enable support.");
                        warningtemp.object = object;
                        *warning = warningtemp;
                        break;
                    }
                }
            }

            double initial_layer_print_height = m_config.initial_layer_print_height.value;
            double first_layer_min_nozzle_diameter;
            if (object->has_raft()) {
                size_t first_layer_extruder = object->config().raft_layers == 1
                    ? object->config().support_interface_filament - 1
                    : object->config().support_filament - 1;
                first_layer_min_nozzle_diameter = (first_layer_extruder == size_t(-1)) ?
                    min_nozzle_diameter :
                    m_config.nozzle_diameter.get_at(first_layer_extruder);
            } else {
                first_layer_min_nozzle_diameter = min_nozzle_diameter;
            }
            if (initial_layer_print_height > first_layer_min_nozzle_diameter)
                return {L("Layer height cannot exceed nozzle diameter"), object, "initial_layer_print_height"};

            double layer_height = object->config().layer_height.value;
            if (layer_height > min_nozzle_diameter)
                return {L("Layer height cannot exceed nozzle diameter"), object, "layer_height"};

            std::string err_msg;
            if (! validate_extrusion_width(object->config(), "line_width", layer_height, err_msg))
                return {err_msg, object, "line_width"};
            if (object->has_support() || object->has_raft()) {
                if (! validate_extrusion_width(object->config(), "support_line_width", layer_height, err_msg))
                    return {err_msg, object, "support_line_width"};
            }
            for (const char *opt_key : { "inner_wall_line_width", "outer_wall_line_width", "sparse_infill_line_width", "internal_solid_infill_line_width", "top_surface_line_width", "skin_infill_line_width", "skeleton_infill_line_width" })
                for (const PrintRegion &region : object->all_regions())
                    if (! validate_extrusion_width(region.config(), opt_key, layer_height, err_msg))
                        return {err_msg, object, opt_key};
        }
    }

    const ConfigOptionDef *bed_type_def = print_config_def.get("curr_bed_type");
    assert(bed_type_def != nullptr);

    const t_config_enum_values *bed_type_keys_map = bed_type_def->enum_keys_map;
    for (unsigned int extruder_id : extruders) {
        const ConfigOptionInts *bed_temp_opt = m_config.option<ConfigOptionInts>(get_bed_temp_key(m_config.curr_bed_type));
        for (unsigned int extruder_id2 : extruders) {
            int curr_bed_temp = bed_temp_opt->get_at(extruder_id2);
            if (curr_bed_temp == 0 && bed_type_keys_map != nullptr) {
                std::string bed_type_name;
                for (auto item : *bed_type_keys_map) {
                    if (item.second == m_config.curr_bed_type) {
                        bed_type_name = item.first;
                        break;
                    }
                }

                StringObjectException except;
                except.string = Slic3r::format(L("Plate %d: %s does not support filament %s"), this->get_plate_index() + 1, L(bed_type_name), extruder_id2 + 1);
                except.string += "\n";
                except.type   = STRING_EXCEPT_FILAMENT_NOT_MATCH_BED_TYPE;
                except.params.push_back(std::to_string(this->get_plate_index() + 1));
                except.params.push_back(L(bed_type_name));
                except.params.push_back(std::to_string(extruder_id2 + 1));
                except.object = nullptr;
                return except;
            }
        }

        if (m_default_region_config.precise_outer_wall && m_default_region_config.wall_sequence != WallSequence::InnerOuter) {
            warning->string  = L("Precise outer wall will be ignored unless the order of walls is set to inner/outer");
            warning->opt_key = "precise_outer_wall";
        }
    }

    return {};
}

} // namespace Slic3r
