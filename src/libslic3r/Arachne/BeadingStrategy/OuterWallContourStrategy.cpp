
#include "OuterWallContourStrategy.hpp"
#include "Point.hpp"

namespace Slic3r::Arachne
{


OuterWallContourStrategy::OuterWallContourStrategy(BeadingStrategyPtr parent)
    : BeadingStrategy(*parent)
    , parent(std::move(parent))
{
}

std::string OuterWallContourStrategy::toString() const
{
    return std::string("OuterWallContourStrategy+") + parent->toString();
}

coord_t OuterWallContourStrategy::getTransitioningLength(coord_t lower_bead_count) const
{
    return parent->getTransitioningLength(lower_bead_count);
}

float OuterWallContourStrategy::getTransitionAnchorPos(coord_t lower_bead_count) const
{
    return parent->getTransitionAnchorPos(lower_bead_count);
}

std::vector<coord_t> OuterWallContourStrategy::getNonlinearThicknesses(coord_t lower_bead_count) const
{
    return parent->getNonlinearThicknesses(lower_bead_count);
}


coord_t OuterWallContourStrategy::getTransitionThickness(coord_t lower_bead_count) const
{
    if(lower_bead_count <= 1)
        return parent->getTransitionThickness(lower_bead_count);
    else if(lower_bead_count == 2 || lower_bead_count ==3)
        return parent->getTransitionThickness(1);
    return parent->getTransitionThickness(lower_bead_count-2);
}


coord_t OuterWallContourStrategy::getOptimalBeadCount(coord_t thickness) const
{
    coord_t parent_bead_count = parent->getOptimalBeadCount(thickness);
    if(parent_bead_count <= 1)
        return parent_bead_count;
    return parent_bead_count + 2;
}


coord_t OuterWallContourStrategy::getOptimalThickness(coord_t bead_count) const
{
    if (bead_count <= 1)
        return parent->getOptimalThickness(bead_count);
    return parent->getOptimalThickness(bead_count - 2) + 2;
}

BeadingStrategy::Beading OuterWallContourStrategy::compute(coord_t thickness, coord_t bead_count) const
{
    if (bead_count <= 1)
        return parent->compute(thickness, bead_count);

    assert(bead_count >= 3);
    Beading ret = parent->compute(thickness, bead_count - 2);
    if(ret.toolpath_locations.size() == 1){
        return ret;
    }
    if(ret.toolpath_locations.size() > 0 ){
        assert(ret.bead_widths.size()>0);
        double location = ret.toolpath_locations.front() + ret.bead_widths.front() / 2;
        double location_reverse = ret.toolpath_locations.back() - ret.bead_widths.back() / 2;
        ret.toolpath_locations.insert(ret.toolpath_locations.begin()+1, location);
        ret.bead_widths.insert(ret.bead_widths.begin()+1, FirstWallContourMarkedWidth);
        ret.toolpath_locations.insert((ret.toolpath_locations.rbegin()+1).base(), location_reverse);
        ret.bead_widths.insert((ret.bead_widths.rbegin()).base(), FirstWallContourMarkedWidth);
    }
    return ret;
}
} // namespace Slic3r::Arachne