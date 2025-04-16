#ifndef OUTER_WALL_CONTOUR_STRATEGY_H
#define OUTER_WALL_CONTOUR_STRATEGY_H

#include "BeadingStrategy.hpp"
namespace Slic3r::Arachne
{

class OuterWallContourStrategy : public BeadingStrategy
{
public:
  OuterWallContourStrategy(BeadingStrategyPtr parent);
  ~OuterWallContourStrategy() override = default;

    Beading compute(coord_t thickness, coord_t bead_count) const override;
    coord_t getOptimalThickness(coord_t bead_count) const override;
    coord_t getTransitionThickness(coord_t lower_bead_count) const override;
    coord_t getOptimalBeadCount(coord_t thickness) const override;
    std::string toString() const override;

    coord_t getTransitioningLength(coord_t lower_bead_count) const override;
    float getTransitionAnchorPos(coord_t lower_bead_count) const override;
    std::vector<coord_t> getNonlinearThicknesses(coord_t lower_bead_count) const override;

protected:
    const BeadingStrategyPtr parent;
};

}
#endif