#ifndef slic3r_FilamentOrderCacheKey_hpp_
#define slic3r_FilamentOrderCacheKey_hpp_

#include "../libslic3r.h"
#include <array>
#include <bitset>
#include <functional>
#include <optional>
#include <vector>

namespace Slic3r {

// BBS: keep layer sets separate: packing three 32-bit sets into one integer aliases
// current filament 33 with next filament 1 when logical capacity exceeds 32.
using FilamentOrderCacheKey = std::array<std::bitset<MAXIMUM_EXTRUDER_NUMBER>, 3>;

inline FilamentOrderCacheKey make_filament_order_cache_key(
    const std::vector<unsigned int>& current,
    const std::vector<unsigned int>& next,
    const std::optional<unsigned int>& previous,
    bool use_forecast)
{
    FilamentOrderCacheKey key{};
    for (unsigned int id : current)
        key[0].set(id);
    if (use_forecast)
        for (unsigned int id : next)
            key[1].set(id);
    if (previous)
        key[2].set(*previous);
    return key;
}

struct FilamentOrderCacheKeyHash {
    std::size_t operator()(const FilamentOrderCacheKey& key) const
    {
        std::size_t result = 0;
        for (const auto& set : key)
            result ^= std::hash<std::bitset<MAXIMUM_EXTRUDER_NUMBER>>{}(set) +
                      std::size_t(0x9e3779b9) + (result << 6) + (result >> 2);
        return result;
    }
};

} // namespace Slic3r

#endif
