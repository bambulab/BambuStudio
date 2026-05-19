#ifndef CUTENH_MIXED_NOZZLES_HPP
#define CUTENH_MIXED_NOZZLES_HPP

#include "PrintConfig.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace Slic3r {
namespace CutEnhMixedNozzles {

constexpr double kNozzleDiameterTolerance = 1e-6;
constexpr double kFilamentDiameterToleranceRatio = 0.1;

inline bool has_mixed_nozzle_diameters(const PrintConfig &config, const std::vector<unsigned int> &extruders)
{
    if (extruders.empty())
        return false;

    const double first_nozzle_diameter = config.nozzle_diameter.get_at(extruders.front());
    return std::any_of(extruders.begin() + 1, extruders.end(), [&config, first_nozzle_diameter](unsigned int extruder_idx) {
        return std::abs(config.nozzle_diameter.get_at(extruder_idx) - first_nozzle_diameter) > kNozzleDiameterTolerance;
    });
}

inline bool has_mixed_filament_diameters(const PrintConfig &config, const std::vector<unsigned int> &extruders)
{
    if (extruders.empty())
        return false;

    const double first_filament_diameter = config.filament_diameter.get_at(extruders.front());
    return std::any_of(extruders.begin() + 1, extruders.end(), [&config, first_filament_diameter](unsigned int extruder_idx) {
        const double filament_diameter = config.filament_diameter.get_at(extruder_idx);
        if (std::abs(first_filament_diameter) <= kNozzleDiameterTolerance)
            return std::abs(filament_diameter - first_filament_diameter) > kNozzleDiameterTolerance;

        return std::abs((filament_diameter - first_filament_diameter) / first_filament_diameter) > kFilamentDiameterToleranceRatio;
    });
}

inline bool has_multi_nozzle_hardware_config(const PrintConfig &config)
{
    return std::any_of(config.extruder_max_nozzle_count.values.begin(), config.extruder_max_nozzle_count.values.end(), [](int count) {
        return count > 1;
    });
}

inline bool has_manual_nozzle_mapping(const PrintConfig &config)
{
    if (config.filament_map_mode == FilamentMapMode::fmmNozzleManual)
        return true;

    std::set<int> mapped_nozzles;
    for (int nozzle_slot : config.filament_nozzle_map.values) {
        if (nozzle_slot > 0)
            mapped_nozzles.insert(nozzle_slot);
    }

    return mapped_nozzles.size() > 1 || (!mapped_nozzles.empty() && *mapped_nozzles.rbegin() > 1);
}

inline bool allows_mixed_nozzle_wipe_tower(const PrintConfig &config, const std::vector<unsigned int> &extruders)
{
    return !has_mixed_nozzle_diameters(config, extruders) || has_multi_nozzle_hardware_config(config) || has_manual_nozzle_mapping(config);
}

inline bool requires_explicit_support_filaments(const PrintConfig &config, const std::vector<unsigned int> &extruders)
{
    return has_mixed_nozzle_diameters(config, extruders);
}

} // namespace CutEnhMixedNozzles
} // namespace Slic3r

#endif