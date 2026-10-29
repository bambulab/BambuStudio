#include "PaintReproject.hpp"

#include "libslic3r.h"
#include "Geometry.hpp"
#include "I18N.hpp"
#include "Model.hpp"
#include "TriangleMesh.hpp"
#include "TriangleSelector.hpp"
#include "AABBTreeIndirect.hpp"

#include <boost/log/trivial.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace Slic3r {

// A serialized annotation: per-root entries of (root index, first bit) plus the
// concatenated depth-first bit streams of those roots.
using AnnotationData = std::pair<std::vector<std::pair<int, int>>, std::vector<bool>>;

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

// Axis to drop when projecting a triangle to 2D: the one its normal points along
// most strongly, so the projection never degenerates.
static int dominant_projection_axis(const std::array<Vec3f, 3> &triangle)
{
    const Vec3f normal = (triangle[1] - triangle[0]).cross(triangle[2] - triangle[0]).cwiseAbs();
    if (normal.x() >= normal.y() && normal.x() >= normal.z()) return X;
    if (normal.y() >= normal.z()) return Y;
    return Z;
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
        return dominant_projection_axis(triangle);
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
// PaintRegionSource: the source annotation flattened into its leaf triangles,
// indexed for area queries, and grouped by paint state.
//
// This backs the region re-painting path. Where measure_point_sample() asks each
// destination leaf "what colour am I" from 4 samples -- and drives refinement
// from the disagreement among those very same samples, so a feature that misses
// all 4 is invisible to the refinement and gets lost -- this walks the other
// way: each paint state is pushed onto the destination mesh as one brush stroke,
// and refinement is driven by measured in-plane overlap area instead of sampling
// luck. Coverage of a destination triangle is reported as a fraction of the
// source area that maps onto it (not of the triangle's own area), which makes the
// verdict independent of the foreshortening a curved source patch introduces.
// ---------------------------------------------------------------------------

class PaintRegionSource
{
public:
    // Same float box type the AABB tree nodes use, so query boxes need no cast.
    using Box3f = AABBTreeIndirect::Tree3f::BoundingBox;

    struct StateInfo {
        EnforcerBlockerType state { EnforcerBlockerType::NONE };
        double              area { 0.0 };
        Box3f               bbox;
    };

    // band: half-width of the slab around a destination triangle's plane within
    // which a source leaf is still considered to lie on the same surface. Also
    // acts as the new-geometry cull: a destination face the repair invented sits
    // farther away than this from every source leaf, so no state overlaps it and
    // it is left unpainted. Negative disables the test.
    //
    // root_filter, when given, is indexed by source face and restricts the
    // flattening to those faces. A caller that only needs to cover a small part of
    // the destination (the cut path, whose geometric work is confined to the band
    // of fragments along the cut line) pays for the leaves and the AABB tree of
    // that part alone instead of the whole model. Such a caller should also hand in
    // an annotation already narrowed to those roots, otherwise the subdivision tree
    // of the whole model still gets rebuilt here just to be thrown away.
    PaintRegionSource(const TriangleMesh      &source_mesh,
                      const AnnotationData    &annotation,
                      double                   band,
                      const std::vector<char> *root_filter = nullptr)
        : m_selector(std::make_unique<TriangleSelector>(source_mesh))
        , m_band(band)
    {
        m_selector->deserialize(annotation, true);

        const int root_count = int(source_mesh.its.indices.size());
        std::vector<int> vertex_map(m_selector->get_vertices().size(), -1);
        if (root_filter == nullptr)
            m_leaf_indices.reserve(m_selector->get_triangles().size());
        for (int root = 0; root < root_count; ++root)
            if (root_filter == nullptr || (*root_filter)[root] != 0)
                this->collect_leaves(root, vertex_map);
        // The flattened leaves carry everything the coverage test needs, so drop the
        // subdivision tree rather than keeping a second copy of it alive.
        m_selector.reset();
        if (m_leaf_indices.empty())
            return;

        // Per-leaf normal and area, plus the per-state totals and bounding boxes
        // that decide paint order and seed the destination walk.
        m_leaf_normals.reserve(m_leaf_indices.size());
        std::array<StateInfo, StateCount> by_state {};
        for (size_t state = 0; state < by_state.size(); ++state)
            by_state[state].state = static_cast<EnforcerBlockerType>(state);
        for (size_t leaf = 0; leaf < m_leaf_indices.size(); ++leaf) {
            const std::array<Vec3f, 3> triangle = this->leaf_triangle(leaf);
            const Vec3f normal = (triangle[1] - triangle[0]).cross(triangle[2] - triangle[0]);
            const float normal_length = normal.norm();
            m_leaf_normals.emplace_back(
                normal_length > 0.f ? Vec3f(normal / normal_length) : Vec3f(0.f, 0.f, 0.f));
            const size_t state = static_cast<size_t>(m_leaf_states[leaf]);
            if (state >= by_state.size())
                continue;
            by_state[state].area += 0.5 * double(normal_length);
            for (const Vec3f &vertex : triangle)
                by_state[state].bbox.extend(vertex);
        }

        // Paint order: descending painted area. Nested islands are smaller than
        // what encloses them, so painting them later makes them win, exactly as
        // when a user paints a small detail on top of a large fill. NONE is never
        // painted: the destination starts out unpainted, and a state that only
        // touches (rather than covers) a triangle never spreads into a hole, so
        // unpainted areas survive without needing a pass of their own.
        for (size_t state = 1; state < by_state.size(); ++state)
            if (by_state[state].area > 0.0)
                m_states.emplace_back(by_state[state]);
        std::sort(m_states.begin(), m_states.end(),
                  [](const StateInfo &a, const StateInfo &b) { return a.area > b.area; });

        m_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
            m_leaf_vertices, m_leaf_indices);
    }

    bool   empty() const { return m_leaf_indices.empty() || m_states.empty(); }
    size_t leaf_count() const { return m_leaf_indices.size(); }
    const std::vector<StateInfo> &states() const { return m_states; }

    // Shortest edge among the flattened leaves. Diagnostic only: it says how finely
    // the source happened to be subdivided, which is not the size of the features
    // the paint carries and so must not drive the destination's refinement target.
    float finest_leaf_edge() const
    {
        return m_finest_leaf_edge < std::numeric_limits<float>::max() ? m_finest_leaf_edge : 0.f;
    }

    // How much of the source area that maps onto this destination triangle (given
    // in the source frame) carries the requested state.
    TriangleSelector::RegionCoverage coverage(
        EnforcerBlockerType state, const std::array<Vec3f, 3> &triangle_in_source) const
    {
        const Vec3f normal =
            (triangle_in_source[1] - triangle_in_source[0]).cross(
                triangle_in_source[2] - triangle_in_source[0]);
        const float normal_length = normal.norm();
        if (normal_length <= std::numeric_limits<float>::min())
            return TriangleSelector::RegionCoverage::None;
        const Vec3f unit_normal = normal / normal_length;

        const int dropped_axis = dominant_projection_axis(triangle_in_source);
        const double target_area =
            triangle_overlap_area(triangle_in_source, triangle_in_source, dropped_axis);
        if (target_area <= std::numeric_limits<double>::epsilon())
            return TriangleSelector::RegionCoverage::None;
        const double area_epsilon = std::max(1e-12, target_area * 1e-10);

        Box3f query_box;
        for (const Vec3f &vertex : triangle_in_source)
            query_box.extend(vertex);
        // Only the projection axis is widened by the slab, never the two axes the
        // overlap is measured in: a leaf whose footprint misses the triangle in
        // either of those contributes no overlap area whatever its distance, so
        // fetching it is pure cost. That cost is what dominates the run time when
        // it is not suppressed -- the slab half-width is model-relative (millimeters
        // on a normal object) while a leaf is a fraction of a millimeter, so an
        // isotropic query box pulls in the whole coplanar neighbourhood, thousands
        // of leaves deep in the refinement, and clips every one of them.
        //
        // The widening is sqrt(3) * band rather than band so the box stays a superset
        // of what the centroid slab test below accepts: a leaf offset from the plane
        // by the full band sits band / |n| along the projection axis, and since that
        // axis carries the normal's dominant component, |n| there is at least
        // 1 / sqrt(3).
        if (m_band > 0.0) {
            const float widening = float(m_band) * Sqrt3;
            query_box.min()[dropped_axis] -= widening;
            query_box.max()[dropped_axis] += widening;
        }

        double area_in_state = 0.0;
        double area_matched  = 0.0;
        AABBTreeIndirect::traverse(
            m_tree, AABBTreeIndirect::intersecting(query_box),
            [this, state, &unit_normal, &triangle_in_source, dropped_axis, area_epsilon,
             &area_in_state, &area_matched](const AABBTreeIndirect::Tree3f::Node &node) {
                const size_t leaf = node.idx;
                // Reject source surface that faces elsewhere. This is what keeps
                // paint from bleeding through a thin wall onto its far side, whose
                // leaves are equally close but oppositely oriented.
                if (m_leaf_normals[leaf].dot(unit_normal) < NormalAgreementLimit)
                    return true;
                const std::array<Vec3f, 3> leaf_triangle = this->leaf_triangle(leaf);
                // Reject source surface lying off this triangle's plane, e.g. the
                // parallel face of another plate stacked behind it.
                if (m_band > 0.0) {
                    const Vec3f centroid =
                        (leaf_triangle[0] + leaf_triangle[1] + leaf_triangle[2]) / 3.f;
                    if (std::abs(double((centroid - triangle_in_source[0]).dot(unit_normal))) > m_band)
                        return true;
                }
                const double overlap =
                    triangle_overlap_area(leaf_triangle, triangle_in_source, dropped_axis);
                if (overlap <= area_epsilon)
                    return true;
                area_matched += overlap;
                if (m_leaf_states[leaf] == state)
                    area_in_state += overlap;
                return true;
            });

        if (area_matched <= area_epsilon || area_in_state <= area_matched * MinOverlapFraction)
            return TriangleSelector::RegionCoverage::None;
        // Report Full only when the state owns essentially all of the matched
        // source area AND enough of the triangle was matched at all. A triangle
        // straddling a sharp edge matches only the part coplanar with it, so it
        // stays Partial and gets refined until each piece resolves onto one side.
        if (area_in_state >= area_matched * (1.0 - CoverageEpsilon) &&
            area_matched >= target_area * MinMatchedFraction)
            return TriangleSelector::RegionCoverage::Full;
        return TriangleSelector::RegionCoverage::Partial;
    }

private:
    static constexpr size_t StateCount =
        static_cast<size_t>(EnforcerBlockerType::ExtruderMax) + 1;
    // cos(60 deg): source surface tilted further than this away from the
    // destination triangle belongs to a different feature.
    static constexpr float NormalAgreementLimit = 0.5f;
    // A state must own more than this fraction of the matched source area to
    // count as present, so numerical slivers do not drag a triangle into a region.
    static constexpr double MinOverlapFraction = 1e-3;
    // Slack allowed when declaring a state the sole owner of the matched area.
    static constexpr double CoverageEpsilon = 1e-3;
    // A Full verdict additionally needs this fraction of the triangle's own
    // projected area to have been matched by same-facing source surface.
    static constexpr double MinMatchedFraction = 0.75;
    static constexpr float  Sqrt3 = 1.7320509f;

    void collect_leaves(int triangle_idx, std::vector<int> &vertex_map)
    {
        const auto &triangles = m_selector->get_triangles();
        if (triangle_idx < 0 || triangle_idx >= int(triangles.size()))
            return;
        const auto &triangle = triangles[triangle_idx];
        if (!triangle.valid())
            return;
        if (triangle.is_split()) {
            const int child_count = triangle.number_of_split_sides() + 1;
            for (int child = 0; child < child_count; ++child)
                this->collect_leaves(triangle.children[child], vertex_map);
            return;
        }

        const auto &vertices = m_selector->get_vertices();
        Vec3i indices;
        for (int i = 0; i < 3; ++i) {
            const int vertex = triangle.verts_idxs[i];
            if (vertex_map[vertex] < 0) {
                vertex_map[vertex] = int(m_leaf_vertices.size());
                m_leaf_vertices.emplace_back(vertices[vertex].v);
            }
            indices[i] = vertex_map[vertex];
        }
        m_leaf_indices.emplace_back(indices);
        m_leaf_states.emplace_back(triangle.get_state());

        const Vec3f &a = m_leaf_vertices[indices[0]];
        const Vec3f &b = m_leaf_vertices[indices[1]];
        const Vec3f &c = m_leaf_vertices[indices[2]];
        const float shortest = std::sqrt(std::min(
            (b - a).squaredNorm(), std::min((c - b).squaredNorm(), (a - c).squaredNorm())));
        if (shortest > 0.f)
            m_finest_leaf_edge = std::min(m_finest_leaf_edge, shortest);
    }

    std::array<Vec3f, 3> leaf_triangle(size_t leaf) const
    {
        const Vec3i &indices = m_leaf_indices[leaf];
        return {
            m_leaf_vertices[indices[0]],
            m_leaf_vertices[indices[1]],
            m_leaf_vertices[indices[2]]
        };
    }

    // Released once the leaves have been flattened out of it.
    std::unique_ptr<TriangleSelector> m_selector;
    double                            m_band { -1.0 };

    std::vector<Vec3f>               m_leaf_vertices;
    std::vector<Vec3i>               m_leaf_indices;
    std::vector<EnforcerBlockerType> m_leaf_states;
    std::vector<Vec3f>               m_leaf_normals;
    std::vector<StateInfo>           m_states;
    AABBTreeIndirect::Tree3f         m_tree;
    float                            m_finest_leaf_edge { std::numeric_limits<float>::max() };
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
                                     const char             *progress_message = nullptr,
                                     double                  absolute_target_error = ReprojectAbsoluteErrorEpsilon)
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

    // absolute_target_error floors the per-face convergence target so tiny faces
    // stop early (computed once by the caller from the whole mesh area for the
    // repair path; the coplanar/cut path keeps the tiny epsilon default).

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
// Region re-painting driver (repair path).
// ---------------------------------------------------------------------------

// Target edge length for destination leaves along a paint boundary, in mm. The
// repair re-triangulates the mesh, so destination leaves are not aligned with the
// source paint boundaries: the rebuilt boundary is quantized to the leaf size and
// the conservative fill dilates it by up to one leaf. 0.2mm is the finest stroke
// the painting gizmo itself can lay down (select_patch() clamps a brush to
// min(radius / 5, 0.2mm)) and sits below the nozzle diameter and the layer height,
// so the residual boundary error cannot reach the sliced result.
//
// Deliberately an absolute target rather than a multiple of the source's own leaf
// size: that size reflects how the source happened to be painted, not the size of
// the features it carries. The height-range cursor pins its edge limit at 0.1mm and
// split_triangle() halves sides from there, so the finest leaf in a layer is
// routinely an order of magnitude below anything visible, and deriving the target
// from it drove the whole layer to the floor for no gain in accuracy.
//
// Being millimeters requires the caller to hand paint_region() the transform that maps
// the destination mesh into world space; measured in the mesh's own coordinates this
// would be off by the volume scaling. See the two call sites below.
static constexpr float RegionPaintTargetEdge = 0.2f;
// Model-relative floor, so a large object cannot be driven to a huge node count by
// the absolute target alone. Takes over above a ~820mm diagonal.
static constexpr int RegionPaintDiagonalEdgeDivisor = 4096;
// Beyond this many source leaves the flattening and its AABB tree stop being
// worth the memory; such an annotation falls back to point sampling.
static constexpr size_t RegionPaintMaxSourceLeaves = 4000000;

// Resolves the edge-length floor into the frame split_triangle() will measure in, mirroring
// what select_patch() does for a brush stroke: a uniformly scaled volume keeps measuring in
// mesh coordinates and just divides the millimeter target by its scale, which avoids
// transforming every vertex, while a non-uniformly scaled one has to measure in world
// coordinates. The model-relative floor is taken in the same frame as the target so the two
// terms of the max() are comparable.
class RegionEdgeLimit
{
public:
    // dst_world_matrix maps the destination mesh into world space; null means the mesh is
    // already there (the cut path bakes instance x volume into the mesh before slicing).
    RegionEdgeLimit(const TriangleMesh &dst_mesh, const Transform3d *dst_world_matrix)
    {
        double diagonal = dst_mesh.bounding_box().size().norm();
        double target   = RegionPaintTargetEdge;
        if (dst_world_matrix != nullptr) {
            const Vec3d sf = Geometry::Transformation(*dst_world_matrix).get_scaling_factor();
            if (is_approx(sf.x(), sf.y()) && is_approx(sf.y(), sf.z())) {
                if (sf.x() > EPSILON)
                    target /= sf.x();
            } else {
                m_storage = dst_world_matrix->cast<float>();
                m_trafo   = &m_storage;
                diagonal  = dst_mesh.transformed_bounding_box(*dst_world_matrix).size().norm();
            }
        }
        m_value = std::max(float(target),
                           diagonal > 0.0 ? float(diagonal / RegionPaintDiagonalEdgeDivisor) : 0.f);
    }

    // m_trafo points into m_storage, so a copy would dangle.
    RegionEdgeLimit(const RegionEdgeLimit &) = delete;
    RegionEdgeLimit &operator=(const RegionEdgeLimit &) = delete;

    float              value() const { return m_value; }
    const Transform3f *trafo() const { return m_trafo; }

private:
    Transform3f        m_storage { Transform3f::Identity() };
    const Transform3f *m_trafo { nullptr };
    float              m_value { RegionPaintTargetEdge };
};

// Destination geometry expressed in the source frame. Built once per reprojection
// and shared by all four annotation layers, which would otherwise each re-transform
// the whole vertex list.
class DestinationInSourceFrame
{
public:
    DestinationInSourceFrame(const TriangleMesh &dst_mesh, const Transform3d &dst_to_src)
        : m_dst_to_src(dst_to_src)
        , m_identity(dst_to_src.isApprox(Transform3d::Identity()))
    {
        if (!m_identity) {
            m_storage.reserve(dst_mesh.its.vertices.size());
            for (const Vec3f &vertex : dst_mesh.its.vertices)
                m_storage.emplace_back((dst_to_src * vertex.cast<double>()).cast<float>());
        }
        m_vertices = m_identity ? &dst_mesh.its.vertices : &m_storage;
    }

    // m_vertices points into m_storage, so a copy would dangle.
    DestinationInSourceFrame(const DestinationInSourceFrame &) = delete;
    DestinationInSourceFrame &operator=(const DestinationInSourceFrame &) = delete;

    const std::vector<Vec3f> &vertices() const { return *m_vertices; }

    std::array<Vec3f, 3> triangle(const Vec3i &face) const
    {
        return {(*m_vertices)[face[0]], (*m_vertices)[face[1]], (*m_vertices)[face[2]]};
    }

    std::array<Vec3f, 3> to_source(const std::array<Vec3f, 3> &triangle_in_destination) const
    {
        return m_identity ? triangle_in_destination
                          : transform_triangle(triangle_in_destination, m_dst_to_src);
    }

private:
    Transform3d               m_dst_to_src;
    bool                      m_identity { false };
    std::vector<Vec3f>        m_storage;
    const std::vector<Vec3f> *m_vertices { nullptr };
};

// Below this many states the one-off AABB build over the destination faces costs more than
// the repeated linear scans it would save.
static constexpr size_t RegionSeedTreeMinStates = 3;

// Destination faces prepared for seeding the region walks: their bounding boxes in the source
// frame and, once a layer carries enough states to amortize it, an AABB tree over the same
// faces. Built once per reprojection and shared by all four annotation layers, which restrict
// the walk with the same mask - the cut path passes the fragment faces to every layer and the
// repair path passes none, so rebuilding this per layer was a wasted full-mesh pass each time.
class DestinationSeedIndex
{
public:
    using Box3f = PaintRegionSource::Box3f;

    DestinationSeedIndex(const TriangleMesh             &dst_mesh,
                         const DestinationInSourceFrame &dst_in_source,
                         const std::vector<char>        *paintable_facets)
        : m_vertices(dst_in_source.vertices())
        , m_all_indices(dst_mesh.its.indices)
        , m_masked(paintable_facets != nullptr)
    {
        const size_t face_count = m_all_indices.size();
        if (!m_masked) {
            m_faces.reserve(face_count);
            m_boxes.reserve(face_count);
        }
        for (size_t face_idx = 0; face_idx < face_count; ++face_idx) {
            if (m_masked && (*paintable_facets)[face_idx] == 0)
                continue;
            const Vec3i &face = m_all_indices[face_idx];
            Box3f        box;
            for (int i = 0; i < 3; ++i)
                box.extend(m_vertices[face[i]]);
            m_faces.emplace_back(int(face_idx));
            m_boxes.emplace_back(box);
            if (m_masked)
                m_masked_indices.emplace_back(face);
        }
    }

    // m_vertices and m_all_indices are references, and the tree indexes into them.
    DestinationSeedIndex(const DestinationSeedIndex &) = delete;
    DestinationSeedIndex &operator=(const DestinationSeedIndex &) = delete;

    void ensure_tree()
    {
        if (m_has_tree || m_faces.empty())
            return;
        m_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
            m_vertices, m_masked ? m_masked_indices : m_all_indices);
        m_has_tree = true;
    }

    // Every paintable destination face whose box meets query, as original face indices.
    void seeds(const Box3f &query, std::vector<int> &out) const
    {
        out.clear();
        if (m_has_tree) {
            AABBTreeIndirect::traverse(
                m_tree, AABBTreeIndirect::intersecting(query),
                [this, &out](const AABBTreeIndirect::Tree3f::Node &node) {
                    out.emplace_back(m_faces[node.idx]);
                    return true;
                });
            // Tree order is arbitrary, and the seed order decides which region wins the last
            // nodes when the budget runs out. Sort so the result does not depend on whether
            // the tree was built.
            std::sort(out.begin(), out.end());
            return;
        }
        for (size_t i = 0; i < m_faces.size(); ++i)
            if (query.intersects(m_boxes[i]))
                out.emplace_back(m_faces[i]);
    }

private:
    const std::vector<Vec3f> &m_vertices;
    const std::vector<Vec3i> &m_all_indices;
    bool                      m_masked { false };
    // Paintable original face indices and their boxes, kept parallel.
    std::vector<int>          m_faces;
    std::vector<Box3f>        m_boxes;
    // Only populated when a mask is given; unmasked the tree runs over m_all_indices directly.
    std::vector<Vec3i>        m_masked_indices;
    AABBTreeIndirect::Tree3f  m_tree;
    bool                      m_has_tree { false };
};

struct RegionReprojectStats {
    size_t nodes_created { 0 };
    bool   budget_exhausted { false };
    bool   canceled { false };
};

// Paints every state of the source onto dst_sel, one state per stroke. The caller
// owns dst_sel, so paint already present on it (e.g. spliced in from the source)
// survives as long as paintable_facets excludes those faces.
static RegionReprojectStats reproject_one_annotation_by_region(
    const PaintRegionSource        &source,
    TriangleSelector               &dst_sel,
    DestinationSeedIndex           &seed_index,
    const DestinationInSourceFrame &dst_in_source,
    float                           edge_limit,
    float                           seed_inflate,
    const PaintReprojectProgressCallback &progress,
    const PaintReprojectCancelCallback   &cancel,
    const char                     *progress_message,
    const std::vector<char>        *paintable_facets = nullptr,
    // Frame edge_limit is measured in; null measures the destination mesh's own coordinates.
    const Transform3f              *edge_limit_trafo = nullptr)
{
    RegionReprojectStats stats;
    const std::vector<PaintRegionSource::StateInfo> &states = source.states();
    if (states.empty())
        return stats;

    if (states.size() >= RegionSeedTreeMinStates)
        seed_index.ensure_tree();

    // Boundary-driven refinement scales with paint boundary length, so budget from
    // the source's own node count with a generous floor and a hard ceiling.
    const size_t source_leaves = source.leaf_count();
    const size_t scaled_leaves = source_leaves > ReprojectMaxNodeBudget / 16
        ? ReprojectMaxNodeBudget : source_leaves * 16;
    const size_t node_budget = std::max<size_t>(
        ReprojectMinNodeBudget, std::min<size_t>(ReprojectMaxNodeBudget, scaled_leaves));

    int              last_reported_percent = -1;
    std::vector<int> start_facets;
    for (size_t state_idx = 0; state_idx < states.size(); ++state_idx) {
        const PaintRegionSource::StateInfo &info = states[state_idx];

        // Seed the walk with every destination face whose box meets this state's
        // box, inflated by the same slab half-width the coverage test uses.
        // paint_region() then spreads over neighbors, so a face the box test misses
        // is still reached as long as one of its neighbors was seeded.
        PaintRegionSource::Box3f state_box = info.bbox;
        if (seed_inflate > 0.f) {
            state_box.min().array() -= seed_inflate;
            state_box.max().array() += seed_inflate;
        }
        seed_index.seeds(state_box, start_facets);
        if (start_facets.empty())
            continue;

        const EnforcerBlockerType state = info.state;
        const TriangleSelector::RegionCoverageEvaluator coverage =
            [&source, &dst_in_source, state](const std::array<Vec3f, 3> &triangle) {
                return source.coverage(state, dst_in_source.to_source(triangle));
            };

        const TriangleSelector::RegionPaintResult result = dst_sel.paint_region(
            start_facets, coverage, state, edge_limit, node_budget,
            ReprojectMaxSubdivisionDepth, cancel, paintable_facets, edge_limit_trafo);
        if (result.canceled) {
            stats.canceled = true;
            return stats;
        }
        stats.nodes_created += result.nodes_created;
        stats.budget_exhausted |= result.node_budget_exhausted;

        if (progress) {
            const int percent = int((state_idx + 1) * 100 / states.size());
            if (percent != last_reported_percent) {
                last_reported_percent = percent;
                progress(percent, progress_message);
            }
        }
    }
    return stats;
}

// get_triangles() is not const-qualified, hence the non-const reference.
static size_t count_valid_nodes(TriangleSelector &selector)
{
    const auto &triangles = selector.get_triangles();
    return size_t(std::count_if(triangles.begin(), triangles.end(),
                                [](const auto &triangle) { return triangle.valid(); }));
}

// ---------------------------------------------------------------------------
// Cut re-painting driver.
//
// A cut leaves most of the surface untouched: cut_mesh() copies every face the cut
// plane misses over verbatim, vertex index triple and all. For those the source
// subdivision tree can be moved across as-is, which preserves the paint bit for bit
// and costs no geometry at all. Only the faces the plane crossed were
// re-triangulated and need their paint measured geometrically, and the
// cross-section cap is new surface that carries no paint by definition.
//
// This replaces resampling the whole destination, which cost accuracy everywhere
// (the untouched majority was re-derived from area measurements instead of copied)
// to solve a problem that exists only in the narrow band along the cut line.
// ---------------------------------------------------------------------------

// Relative tolerance for recognising a destination face as the untouched source
// face. Its vertices round-trip through two float transforms, so an exact compare
// is unusable. Scaled off the source bounding box, this lands three orders of
// magnitude below the finest paint leaf a user can produce, so it cannot merge
// distinct faces, and a face that fails the test only falls back to the geometric
// path.
static constexpr double CutCongruenceRelativeTolerance = 1e-5;

// How a destination face relates to the source face the cut recorded for it.
enum class CutFaceOrigin : unsigned char {
    // Surface the cut invented (the cross-section cap): no source paint applies.
    Invented,
    // Carried over untouched: the source subdivision tree can be copied verbatim.
    Congruent,
    // Fragment of a source face the cut plane crossed: needs geometric coverage.
    Fragment,
};

struct CutFaceClassification {
    std::vector<CutFaceOrigin> origin;
    // Indexed by destination face, for paint_region()'s walk restriction.
    std::vector<char> fragment_faces;
    // Indexed by source face: the roots the fragments descend from, so the source
    // flattening covers the cut band instead of the whole model.
    std::vector<char> fragment_source_roots;
    size_t congruent_count { 0 };
    size_t fragment_count { 0 };
    size_t invented_count { 0 };
};

// tolerance: how far a destination vertex may sit from its source vertex and still
// count as the same point.
static CutFaceClassification classify_cut_faces(const TriangleMesh             &src_mesh,
                                                const TriangleMesh             &dst_mesh,
                                                const std::vector<int>         &dst_to_src_face,
                                                const DestinationInSourceFrame &dst_in_source,
                                                float                           tolerance)
{
    const size_t dst_face_count = dst_mesh.its.indices.size();
    const int    src_root_count = int(src_mesh.its.indices.size());
    const float  tolerance_sq   = tolerance * tolerance;

    CutFaceClassification classification;
    classification.origin.assign(dst_face_count, CutFaceOrigin::Invented);
    classification.fragment_faces.assign(dst_face_count, 0);
    classification.fragment_source_roots.assign(size_t(src_root_count), 0);

    for (size_t dst_face = 0; dst_face < dst_face_count; ++dst_face) {
        // Faces past the end of the provenance map, and the cap faces cut_mesh()
        // marks with -1, have no source face at all.
        const int source_root =
            dst_face < dst_to_src_face.size() ? dst_to_src_face[dst_face] : -1;
        if (source_root < 0 || source_root >= src_root_count) {
            ++classification.invented_count;
            continue;
        }

        // Congruence requires the same three points in the same order, not merely
        // the same triangle: the bit stream encodes the subdivision relative to the
        // triangle's own vertex numbering, so a rotated or mirrored face would
        // decode into a differently oriented subdivision. A left-handed cut
        // transform flips the winding, which fails this test on every face and
        // sends the whole volume down the geometric path -- slower, still correct.
        const std::array<Vec3f, 3> dst_triangle =
            dst_in_source.triangle(dst_mesh.its.indices[dst_face]);
        const Vec3i &src_face = src_mesh.its.indices[source_root];
        bool congruent = true;
        for (int i = 0; i < 3 && congruent; ++i)
            congruent = (dst_triangle[i] - src_mesh.its.vertices[src_face[i]]).squaredNorm()
                        <= tolerance_sq;

        if (congruent) {
            classification.origin[dst_face] = CutFaceOrigin::Congruent;
            ++classification.congruent_count;
        } else {
            classification.origin[dst_face] = CutFaceOrigin::Fragment;
            classification.fragment_faces[dst_face] = 1;
            // The fragment lies inside its own source face -- cut_mesh() builds it
            // from that face's corners and the intersection points -- so that one
            // root carries all the paint the coverage test can need.
            classification.fragment_source_roots[size_t(source_root)] = 1;
            ++classification.fragment_count;
        }
    }
    return classification;
}

// Per-root bit ranges of a serialized annotation, indexed by source face. Roots
// that carry no paint get an empty range.
//
// serialize() walks the roots in increasing index order and appends each root's
// stream, so a root's range ends where the next one begins. Returns an empty table
// if that layout does not hold, so a caller splices nothing rather than slicing the
// stream in the wrong place.
static std::vector<std::pair<int, int>> annotation_bit_ranges(const FacetsAnnotation &annotation,
                                                              int                     root_count)
{
    const AnnotationData &data = annotation.get_data();
    const int             total_bits = int(data.second.size());
    for (size_t i = 1; i < data.first.size(); ++i)
        if (data.first[i].first <= data.first[i - 1].first ||
            data.first[i].second <= data.first[i - 1].second)
            return {};

    std::vector<std::pair<int, int>> ranges(size_t(root_count), std::make_pair(0, 0));
    for (size_t i = 0; i < data.first.size(); ++i) {
        const int root = data.first[i].first;
        if (root < 0 || root >= root_count)
            return {};
        const int first = data.first[i].second;
        const int last = i + 1 < data.first.size() ? data.first[i + 1].second : total_bits;
        if (first < 0 || first >= last || last > total_bits)
            return {};
        ranges[size_t(root)] = {first, last};
    }
    return ranges;
}

// The paint of the selected source roots on its own, with the root numbering
// unchanged. Deserializing this instead of the whole annotation keeps the source
// side of the cut proportional to the band along the cut line: rebuilding the
// subdivision tree of a fully painted model is the single most expensive step of a
// reprojection, and the cut needs only a sliver of it.
static AnnotationData extract_source_roots(const FacetsAnnotation                 &src,
                                           const std::vector<char>                &roots,
                                           const std::vector<std::pair<int, int>> &src_ranges)
{
    AnnotationData extracted;
    if (src_ranges.empty())
        return extracted;

    const std::vector<bool> &src_bits = src.get_data().second;
    for (size_t root = 0; root < roots.size(); ++root) {
        if (roots[root] == 0)
            continue;
        const auto [first, last] = src_ranges[root];
        if (first >= last)
            continue;
        extracted.first.emplace_back(int(root), int(extracted.second.size()));
        extracted.second.insert(extracted.second.end(), src_bits.begin() + first,
                                src_bits.begin() + last);
    }
    return extracted;
}

// Moves the subdivision tree of every congruent destination face over from the
// source. A root's bit stream is a self-contained depth-first walk of its own
// subtree, so re-filing it under another root index preserves the paint exactly.
static AnnotationData splice_congruent_paint(const FacetsAnnotation                 &src,
                                             const std::vector<int>                 &dst_to_src_face,
                                             const std::vector<CutFaceOrigin>       &origin,
                                             const std::vector<std::pair<int, int>> &src_ranges)
{
    AnnotationData spliced;
    if (src_ranges.empty())
        return spliced;

    const std::vector<bool> &src_bits = src.get_data().second;
    for (size_t dst_face = 0; dst_face < origin.size(); ++dst_face) {
        if (origin[dst_face] != CutFaceOrigin::Congruent)
            continue;
        const auto [first, last] = src_ranges[size_t(dst_to_src_face[dst_face])];
        if (first >= last)
            continue; // The source face is unpainted and unsplit.
        spliced.first.emplace_back(int(dst_face), int(spliced.second.size()));
        spliced.second.insert(spliced.second.end(), src_bits.begin() + first,
                              src_bits.begin() + last);
    }
    return spliced;
}

// ---------------------------------------------------------------------------
// High-level entry points.
// ---------------------------------------------------------------------------

bool reproject_paint(const ModelVolume &src_volume,
                     ModelVolume       &dst_volume,
                     const std::vector<int> &dst_to_src_face,
                     const Transform3d      &destination_to_source,
                     const PaintReprojectProgressCallback &progress,
                     const PaintReprojectCancelCallback   &cancel)
{
    // Before anything else: the prologue below (the vertex transform and the congruence
    // classification) walks the whole mesh once per volume, which a cut that is already
    // being canceled should not have to pay for.
    if (cancel && cancel())
        return false;
    if (dst_to_src_face.empty())
        return true;
    const TriangleMesh &src_mesh = src_volume.mesh();
    const TriangleMesh &dst_mesh = dst_volume.mesh();
    if (src_mesh.its.indices.empty() || dst_mesh.its.indices.empty())
        return true;

    // Same layer labels as the repair path, so a cut and a repair show the user the
    // very same progress messages while restoring the paint.
    struct Layer {
        const FacetsAnnotation &src;
        FacetsAnnotation       &dst;
        const char             *name;    // internal log tag
        const char             *display; // user-facing progress label (English)
    };
    const std::array<Layer, 4> layers {{
        {src_volume.supported_facets,        dst_volume.supported_facets,        "support",    L("Restoring support painting")},
        {src_volume.seam_facets,             dst_volume.seam_facets,             "seam",       L("Restoring seam painting")},
        {src_volume.mmu_segmentation_facets, dst_volume.mmu_segmentation_facets, "mmu",        L("Restoring paint color")},
        {src_volume.fuzzy_skin_facets,       dst_volume.fuzzy_skin_facets,       "fuzzy skin", L("Restoring fuzzy skin")},
    }};
    int active_layers = 0;
    for (const Layer &layer : layers)
        if (!layer.src.empty())
            ++ active_layers;
    if (active_layers == 0)
        return true;

    // Geometry-only, so it is computed once and shared by all four layers.
    const DestinationInSourceFrame dst_in_source(dst_mesh, destination_to_source);
    const double                   src_diagonal = src_mesh.bounding_box().size().norm();
    const CutFaceClassification    classification =
        classify_cut_faces(src_mesh, dst_mesh, dst_to_src_face, dst_in_source,
                           float(src_diagonal * CutCongruenceRelativeTolerance));

    // Same slab half-width and refinement target as the repair path. Because the
    // target is absolute rather than derived per volume, both halves of a cut stop
    // at the same resolution and their boundaries meet at the cut line instead of
    // drifting apart, which the resampling path needed a dedicated root-relative
    // floor to achieve.
    const double band = src_diagonal > 0.0 ? src_diagonal / 100.0 : -1.0;
    // No world matrix: process_volume_cut() slices a mesh it already transformed by
    // instance x volume, and add_cut_volume() only recenters it, so these vertices are
    // millimeters and the target applies as-is.
    const RegionEdgeLimit edge_limit(dst_mesh, nullptr);
    // The mask is the same for all four layers, so this is geometry-only as well.
    DestinationSeedIndex seed_index(dst_mesh, dst_in_source, &classification.fragment_faces);

    int layer_index = 0;
    for (const Layer &layer : layers) {
        if (layer.src.empty())
            continue;
        if (cancel && cancel())
            return false;

        // Distribute the [0, 100] range evenly across the non-empty layers so the bar
        // advances smoothly regardless of how many paint types are present.
        const int base_percent = layer_index * 100 / active_layers;
        const int span_percent = 100 / active_layers;
        PaintReprojectProgressCallback layer_progress;
        if (progress)
            layer_progress = [&progress, base_percent, span_percent](int percent, const char *message) {
                progress(base_percent + percent * span_percent / 100, message);
            };
        ++ layer_index;
        if (progress)
            progress(base_percent, layer.display);

        const auto started_at = std::chrono::steady_clock::now();
        const std::vector<std::pair<int, int>> src_ranges =
            annotation_bit_ranges(layer.src, int(src_mesh.its.indices.size()));
        if (src_ranges.empty()) {
            // The serialized layout the splice relies on does not hold. Should not
            // happen for anything serialize() produced; resample the whole volume
            // rather than risk splicing the stream at the wrong offset.
            BOOST_LOG_TRIVIAL(error)
                << "Paint reprojection [" << layer.name
                << "] has an unexpected serialized layout, falling back to resampling";
            if (!reproject_one_annotation(layer.src, layer.dst, src_mesh, dst_mesh,
                                          destination_to_source, &dst_to_src_face,
                                          ReprojectMode::OverlapCoplanar, layer.name,
                                          layer_progress, cancel, -1.0, layer.display))
                return false;
            continue;
        }

        TriangleSelector dst_sel(dst_mesh);
        dst_sel.deserialize(splice_congruent_paint(layer.src, dst_to_src_face,
                                                   classification.origin, src_ranges),
                            true);
        const size_t spliced_nodes = count_valid_nodes(dst_sel);

        // Measure the paint only where the cut re-triangulated the surface. The mask
        // keeps the walk inside those fragments so the spliced trees stay untouched.
        RegionReprojectStats stats;
        size_t               fragment_source_leaves = 0;
        if (classification.fragment_count > 0) {
            const AnnotationData fragment_paint = extract_source_roots(
                layer.src, classification.fragment_source_roots, src_ranges);
            PaintRegionSource source(src_mesh, fragment_paint, band,
                                     &classification.fragment_source_roots);
            fragment_source_leaves = source.leaf_count();
            if (!source.empty())
                stats = reproject_one_annotation_by_region(
                    source, dst_sel, seed_index, dst_in_source, edge_limit.value(),
                    band > 0.0 ? float(band) : 0.f, layer_progress, cancel, layer.display,
                    &classification.fragment_faces, edge_limit.trafo());
        }
        if (stats.canceled)
            return false;
        layer.dst.set(dst_sel);

        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at).count();
        BOOST_LOG_TRIVIAL(info)
            << "Paint reprojection [" << layer.name << "] by cut splice: congruent faces="
            << classification.congruent_count << ", fragments="
            << classification.fragment_count << ", invented="
            << classification.invented_count << ", spliced nodes=" << spliced_nodes
            << ", fragment source leaves=" << fragment_source_leaves
            << ", edge limit=" << edge_limit.value()
            << ", fragment nodes created=" << stats.nodes_created
            << ", target nodes=" << count_valid_nodes(dst_sel)
            << ", budget exhausted=" << stats.budget_exhausted
            << ", elapsed=" << elapsed_ms << "ms";
    }
    if (progress)
        progress(100, L("Restoring painting"));
    return true;
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
                               const PaintReprojectCancelCallback   &cancel,
                               const Transform3d      *dst_world_matrix)
{
    // Bail out before the whole-mesh prologue below, and before resetting anything, so a
    // cancellation that arrived in the meantime leaves the destination exactly as it was.
    if (cancel && cancel())
        return false;

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
        {src_supported, dst_supported, "support",    L("Restoring support painting")},
        {src_seam,      dst_seam,      "seam",       L("Restoring seam painting")},
        {src_mmu,       dst_mmu,       "mmu",        L("Restoring paint color")},
        {src_fuzzy,     dst_fuzzy,     "fuzzy skin", L("Restoring fuzzy skin")},
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

    // Floor the per-face convergence target at a fraction of the whole destination
    // mesh area so tiny faces stop early instead of chasing a negligible absolute
    // error. Computed once here and shared by every layer (was recomputed per layer).
    double dst_total_area = 0.0;
    for (const Vec3i &face : dst_mesh.its.indices) {
        const Vec3f &a = dst_mesh.its.vertices[face[0]];
        const Vec3f &b = dst_mesh.its.vertices[face[1]];
        const Vec3f &c = dst_mesh.its.vertices[face[2]];
        dst_total_area += 0.5 * double((b - a).cross(c - a).norm());
    }
    const double absolute_target_error = std::max(
        ReprojectAbsoluteErrorEpsilon, ReprojectAbsoluteTargetAreaFraction * dst_total_area);

    // Destination leaf edge length for the region path: the absolute accuracy
    // target, relaxed to a fixed share of the model diagonal on objects large
    // enough that the target alone would blow up the node count. Unlike the cut
    // path this mesh is volume-local, so the caller's world matrix is what turns
    // the target into actual millimeters on a scaled volume.
    const RegionEdgeLimit edge_limit(dst_mesh, dst_world_matrix);

    const DestinationInSourceFrame dst_in_source(dst_mesh, dst_to_src);
    // Repair paints the whole destination, so no mask; shared by all four layers.
    DestinationSeedIndex seed_index(dst_mesh, dst_in_source, nullptr);

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

        // Re-paint each paint state as one brush stroke. This is the accurate path:
        // refinement follows measured overlap area, so a boundary is captured
        // regardless of how coarsely the repair re-triangulated the destination.
        PaintRegionSource source(src_mesh, layer.src.get_data(), max_sample_distance);
        bool completed = true;
        if (!source.empty() && source.leaf_count() <= RegionPaintMaxSourceLeaves) {
            const auto       started_at = std::chrono::steady_clock::now();
            TriangleSelector dst_sel(dst_mesh);
            const RegionReprojectStats stats = reproject_one_annotation_by_region(
                source, dst_sel, seed_index, dst_in_source, edge_limit.value(),
                max_sample_distance > 0.0 ? float(max_sample_distance) : 0.f,
                layer_progress, cancel, layer.display, nullptr, edge_limit.trafo());
            completed = !stats.canceled;
            if (completed) {
                const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started_at).count();
                BOOST_LOG_TRIVIAL(info)
                    << "Paint reprojection [" << layer.name << "] by region: source leaves="
                    << source.leaf_count() << ", states=" << source.states().size()
                    << ", source finest leaf=" << source.finest_leaf_edge()
                    << ", edge limit=" << edge_limit.value()
                    << ", nodes created=" << stats.nodes_created
                    << ", target nodes=" << count_valid_nodes(dst_sel)
                    << ", budget exhausted=" << stats.budget_exhausted
                    << ", elapsed=" << elapsed_ms << "ms";
                if (stats.budget_exhausted)
                    BOOST_LOG_TRIVIAL(warning)
                        << "Paint reprojection [" << layer.name
                        << "] by region exhausted its node budget; the paint boundary stays "
                           "coarser than " << edge_limit.value() << " in places";
                layer.dst.set(dst_sel);
            }
        } else if (!source.empty()) {
            // Pathological annotation: flattening it would cost more memory than
            // the result is worth, so fall back to point sampling for this layer.
            BOOST_LOG_TRIVIAL(warning)
                << "Paint reprojection [" << layer.name << "] has " << source.leaf_count()
                << " source leaves, falling back to point sampling";
            completed = reproject_one_annotation(
                layer.src, layer.dst, src_mesh, dst_mesh, dst_to_src, nullptr,
                ReprojectMode::PointNearest, layer.name, layer_progress, cancel,
                max_sample_distance, layer.display, absolute_target_error);
        }
        if (!completed)
            return false;
        ++layer_index;
    }
    if (progress)
        progress(100, L("Restoring painting"));
    return true;
}

} // namespace Slic3r
