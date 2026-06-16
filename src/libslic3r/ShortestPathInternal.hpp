#ifndef slic3r_ShortestPathInternal_hpp_
#define slic3r_ShortestPathInternal_hpp_

#include "KDTreeIndirect.hpp"

#include <cassert>
#include <utility>
#include <vector>

namespace Slic3r {

// Naive implementation of the Traveling Salesman Problem, it works by always taking the next closest neighbor.
// This implementation will always produce valid result even if some segments cannot reverse.
template<typename EndPointType, typename KDTreeType, typename CouldReverseFunc>
std::vector<std::pair<size_t, bool>> chain_segments_closest_point(std::vector<EndPointType> &end_points, KDTreeType &kdtree, CouldReverseFunc &could_reverse_func, EndPointType &first_point)
{
    assert((end_points.size() & 1) == 0);
    size_t num_segments = end_points.size() / 2;
    assert(num_segments >= 2);
    for (EndPointType &ep : end_points)
        ep.chain_id = 0;
    std::vector<std::pair<size_t, bool>> out;
    out.reserve(num_segments);
    size_t first_point_idx = &first_point - end_points.data();
    out.emplace_back(first_point_idx / 2, (first_point_idx & 1) != 0);
    first_point.chain_id = 1;
    size_t this_idx = first_point_idx ^ 1;
    for (int iter = (int)num_segments - 2; iter >= 0; -- iter) {
        EndPointType &this_point = end_points[this_idx];
        this_point.chain_id = 1;
        size_t next_idx = find_closest_point(kdtree, this_point.pos,
            [this_idx, &end_points, &could_reverse_func](size_t idx) {
                return (idx ^ this_idx) > 1 && end_points[idx].chain_id == 0 && ((idx & 1) == 0 || could_reverse_func(idx >> 1));
        });
        assert(next_idx < end_points.size());
        EndPointType &end_point = end_points[next_idx];
        end_point.chain_id = 1;
        assert((next_idx & 1) == 0 || could_reverse_func(next_idx >> 1));
        out.emplace_back(next_idx / 2, (next_idx & 1) != 0);
        this_idx = next_idx ^ 1;
    }
#ifndef NDEBUG
    assert(end_points[this_idx].chain_id == 0);
    for (EndPointType &ep : end_points)
        assert(&ep == &end_points[this_idx] || ep.chain_id == 1);
#endif /* NDEBUG */
    return out;
}

} // namespace Slic3r

#endif // slic3r_ShortestPathInternal_hpp_
