#include "Print.hpp"
#include "PrintFilamentMappingConfigSync.hpp"
#include "PrintFilamentMappingRules.hpp"

#include <set>
#include <unordered_map>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r {

bool Print::is_dynamic_group_reorder() const
{
    return FilamentMappingRules::is_dynamic_group_reorder_enabled(
        config().enable_filament_dynamic_map,
        config().filament_map_mode,
        config().nozzle_diameter.size(),
        config().filament_is_mixed.values,
        extruders());
}

int Print::get_filament_config_indx(int filament_id, int layer_id)
{
    return get_config_index(filament_id, layer_id, m_config.filament_extruder_variant.values, m_filament_self_index, m_filament_index_map);
}

void Print::update_filament_self_index_cache()
{
    std::vector<int> values;
    if (m_full_print_config.has("filament_self_index")) {
        values = m_full_print_config.option<ConfigOptionInts>("filament_self_index")->values;
    } else if (m_ori_full_print_config.has("filament_self_index")) {
        values = m_ori_full_print_config.option<ConfigOptionInts>("filament_self_index")->values;
    } else {
        values = m_config.filament_self_index.values;
    }

    size_t expected_size = m_config.filament_extruder_variant.values.size();
    m_filament_self_index.clear();
    if (expected_size == 0) {
        m_filament_index_map.clear();
        m_nozzle_index_map.clear();
        return;
    }
    m_filament_self_index.resize(expected_size, 1);
    if (!values.empty()) {
        for (size_t i = 0; i < expected_size; ++i) {
            int v = i < values.size() ? values[i] : 1;
            if (v <= 0)
                v = 1;
            m_filament_self_index[i] = v;
        }
    }
    m_filament_index_map.clear();
    m_nozzle_index_map.clear();
}

int Print::get_nozzle_config_index(int filament_id, int layer_id)
{
    return get_config_index(filament_id, layer_id, m_config.print_extruder_variant.values, m_config.print_extruder_id.values, m_nozzle_index_map);
}

int Print::get_config_index(int filament_id, int layer_id, const std::vector<std::string> &variant_list, const std::vector<int> &self_index_list, FilamentIndexMap &index_map)
{
    auto group_result = get_layered_nozzle_group_result();
    auto nozzle_info  = group_result->get_nozzle_for_filament(filament_id, layer_id);
    if (!nozzle_info.has_value()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__
                                 << boost::format(", Line %1%: could not found group_nozzle_info corresponding to filament_id %2%, layer_id %3%") % __LINE__ % filament_id %
                                        layer_id;
        return 0;
    }

    ExtruderType     extruder_type      = ExtruderType(m_config.extruder_type.get_at(nozzle_info->extruder_id));
    NozzleVolumeType nozzle_volume_type = nozzle_info->volume_type;

    FilamentIndexKey key{filament_id, extruder_type, nozzle_volume_type};
    auto             iter = index_map.find(key);
    if (iter == index_map.end()) {
        int index = get_config_index_base(nozzle_volume_type, extruder_type, filament_id + 1, variant_list, self_index_list);
        index_map[key] = index;
        return index;
    } else {
        return index_map[key];
    }
}

int Print::get_config_index(int filament_id, int layer_id, const std::vector<std::string> &variant_list, const std::vector<int> &self_index_list, PrintIndexMap &index_map)
{
    auto group_result = get_layered_nozzle_group_result();
    auto nozzle_info  = group_result->get_nozzle_for_filament(filament_id, layer_id);
    if (!nozzle_info.has_value()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__
                                 << boost::format(", Line %1%: could not found group_nozzle_info corresponding to filament_id %2%, layer_id %3%") % __LINE__ % filament_id %
                                        layer_id;
        return 0;
    }

    int              extruder_id        = nozzle_info->extruder_id + 1; // to 1 based
    ExtruderType     extruder_type      = ExtruderType(m_config.extruder_type.get_at(nozzle_info->extruder_id));
    NozzleVolumeType nozzle_volume_type = nozzle_info->volume_type;

    PrintIndexKey key{filament_id, extruder_id, extruder_type, nozzle_volume_type};
    auto          iter = index_map.find(key);
    if (iter == index_map.end()) {
        int index = get_config_index_base(nozzle_volume_type, extruder_type, extruder_id, variant_list, self_index_list);
        index_map[key] = index;
        return index;
    } else {
        return index_map[key];
    }
}

void Print::update_filament_maps_to_config(std::vector<int> f_maps, std::vector<int> f_volume_maps, std::vector<int> f_nozzle_maps)
{
    const auto sync_result = FilamentMappingConfigSync::sync_maps_to_config(
        m_config,
        m_ori_full_print_config,
        m_full_print_config,
        f_maps,
        f_volume_maps,
        f_nozzle_maps);

    if (sync_result.maps_changed)
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": filament maps changed after pre-slicing.");

    if (!sync_result.print_diff.empty())
        m_placeholder_parser.apply_config(sync_result.filament_overrides);

    update_filament_self_index_cache();
    m_has_auto_filament_map_result = sync_result.has_auto_filament_map_result;
}

void Print::update_to_config_by_nozzle_group_result(const MultiNozzleUtils::NozzleGroupResultBase& group_result)
{
    int  extruder_count, extruder_volume_type_count;
    bool support_multi = m_ori_full_print_config.support_different_extruders(extruder_count);
    std::vector<std::vector<NozzleVolumeType>> nozzle_volume_types;
    extruder_volume_type_count = m_ori_full_print_config.get_extruder_nozzle_volume_count(extruder_count, nozzle_volume_types);

    std::unordered_map<int, std::vector<ExtruderNozleInfo>> filament_extruder_map;

    auto filament_count = m_config.option<ConfigOptionStrings>("filament_type")->size();
    auto extruder_type  = m_config.option<ConfigOptionEnumsGeneric>("extruder_type")->values;

    for (int fidx = 0; fidx < filament_count; ++fidx) {
        auto used_nozzles = group_result.get_nozzles_for_filament(fidx);
        std::set<ExtruderNozleInfo> extruder_nozzle_set;
        for (auto nozzle : used_nozzles) {
            ExtruderNozleInfo tmp;
            tmp.extruder_type = ExtruderType(extruder_type[nozzle.extruder_id]);
            tmp.nozzle_volume_type = nozzle.volume_type;
            extruder_nozzle_set.insert(tmp);
        }
        filament_extruder_map[fidx] = std::vector<ExtruderNozleInfo>(extruder_nozzle_set.begin(), extruder_nozzle_set.end());
    }


    m_full_print_config = m_ori_full_print_config;
    std::set<std::string> filament_keys = filament_options_with_variant;
    filament_keys.insert("filament_self_index");
    m_full_print_config.update_filament_config_values_for_multiple_extruders(m_full_print_config, filament_extruder_map, extruder_count, extruder_volume_type_count,
                                                                             filament_keys, "filament_self_index", "filament_extruder_variant");

    const std::vector<std::string> &extruder_retract_keys = print_config_def.extruder_retract_keys();
    const std::string               filament_prefix       = "filament_";
    t_config_option_keys            print_diff;
    DynamicPrintConfig              filament_overrides;
    for (auto &opt_key : extruder_retract_keys) {
        const ConfigOption *opt_new_filament = m_full_print_config.option(filament_prefix + opt_key);
        const ConfigOption *opt_new_machine  = m_full_print_config.option(opt_key);
        const ConfigOption *opt_old_machine  = m_config.option(opt_key);

        if (opt_new_filament)
            compute_filament_override_value(opt_key, opt_old_machine, opt_new_machine, opt_new_filament, m_full_print_config, print_diff, filament_overrides,
                                            m_config.filament_map_2.values);
    }

    t_config_option_keys keys(filament_options_with_variant.begin(), filament_options_with_variant.end());
    keys.push_back("filament_self_index");
    m_config.apply_only(m_full_print_config, keys, true);
    if (!print_diff.empty()) {
        m_placeholder_parser.apply_config(filament_overrides);
        m_config.apply(filament_overrides);
    }
    update_filament_self_index_cache();
}

std::vector<int> Print::get_filament_maps() const
{
    return m_config.filament_map.values;
}

std::vector<int> Print::get_filament_nozzle_maps() const
{
    return m_config.filament_nozzle_map.values;
}

std::vector<int> Print::get_filament_volume_maps() const
{
    return m_config.filament_volume_map.values;
}

FilamentMapMode Print::get_filament_map_mode() const
{
    return m_config.filament_map_mode;
}

std::vector<FilamentMapMode> Print::get_available_filament_map_modes() const
{
    auto opt_extruder_type = dynamic_cast<const ConfigOptionEnumsGeneric*>(m_config.option("extruder_type"));
    return FilamentMappingRules::available_modes_for_extruder_types(opt_extruder_type ? opt_extruder_type->values : std::vector<int>{});
}

bool Print::get_full_filament_extruder_variants(const size_t filament_id, std::vector<std::string> &variants) const
{
    variants.clear();
    if (!m_ori_full_print_config.has("filament_extruder_variant"))
        return false;
    auto filament_variants = m_ori_full_print_config.option<ConfigOptionStrings>("filament_extruder_variant")->values;

    if (!m_ori_full_print_config.has("filament_self_index")) {
        std::set<std::string> dup_variants(filament_variants.begin(), filament_variants.end());
        variants.insert(variants.end(), dup_variants.begin(), dup_variants.end());
    } else {
        auto filament_self_index = m_ori_full_print_config.option<ConfigOptionInts>("filament_self_index")->values;

        for (int i = 0; i < filament_self_index.size(); i++){
            if (filament_self_index[i] - 1 == filament_id)
                variants.emplace_back(filament_variants[i]);
        }
    }

    return true;
}

} // namespace Slic3r
