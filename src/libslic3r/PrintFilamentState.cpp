#include "Print.hpp"

namespace Slic3r {

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

} // namespace Slic3r
