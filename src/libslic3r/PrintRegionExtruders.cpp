#include "Print.hpp"

#include <algorithm>
#include <cassert>

namespace Slic3r {

void PrintRegion::collect_object_printing_extruders(const Print &print, std::vector<unsigned int> &object_extruders) const
{
    // PrintRegion, if used by some PrintObject, shall have all the extruders set to an existing printer extruder.
    // If not, then there must be something wrong with the Print::apply() function.
#ifndef NDEBUG
    // BBS
    auto num_extruders = int(print.config().filament_diameter.size());
    assert(this->config().wall_filament <= num_extruders);
    assert(this->config().sparse_infill_filament <= num_extruders);
    assert(this->config().solid_infill_filament <= num_extruders);
#endif
    collect_object_printing_extruders(print.config(), this->config(), print.has_brim(), object_extruders);
}

} // namespace Slic3r
