#include "Print.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

using namespace std::literals;

namespace Slic3r {

static void clamp_exturder_to_default(ConfigOptionInt &opt, size_t num_extruders)
{
    if (opt.value > (int)num_extruders)
        opt.value = 1;
}

static void clamp_exturder_to_default_protect0(ConfigOptionInt &opt, size_t num_extruders)
{
    if (opt.value > (int)num_extruders)
        opt.value = 1;
    else if (opt.value < 1)
        opt.value = 1;
}

PrintObjectConfig PrintObject::object_config_from_model_object(const PrintObjectConfig &default_object_config, const ModelObject &object, size_t num_extruders, std::vector<int> &variant_index)
{
    PrintObjectConfig config = default_object_config;
    {
        DynamicPrintConfig src_normalized(object.config.get());
        src_normalized.normalize_fdm();
        update_static_print_config_from_dynamic(config, src_normalized, variant_index, print_options_with_variant, 1);
    }
    clamp_exturder_to_default(config.support_filament, num_extruders);
    clamp_exturder_to_default(config.support_interface_filament, num_extruders);
    return config;
}

const std::string                                                    key_extruder{ "extruder" };
static constexpr const std::initializer_list<const std::string_view> keys_extruders{ "sparse_infill_filament"sv, "solid_infill_filament"sv, "wall_filament"sv };

static void apply_to_print_region_config(PrintRegionConfig &out, const DynamicPrintConfig &in, std::vector<int> &variant_index)
{
    auto *opt_extruder = in.opt<ConfigOptionInt>(key_extruder);
    if (opt_extruder)
        if (int extruder = opt_extruder->value; extruder != 0) {
            out.sparse_infill_filament.value = extruder;
            out.solid_infill_filament.value  = extruder;
            out.wall_filament.value          = extruder;
        }

    for (auto it = in.cbegin(); it != in.cend(); ++it)
        if (it->first != key_extruder)
            if (ConfigOption *my_opt = out.option(it->first, false); my_opt != nullptr) {
                if (one_of(it->first, keys_extruders)) {
                } else if (*my_opt != *(it->second)) {
                    if (my_opt->is_scalar() || variant_index.empty() || (print_options_with_variant.find(it->first) == print_options_with_variant.end()))
                        my_opt->set(it->second.get());
                    else {
                        ConfigOptionVectorBase       *opt_vec_src  = static_cast<ConfigOptionVectorBase *>(my_opt);
                        const ConfigOptionVectorBase *opt_vec_dest = static_cast<const ConfigOptionVectorBase *>(it->second.get());
                        opt_vec_src->set_to_index(opt_vec_dest, variant_index, 1);
                    }
                }
            }
}

PrintRegionConfig region_config_from_model_volume(const PrintRegionConfig &default_or_parent_region_config, const DynamicPrintConfig *layer_range_config, const ModelVolume &volume, size_t num_extruders, std::vector<int> &variant_index)
{
    PrintRegionConfig config = default_or_parent_region_config;
    if (volume.is_model_part())
        apply_to_print_region_config(config, volume.get_object()->config.get(), variant_index);

    apply_to_print_region_config(config, volume.config.get(), variant_index);
    if (!volume.material_id().empty())
        apply_to_print_region_config(config, volume.material()->config.get(), variant_index);
    if (layer_range_config != nullptr) {
        assert(volume.is_model_part());
        apply_to_print_region_config(config, *layer_range_config, variant_index);
    }

    {
        auto resolve_filament_value = [&](const std::string &key, int default_or_parent, int previous_value) -> int {
            int   filament_value_temp = 0;
            auto *opt_vol             = volume.config.get().opt<ConfigOptionInt>(key);
            auto *opt_obj             = volume.get_object()->config.get().opt<ConfigOptionInt>(key);
            if (opt_vol)
                filament_value_temp = opt_vol->value;
            else if (opt_obj)
                filament_value_temp = opt_obj->value;

            if (layer_range_config != nullptr && volume.is_model_part()) {
                auto *opt = layer_range_config->opt<ConfigOptionInt>(key);
                if (opt)
                    filament_value_temp = opt->value;
            }

            if (filament_value_temp > 0)
                return filament_value_temp;
            return previous_value;
        };
        config.wall_filament.value = resolve_filament_value("wall_filament", default_or_parent_region_config.wall_filament.value, config.wall_filament.value);
        config.sparse_infill_filament.value = resolve_filament_value(
            "sparse_infill_filament", default_or_parent_region_config.sparse_infill_filament.value, config.sparse_infill_filament.value);
        config.solid_infill_filament.value = resolve_filament_value(
            "solid_infill_filament", default_or_parent_region_config.solid_infill_filament.value, config.solid_infill_filament.value);
    }

    clamp_exturder_to_default_protect0(config.sparse_infill_filament, num_extruders);
    clamp_exturder_to_default_protect0(config.wall_filament, num_extruders);
    clamp_exturder_to_default_protect0(config.solid_infill_filament, num_extruders);
    if (config.sparse_infill_density.value < 0.00011f)
        config.sparse_infill_density.value = 0;
    else
        config.sparse_infill_density.value = std::min(config.sparse_infill_density.value, 100.);

    if (config.fuzzy_skin.value != FuzzySkinType::Disabled_fuzzy &&
        (config.fuzzy_skin_point_distance.value < 0.01 || config.fuzzy_skin_thickness.value < 0.001))
        config.fuzzy_skin.value = FuzzySkinType::Disabled_fuzzy;
    return config;
}

SlicingParameters PrintObject::slicing_parameters(const DynamicPrintConfig &full_config, const ModelObject &model_object, float object_max_z, std::vector<int> variant_index)
{
    PrintConfig       print_config;
    PrintObjectConfig object_config;
    PrintRegionConfig default_region_config;
    print_config.apply(full_config, true);
    object_config.apply(full_config, true);
    default_region_config.apply(full_config, true);

    size_t filament_extruders = print_config.filament_diameter.size();
    object_config             = object_config_from_model_object(object_config, model_object, filament_extruders, variant_index);

    std::vector<unsigned int> object_extruders;
    for (const ModelVolume *model_volume : model_object.volumes)
        if (model_volume->is_model_part()) {
            PrintRegion::collect_object_printing_extruders(
                print_config,
                region_config_from_model_volume(default_region_config, nullptr, *model_volume, filament_extruders, variant_index),
                object_config.brim_type != btNoBrim && object_config.brim_width > 0.,
                object_extruders);
            for (const std::pair<const t_layer_height_range, ModelConfig> &range_and_config : model_object.layer_config_ranges)
                if (range_and_config.second.has("wall_filament") || range_and_config.second.has("sparse_infill_filament") ||
                    range_and_config.second.has("solid_infill_filament"))
                    PrintRegion::collect_object_printing_extruders(
                        print_config,
                        region_config_from_model_volume(default_region_config, &range_and_config.second.get(), *model_volume, filament_extruders, variant_index),
                        object_config.brim_type != btNoBrim && object_config.brim_width > 0.,
                        object_extruders);
        }
    sort_remove_duplicates(object_extruders);

    if (object_max_z <= 0.f)
        object_max_z = (float)model_object.raw_bounding_box().size().z();
    return SlicingParameters::create_from_config(print_config, object_config, object_max_z, object_extruders);
}

bool PrintObject::update_layer_height_profile(const ModelObject &model_object, const SlicingParameters &slicing_parameters, std::vector<coordf_t> &layer_height_profile, bool &out_nozzle_range_reset)
{
    bool updated = false;
    out_nozzle_range_reset = false;

    if (layer_height_profile.empty()) {
        layer_height_profile = std::vector<coordf_t>(model_object.layer_height_profile.get());
        updated = true;
    }

    if (!layer_height_profile.empty() &&
        ((layer_height_profile.size() & 1) != 0 ||
         std::abs(layer_height_profile[layer_height_profile.size() - 2] - slicing_parameters.object_print_z_max + slicing_parameters.object_print_z_min) > 1e-3))
        layer_height_profile.clear();

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

    bool not_match_flag = !slicing_parameters.has_raft();
    not_match_flag &= !layer_height_profile.empty() && (layer_height_profile[1] != slicing_parameters.first_object_layer_height);
    if (layer_height_profile.empty() || not_match_flag) {
        layer_height_profile = layer_height_profile_from_ranges(slicing_parameters, model_object.layer_config_ranges);
        updated = true;
    }

    return updated;
}

} // namespace Slic3r
