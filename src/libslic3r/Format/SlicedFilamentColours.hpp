#ifndef slic3r_SlicedFilamentColours_hpp_
#define slic3r_SlicedFilamentColours_hpp_

#include <cstddef>

namespace Slic3r {

// BBS: apply the tested compatibility rule to archives without model geometry.
// Preserve legacy projects and physical filament indices. In particular, do not
// compact an interleaved list: G-code and slice metadata retain those indices.
template <class MixedFlags>
inline std::size_t sliced_filament_colour_count(std::size_t colour_count,
                                                const MixedFlags& is_mixed,
                                                bool skip_model)
{
    constexpr std::size_t legacy_project_capacity = 32;
    if (!skip_model || colour_count <= legacy_project_capacity ||
        is_mixed.size() != colour_count)
        return colour_count;

    std::size_t retained_count = colour_count;
    while (retained_count > 0 && is_mixed[retained_count - 1])
        --retained_count;

    // BBS: an all-mixed or malformed project must not acquire an empty colour list.
    return retained_count == 0 ? colour_count : retained_count;
}

} // namespace Slic3r

#endif
