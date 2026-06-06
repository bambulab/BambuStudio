#include "Print.hpp"

#include "Model.hpp"
#include "PrintConfig.hpp"
#include "Utils.hpp"

#include <unordered_set>

namespace Slic3r {

template class PrintState<PrintStep, psCount>;
template class PrintState<PrintObjectStep, posCount>;

void Print::clear()
{
    std::scoped_lock<std::mutex> lock(this->state_mutex());
    // The following call should stop background processing if it is running.
    this->invalidate_all_steps();
    for (PrintObject *object : m_objects)
        delete object;
    m_objects.clear();
    m_print_regions.clear();
    m_model.clear_objects();
    m_statistics_by_extruder_count.clear();
    m_nozzle_group_result.reset();
}

bool Print::has_tpu_filament() const
{
    for (unsigned int filament_id : m_wipe_tower_data.tool_ordering.all_extruders()) {
        std::string filament_name = m_config.filament_type.get_at(filament_id);
        if (filament_name == "TPU") {
            return true;
        }
    }
    return false;
}

// Called by Print::apply().
// This method only accepts PrintConfig option keys.
bool Print::invalidate_state_by_config_options(const ConfigOptionResolver & /* new_config */, const std::vector<t_config_option_key> &opt_keys)
{
    if (opt_keys.empty())
        return false;

    // Cache the plenty of parameters, which influence the G-code generator only,
    // or they are only notes not influencing the generated G-code.
    static std::unordered_set<std::string> steps_gcode = {
        //BBS
        "additional_cooling_fan_speed",
        "reduce_crossing_wall",
        "max_travel_detour_distance",
        "avoid_crossing_wall_includes_support",
        "printable_area",
        //BBS: add bed_exclude_area
        "bed_exclude_area",
        "thumbnail_size",
        "before_layer_change_gcode",
        "enable_pressure_advance",
        "pressure_advance",
        "enable_overhang_bridge_fan",
        "overhang_fan_speed",
        "ironing_fan_speed",
        "pre_start_fan_time",
        "overhang_fan_threshold",
        "overhang_threshold_participating_cooling",
        "slow_down_for_layer_cooling",
        "no_slow_down_for_cooling_on_outwalls",
        "deretraction_speed",
        "close_fan_the_first_x_layers",
        "first_x_layer_part_fan_speed",
        "close_additional_fan_first_x_layers",
        "additional_fan_full_speed_layer",
        "first_x_layer_fan_speed",
        "machine_end_gcode",
        "printing_by_object_gcode",
        "filament_end_gcode",
        "post_process",
        "extruder_clearance_height_to_rod",
        "extruder_clearance_height_to_lid",
        "extruder_clearance_dist_to_rod",
        "nozzle_height",
        "extruder_clearance_max_radius",
        "extruder_colour",
        "extruder_offset",
        "filament_flow_ratio",
        "reduce_fan_stop_start_freq",
        "fan_cooling_layer_time",
        "full_fan_speed_layer",
        "filament_colour",
        "default_filament_colour",
        "filament_diameter",
         "volumetric_speed_coefficients",
        "filament_density",
        "filament_cost",
        "initial_layer_acceleration",
        "outer_wall_acceleration",
        "top_surface_acceleration",
        "accel_to_decel_enable",
        "accel_to_decel_factor",
        // BBS
        "supertack_plate_temp_initial_layer",
        "cool_plate_temp_initial_layer",
        "eng_plate_temp_initial_layer",
        "hot_plate_temp_initial_layer",
        "textured_plate_temp_initial_layer",
        "gcode_add_line_number",
        "layer_change_gcode",
        "time_lapse_gcode",
        "wrapping_detection_gcode",
        "fan_min_speed",
        "fan_max_speed",
        "printable_height",
        "slow_down_min_speed",
#ifdef HAS_PRESSURE_EQUALIZER
        "max_volumetric_extrusion_rate_slope_positive",
        "max_volumetric_extrusion_rate_slope_negative",
#endif /* HAS_PRESSURE_EQUALIZER */
        "reduce_infill_retraction_mode",
        "filename_format",
        "retraction_minimum_travel",
        "retract_before_wipe",
        "retract_when_changing_layer",
        "retraction_length",
        "retract_length_toolchange",
        "z_hop",
        "filament_retract_length_nc",
        "retract_restart_extra",
        "retract_restart_extra_toolchange",
        "retraction_speed",
        "retract_lift_above",
        "retract_lift_below",
        "slow_down_layer_time",
        "standby_temperature_delta",
        "machine_start_gcode",
        "filament_start_gcode",
        "change_filament_gcode",
        "wipe",
        // BBS
        "wipe_distance",
        "curr_bed_type",
        "nozzle_volume",
        "chamber_temperatures",
        "required_nozzle_HRC",
        "upward_compatible_machine",
        "is_infill_first",
        //OrcaSlicer
        "seam_gap",
        "wipe_speed"
        "default_jerk",
        "outer_wall_jerk",
        "inner_wall_jerk",
        "infill_jerk",
        "top_surface_jerk",
        "initial_layer_jerk",
        "travel_jerk",
        "inner_wall_acceleration",
        "sparse_infill_acceleration",
        "exclude_object",
        "print_in_clockwise",
        "use_relative_e_distances",
        "activate_air_filtration",
        "during_print_exhaust_fan_speed",
        "complete_print_exhaust_fan_speed",
        "use_firmware_retraction",
        "enable_long_retraction_when_cut",
        "long_retractions_when_cut",
        "retraction_distances_when_cut",
        "filament_long_retractions_when_cut",
        "filament_retraction_distances_when_cut",
        "grab_length",
        "bed_temperature_formula",
        "filament_notes",
        "process_notes",
        "printer_notes",
        "filament_velocity_adaptation_factor",
        "filament_tower_interface_purge_volume",
    };

    static std::unordered_set<std::string> steps_ignore;

    std::vector<PrintStep>       steps;
    std::vector<PrintObjectStep> osteps;
    bool                         invalidated = false;

    for (const t_config_option_key &opt_key : opt_keys) {
        if (steps_gcode.find(opt_key) != steps_gcode.end()) {
            // These options only affect G-code export or they are just notes without influence on the generated G-code,
            // so there is nothing to invalidate.
            steps.emplace_back(psGCodeExport);
        } else if (steps_ignore.find(opt_key) != steps_ignore.end()) {
            // These steps have no influence on the G-code whatsoever. Just ignore them.
        } else if (
               opt_key == "skirt_loops"
            || opt_key == "skirt_height"
            || opt_key == "draft_shield"
            || opt_key == "skirt_distance"
            || opt_key == "skirt_per_object"
            || opt_key == "ooze_prevention"
            || opt_key == "wipe_tower_x"
            || opt_key == "wipe_tower_y"
            || opt_key == "wipe_tower_rotation_angle") {
            steps.emplace_back(psSkirtBrim);
        } else if (
               opt_key == "initial_layer_print_height"
            || opt_key == "nozzle_diameter"
            || opt_key == "resolution"
            || opt_key == "precise_z_height"
            || opt_key == "filament_shrink"
            || opt_key == "enable_circle_compensation"
            || opt_key == "circle_compensation_manual_offset"
            || opt_key == "circle_compensation_speed"
            || opt_key == "counter_coef_1"
            || opt_key == "counter_coef_2"
            || opt_key == "counter_coef_3"
            || opt_key == "hole_coef_1"
            || opt_key == "hole_coef_2"
            || opt_key == "hole_coef_3"
            || opt_key == "counter_limit_min"
            || opt_key == "counter_limit_max"
            || opt_key == "hole_limit_min"
            || opt_key == "hole_limit_max"
            || opt_key == "diameter_limit"
            // Spiral Vase forces different kind of slicing than the normal model:
            // In Spiral Vase mode, holes are closed and only the largest area contour is kept at each layer.
            // Therefore toggling the Spiral Vase on / off requires complete reslicing.
            || opt_key == "spiral_mode"
            || opt_key == "enable_order_independent_overlap_carving") {
            osteps.emplace_back(posSlice);
        } else if (
               opt_key == "print_sequence"
            || opt_key == "filament_type"
            || opt_key == "nozzle_temperature_initial_layer"
            || opt_key == "filament_minimal_purge_on_wipe_tower"
            || opt_key == "filament_max_volumetric_speed"
            || opt_key == "filament_adaptive_volumetric_speed"
            || opt_key == "filament_ramming_volumetric_speed"
            || opt_key == "filament_ramming_volumetric_speed_nc"
            || opt_key == "gcode_flavor"
            || opt_key == "single_extruder_multi_material"
            || opt_key == "nozzle_temperature"
            || opt_key == "filament_pre_cooling_temperature"
            || opt_key == "filament_pre_cooling_temperature_nc"
            || opt_key == "filament_ramming_travel_time"
            || opt_key == "filament_ramming_travel_time_nc"
            // BBS
            || opt_key == "supertack_plate_temp"
            || opt_key == "cool_plate_temp"
            || opt_key == "eng_plate_temp"
            || opt_key == "hot_plate_temp"
            || opt_key == "textured_plate_temp"
            || opt_key == "enable_prime_tower"
            || opt_key == "enable_wrapping_detection"
            || opt_key == "prime_tower_enable_framework"
            || opt_key == "prime_tower_width"
            || opt_key == "prime_tower_max_speed"
            || opt_key == "prime_tower_lift_speed"
            || opt_key == "prime_tower_lift_height"
            || opt_key == "prime_tower_brim_width"
            || opt_key == "prime_tower_skip_points"
            || opt_key == "prime_tower_flat_ironing"
            || opt_key == "prime_tower_rib_wall"
            || opt_key == "prime_tower_extra_rib_length"
            || opt_key == "prime_tower_rib_width"
            || opt_key == "prime_tower_fillet_wall"
            || opt_key == "first_layer_print_sequence"
            || opt_key == "other_layers_print_sequence"
            || opt_key == "other_layers_print_sequence_nums"
            || opt_key == "extruder_ams_count"
            || opt_key == "extruder_nozzle_stats"
            || opt_key == "enable_filament_dynamic_map"
            || opt_key == "filament_cooling_before_tower"
            || opt_key == "enable_tower_interface_features"
            || opt_key == "filament_tower_ironing_area"
            || opt_key == "filament_tower_interface_print_temp"
            || opt_key == "filament_tower_interface_pre_extrusion_dist"
            || opt_key == "filament_tower_interface_pre_extrusion_length"
            || opt_key == "prime_volume_mode"
            || opt_key == "filament_map_mode"
            || opt_key == "filament_map"
            || opt_key == "filament_nozzle_map"
            || opt_key == "filament_volume_map"
            || opt_key == "filament_adhesiveness_category"
            //|| opt_key == "wipe_tower_bridging"
            || opt_key == "wipe_tower_no_sparse_layers"
            || opt_key == "flush_volumes_matrix"
            || opt_key == "filament_prime_volume"
            || opt_key == "filament_prime_volume_nc"
            || opt_key == "flush_into_infill"
            || opt_key == "flush_into_support"
            || opt_key == "initial_layer_infill_speed"
            || opt_key == "travel_speed"
            || opt_key == "travel_speed_z"
            || opt_key == "initial_layer_speed"
            || opt_key == "default_acceleration"
            || opt_key == "travel_acceleration"
            || opt_key == "initial_layer_travel_acceleration") {
            //|| opt_key == "z_offset") {
            steps.emplace_back(psWipeTower);
            steps.emplace_back(psSkirtBrim);
        } else if (opt_key == "filament_soluble"
                || opt_key == "filament_is_support"
                || opt_key == "filament_printable"
                || opt_key == "impact_strength_z"
                || opt_key == "filament_scarf_seam_type"
                || opt_key == "filament_scarf_height"
                || opt_key == "filament_scarf_gap"
                || opt_key == "filament_scarf_length"
                || opt_key == "filament_change_length"
                || opt_key == "independent_support_layer_height"
                || opt_key == "top_z_overrides_xy_distance"
                || opt_key == "filament_change_length_nc") {
            steps.emplace_back(psWipeTower);
            // Soluble support interface / non-soluble base interface produces non-soluble interface layers below soluble interface layers.
            // Thus switching between soluble / non-soluble interface layer material may require recalculation of supports.
            //FIXME Killing supports on any change of "filament_soluble" is rough. We should check for each object whether that is necessary.
            osteps.emplace_back(posSupportMaterial);
            osteps.emplace_back(posSimplifySupportPath);
        } else if (
               opt_key == "initial_layer_line_width"
            || opt_key == "min_layer_height"
            || opt_key == "max_layer_height"
            //|| opt_key == "resolution"
            //BBS: when enable arc fitting, we must re-generate perimeter
            || opt_key == "enable_arc_fitting"
            || opt_key == "wall_sequence"
            || opt_key == "z_direction_outwall_speed_continuous"
            || opt_key == "override_filament_scarf_seam_setting"
            || opt_key == "seam_slope_type"
            || opt_key == "seam_slope_start_height"
            || opt_key == "seam_slope_gap"
            || opt_key == "seam_slope_min_length"
            || opt_key == "embedding_wall_into_infill"
            || opt_key == "alternate_extra_wall") {
            osteps.emplace_back(posPerimeters);
            osteps.emplace_back(posInfill);
            osteps.emplace_back(posSupportMaterial);
            osteps.emplace_back(posSimplifyWall);
            osteps.emplace_back(posSimplifyInfill);
            osteps.emplace_back(posSimplifySupportPath);
            steps.emplace_back(psSkirtBrim);
        } else if (opt_key == "z_hop_types") {
            osteps.emplace_back(posDetectOverhangsForLift);
        } else {
            // for legacy, if we can't handle this option let's invalidate all steps
            //FIXME invalidate all steps of all objects as well?
            invalidated |= this->invalidate_all_steps();
            // Continue with the other opt_keys to possibly invalidate any object specific steps.
        }
    }

    sort_remove_duplicates(steps);
    for (PrintStep step : steps)
        invalidated |= this->invalidate_step(step);
    sort_remove_duplicates(osteps);
    for (PrintObjectStep ostep : osteps)
        for (PrintObject *object : m_objects)
            invalidated |= object->invalidate_step(ostep);

    return invalidated;
}

void Print::set_calib_params(const Calib_Params &params)
{
    m_calib_params = params;
}

bool Print::invalidate_step(PrintStep step)
{
    bool invalidated = Inherited::invalidate_step(step);
    // Propagate to dependent steps.
    if (step != psGCodeExport)
        invalidated |= Inherited::invalidate_step(psGCodeExport);
    return invalidated;
}

// returns true if an object step is done on all objects
// and there's at least one object
bool Print::is_step_done(PrintObjectStep step) const
{
    if (m_objects.empty())
        return false;
    std::scoped_lock<std::mutex> lock(this->state_mutex());
    for (const PrintObject *object : m_objects)
        if (!object->is_step_done_unguarded(step))
            return false;
    return true;
}

} // namespace Slic3r
