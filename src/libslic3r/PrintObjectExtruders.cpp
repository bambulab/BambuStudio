#include "Print.hpp"
#include "libslic3r.h"

#include <cassert>

namespace Slic3r {

// returns 0-based indices of extruders used to print the object (without brim, support and other helper extrusions)
std::vector<unsigned int> PrintObject::object_extruders() const
{
    std::vector<unsigned int> extruders;
    extruders.reserve(this->all_regions().size() * 3);
#if 0
    for (const PrintRegion &region : this->all_regions())
        region.collect_object_printing_extruders(*this->print(), extruders);
#else
    const ModelObject *mo = this->model_object();
    for (const ModelVolume *mv : mo->volumes) {
        std::vector<int> volume_extruders = mv->get_extruders();
        for (int extruder : volume_extruders) {
            assert(extruder > 0);
            extruders.push_back(extruder - 1);
        }
    }
#endif
    sort_remove_duplicates(extruders);
    return extruders;
}

} // namespace Slic3r
