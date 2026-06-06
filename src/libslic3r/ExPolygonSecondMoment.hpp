#ifndef slic3r_ExPolygonSecondMoment_hpp_
#define slic3r_ExPolygonSecondMoment_hpp_

#include "ExPolygon.hpp"

namespace Slic3r {

bool compSecondMoment(const ExPolygons &expolys, double &smExpolysX, double &smExpolysY);

} // namespace Slic3r

#endif
