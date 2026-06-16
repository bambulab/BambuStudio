#include "Print.hpp"
#include "PrintConfig.hpp"

#include <cassert>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Slic3r {

std::vector<FilamentUsageType> Print::get_filament_usage_type() const
{
    std::vector<FilamentUsageType> filament_usage_types;

    std::unordered_set<int> model_filaments, support_filaments; //0 base
    for (auto &obj : this->objects().vector()) {
        auto obj_filaments = obj->object_extruders();
        model_filaments.insert(obj_filaments.begin(), obj_filaments.end());

        int support_fil           = obj->config().support_filament - 1;
        int support_interface_fil = obj->config().support_interface_filament - 1;
        if (support_fil >= 0) support_filaments.insert(support_fil);
        if (support_interface_fil >= 0) support_filaments.insert(support_interface_fil);
    }

    for (int idx = 0; idx < m_config.filament_type.size(); ++idx) {
        bool is_model   = model_filaments.count(idx);
        bool is_support = support_filaments.count(idx);

        if (is_model && is_support)
            filament_usage_types.emplace_back(FilamentUsageType::Hybrid);
        else if (is_support)
            filament_usage_types.emplace_back(FilamentUsageType::SupportOnly);
        else
            filament_usage_types.emplace_back(FilamentUsageType::ModelOnly);
    }
    return filament_usage_types;
}

std::vector<std::set<int>> Print::get_physical_unprintable_filaments(const std::vector<unsigned int> &used_filaments) const
{
    int extruder_num = m_config.nozzle_diameter.size();
    std::vector<std::set<int>> physical_unprintables(extruder_num);
    if (extruder_num < 2)
        return physical_unprintables;

    auto get_unprintable_extruder_id = [&](unsigned int filament_idx) -> int {
        int status = m_config.filament_printable.values[filament_idx];
        for (int i = 0; i < extruder_num; ++i) {
            if (!(status >> i & 1)) {
                return i;
            }
        }
        return -1;
    };

    std::set<int> tpu_filaments;
    for (auto f : used_filaments) {
        if (m_config.filament_type.get_at(f) == "TPU")
            tpu_filaments.insert(f);
    }

    for (auto f : used_filaments) {
        int extruder_id = get_unprintable_extruder_id(f);
        if (extruder_id == -1)
            continue;
        physical_unprintables[extruder_id].insert(f);
    }

    return physical_unprintables;
}

std::map<int, std::set<NozzleVolumeType>> Print::get_filament_unprintable_flow(const std::vector<unsigned int> &used_filaments) const
{
    std::vector<std::string> extruder_variant_list = m_config.printer_extruder_variant.values;
    std::vector<std::string> filament_variant_list = m_ori_full_print_config.option<ConfigOptionStrings>("filament_extruder_variant")->values;
    std::vector<int>         filament_self_index;
    if (!m_ori_full_print_config.has("filament_self_index"))
        filament_self_index.resize(filament_variant_list.size(), 1);
    else
        filament_self_index = m_ori_full_print_config.option<ConfigOptionInts>("filament_self_index")->values;
    std::map<int, std::set<NozzleVolumeType>> ret;
    std::unordered_set<int>                   used_fils_set(used_filaments.begin(), used_filaments.end());

    std::unordered_map<int, std::set<NozzleVolumeType>> filament_variant_map;
    for (int i = 0; i < filament_variant_list.size(); ++i) {
        NozzleVolumeType volume = convert_to_nvt_type(filament_variant_list[i]);
        if (volume != nvtHybrid) filament_variant_map[filament_self_index[i]].insert(volume);
    }

    for (auto iter : filament_variant_map) {
        int fil_idx = iter.first - 1;
        if (used_fils_set.find(fil_idx) == used_fils_set.end()) continue;
        const std::set<NozzleVolumeType> &volumes = iter.second;
        for (int exd_idx = 0; exd_idx < extruder_variant_list.size(); ++exd_idx) {
            auto exd_volume = convert_to_nvt_type(extruder_variant_list[exd_idx]);
            assert(exd_volume != nvtHybrid);
            if (volumes.find(exd_volume) == volumes.end() && exd_volume != nvtHybrid) ret[fil_idx].insert(exd_volume);
        }
    }
    return ret;
}

} // namespace Slic3r
