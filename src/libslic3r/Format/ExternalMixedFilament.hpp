#ifndef slic3r_Format_ExternalMixedFilament_hpp_
#define slic3r_Format_ExternalMixedFilament_hpp_

#include <string>
#include <vector>

namespace Slic3r {

// Mixed filaments of a third-party 3MF project that stores all of its mixes in a single
// "mixed_filament_definitions" project setting. Rows are ';' separated, each row being
// "a,b,enabled,custom,mix_b_percent[,legacy][,tagged tokens...]". The source addresses a mix
// as virtual filament id (num_physical + 1 + index among the enabled, non-deleted rows).
struct ExternalMixedFilamentInfo
{
    std::vector<std::string> physical_colours; // filament_colour of the physical slots
    std::vector<std::string> physical_types;   // filament_type of the physical slots, may be shorter
    std::string              definitions;      // raw mixed_filament_definitions

    bool empty() const { return definitions.empty() || physical_colours.size() < 2; }
};

struct ExternalMixedSlot
{
    std::vector<unsigned int> components; // 1-based physical filament ids, distinct
    std::vector<int>          ratios;     // percentages aligned with components, each > 0, sum = 100
};

// Converts the definitions into mixed slots in the order of their virtual filament ids.
// Rows the source never exposes as a filament (deleted, disabled, malformed) are dropped, so
// slot i of the result is the source's virtual filament num_physical + 1 + i.
std::vector<ExternalMixedSlot> parse_external_mixed_definitions(const std::string &definitions, size_t num_physical);

} // namespace Slic3r

#endif // slic3r_Format_ExternalMixedFilament_hpp_
