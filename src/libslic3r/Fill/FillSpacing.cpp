#include "FillBase.hpp"

#include <cassert>
#include <cmath>

namespace Slic3r {

// Calculate a new spacing to fill width with possibly integer number of lines,
// the first and last line being centered at the interval ends.
// This function possibly increases the spacing, never decreases,
// and for a narrow width the increase in spacing may become severe,
// therefore the adjustment is limited to 20% increase.
coord_t Fill::_adjust_solid_spacing(const coord_t width, const coord_t distance)
{
    assert(width >= 0);
    assert(distance > 0);
    // floor(width / distance)
    const auto number_of_intervals = coord_t((width - EPSILON) / distance);
    coord_t    distance_new        = (number_of_intervals == 0) ?
        distance :
        coord_t((width - EPSILON) / number_of_intervals);
    const coordf_t factor = coordf_t(distance_new) / coordf_t(distance);
    assert(factor > 1. - 1e-5);
    // How much could the extrusion width be increased? By 20%.
    const coordf_t factor_max = 1.2;
    if (factor > factor_max)
        distance_new = coord_t(std::floor((coordf_t(distance) * factor_max + 0.5)));
    return distance_new;
}

} // namespace Slic3r
