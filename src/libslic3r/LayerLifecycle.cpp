#include "Layer.hpp"

namespace Slic3r {

Layer::~Layer()
{
    this->lower_layer = this->upper_layer = nullptr;
    for (LayerRegion *region : m_regions)
        delete region;
    m_regions.clear();
}

} // namespace Slic3r
