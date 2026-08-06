#ifndef SLIC3R_FILAMENT_REMAPPER_HPP
#define SLIC3R_FILAMENT_REMAPPER_HPP

#include <string>
#include <vector>

#include "PrintConfig.hpp"

namespace Slic3r {

class Model;

// Input describing the new filament list and old→new mapping.
// All arrays must have the same length N_new. old_to_new_map size must equal the project's
// current filament count N_old; each entry is a 1-based new filament id in [1, N_new].
// Many-to-one mappings (N_new < N_old) are allowed.
struct FilamentRemapInput
{
    std::vector<std::string>    types;
    std::vector<std::string>    colors;
    std::vector<unsigned char>  is_mixed;
    std::vector<std::string>    mixed_components; // "1,3" or empty for physical
    std::vector<std::string>    mixed_ratios;      // "0.7,0.3" or empty
    std::vector<unsigned char>  mixed_gradient;
    std::vector<int>            old_to_new_map;   // size == N_old, 1-based new ids

    // Per-slot default filament presets already resolved by the caller (size == N_new).
    // Mixed slots should carry a copy of the first component's physical preset.
    std::vector<DynamicPrintConfig> filament_presets;
    std::vector<std::string>        filament_preset_names;
    std::vector<std::string>        filament_ids;
};

struct FilamentRemapResult
{
    bool        success{false};
    std::string error;
};

// Validate CLI-style remap input against the current project filament count.
// Does not mutate model/config.
FilamentRemapResult validate_filament_remap_input(const FilamentRemapInput &input, size_t n_old);

// Remap project filaments:
//  - rebuild all filament parallel arrays from filament_presets + mixed metadata
//  - rewrite Model object/volume extruder assignments and painted MMU facets
//  - rewrite custom tool-change G-codes
//  - rebuild flush_volumes_matrix from colors
//  - reset filament_map / filament_nozzle_map / filament_volume_map defaults
//
// On failure, model/config may be partially modified; caller should abort.
FilamentRemapResult remap_project_filaments(Model &model, DynamicPrintConfig &config, const FilamentRemapInput &input);

// Helper: parse "#RRGGBB" / "#RRGGBBAA" into rgba (alpha defaults to 255).
bool parse_filament_color_rgba(const std::string &color, unsigned char rgba[4]);

} // namespace Slic3r

#endif // SLIC3R_FILAMENT_REMAPPER_HPP
