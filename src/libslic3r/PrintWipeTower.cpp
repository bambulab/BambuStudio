#include "Print.hpp"

#include "ClipperUtils.hpp"
#include "GCode/ConflictChecker.hpp"
#include "Layer.hpp"
#include "Utils.hpp"

namespace Slic3r {

bool Print::has_wipe_tower() const
{
    if (m_config.enable_prime_tower.value == true) {
        if (m_config.enable_wrapping_detection.value && m_config.wrapping_exclude_area.values.size() > 2)
            return true;

        if (enable_timelapse_print())
            return true;

        return !m_config.spiral_mode.value && m_config.filament_diameter.values.size() > 1;
    }
    return false;
}

const WipeTowerData& Print::wipe_tower_data(size_t filaments_cnt) const
{
    // If the wipe tower wasn't created yet, make sure the depth and brim_width members are set to default.
    double max_height = 0;
    for (size_t obj_idx = 0; obj_idx < m_objects.size(); obj_idx++) {
        double object_z = (double) m_objects[obj_idx]->size().z();
        max_height      = std::max(unscale_(object_z), max_height);
    }
    if (max_height < EPSILON) return m_wipe_tower_data;

    double layer_height = 0.08f; // hard code layer height
    layer_height        = m_objects.front()->config().layer_height.value;

    auto   timelapse_type  = config().option<ConfigOptionEnum<TimelapseType>>("timelapse_type");
    bool   need_wipe_tower = (timelapse_type ? (timelapse_type->value == TimelapseType::tlSmooth) : false) | m_config.prime_tower_rib_wall.value;
    double extra_spacing   = config().option("prime_tower_infill_gap")->getFloat() / 100.;
    double rib_width       = config().option("prime_tower_rib_width")->getFloat();

    double filament_change_volume = 0.;
    {
        std::vector<double> filament_change_lengths;
        auto                filament_change_lengths_opt = config().option<ConfigOptionFloats>("filament_change_length");
        if (filament_change_lengths_opt) filament_change_lengths = filament_change_lengths_opt->values;
        double length   = filament_change_lengths.empty() ? 0 : *std::max_element(filament_change_lengths.begin(), filament_change_lengths.end());
        double diameter = 1.75;
        std::vector<double> diameters;
        auto                filament_diameter_opt = config().option<ConfigOptionFloats>("filament_diameter");
        if (filament_diameter_opt) diameters = filament_diameter_opt->values;
        diameter               = diameters.empty() ? diameter : *std::max_element(diameters.begin(), diameters.end());
        filament_change_volume = length * PI * diameter * diameter / 4.;
    }

    if (! is_step_done(psWipeTower) && filaments_cnt != 0) {
        std::vector<double> filament_wipe_volume = m_config.filament_prime_volume.values;
        if (m_config.prime_volume_mode == pvmSaving) {
            for (auto &v : filament_wipe_volume)
                v = 15.f;
        }
        double wipe_volume         = get_max_element(filament_wipe_volume);
        int    filament_depth_count = m_config.nozzle_diameter.values.size() == 2 ? filaments_cnt : filaments_cnt - 1;
        if (filaments_cnt == 1 && enable_timelapse_print()) filament_depth_count = 1;
        double volume = wipe_volume * filament_depth_count;
        if (m_config.nozzle_diameter.values.size() == 2) volume += filament_change_volume * (int) (filaments_cnt / 2);

        if (m_config.prime_tower_rib_wall.value) {
            double depth = std::sqrt(volume / layer_height * extra_spacing);
            if (need_wipe_tower || filaments_cnt > 1) {
                float min_wipe_tower_depth = WipeTower::get_limit_depth_by_height(max_height);
                depth = std::max((double) min_wipe_tower_depth, depth);
                depth += rib_width / std::sqrt(2) + config().prime_tower_extra_rib_length.value;
                const_cast<Print *>(this)->m_wipe_tower_data.depth = depth;
                const_cast<Print *>(this)->m_wipe_tower_data.brim_width = m_config.prime_tower_brim_width;
            }
        } else {
            double width = m_config.prime_tower_width;
            double depth = volume / (layer_height * width) * extra_spacing;
            if (need_wipe_tower || m_wipe_tower_data.depth > EPSILON) {
                float min_wipe_tower_depth = WipeTower::get_limit_depth_by_height(max_height);
                depth = std::max((double) min_wipe_tower_depth, depth);
            }
            const_cast<Print *>(this)->m_wipe_tower_data.depth = depth;
            const_cast<Print *>(this)->m_wipe_tower_data.brim_width = m_config.prime_tower_brim_width;
        }
        if (m_config.prime_tower_brim_width < 0) const_cast<Print *>(this)->m_wipe_tower_data.brim_width = WipeTower::get_auto_brim_by_height(max_height);
    }
    return m_wipe_tower_data;
}

bool Print::enable_timelapse_print() const
{
    return m_config.timelapse_type.value == TimelapseType::tlSmooth;
}

void Print::_make_wipe_tower()
{
    m_wipe_tower_data.clear();

    const unsigned int number_of_extruders = (unsigned int)(m_config.filament_colour.values.size());

    // BBS: priming logic is removed, so don't consider it in tool ordering
    m_wipe_tower_data.tool_ordering = ToolOrdering(*this, (unsigned int)-1, false);
    m_wipe_tower_data.tool_ordering.sort_and_build_data(*this, (unsigned int)-1, false);

    if (! m_wipe_tower_data.tool_ordering.has_wipe_tower())
        return;

    {
        size_t idx_begin = size_t(-1);
        size_t idx_end   = m_wipe_tower_data.tool_ordering.layer_tools().size();
        for (size_t i = 0; i < idx_end; ++i) {
            const LayerTools &lt = m_wipe_tower_data.tool_ordering.layer_tools()[i];
            if (lt.has_wipe_tower && ! lt.has_object && ! lt.has_support) {
                idx_begin = i;
                break;
            }
        }
        if (idx_begin != size_t(-1)) {
            double wipe_tower_new_layer_print_z_first = m_wipe_tower_data.tool_ordering.layer_tools()[idx_begin].print_z;
            auto   it_layer = m_objects.front()->support_layers().begin();
            auto   it_end   = m_objects.front()->support_layers().end();
            for (; it_layer != it_end && (*it_layer)->print_z - EPSILON < wipe_tower_new_layer_print_z_first; ++it_layer);
            for (size_t i = idx_begin; i < idx_end; ++i) {
                LayerTools &lt = const_cast<LayerTools &>(m_wipe_tower_data.tool_ordering.layer_tools()[i]);
                if (! (lt.has_wipe_tower && ! lt.has_object && ! lt.has_support))
                    break;
                lt.has_support = true;
                double height = lt.print_z - (i == 0 ? 0. : m_wipe_tower_data.tool_ordering.layer_tools()[i - 1].print_z);
                it_layer = m_objects.front()->insert_support_layer(it_layer, -1, 0, height, lt.print_z, lt.print_z - 0.5 * height);
                ++it_layer;
            }
        }
    }
    this->throw_if_canceled();

    auto group_result = get_layered_nozzle_group_result();
    assert(m_nozzle_group_result);

    WipeTower wipe_tower(m_config, m_plate_index, m_origin, m_wipe_tower_data.tool_ordering.first_extruder(),
                         m_wipe_tower_data.tool_ordering.empty() ? 0.f : m_wipe_tower_data.tool_ordering.back().print_z, m_wipe_tower_data.tool_ordering.all_extruders());
    wipe_tower.set_first_layer_flow_ratio(m_default_region_config.initial_layer_flow_ratio);
    wipe_tower.set_has_tpu_filament(this->has_tpu_filament());
    wipe_tower.set_nozzle_group_result(*group_result);
    wipe_tower.set_shared_print_bed(this->get_extruder_shared_printable_polygon());
    for (size_t i = 0; i < number_of_extruders; ++i)
        wipe_tower.set_extruder(i, m_config);

    std::set<int> used_filament_ids;

    {
        size_t nozzle_nums = m_config.nozzle_diameter.values.size();
        using FlushMatrix = std::vector<std::vector<float>>;
        std::vector<FlushMatrix> multi_extruder_flush;
        for (size_t nozzle_id = 0; nozzle_id < nozzle_nums; ++nozzle_id) {
            std::vector<float> flush_matrix(cast<float>(get_flush_volumes_matrix(m_config.flush_volumes_matrix.values, nozzle_id, nozzle_nums)));
            std::vector<std::vector<float>> wipe_volumes;
            for (unsigned int i = 0; i < number_of_extruders; ++i)
                wipe_volumes.push_back(std::vector<float>(flush_matrix.begin() + i * number_of_extruders, flush_matrix.begin() + (i + 1) * number_of_extruders));

            multi_extruder_flush.emplace_back(wipe_volumes);
        }

        MultiNozzleUtils::NozzleStatusRecorder nozzle_recorder;

        int layer_idx = -1;

        unsigned int old_filament_id = m_wipe_tower_data.tool_ordering.first_extruder();
        nozzle_recorder.set_nozzle_status(group_result->get_nozzle_for_filament(old_filament_id, layer_idx)->group_id, old_filament_id);

        for (auto &layer_tools : m_wipe_tower_data.tool_ordering.layer_tools()) {
            ++layer_idx;

            if (! layer_tools.has_wipe_tower) continue;
            wipe_tower.plan_toolchange((float) layer_tools.print_z, (float) layer_tools.wipe_tower_layer_height, old_filament_id, old_filament_id);

            used_filament_ids.insert(layer_tools.extruders.begin(), layer_tools.extruders.end());

            for (const auto filament_id : layer_tools.extruders) {
                if (filament_id == old_filament_id)
                    continue;

                auto nozzle_info = group_result->get_nozzle_for_filament(filament_id, layer_idx);

                int extruder_id          = nozzle_info->extruder_id;
                int nozzle_id            = nozzle_info->group_id;
                int prev_nozzle_filament = nozzle_recorder.get_filament_in_nozzle(nozzle_id);

                float volume_to_purge = 0;

                if (! nozzle_recorder.is_nozzle_empty(nozzle_id) && filament_id != prev_nozzle_filament) {
                    volume_to_purge = multi_extruder_flush[extruder_id][prev_nozzle_filament][filament_id];
                    float multiplier = (m_config.prime_volume_mode == PrimeVolumeMode::pvmFast) ? m_config.flush_multiplier_fast.get_at(extruder_id) :
                                                                                                   m_config.flush_multiplier.get_at(extruder_id);
                    volume_to_purge *= multiplier;
                    volume_to_purge = layer_tools.wiping_extrusions().mark_wiping_extrusions(*this, old_filament_id, filament_id, volume_to_purge);
                }

                float grab_purge_volume = m_config.grab_length.get_at(extruder_id) * 2.4;
                volume_to_purge = std::max(0.f, volume_to_purge - grab_purge_volume);

                float wipe_volume_ec = m_config.filament_prime_volume.values[filament_id];
                float wipe_volume_nc = m_config.filament_prime_volume_nc.values[filament_id];
                if (m_config.prime_volume_mode == PrimeVolumeMode::pvmSaving) {
                    wipe_volume_ec = 15.f;
                    wipe_volume_nc = 15.f;
                }
                wipe_tower.plan_toolchange((float) layer_tools.print_z, (float) layer_tools.wipe_tower_layer_height, old_filament_id, filament_id,
                                           wipe_volume_ec, wipe_volume_nc, volume_to_purge);
                old_filament_id = filament_id;

                nozzle_recorder.set_nozzle_status(nozzle_id, filament_id);
            }
            layer_tools.wiping_extrusions().ensure_perimeters_infills_order(*this);

            if (m_config.enable_wrapping_detection || enable_timelapse_print()) {
                if (layer_tools.wipe_tower_partitions == 0) wipe_tower.set_last_layer_extruder_fill(false);
                continue;
            }

            if (&layer_tools == &m_wipe_tower_data.tool_ordering.back() || (&layer_tools + 1)->wipe_tower_partitions == 0)
                break;
        }
    }
    wipe_tower.set_used_filament_ids(std::vector<int>(used_filament_ids.begin(), used_filament_ids.end()));

    std::vector<int> categories;
    for (size_t i = 0; i < m_config.filament_adhesiveness_category.values.size(); ++i)
        categories.push_back(m_config.filament_adhesiveness_category.get_at(i));
    wipe_tower.set_filament_categories(categories);

    m_wipe_tower_data.tool_changes.reserve(m_wipe_tower_data.tool_ordering.layer_tools().size());
    wipe_tower.generate_new(m_wipe_tower_data.tool_changes);
    m_wipe_tower_data.depth      = wipe_tower.get_depth();
    m_wipe_tower_data.brim_width = wipe_tower.get_brim_width();
    m_wipe_tower_data.bbx        = wipe_tower.get_bbx();
    m_wipe_tower_data.rib_offset = wipe_tower.get_rib_offset();

    coordf_t layer_height = m_objects.front()->config().layer_height.value;
    if (m_wipe_tower_data.tool_ordering.back().wipe_tower_partitions > 0) {
        if (wipe_tower.layer_finished()) {
            wipe_tower.set_layer(float(m_wipe_tower_data.tool_ordering.back().print_z + layer_height), float(layer_height), 0, false, true);
        }
    } else {
        assert(m_wipe_tower_data.tool_ordering.back().wipe_tower_partitions == 0);
        wipe_tower.set_layer(float(m_wipe_tower_data.tool_ordering.back().print_z), float(layer_height), 0, false, true);
    }
    m_wipe_tower_data.final_purge = Slic3r::make_unique<WipeTower::ToolChangeResult>(
        wipe_tower.tool_change((unsigned int) (-1)));

    m_wipe_tower_data.used_filament         = wipe_tower.get_used_filament();
    m_wipe_tower_data.number_of_toolchanges = wipe_tower.get_number_of_toolchanges();
    m_wipe_tower_data.construct_mesh(wipe_tower.width(), wipe_tower.get_depth(), wipe_tower.get_height(), wipe_tower.get_brim_width(), config().prime_tower_rib_wall.value,
                                     wipe_tower.get_rib_width(), wipe_tower.get_rib_length(), config().prime_tower_fillet_wall.value);
    const Vec3d origin = this->get_plate_origin();
    m_fake_wipe_tower.rib_offset = wipe_tower.get_rib_offset();
    m_fake_wipe_tower.set_fake_extrusion_data(wipe_tower.position() + m_fake_wipe_tower.rib_offset, wipe_tower.width(), wipe_tower.get_height(), wipe_tower.get_layer_height(),
                                              m_wipe_tower_data.depth,
                                              m_wipe_tower_data.brim_width, {scale_(origin.x()), scale_(origin.y())});
    m_fake_wipe_tower.outer_wall = wipe_tower.get_outer_wall();
}

ExtrusionLayers FakeWipeTower::getTrueExtrusionLayersFromWipeTower() const
{
    ExtrusionLayers wtels;
    wtels.type = ExtrusionLayersType::WIPE_TOWER;
    std::vector<float> layer_heights;
    layer_heights.reserve(outer_wall.size());
    auto pre = outer_wall.begin();
    for (auto it = outer_wall.begin(); it != outer_wall.end(); ++it) {
        if (it == outer_wall.begin())
            layer_heights.push_back(it->first);
        else {
            layer_heights.push_back(it->first - pre->first);
            ++pre;
        }
    }
    Point trans = {scale_(pos.x()), scale_(pos.y())};
    for (auto it = outer_wall.begin(); it != outer_wall.end(); ++it) {
        int            index = std::distance(outer_wall.begin(), it);
        ExtrusionLayer el;
        ExtrusionPaths paths;
        paths.reserve(it->second.size());
        for (auto &polyline : it->second) {
            ExtrusionPath path(ExtrusionRole::erWipeTower, 0.0, 0.0, layer_heights[index]);
            path.polyline = polyline;
            for (auto &p : path.polyline.points) p += trans;
            paths.push_back(path);
        }
        el.paths    = std::move(paths);
        el.bottom_z = it->first - layer_heights[index];
        el.layer    = nullptr;
        wtels.push_back(el);
    }
    return wtels;
}

void WipeTowerData::construct_mesh(float width, float depth, float height, float brim_width, bool is_rib_wipe_tower, float rib_width, float rib_length, bool fillet_wall)
{
    wipe_tower_mesh_data = WipeTowerMeshData{};
    float first_layer_height = 0.08f;
    if (width < EPSILON || depth < EPSILON || height < EPSILON) return;
    if (! is_rib_wipe_tower || rib_length < EPSILON) {
        wipe_tower_mesh_data->real_wipe_tower_mesh = make_cube(width, depth, height);
        wipe_tower_mesh_data->real_brim_mesh       = make_cube(width + 2 * brim_width, depth + 2 * brim_width, first_layer_height);
        wipe_tower_mesh_data->real_brim_mesh.translate({-brim_width, -brim_width, 0});
        wipe_tower_mesh_data->bottom = {scaled(Vec2f{-brim_width, -brim_width}), scaled(Vec2f{width + brim_width, 0}), scaled(Vec2f{width + brim_width, depth + brim_width}),
                                        scaled(Vec2f{0, depth})};
    } else {
        wipe_tower_mesh_data->real_wipe_tower_mesh = WipeTower::its_make_rib_tower(width, depth, height, rib_length, rib_width, fillet_wall);
        wipe_tower_mesh_data->bottom               = WipeTower::rib_section(width, depth, rib_length, rib_width, fillet_wall);
        auto brim_bottom                           = offset(wipe_tower_mesh_data->bottom, scaled(brim_width));
        if (! brim_bottom.empty())
            wipe_tower_mesh_data->bottom = brim_bottom.front();
        wipe_tower_mesh_data->real_brim_mesh = WipeTower::its_make_rib_brim(wipe_tower_mesh_data->bottom, first_layer_height);
        wipe_tower_mesh_data->real_wipe_tower_mesh.translate(Vec3f(rib_offset[0], rib_offset[1], 0));
        wipe_tower_mesh_data->real_brim_mesh.translate(Vec3f(rib_offset[0], rib_offset[1], 0));
        wipe_tower_mesh_data->bottom.translate(scaled(Vec2f(rib_offset[0], rib_offset[1])));
    }
}

} // namespace Slic3r
