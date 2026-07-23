#include "PaintReproject.hpp"

#include "libslic3r.h"
#include "Model.hpp"
#include "TriangleMesh.hpp"
#include "TriangleSelector.hpp"
#include "AABBTreeIndirect.hpp"

#include <boost/log/trivial.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace Slic3r {

// ---------------------------------------------------------------------------
// 2D planar helpers (shared by the coplanar-overlap sampling path).
// ---------------------------------------------------------------------------

static double cross_2d(const Vec2d &a, const Vec2d &b)
{
    return a.x() * b.y() - a.y() * b.x();
}

static Vec2d project_to_2d(const Vec3f &point, int dropped_axis)
{
    switch (dropped_axis) {
    case X: return Vec2d(point.y(), point.z());
    case Y: return Vec2d(point.x(), point.z());
    default: return Vec2d(point.x(), point.y());
    }
}

static double polygon_signed_area(const std::vector<Vec2d> &polygon)
{
    double area = 0.0;
    for (size_t i = 0; i < polygon.size(); ++i)
        area += cross_2d(polygon[i], polygon[(i + 1) % polygon.size()]);
    return 0.5 * area;
}

static std::vector<Vec2d> clip_polygon_by_triangle(std::vector<Vec2d> polygon, std::array<Vec2d, 3> clip)
{
    const double clip_signed_area = 0.5 * (
        cross_2d(clip[0], clip[1]) + cross_2d(clip[1], clip[2]) + cross_2d(clip[2], clip[0]));
    if (clip_signed_area < 0.0)
        std::swap(clip[1], clip[2]);

    for (int edge_idx = 0; edge_idx < 3 && !polygon.empty(); ++edge_idx) {
        const Vec2d edge_start = clip[edge_idx];
        const Vec2d edge_end   = clip[(edge_idx + 1) % 3];
        const Vec2d edge       = edge_end - edge_start;
        const double edge_epsilon = std::max(1e-12, edge.squaredNorm() * 1e-12);
        auto signed_distance = [&edge_start, &edge](const Vec2d &point) {
            return cross_2d(edge, point - edge_start);
        };
        auto inside = [&signed_distance, edge_epsilon](const Vec2d &point) {
            return signed_distance(point) >= -edge_epsilon;
        };
        auto intersection = [&signed_distance, edge_epsilon](const Vec2d &from, const Vec2d &to) -> Vec2d {
            const double from_distance = signed_distance(from);
            const double to_distance = signed_distance(to);
            const double denom = from_distance - to_distance;
            if (std::abs(denom) <= edge_epsilon)
                return std::abs(from_distance) <= std::abs(to_distance) ? from : to;
            const double t = std::clamp(from_distance / denom, 0.0, 1.0);
            return (from + t * (to - from)).eval();
        };

        std::vector<Vec2d> output;
        output.reserve(polygon.size() + 1);
        Vec2d previous = polygon.back();
        bool previous_inside = inside(previous);
        for (const Vec2d &current : polygon) {
            const bool current_inside = inside(current);
            if (current_inside != previous_inside)
                output.emplace_back(intersection(previous, current));
            if (current_inside)
                output.emplace_back(current);
            previous = current;
            previous_inside = current_inside;
        }
        polygon = std::move(output);
    }
    return polygon;
}

static double triangle_overlap_area(const std::array<Vec3f, 3> &lhs, const std::array<Vec3f, 3> &rhs, int dropped_axis)
{
    std::vector<Vec2d> lhs_polygon {
        project_to_2d(lhs[0], dropped_axis),
        project_to_2d(lhs[1], dropped_axis),
        project_to_2d(lhs[2], dropped_axis)
    };
    std::array<Vec2d, 3> rhs_triangle {
        project_to_2d(rhs[0], dropped_axis),
        project_to_2d(rhs[1], dropped_axis),
        project_to_2d(rhs[2], dropped_axis)
    };
    const std::vector<Vec2d> overlap = clip_polygon_by_triangle(std::move(lhs_polygon), rhs_triangle);
    return overlap.size() < 3 ? 0.0 : std::abs(polygon_signed_area(overlap));
}

static std::array<Vec3f, 3> transform_triangle(
    const std::array<Vec3f, 3> &triangle, const Transform3d &transformation)
{
    return {
        (transformation * triangle[0].cast<double>()).cast<float>(),
        (transformation * triangle[1].cast<double>()).cast<float>(),
        (transformation * triangle[2].cast<double>()).cast<float>()
    };
}

// ---------------------------------------------------------------------------
// PaintReprojector: samples a source annotation for the subdivision engine.
//
// It owns a TriangleSelector deserialized from the source annotation, the source
// face adjacency (for the coplanar neighborhood walk) and, in the geometric mode,
// an AABB tree over the source mesh (for nearest-face / nearest-point queries).
// ---------------------------------------------------------------------------

class PaintReprojector
{
public:
    // max_sample_distance: when >= 0, a destination face whose centroid is farther
    // than this from the source surface is treated as geometry the repair newly
    // created (e.g. a hole-fill patch) and is left unpainted instead of inheriting
    // the nearest source paint. Negative disables the cull (geometric provenance
    // for cutting always samples the nearest face).
    PaintReprojector(const TriangleMesh &source_mesh, const FacetsAnnotation &annotation, bool build_aabb,
                     double max_sample_distance = -1.0)
        : m_source_mesh(source_mesh)
        , m_selector(source_mesh)
        , m_source_root_count(int(source_mesh.its.indices.size()))
        , m_face_neighbors(its_face_neighbors(source_mesh.its))
        , m_max_sample_distance_sq(max_sample_distance >= 0.0 ? max_sample_distance * max_sample_distance : -1.0)
    {
        m_selector.deserialize(annotation.get_data(), true);
        if (build_aabb && !source_mesh.its.indices.empty()) {
            m_aabb = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
                source_mesh.its.vertices, source_mesh.its.indices);
            m_has_aabb = true;
        }
    }

    size_t source_node_count()
    {
        return m_selector.get_triangles().size();
    }

    size_t fallback_count() const
    {
        return m_fallback_count;
    }

    size_t rebound_count() const
    {
        return m_rebound_count;
    }

    // Resolve the set of source roots whose in-plane overlap with the target
    // fragment is significant. Re-triangulation along the cut edge can produce a
    // fragment that straddles the diagonal shared by the two coplanar triangles of
    // an original quad face, so the fragment may legitimately belong to more than
    // one source root. Binding it to a single root would drop the paint over the
    // sibling triangle and leave jagged holes along the diagonal. The result is
    // ordered by decreasing overlap; the first entry is the dominant source.
    std::vector<int> resolve_source_roots(
        int preferred_root, const std::array<Vec3f, 3> &target_triangle_in_source)
    {
        std::vector<int> resolved;
        const int dropped_axis = projection_axis(target_triangle_in_source);
        const double target_area =
            triangle_overlap_area(target_triangle_in_source, target_triangle_in_source, dropped_axis);
        if (target_area <= std::numeric_limits<double>::epsilon()) {
            resolved.push_back(preferred_root);
            return resolved;
        }

        const double preferred_overlap =
            root_overlap_area(preferred_root, target_triangle_in_source, dropped_axis);
        // Fast path: provenance already covers essentially the whole fragment, so it
        // lies inside a single source triangle. This is the common case for uncut
        // faces and interior cut fragments.
        if (preferred_overlap >= target_area * (1.0 - PreferredCoverageEpsilon)) {
            resolved.push_back(preferred_root);
            return resolved;
        }

        // Gather candidate source roots from the coplanar neighborhood of the
        // provenance face only (a small breadth-first walk over edge neighbors that
        // share the same plane). A cut fragment can only straddle the sibling
        // triangles of its own flat face, which are edge-connected and coplanar, so
        // this finds them without an O(all-faces) scan that would hang large meshes.
        std::vector<std::pair<int, double>> candidates;
        std::vector<int> frontier;
        std::vector<int> visited;
        frontier.push_back(preferred_root);
        while (!frontier.empty() && int(visited.size()) < MaxCoplanarSearch) {
            const int root = frontier.back();
            frontier.pop_back();
            if (root < 0 || root >= m_source_root_count)
                continue;
            if (std::find(visited.begin(), visited.end(), root) != visited.end())
                continue;
            visited.push_back(root);

            const double overlap =
                root_overlap_area(root, target_triangle_in_source, dropped_axis);
            if (overlap > target_area * MinRootOverlapFraction)
                candidates.emplace_back(root, overlap);

            // Only expand through coplanar edge neighbors so the walk stays on the
            // flat face and does not wander across the whole mesh.
            if (root < int(m_face_neighbors.size())) {
                const Vec3i &neighbors = m_face_neighbors[root];
                for (int e = 0; e < 3; ++e) {
                    const int neighbor = neighbors[e];
                    if (neighbor >= 0 && neighbor != preferred_root &&
                        normals_parallel(preferred_root, neighbor))
                        frontier.push_back(neighbor);
                }
            }
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const std::pair<int, double> &a, const std::pair<int, double> &b) {
                      return a.second > b.second;
                  });

        // No coplanar source overlaps the fragment: it is a genuinely new surface
        // (e.g. the cut cross-section cap). Keep provenance so measure() can point
        // sample and, failing that, leave it unpainted.
        if (candidates.empty()) {
            resolved.push_back(preferred_root);
            return resolved;
        }

        double covered = 0.0;
        for (const auto &candidate : candidates) {
            resolved.push_back(candidate.first);
            covered += candidate.second;
            if (resolved.size() >= MaxSourceRootSet ||
                covered >= target_area * (1.0 - PreferredCoverageEpsilon))
                break;
        }

        if (resolved.front() != preferred_root)
            ++m_rebound_count;

        return resolved;
    }

    // Coplanar-overlap sampling (cutting): measure paint area of the destination
    // fragment against the coplanar source roots it overlaps in-plane.
    TriangleSelector::FacetSubdivisionMeasurement measure_overlap(
        const std::vector<int> &source_roots,
        const std::array<Vec3f, 3> &target_triangle_in_source,
        const std::array<Vec3f, 3> &target_triangle_in_destination)
    {
        const double surface_area = 0.5 * double(
            (target_triangle_in_destination[1] - target_triangle_in_destination[0])
                .cross(target_triangle_in_destination[2] - target_triangle_in_destination[0]).norm());
        if (surface_area <= std::numeric_limits<double>::epsilon())
            return {};

        const auto &triangles = m_selector.get_triangles();
        const auto root_valid = [&](int root) {
            return root >= 0 && root < int(triangles.size()) && triangles[root].valid();
        };

        // All roots in the set are coplanar, so any valid one defines the shared
        // projection plane used to measure overlaps.
        int primary_root = -1;
        for (int root : source_roots) {
            if (root_valid(root)) {
                primary_root = root;
                break;
            }
        }
        if (primary_root < 0) {
            ++m_fallback_count;
            return {EnforcerBlockerType::NONE, surface_area, 0.0};
        }

        const int dropped_axis = projection_axis(primary_root);
        const double projected_area =
            triangle_overlap_area(target_triangle_in_source, target_triangle_in_source, dropped_axis);
        const double area_epsilon = std::max(1e-12, projected_area * 1e-10);
        // Fixed boundary leaf-size floor derived only from the source root, so it is
        // identical for both cut halves that share this root. Using a per-half or
        // source-leaf-dependent floor made the two halves stop at different
        // resolutions, whose staircase phase mismatch showed up as an offset along
        // the cut line. A root-relative depth keeps refinement bounded yet uniform.
        const std::array<Vec3f, 3> primary_root_triangle = triangle_vertices(primary_root);
        const double primary_root_area =
            triangle_overlap_area(primary_root_triangle, primary_root_triangle, dropped_axis);
        const double boundary_leaf_floor = primary_root_area * BoundaryLeafAreaRatio;
        // Accumulate paint area across the whole source-root set. A fragment that
        // straddles the diagonal of an original quad face overlaps both coplanar
        // sibling triangles; summing their overlaps recovers the paint on both
        // sides instead of dropping the minority side.
        StateAreaInfo area_info;
        for (int root : source_roots) {
            if (root_valid(root))
                collect_overlap_areas(
                    root, target_triangle_in_source, dropped_axis, area_epsilon, area_info);
        }

        if (area_info.total_area <= area_epsilon) {
            // The target face does not measurably overlap any source leaf. This
            // happens for the thin slivers produced by re-triangulation along the
            // cut edge, whose projected area degenerates. Point-sample the source
            // faces at the fragment centroid so the cut-edge paint is preserved
            // instead of leaving a notch. If the centroid lies outside every source
            // face the fragment is genuinely off-face, so keep it unpainted to
            // avoid spreading paint into unpainted areas.
            ++m_fallback_count;
            const Vec3f centroid =
                (target_triangle_in_source[0] + target_triangle_in_source[1] + target_triangle_in_source[2]) / 3.f;
            for (int root : source_roots) {
                if (!root_valid(root))
                    continue;
                const int containing_leaf = m_selector.select_unsplit_triangle(centroid, root);
                if (containing_leaf >= 0) {
                    const auto &leaf = triangles[containing_leaf];
                    if (leaf.valid() && !leaf.is_split())
                        return {leaf.get_state(), surface_area, 0.0};
                }
            }
            return {EnforcerBlockerType::NONE, surface_area, 0.0};
        }

        size_t dominant_state = 0;
        size_t present_states = 0;
        for (size_t state = 0; state < area_info.area_by_state.size(); ++state) {
            if (area_info.area_by_state[state] > area_info.area_by_state[dominant_state])
                dominant_state = state;
            if (area_info.area_by_state[state] > area_info.total_area * MixedStateAreaFraction)
                ++present_states;
        }

        // Refine a boundary-crossing leaf until it drops below the fixed root-
        // relative floor, then terminate. The floor is deep enough that its
        // half-leaf boundary bias is sub-visible, and because it depends only on
        // the shared source root it is identical for both cut halves, so their
        // boundaries line up at the cut line instead of drifting apart.
        const bool below_floor =
            primary_root_area > 0.0 && projected_area <= boundary_leaf_floor;

        const bool crosses_boundary = present_states >= 2;
        const double dominant_fraction =
            area_info.area_by_state[dominant_state] / area_info.total_area;
        double error_area;
        if (below_floor)
            error_area = 0.0;
        else if (crosses_boundary)
            error_area = surface_area;
        else
            error_area = surface_area * std::max(0.0, 1.0 - dominant_fraction);
        return {static_cast<EnforcerBlockerType>(dominant_state), surface_area, error_area};
    }

    // 3D nearest-point sampling (repair/boolean): sample the source paint at the
    // fragment's vertices and centroid by snapping each to the nearest point on the
    // source mesh and reading the leaf state there. The fragment is labeled with the
    // dominant sampled state and reports an area-proportional error (the fraction of
    // samples that disagree with the dominant, times the fragment area). Boundary
    // faces are NOT forced down to a fixed size floor; the subdivision driver refines
    // whichever leaves carry the most mislabeled area and stops once the residual
    // error drops under the relative limit -- adaptive area-error refinement. A fixed
    // size floor is still applied as a pure safety cap so a pathological boundary
    // (e.g. thin seam strokes that stay mixed at every scale) cannot subdivide
    // without bound.
    TriangleSelector::FacetSubdivisionMeasurement measure_point_sample(
        const std::array<Vec3f, 3> &target_triangle_in_source,
        const std::array<Vec3f, 3> &target_triangle_in_destination)
    {
        const double surface_area = 0.5 * double(
            (target_triangle_in_destination[1] - target_triangle_in_destination[0])
                .cross(target_triangle_in_destination[2] - target_triangle_in_destination[0]).norm());
        if (surface_area <= std::numeric_limits<double>::epsilon())
            return {};
        if (!m_has_aabb) {
            ++m_fallback_count;
            return {EnforcerBlockerType::NONE, surface_area, 0.0};
        }

        const Vec3f centroid =
            (target_triangle_in_source[0] + target_triangle_in_source[1] + target_triangle_in_source[2]) / 3.f;
        size_t centroid_hit_face = size_t(-1);
        Vec3f  centroid_closest_point;
        const double centroid_distance_sq = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(
            m_source_mesh.its.vertices, m_source_mesh.its.indices, m_aabb, centroid,
            centroid_hit_face, centroid_closest_point);
        if (centroid_distance_sq < 0.0 || centroid_hit_face == size_t(-1) ||
            int(centroid_hit_face) >= m_source_root_count) {
            ++m_fallback_count;
            return {EnforcerBlockerType::NONE, surface_area, 0.0};
        }
        // New-geometry cull: the repair may add faces (e.g. hole-fill patches) that
        // sit off the original surface. Such faces are farther from the source mesh
        // than the model-relative tolerance, so leave them unpainted across every
        // layer instead of inheriting the nearest source paint.
        if (m_max_sample_distance_sq >= 0.0 && centroid_distance_sq > m_max_sample_distance_sq)
            return {EnforcerBlockerType::NONE, surface_area, 0.0};

        const std::array<Vec3f, 4> sample_points {
            target_triangle_in_source[0],
            target_triangle_in_source[1],
            target_triangle_in_source[2],
            centroid
        };
        std::array<int, StateCount> sample_counts {};
        int sampled = 0;
        for (const Vec3f &point : sample_points) {
            const size_t state = static_cast<size_t>(sample_state(point));
            if (state < sample_counts.size()) {
                ++sample_counts[state];
                ++sampled;
            }
        }
        if (sampled == 0)
            return {EnforcerBlockerType::NONE, surface_area, 0.0};

        size_t dominant_state = 0;
        for (size_t state = 1; state < sample_counts.size(); ++state)
            if (sample_counts[state] > sample_counts[dominant_state])
                dominant_state = state;

        // Area-proportional error: the share of samples that disagree with the
        // dominant state, scaled by the fragment area. A uniform fragment reports
        // zero error (no refinement); a boundary fragment reports a positive error
        // that shrinks as it subdivides, so refinement is driven by area error alone.
        const int minority_samples = sampled - sample_counts[dominant_state];
        double error_area = surface_area * (double(minority_samples) / double(sampled));

        // Safety floor (not a forced target): normal boundaries converge by area
        // error well before this, but a pathological boundary -- e.g. thin seam
        // strokes whose 4 samples stay mixed at every scale because the boundary is
        // narrower than the fragment -- would otherwise never reach zero error and
        // keep subdividing up to the per-face node budget, freezing the repair. Once
        // a fragment shrinks below a fixed fraction of its source root, report zero
        // error so worst-case work per face stays bounded.
        if (error_area > 0.0) {
            const int nearest_root = int(centroid_hit_face);
            const int dropped_axis = projection_axis(target_triangle_in_source);
            const double leaf_area = triangle_overlap_area(
                target_triangle_in_source, target_triangle_in_source, dropped_axis);
            const std::array<Vec3f, 3> root_triangle = triangle_vertices(nearest_root);
            const double root_area =
                triangle_overlap_area(root_triangle, root_triangle, dropped_axis);
            if (root_area > 0.0 && leaf_area <= root_area * BoundaryLeafAreaRatio)
                error_area = 0.0;
        }
        return {static_cast<EnforcerBlockerType>(dominant_state), surface_area, error_area};
    }

private:
    static constexpr size_t StateCount =
        static_cast<size_t>(EnforcerBlockerType::ExtruderMax) + 1;
    // A state must cover at least this fraction of a target leaf's overlap to be
    // counted as present, so numerical slivers don't flag a leaf as mixed.
    static constexpr double MixedStateAreaFraction = 1e-3;
    // When a source root already covers (1 - eps) of the target fragment it fully
    // contains it, so the multi-root search can stop early.
    static constexpr double PreferredCoverageEpsilon = 1e-4;
    // A source root must cover at least this fraction of the target fragment to
    // join its source-root set, so distant coplanar faces don't add speckles.
    static constexpr double MinRootOverlapFraction = 1e-2;
    // Upper bound on the source-root set size. A cut fragment normally straddles at
    // most the two sibling triangles of one quad face; the cap only guards against
    // pathological coplanar fans.
    static constexpr size_t MaxSourceRootSet = 4;
    // Upper bound on how many faces the coplanar neighborhood walk visits per
    // straddle fragment. Keeps provenance resolution O(1) instead of O(all faces),
    // which is what previously hung on densely tessellated meshes.
    static constexpr int MaxCoplanarSearch = 32;
    // Boundary leaf-size floor as a fraction of the source root area. 1/4^7 means a
    // boundary leaf edge is the root edge / 2^7 = / 128, i.e. sub-0.2mm on a 25mm
    // face, so the residual half-leaf boundary bias is not visible. Depends only on
    // the source root, so both cut halves refine boundaries to the same resolution.
    static constexpr double BoundaryLeafAreaRatio = 1.0 / 16384.0; // 4^7
    struct StateAreaInfo {
        std::array<double, StateCount> area_by_state {};
        double total_area { 0.0 };
    };

    std::array<Vec3f, 3> triangle_vertices(int triangle_idx)
    {
        const auto &triangle = m_selector.get_triangles()[triangle_idx];
        const auto &vertices = m_selector.get_vertices();
        return {
            vertices[triangle.verts_idxs[0]].v,
            vertices[triangle.verts_idxs[1]].v,
            vertices[triangle.verts_idxs[2]].v
        };
    }

    // Snap a source-frame point to the nearest point on the source mesh and read
    // the (sub-triangle) leaf state there.
    EnforcerBlockerType sample_state(const Vec3f &point_in_source)
    {
        if (!m_has_aabb)
            return EnforcerBlockerType::NONE;
        size_t hit_face = size_t(-1);
        Vec3f  closest_point;
        const double distance_squared = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(
            m_source_mesh.its.vertices, m_source_mesh.its.indices, m_aabb, point_in_source, hit_face, closest_point);
        if (distance_squared < 0.0 || hit_face == size_t(-1) || int(hit_face) >= m_source_root_count)
            return EnforcerBlockerType::NONE;
        const int leaf = m_selector.select_unsplit_triangle(closest_point, int(hit_face));
        if (leaf < 0)
            return EnforcerBlockerType::NONE;
        const auto &triangles = m_selector.get_triangles();
        if (leaf >= int(triangles.size()) || !triangles[leaf].valid() || triangles[leaf].is_split())
            return EnforcerBlockerType::NONE;
        return triangles[leaf].get_state();
    }

    double root_overlap_area(
        int source_root,
        const std::array<Vec3f, 3> &target_triangle,
        int dropped_axis)
    {
        const auto &triangles = m_selector.get_triangles();
        if (source_root < 0 || source_root >= m_source_root_count ||
            source_root >= int(triangles.size()) || !triangles[source_root].valid())
            return 0.0;

        const std::array<Vec3f, 3> source_triangle = triangle_vertices(source_root);
        const Vec3d source_edge_1 =
            (source_triangle[1] - source_triangle[0]).cast<double>();
        const Vec3d source_edge_2 =
            (source_triangle[2] - source_triangle[0]).cast<double>();
        const Vec3d source_normal = source_edge_1.cross(source_edge_2);
        const double normal_length = source_normal.norm();
        if (normal_length <= std::numeric_limits<double>::epsilon())
            return 0.0;

        const double edge_scale = std::max(source_edge_1.norm(), source_edge_2.norm());
        const double plane_tolerance = std::max(1e-5, edge_scale * 1e-5);
        const Vec3d unit_normal = source_normal / normal_length;
        const Vec3d source_origin = source_triangle[0].cast<double>();
        for (const Vec3f &point : target_triangle)
            if (std::abs((point.cast<double>() - source_origin).dot(unit_normal)) > plane_tolerance)
                return 0.0;

        return triangle_overlap_area(target_triangle, source_triangle, dropped_axis);
    }

    void collect_overlap_areas(int triangle_idx,
                               const std::array<Vec3f, 3> &target_triangle,
                               int dropped_axis,
                               double area_epsilon,
                               StateAreaInfo &info)
    {
        const auto &triangle = m_selector.get_triangles()[triangle_idx];
        if (!triangle.valid())
            return;

        const std::array<Vec3f, 3> source_triangle = triangle_vertices(triangle_idx);
        const double overlap_area =
            triangle_overlap_area(target_triangle, source_triangle, dropped_axis);
        if (overlap_area <= area_epsilon)
            return;

        if (!triangle.is_split()) {
            const size_t state = static_cast<size_t>(triangle.get_state());
            if (state < info.area_by_state.size()) {
                info.area_by_state[state] += overlap_area;
                info.total_area += overlap_area;
            }
            return;
        }

        const int child_count = triangle.number_of_split_sides() + 1;
        for (int child_idx = 0; child_idx < child_count; ++child_idx)
            collect_overlap_areas(
                triangle.children[child_idx], target_triangle, dropped_axis, area_epsilon, info);
    }

    static int projection_axis(const std::array<Vec3f, 3> &triangle)
    {
        const Vec3f normal = (triangle[1] - triangle[0]).cross(triangle[2] - triangle[0]).cwiseAbs();
        if (normal.x() >= normal.y() && normal.x() >= normal.z()) return X;
        if (normal.y() >= normal.z()) return Y;
        return Z;
    }

    // Whether two source roots have (anti)parallel normals. Combined with sharing an
    // edge this means they are coplanar, so a cut fragment may straddle both.
    bool normals_parallel(int root_a, int root_b)
    {
        if (root_a < 0 || root_a >= m_source_root_count ||
            root_b < 0 || root_b >= m_source_root_count)
            return false;
        const std::array<Vec3f, 3> ta = triangle_vertices(root_a);
        const std::array<Vec3f, 3> tb = triangle_vertices(root_b);
        Vec3d na = (ta[1] - ta[0]).cast<double>().cross((ta[2] - ta[0]).cast<double>());
        Vec3d nb = (tb[1] - tb[0]).cast<double>().cross((tb[2] - tb[0]).cast<double>());
        const double la = na.norm();
        const double lb = nb.norm();
        if (la <= std::numeric_limits<double>::epsilon() ||
            lb <= std::numeric_limits<double>::epsilon())
            return false;
        return std::abs(na.dot(nb) / (la * lb)) >= 1.0 - 1e-4;
    }

    int projection_axis(int source_root)
    {
        return projection_axis(triangle_vertices(source_root));
    }

    const TriangleMesh &m_source_mesh;
    TriangleSelector m_selector;
    int m_source_root_count { 0 };
    std::vector<Vec3i> m_face_neighbors;
    AABBTreeIndirect::Tree3f m_aabb;
    bool m_has_aabb { false };
    // Squared distance threshold beyond which a destination face is considered
    // newly created by the repair and skipped; < 0 disables the cull.
    double m_max_sample_distance_sq { -1.0 };
    size_t m_fallback_count { 0 };
    size_t m_rebound_count { 0 };
};

// ---------------------------------------------------------------------------
// Subdivision driver.
// ---------------------------------------------------------------------------

enum class ReprojectMode { OverlapCoplanar, PointNearest };

static constexpr double ReprojectRelativeErrorLimit = 1e-3;
static constexpr double ReprojectAbsoluteErrorEpsilon = 1e-8;
// Absolute cap on the per-face convergence target, expressed as a fraction of the
// whole mesh area. The per-face target is relative_error_limit * face_area, which
// becomes vanishingly small for tiny faces and would force deep subdivision to chase
// a negligible absolute area. Flooring the target at this model-relative value lets
// small faces converge early so they no longer dominate the compute time.
static constexpr double ReprojectAbsoluteTargetAreaFraction = 1e-6;
static constexpr int ReprojectMaxSubdivisionDepth = 20;
static constexpr size_t ReprojectMinNodeBudget = 65536;
static constexpr size_t ReprojectMaxNodeBudget = 4000000;

// Returns false if the operation was canceled via the cancel callback, true
// otherwise (including when there was nothing to do).
static bool reproject_one_annotation(const FacetsAnnotation &src,
                                     FacetsAnnotation       &dst,
                                     const TriangleMesh     &src_mesh,
                                     const TriangleMesh     &dst_mesh,
                                     const Transform3d      &dst_to_src,
                                     const std::vector<int> *dst_to_src_face,
                                     ReprojectMode           mode,
                                     const char             *annotation_name,
                                     const PaintReprojectProgressCallback &progress = nullptr,
                                     const PaintReprojectCancelCallback   &cancel   = nullptr,
                                     double                  max_sample_distance = -1.0,
                                     const char             *progress_message = nullptr)
{
    if (src.empty())
        return true;
    if (mode == ReprojectMode::OverlapCoplanar && (dst_to_src_face == nullptr || dst_to_src_face->empty()))
        return true;

    // User-facing progress label (English); falls back to the internal log tag.
    const char *const progress_label = progress_message ? progress_message : annotation_name;

    PaintReprojector projector(src_mesh, src, /*build_aabb=*/ mode == ReprojectMode::PointNearest,
                               max_sample_distance);
    TriangleSelector dst_sel(dst_mesh);
    const size_t source_nodes = projector.source_node_count();
    // Boundary-driven refinement scales with paint boundary length rather than raw
    // source node count, so keep a generous floor for simple models with long
    // boundaries and a hard ceiling to bound worst-case complex paint.
    const size_t scaled_source_nodes =
        source_nodes > ReprojectMaxNodeBudget / 16 ? ReprojectMaxNodeBudget : source_nodes * 16;
    const size_t node_budget = std::max<size_t>(
        ReprojectMinNodeBudget, std::min<size_t>(ReprojectMaxNodeBudget, scaled_source_nodes));

    // Repair path: floor the per-face convergence target at a fraction of the whole
    // mesh area so tiny faces stop early instead of chasing a negligible absolute
    // error. Left at the tiny epsilon for the coplanar (cut) path, whose boundary
    // alignment relies on the existing target.
    double absolute_target_error = ReprojectAbsoluteErrorEpsilon;
    if (mode == ReprojectMode::PointNearest) {
        double dst_total_area = 0.0;
        for (const Vec3i &face : dst_mesh.its.indices) {
            const Vec3f &a = dst_mesh.its.vertices[face[0]];
            const Vec3f &b = dst_mesh.its.vertices[face[1]];
            const Vec3f &c = dst_mesh.its.vertices[face[2]];
            dst_total_area += 0.5 * double((b - a).cross(c - a).norm());
        }
        absolute_target_error = std::max(
            ReprojectAbsoluteErrorEpsilon, ReprojectAbsoluteTargetAreaFraction * dst_total_area);
    }

    int n_dst = int(dst_mesh.its.indices.size());
    if (mode == ReprojectMode::OverlapCoplanar)
        n_dst = std::min<int>(n_dst, int(dst_to_src_face->size()));

    std::vector<int> destination_roots;
    destination_roots.reserve(n_dst);
    std::vector<std::vector<int>> resolved_source_root_sets(n_dst);
    std::vector<char> source_roots_resolved(n_dst, 0);
    if (mode == ReprojectMode::OverlapCoplanar) {
        for (int new_idx = 0; new_idx < n_dst; ++new_idx)
            if ((*dst_to_src_face)[new_idx] >= 0)
                destination_roots.emplace_back(new_idx);
    } else {
        for (int new_idx = 0; new_idx < n_dst; ++new_idx)
            destination_roots.emplace_back(new_idx);
    }

    const auto evaluator =
        [&projector, &resolved_source_root_sets, &source_roots_resolved, dst_to_src_face, mode,
         &dst_to_src](
            int destination_root, const std::array<Vec3f, 3> &triangle)
            -> TriangleSelector::FacetSubdivisionMeasurement {
            const std::array<Vec3f, 3> triangle_in_source =
                transform_triangle(triangle, dst_to_src);
            if (mode == ReprojectMode::OverlapCoplanar) {
                const bool first_visit = !source_roots_resolved[destination_root];
                if (first_visit) {
                    resolved_source_root_sets[destination_root] = projector.resolve_source_roots(
                        (*dst_to_src_face)[destination_root], triangle_in_source);
                    source_roots_resolved[destination_root] = 1;
                }
                return projector.measure_overlap(
                    resolved_source_root_sets[destination_root], triangle_in_source, triangle);
            }
            return projector.measure_point_sample(triangle_in_source, triangle);
        };

    // Converge each destination face independently to the same relative accuracy.
    // A single global best-first pass spends the shared budget on the few large,
    // heavily painted cut faces and starves the small/uncut faces, leaving their
    // boundaries coarse. That coarse-vs-fine mismatch shows up as a paint offset
    // along the edge shared by a cut face and an uncut face. Refining per face
    // guarantees uniform boundary resolution so those edges line up.
    TriangleSelector::FacetSubdivisionResult result;
    bool canceled = false;
    const int total_faces = int(destination_roots.size());
    int processed_faces = 0;
    int last_reported_percent = -1;
    for (int destination_root : destination_roots) {
        // Poll cancellation between faces; the subdivision loop also polls
        // internally so a single heavy boundary face cannot delay the response.
        if (cancel && cancel()) {
            canceled = true;
            break;
        }
        const TriangleSelector::FacetSubdivisionResult face_result =
            dst_sel.set_facets_with_subdivision(
                std::vector<int>{destination_root},
                evaluator,
                node_budget,
                ReprojectRelativeErrorLimit,
                absolute_target_error,
                ReprojectMaxSubdivisionDepth,
                cancel);
        result.surface_area += face_result.surface_area;
        result.error_area += face_result.error_area;
        result.nodes_created += face_result.nodes_created;
        result.node_budget_exhausted |= face_result.node_budget_exhausted;
        result.depth_limit_reached |= face_result.depth_limit_reached;
        if (face_result.canceled) {
            canceled = true;
            break;
        }

        ++processed_faces;
        if (progress && total_faces > 0) {
            const int percent = int(size_t(processed_faces) * 100 / size_t(total_faces));
            if (percent != last_reported_percent) {
                last_reported_percent = percent;
                progress(percent, progress_label);
            }
        }
    }
    if (canceled)
        return false;
    const double relative_error =
        result.surface_area > 0.0 ? result.error_area / result.surface_area : 0.0;
    const size_t target_nodes = std::count_if(
        dst_sel.get_triangles().begin(), dst_sel.get_triangles().end(),
        [](const auto &triangle) { return triangle.valid(); });
    BOOST_LOG_TRIVIAL(info)
        << "Paint reprojection [" << annotation_name << "]: source nodes=" << source_nodes
        << ", target nodes=" << target_nodes
        << ", relative error=" << relative_error
        << ", budget exhausted=" << result.node_budget_exhausted
        << ", depth limit reached=" << result.depth_limit_reached
        << ", rebound roots=" << projector.rebound_count()
        << ", fallbacks=" << projector.fallback_count();
    if (result.node_budget_exhausted || result.depth_limit_reached)
        BOOST_LOG_TRIVIAL(warning)
            << "Paint reprojection [" << annotation_name << "] stopped with relative error "
            << relative_error << " (budget exhausted=" << result.node_budget_exhausted
            << ", depth limit reached=" << result.depth_limit_reached << ")";
    if (projector.rebound_count() > 0)
        BOOST_LOG_TRIVIAL(warning)
            << "Paint reprojection [" << annotation_name << "] rebound "
            << projector.rebound_count() << " destination roots to geometrically matching source roots";
    if (projector.fallback_count() > 0)
        BOOST_LOG_TRIVIAL(warning)
            << "Paint reprojection [" << annotation_name << "] used point-sample fallback "
            << projector.fallback_count() << " times";

    dst.set(dst_sel);
    return true;
}

// ---------------------------------------------------------------------------
// High-level entry points.
// ---------------------------------------------------------------------------

void reproject_paint(const ModelVolume &src_volume,
                     ModelVolume       &dst_volume,
                     const std::vector<int> &dst_to_src_face,
                     const Transform3d      &destination_to_source)
{
    if (dst_to_src_face.empty())
        return;
    const TriangleMesh &src_mesh = src_volume.mesh();
    const TriangleMesh &dst_mesh = dst_volume.mesh();
    reproject_one_annotation(src_volume.supported_facets,        dst_volume.supported_facets,        src_mesh, dst_mesh, destination_to_source, &dst_to_src_face, ReprojectMode::OverlapCoplanar, "support");
    reproject_one_annotation(src_volume.seam_facets,             dst_volume.seam_facets,             src_mesh, dst_mesh, destination_to_source, &dst_to_src_face, ReprojectMode::OverlapCoplanar, "seam");
    reproject_one_annotation(src_volume.mmu_segmentation_facets, dst_volume.mmu_segmentation_facets, src_mesh, dst_mesh, destination_to_source, &dst_to_src_face, ReprojectMode::OverlapCoplanar, "mmu");
    reproject_one_annotation(src_volume.fuzzy_skin_facets,       dst_volume.fuzzy_skin_facets,       src_mesh, dst_mesh, destination_to_source, &dst_to_src_face, ReprojectMode::OverlapCoplanar, "fuzzy skin");
}

bool reproject_paint_geometric(const TriangleMesh     &src_mesh,
                               const FacetsAnnotation &src_supported,
                               const FacetsAnnotation &src_seam,
                               const FacetsAnnotation &src_mmu,
                               const FacetsAnnotation &src_fuzzy,
                               const TriangleMesh     &dst_mesh,
                               const Transform3d      &dst_to_src,
                               FacetsAnnotation       &dst_supported,
                               FacetsAnnotation       &dst_seam,
                               FacetsAnnotation       &dst_mmu,
                               FacetsAnnotation       &dst_fuzzy,
                               const PaintReprojectProgressCallback &progress,
                               const PaintReprojectCancelCallback   &cancel)
{
    // Geometric callers rebuild the whole annotation from scratch, so clear any
    // stale destination paint first: reproject_one_annotation leaves the layer
    // untouched when the corresponding source layer is empty, which would
    // otherwise keep the pre-rebuild paint (serialized for the old topology).
    dst_supported.reset();
    dst_seam.reset();
    dst_mmu.reset();
    dst_fuzzy.reset();
    if (dst_mesh.its.indices.empty() || src_mesh.its.indices.empty())
        return true;

    // Distribute the [0, 100] progress range evenly across the non-empty layers so
    // the bar advances smoothly regardless of how many paint types are present.
    struct Layer {
        const FacetsAnnotation &src;
        FacetsAnnotation       &dst;
        const char             *name;    // internal log tag
        const char             *display; // user-facing progress label (English)
    };
    const std::array<Layer, 4> layers {{
        {src_supported, dst_supported, "support",    "Restoring support painting"},
        {src_seam,      dst_seam,      "seam",       "Restoring seam painting"},
        {src_mmu,       dst_mmu,       "mmu",        "Restoring paint color"},
        {src_fuzzy,     dst_fuzzy,     "fuzzy skin", "Restoring fuzzy skin"},
    }};
    int active_layers = 0;
    for (const Layer &layer : layers)
        if (!layer.src.empty())
            ++active_layers;
    if (active_layers == 0)
        return true;

    // Faces the repair added (hole-fill patches, etc.) sit off the original
    // surface. Treat a destination face as new-geometry when its centroid is
    // farther from the source mesh than 1/100 of the source bounding-box diagonal,
    // a model-relative tolerance that scales with the object instead of an absolute
    // millimeter cutoff. New faces are then left unpainted across every layer.
    const BoundingBoxf3 src_bbox = src_mesh.bounding_box();
    const double src_diagonal = src_bbox.size().norm();
    const double max_sample_distance = src_diagonal > 0.0 ? src_diagonal / 100.0 : -1.0;

    int layer_index = 0;
    for (const Layer &layer : layers) {
        if (layer.src.empty())
            continue;
        const int base_percent = layer_index * 100 / active_layers;
        const int span_percent = 100 / active_layers;
        PaintReprojectProgressCallback layer_progress;
        if (progress)
            layer_progress = [&progress, base_percent, span_percent](int percent, const char *message) {
                progress(base_percent + percent * span_percent / 100, message);
            };
        const bool completed = reproject_one_annotation(
            layer.src, layer.dst, src_mesh, dst_mesh, dst_to_src, nullptr,
            ReprojectMode::PointNearest, layer.name, layer_progress, cancel, max_sample_distance,
            layer.display);
        if (!completed)
            return false;
        ++layer_index;
    }
    if (progress)
        progress(100, "Restoring painting");
    return true;
}

} // namespace Slic3r
