#ifndef SLIC3R_MIXED_FILAMENT_UTILS_HPP
#define SLIC3R_MIXED_FILAMENT_UTILS_HPP

#include <set>
#include <string>
#include <vector>

namespace Slic3r {

// Parse comma-separated 1-based component IDs, e.g. "1,3" -> {1, 3}.
std::vector<unsigned int> parse_mixed_components(const std::string &str);

// Parse comma-separated ratio values, e.g. "0.7,0.3" -> {0.7, 0.3}.
// Returns equal ratios (1/n each) when str is empty or invalid.
// Normalizes so the sum equals 1.0.
std::vector<double> parse_mixed_ratios(const std::string &str, size_t n_components);

// Returns true if any element in is_mixed is true.
// ConfigOptionBools stores values as std::vector<unsigned char>.
bool has_any_mixed_filament(const std::vector<unsigned char> &is_mixed);

// Check which mixed filament slots have broken component references.
// Returns 0-based indices of mixed slots whose components reference
// filaments beyond num_physical (i.e., deleted filaments).
std::vector<size_t> check_mixed_filament_integrity(
    const std::vector<unsigned char> &is_mixed,
    const std::vector<std::string>   &comp_strs,
    size_t num_physical);

// Expand mixed filament slots in an extruder list to their physical components.
// Input/output are 0-based indices. Non-mixed slots pass through unchanged.
// Result is sorted and deduplicated.
std::vector<unsigned int> expand_mixed_filaments(
    const std::vector<unsigned int> &extruders_0based,
    const std::vector<unsigned char> &is_mixed,
    const std::vector<std::string>  &comp_strs);

// Remap mixed filament component references after a physical filament is deleted.
// del_1based: the 1-based index of the deleted physical filament.
// For each mixed slot:
//   - if component == del_1based -> replace with 0 (sentinel for deleted/unselected)
//   - if component > del_1based -> decrement by 1
void remap_mixed_components_on_delete(
    const std::vector<unsigned char> &is_mixed,
    std::vector<std::string>         &comp_strs,
    unsigned int                      del_1based);

// Check which mixed filament slots have type-mismatched components.
// filament_types: type strings for physical filaments (0-based, size == num_physical).
// Component IDs in comp_strs are 1-based; the function converts to 0-based to look up types.
// Returns 0-based config indices of mixed slots with mismatched component types.
std::vector<size_t> check_mixed_filament_type_consistency(
    const std::vector<unsigned char> &is_mixed,
    const std::vector<std::string>   &comp_strs,
    const std::vector<std::string>   &filament_types);

// Expand mixed-slot IDs in geometric unprintable sets to their physical component IDs.
// Each set entry that corresponds to a mixed slot is replaced by the slot's component
// IDs (0-based). Non-mixed entries pass through unchanged.
void expand_mixed_slots_in_unprintables(
    std::vector<std::set<int>>        &unprintables,
    const std::vector<unsigned char>  &is_mixed,
    const std::vector<std::string>    &comp_strs);

} // namespace Slic3r

#endif // SLIC3R_MIXED_FILAMENT_UTILS_HPP
