#include "Print.hpp"

namespace Slic3r {

bool PrintObject::invalidate_state_by_config_options(
    const ConfigOptionResolver &old_config, const ConfigOptionResolver &new_config, const std::vector<t_config_option_key> &opt_keys)
{
    if (opt_keys.empty())
        return false;

    std::vector<PrintObjectStep> steps;
    bool                         invalidated = false;
    for (const t_config_option_key &opt_key : opt_keys) {
        if (opt_key == "brim_width" || opt_key == "brim_object_gap" || opt_key == "brim_type" || opt_key == "outer_wall_speed" ||
            opt_key == "sparse_infill_speed" || opt_key == "inner_wall_speed" || opt_key == "support_speed" ||
            opt_key == "internal_solid_infill_speed" || opt_key == "top_surface_speed") {
            steps.emplace_back(posSupportMaterial);
            if (opt_key == "brim_type") {
                const auto *old_brim_type = old_config.option<ConfigOptionEnum<BrimType>>(opt_key);
                const auto *new_brim_type = new_config.option<ConfigOptionEnum<BrimType>>(opt_key);
                if (old_brim_type->value == btOuterOnly || new_brim_type->value == btOuterOnly)
                    steps.emplace_back(posPerimeters);
            }
        } else if (
            opt_key == "wall_loops" || opt_key == "top_one_wall_type" || opt_key == "top_area_threshold" ||
            opt_key == "only_one_wall_first_layer" || opt_key == "initial_layer_line_width" || opt_key == "inner_wall_line_width" ||
            opt_key == "infill_wall_overlap" || opt_key == "apply_scarf_seam_on_circles") {
            steps.emplace_back(posPerimeters);
        } else if (opt_key == "gap_infill_speed" || opt_key == "filter_out_gap_fill") {
            auto is_gap_fill_changed_state_due_to_speed = [&opt_key, &old_config, &new_config]() -> bool {
                if (opt_key == "gap_infill_speed") {
                    const auto *old_gap_fill_speed = old_config.option<ConfigOptionFloatsNullable>(opt_key);
                    const auto *new_gap_fill_speed = new_config.option<ConfigOptionFloatsNullable>(opt_key);
                    assert(old_gap_fill_speed && new_gap_fill_speed);
                    return (old_gap_fill_speed->values.size() != new_gap_fill_speed->values.size()) || (old_gap_fill_speed->values != new_gap_fill_speed->values);
                }
                return false;
            };

            if (this->is_mm_painted() && ((opt_key == "gap_infill_speed" || opt_key == "filter_out_gap_fill") && is_gap_fill_changed_state_due_to_speed()))
                steps.emplace_back(posSlice);
            steps.emplace_back(posPerimeters);
        } else if (
            opt_key == "layer_height" || opt_key == "mmu_segmented_region_max_width" || opt_key == "mmu_segmented_region_interlocking_depth" ||
            opt_key == "raft_layers" || opt_key == "raft_contact_distance" || opt_key == "slice_closing_radius" || opt_key == "slicing_mode" ||
            opt_key == "interlocking_beam" || opt_key == "interlocking_orientation" || opt_key == "interlocking_beam_layer_count" ||
            opt_key == "interlocking_depth" || opt_key == "interlocking_boundary_avoidance" || opt_key == "interlocking_beam_width") {
            steps.emplace_back(posSlice);
        } else if (
            opt_key == "elefant_foot_compensation" || opt_key == "support_top_z_distance" || opt_key == "support_bottom_z_distance" ||
            opt_key == "xy_hole_compensation" || opt_key == "xy_contour_compensation") {
            steps.emplace_back(posSlice);
        } else if (opt_key == "enable_support") {
            steps.emplace_back(posSupportMaterial);
            if (m_config.support_top_z_distance == 0.) {
                steps.emplace_back(posSlice);
            }
        } else if (
            opt_key == "support_type" || opt_key == "support_angle" || opt_key == "support_on_build_plate_only" ||
            opt_key == "support_critical_regions_only" || opt_key == "support_remove_small_overhang" || opt_key == "enforce_support_layers" ||
            opt_key == "support_filament" || opt_key == "support_line_width" || opt_key == "support_interface_top_layers" ||
            opt_key == "support_interface_bottom_layers" || opt_key == "support_interface_pattern" || opt_key == "support_interface_loop_pattern" ||
            opt_key == "support_interface_filament" || opt_key == "support_interface_not_for_body" || opt_key == "support_interface_spacing" ||
            opt_key == "support_bottom_interface_spacing" || opt_key == "support_base_pattern" || opt_key == "support_style" ||
            opt_key == "support_object_xy_distance" || opt_key == "support_object_first_layer_gap" || opt_key == "support_base_pattern_spacing" ||
            opt_key == "support_expansion" || opt_key == "support_threshold_angle" || opt_key == "enable_support_ironing" ||
            opt_key == "support_ironing_pattern" || opt_key == "support_ironing_flow" || opt_key == "support_ironing_spacing" ||
            opt_key == "support_ironing_inset" || opt_key == "support_ironing_direction" || opt_key == "support_ironing_speed" ||
            opt_key == "raft_expansion" || opt_key == "raft_first_layer_density" || opt_key == "raft_first_layer_expansion" ||
            opt_key == "bridge_no_support" || opt_key == "max_bridge_length" || opt_key == "initial_layer_line_width" ||
            opt_key == "tree_support_branch_distance" || opt_key == "tree_support_branch_diameter" || opt_key == "tree_support_branch_angle" ||
            opt_key == "tree_support_branch_diameter_angle" || opt_key == "tree_support_wall_count") {
            steps.emplace_back(posSupportMaterial);
        } else if (
            opt_key == "bottom_shell_layers" || opt_key == "top_shell_layers" || opt_key == "top_color_penetration_layers" ||
            opt_key == "bottom_color_penetration_layers" || opt_key == "infill_instead_top_bottom_surfaces") {
            steps.emplace_back(posSlice);
        } else if (
            opt_key == "interface_shells" || opt_key == "fill_multiline" || opt_key == "infill_combination" || opt_key == "bottom_shell_thickness" ||
            opt_key == "top_shell_thickness" || opt_key == "minimum_sparse_infill_area" || opt_key == "sparse_infill_filament" ||
            opt_key == "solid_infill_filament" || opt_key == "sparse_infill_line_width" || opt_key == "skin_infill_line_width" ||
            opt_key == "skeleton_infill_line_width" || opt_key == "infill_direction" || opt_key == "ensure_vertical_shell_thickness" ||
            opt_key == "bridge_angle") {
            steps.emplace_back(posPrepareInfill);
        } else if (
            opt_key == "top_surface_pattern" || opt_key == "top_surface_density" || opt_key == "monotonic_travel_into_wall" ||
            opt_key == "bottom_surface_pattern" || opt_key == "bottom_surface_density" || opt_key == "internal_solid_infill_pattern" ||
            opt_key == "external_fill_link_max_length" || opt_key == "sparse_infill_anchor" || opt_key == "sparse_infill_anchor_max" ||
            opt_key == "top_surface_line_width" || opt_key == "initial_layer_line_width" || opt_key == "detect_floating_vertical_shell") {
            steps.emplace_back(posInfill);
        } else if (
            opt_key == "sparse_infill_pattern" || opt_key == "symmetric_infill_y_axis" || opt_key == "infill_shift_step" ||
            opt_key == "sparse_infill_lattice_angle_1" || opt_key == "sparse_infill_lattice_angle_2" || opt_key == "infill_rotate_step" ||
            opt_key == "skeleton_infill_density" || opt_key == "skin_infill_density" || opt_key == "infill_lock_depth" ||
            opt_key == "skin_infill_depth" || opt_key == "locked_skin_infill_pattern" || opt_key == "locked_skeleton_infill_pattern") {
            steps.emplace_back(posPrepareInfill);
        } else if (opt_key == "sparse_infill_density") {
            const auto *old_density = old_config.option<ConfigOptionPercent>(opt_key);
            const auto *new_density = new_config.option<ConfigOptionPercent>(opt_key);
            assert(old_density && new_density);
            if (is_approx(old_density->value, 0.) || is_approx(old_density->value, 100.) || is_approx(new_density->value, 0.) ||
                is_approx(new_density->value, 100.))
                steps.emplace_back(posPerimeters);
            steps.emplace_back(posPrepareInfill);
        } else if (opt_key == "internal_solid_infill_line_width") {
            steps.emplace_back(posPerimeters);
            steps.emplace_back(posPrepareInfill);
        } else if (
            opt_key == "outer_wall_line_width" || opt_key == "wall_filament" || opt_key == "fuzzy_skin" ||
            opt_key == "fuzzy_skin_thickness" || opt_key == "fuzzy_skin_point_distance" || opt_key == "fuzzy_skin_first_layer" ||
            opt_key == "fuzzy_skin_noise_type" || opt_key == "fuzzy_skin_scale" || opt_key == "fuzzy_skin_octaves" ||
            opt_key == "fuzzy_skin_persistence" || opt_key == "fuzzy_skin_mode" || opt_key == "detect_overhang_wall" ||
            opt_key == "enable_overhang_speed" || opt_key == "detect_thin_wall" || opt_key == "precise_outer_wall") {
            steps.emplace_back(posPerimeters);
            steps.emplace_back(posSupportMaterial);
        } else if (opt_key == "bridge_flow") {
            if (m_config.support_top_z_distance > 0.) {
                steps.emplace_back(posPerimeters);
                steps.emplace_back(posInfill);
                steps.emplace_back(posSupportMaterial);
            }
        } else if (
            opt_key == "wall_generator" || opt_key == "wall_transition_length" || opt_key == "wall_transition_filter_deviation" ||
            opt_key == "wall_transition_angle" || opt_key == "wall_distribution_count" || opt_key == "min_feature_size" ||
            opt_key == "min_bead_width") {
            steps.emplace_back(posSlice);
        } else if (
            opt_key == "seam_position" || opt_key == "seam_placement_away_from_overhangs" || opt_key == "seam_slope_conditional" ||
            opt_key == "scarf_angle_threshold" || opt_key == "seam_slope_entire_loop" || opt_key == "seam_slope_steps" ||
            opt_key == "seam_slope_inner_walls" || opt_key == "seam_gap" || opt_key == "wipe_speed" || opt_key == "role_base_wipe_speed" ||
            opt_key == "support_speed" || opt_key == "support_interface_speed" || opt_key == "smooth_speed_discontinuity_area" ||
            opt_key == "smooth_coefficient" || opt_key == "overhang_1_4_speed" || opt_key == "overhang_2_4_speed" ||
            opt_key == "overhang_3_4_speed" || opt_key == "overhang_4_4_speed" || opt_key == "overhang_totally_speed" ||
            opt_key == "bridge_speed" || opt_key == "outer_wall_speed" || opt_key == "small_perimeter_speed" ||
            opt_key == "small_perimeter_threshold" || opt_key == "sparse_infill_speed" || opt_key == "inner_wall_speed" ||
            opt_key == "internal_solid_infill_speed" || opt_key == "top_surface_speed" || opt_key == "vertical_shell_speed" ||
            opt_key == "enable_height_slowdown" || opt_key == "slowdown_start_height" || opt_key == "slowdown_start_speed" ||
            opt_key == "slowdown_start_acc" || opt_key == "slowdown_end_height" || opt_key == "slowdown_end_speed" ||
            opt_key == "slowdown_end_acc") {
            invalidated |= m_print->invalidate_step(psGCodeExport);
        } else if (opt_key == "flush_into_infill" || opt_key == "flush_into_objects" || opt_key == "flush_into_support") {
            invalidated |= m_print->invalidate_step(psWipeTower);
            invalidated |= m_print->invalidate_step(psGCodeExport);
        } else {
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

    if (step == posPerimeters) {
        invalidated |= this->invalidate_steps({ posPrepareInfill, posInfill, posIroning, posSimplifyWall, posSimplifyInfill });
        invalidated |= m_print->invalidate_steps({ psSkirtBrim });
    } else if (step == posPrepareInfill) {
        invalidated |= this->invalidate_steps({ posInfill, posIroning, posSimplifyWall, posSimplifyInfill });
        invalidated |= m_print->invalidate_steps({ psSkirtBrim });
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

    invalidated |= m_print->invalidate_step(psWipeTower);
    invalidated |= m_print->invalidate_step(psGCodeExport);
    return invalidated;
}

bool PrintObject::invalidate_all_steps()
{
    bool result = Inherited::invalidate_all_steps() | m_print->invalidate_all_steps();
    m_slicing_params.valid = false;
    return result;
}

} // namespace Slic3r
