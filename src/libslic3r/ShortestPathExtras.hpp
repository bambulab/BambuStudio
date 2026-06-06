#ifndef slic3r_ShortestPathExtras_hpp_
#define slic3r_ShortestPathExtras_hpp_

#include "ShortestPath.hpp"

namespace Slic3r {

class Print;
class PrintObject;
struct PrintInstance;

// Chain instances of print objects by an approximate shortest path.
// Returns the print instances in traversal order.
std::vector<const PrintInstance*> chain_print_object_instances(const std::vector<const PrintObject*>& print_objects, const Point* start_near);
std::vector<const PrintInstance*> chain_print_object_instances(const Print& print);

Polylines chain_polylines(Polylines &&src, const Point *start_near = nullptr);
inline Polylines chain_polylines(const Polylines &src, const Point *start_near = nullptr) { Polylines tmp(src); return chain_polylines(std::move(tmp), start_near); }

// Chain lines into polylines.
Polylines chain_lines(const std::vector<Line>& lines, double point_distance_epsilon);

} // namespace Slic3r

#endif // slic3r_ShortestPathExtras_hpp_
