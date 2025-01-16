#ifndef slic3r_Clipper2Utils_hpp_
#define slic3r_Clipper2Utils_hpp_

#include "Polygon.hpp"
#include "Polyline.hpp"

namespace Slic3r {

Slic3r::Polylines  intersection_pl_2(const Slic3r::Polylines& subject, const Slic3r::Polygons& clip);
Slic3r::Polylines  diff_pl_2(const Slic3r::Polylines& subject, const Slic3r::Polygons& clip);
ExPolygons         union_ex2(const Polygons &expolygons);
ExPolygons         union_ex2(const ExPolygons &expolygons);
}

#endif

