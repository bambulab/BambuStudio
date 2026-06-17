#ifndef slic3r_ShortestPath_hpp_
#define slic3r_ShortestPath_hpp_

#include "libslic3r.h"
#include "ExtrusionEntity.hpp"
#include "Point.hpp"

#include <utility>
#include <vector>

namespace ClipperLib { class PolyNode; }

namespace Slic3r {

std::vector<size_t> 				 chain_points(const Points &points, Point *start_near = nullptr);
std::vector<size_t> 				 chain_expolygons(const ExPolygons &input_exploy);

std::vector<std::pair<size_t, bool>> chain_extrusion_entities(std::vector<ExtrusionEntity*> &entities, const Point *start_near = nullptr);
void                                 reorder_extrusion_entities(std::vector<ExtrusionEntity*> &entities, const std::vector<std::pair<size_t, bool>> &chain);
void                                 chain_and_reorder_extrusion_entities(std::vector<ExtrusionEntity*> &entities, const Point *start_near = nullptr);

std::vector<std::pair<size_t, bool>> chain_extrusion_paths(std::vector<ExtrusionPath> &extrusion_paths, const Point *start_near = nullptr);
void                                 reorder_extrusion_paths(std::vector<ExtrusionPath> &extrusion_paths, std::vector<std::pair<size_t, bool>> &chain);
void                                 chain_and_reorder_extrusion_paths(std::vector<ExtrusionPath> &extrusion_paths, const Point *start_near = nullptr);
template<typename T> inline void reorder_by_shortest_traverse(std::vector<T> &polylines_out)
{
    Points start_point;
    start_point.reserve(polylines_out.size());
    for (const T contour : polylines_out) start_point.push_back(contour.points.front());

    std::vector<Points::size_type> order = chain_points(start_point);

    std::vector<T> Temp = polylines_out;
    polylines_out.erase(polylines_out.begin(), polylines_out.end());

    for (size_t i:order) polylines_out.emplace_back(std::move(Temp[i]));
}

std::vector<ClipperLib::PolyNode*>	 chain_clipper_polynodes(const Points &points, const std::vector<ClipperLib::PolyNode*> &items);

} // namespace Slic3r

#endif /* slic3r_ShortestPath_hpp_ */
