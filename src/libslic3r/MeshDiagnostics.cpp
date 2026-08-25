#include "MeshDiagnostics.hpp"
#include "MeshRaycast.hpp"
#include "Point.hpp"

#include <Eigen/Core>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include <boost/log/trivial.hpp>

namespace Slic3r {

namespace {

struct VertexFan {
    std::vector<size_t> faces;
    std::vector<size_t> parent;
};

struct EdgeRef {
    size_t v0;
    size_t v1;
    size_t v0_fan_idx;
    size_t v1_fan_idx;
    size_t face;
    bool   plus;
};

struct DirectedEdge {
    size_t v0;
    size_t v1;
    size_t face;
    bool   plus;
};

static size_t fan_root(std::vector<size_t> &parent, size_t idx)
{
    size_t root = idx;
    while (parent[root] != root)
        root = parent[root];

    while (parent[idx] != idx) {
        size_t next = parent[idx];
        parent[idx] = root;
        idx = next;
    }

    return root;
}

static void fan_union(VertexFan &fan, size_t a, size_t b)
{
    size_t root_a = fan_root(fan.parent, a);
    size_t root_b = fan_root(fan.parent, b);
    if (root_a != root_b)
        fan.parent[root_b] = root_a;
}

static bool face_vertices_valid(const indexed_triangle_set &its, const stl_triangle_vertex_indices &idx)
{
    const size_t n = its.vertices.size();
    return idx[0] >= 0 && idx[1] >= 0 && idx[2] >= 0
        && static_cast<size_t>(idx[0]) < n
        && static_cast<size_t>(idx[1]) < n
        && static_cast<size_t>(idx[2]) < n;
}

// A hit whose smallest barycentric coordinate is this close to 0 sits on a
// triangle edge or vertex, where neighbouring faces answer the same query
// differently. Both ray-based layers discard such hits instead of guessing.
constexpr double k_bary_eps = 1e-4;

// True if the hit lies on a triangle edge or vertex (barycentric coord ~ 0).
// RayMeshHit uses P = (1-u-v)*v0 + u*v1 + v*v2.
static bool hit_on_edge_or_vertex(const RayMeshHit &hit, double bary_eps)
{
    const double w = 1.0 - hit.u - hit.v;
    return std::min(hit.u, std::min(hit.v, w)) <= bary_eps;
}

// Layer 2: orientation test per face-connected component (shared
// opposite-direction manifold edges — the same "shell / patch" unit as
// its_number_of_patches).
// Closed components use signed volume; open components use the libigl-style
// area-weighted centroid test. Magnitudes below k_orient_eps are treated as
// inconclusive (near-planar sheets) and do not set the flag.
//
// An inward shell is only wrong once its nesting depth is known. A well-formed
// solid alternates orientation with depth: depth 0 shells (outermost) face
// outward, depth 1 shells bound a cavity carved out of solid material and must
// face inward, depth 2 shells are solid islands inside such a cavity and face
// outward again. Reporting every inward shell would flag each correctly
// modelled cavity, so nested shells are resolved by ray parity below.
constexpr double k_orient_eps = 1e-4;

struct ComponentOrient {
    bool        inward = false;
    // False for empty, degenerate and near-planar components. Their inward flag
    // carries no information, so the nesting test must not judge them either.
    bool        conclusive = false;
    double      metric     = 0.;
    double      scale      = 0.;
    double      rel        = 0.;
    const char *reason     = "ok";
};

static ComponentOrient eval_component_orient(const indexed_triangle_set &its,
                                             const std::vector<size_t>  &faces,
                                             bool                        closed)
{
    ComponentOrient out;
    if (faces.empty() || its.vertices.empty()) {
        out.reason = "empty";
        return out;
    }

    if (closed) {
        const stl_vertex p0 = its.vertices.front();
        double volume  = 0.;
        double abs_acc = 0.;
        for (size_t fid : faces) {
            const auto &idx = its.indices[fid];
            if (!face_vertices_valid(its, idx))
                continue;
            const stl_vertex &a = its.vertices[idx[0]];
            const stl_vertex &b = its.vertices[idx[1]];
            const stl_vertex &c = its.vertices[idx[2]];
            const stl_vertex  U = b - a;
            const stl_vertex  V = c - a;
            const stl_vertex  C = U.cross(V);
            const float       n2 = C.squaredNorm();
            if (n2 == 0.f)
                continue;
            const float n      = std::sqrt(n2);
            const float area   = 0.5f * n;
            const float height = (C / n).dot(a - p0);
            const double tet   = (area * height) / 3.0;
            volume  += tet;
            abs_acc += std::abs(tet);
        }
        out.metric = volume;
        out.scale  = abs_acc;
        out.rel    = (abs_acc > 0.) ? std::abs(volume) / abs_acc : 0.;
        if (std::abs(volume) < k_orient_eps * abs_acc) {
            out.reason = "inconclusive";
            return out;
        }
        out.inward     = volume < 0.;
        out.conclusive = true;
        out.reason     = out.inward ? "inward_volume" : "outward_volume";
        return out;
    }

    Eigen::Vector3d weighted_bc = Eigen::Vector3d::Zero();
    double          totA        = 0.;
    struct FaceGeom {
        Eigen::Vector3d N;
        Eigen::Vector3d bc;
        double          area;
    };
    std::vector<FaceGeom> geoms;
    geoms.reserve(faces.size());

    for (size_t fid : faces) {
        const auto &idx = its.indices[fid];
        if (!face_vertices_valid(its, idx))
            continue;
        const Eigen::Vector3d a  = its.vertices[idx[0]].cast<double>();
        const Eigen::Vector3d b  = its.vertices[idx[1]].cast<double>();
        const Eigen::Vector3d c  = its.vertices[idx[2]].cast<double>();
        const Eigen::Vector3d C  = (b - a).cross(c - a);
        const double          n  = C.norm();
        if (n == 0.)
            continue;
        const double          area = 0.5 * n;
        const Eigen::Vector3d bc   = (a + b + c) / 3.0;
        const Eigen::Vector3d N    = C / n;
        weighted_bc += area * bc;
        totA        += area;
        geoms.push_back({ N, bc, area });
    }

    if (totA <= 0.) {
        out.reason = "degenerate";
        return out;
    }

    const Eigen::Vector3d centroid = weighted_bc / totA;
    double                dot      = 0.;
    for (const FaceGeom &g : geoms)
        dot += g.area * g.N.dot(g.bc - centroid);
    out.metric = dot;
    out.scale  = totA;
    out.rel    = std::abs(dot) / totA;
    if (std::abs(dot) < k_orient_eps * totA) {
        out.reason = "inconclusive";
        return out;
    }
    out.inward     = dot < 0.;
    out.conclusive = true;
    out.reason     = out.inward ? "inward_centroid" : "outward_centroid";
    return out;
}

// One face-connected shell plus everything the nesting test needs about it.
struct MeshComponent {
    std::vector<size_t> faces;
    bool                closed     = false;
    bool                inward     = false;
    bool                conclusive = false;
    bool                has_bbox   = false;
    Vec3d               bmin       = Vec3d::Zero();
    Vec3d               bmax       = Vec3d::Zero();
};

// Axis-aligned bounds over the component's valid vertices. Leaves has_bbox
// false when the component contributed no usable vertex at all.
static void component_bbox(const indexed_triangle_set &its, MeshComponent &comp)
{
    for (size_t fid : comp.faces) {
        const auto &idx = its.indices[fid];
        if (!face_vertices_valid(its, idx))
            continue;
        for (int k = 0; k < 3; ++k) {
            const Vec3d v = its.vertices[idx[k]].cast<double>();
            if (comp.has_bbox) {
                comp.bmin = comp.bmin.cwiseMin(v);
                comp.bmax = comp.bmax.cwiseMax(v);
            } else {
                comp.bmin     = v;
                comp.bmax     = v;
                comp.has_bbox = true;
            }
        }
    }
}

// Touching bounds count as possibly contained. Over-reporting here only costs
// the ray test, while under-reporting would flag a correctly modelled cavity.
static bool bbox_may_contain(const MeshComponent &outer, const MeshComponent &inner)
{
    return outer.has_bbox && inner.has_bbox
        && (inner.bmin.array() >= outer.bmin.array()).all()
        && (inner.bmax.array() <= outer.bmax.array()).all();
}

// Nesting depth of the component modulo 2, i.e. the parity of how many other
// closed shells enclose it. Returns -1 when no ray produced an unambiguous
// count, in which case the caller must not draw any conclusion.
static int nesting_parity(const indexed_triangle_set       &its,
                          const TriangleRaycaster          &caster,
                          const std::vector<int>           &face_comp,
                          const std::vector<MeshComponent> &comps,
                          int                               comp_idx)
{
    // Axis and diagonal directions. A shell that degenerates one of them
    // (axis-aligned boxes put face centres right on the diagonal split of the
    // opposite face) rarely degenerates all of them.
    const Vec3d      k_dirs[]      = { Vec3d(1., 0., 0.),  Vec3d(0., 1., 0.), Vec3d(0., 0., 1.), Vec3d(1., 1., 1.),
                                       Vec3d(-1., 1., 1.), Vec3d(1., -1., 1.), Vec3d(1., 1., -1.) };
    constexpr size_t k_max_origins = 4;

    size_t tried = 0;
    for (size_t fid : comps[comp_idx].faces) {
        if (tried >= k_max_origins)
            break;
        const auto &idx = its.indices[fid];
        if (!face_vertices_valid(its, idx))
            continue;
        ++tried;
        // Face centre, so the origin never sits on an edge of its own shell.
        const Vec3d origin = (its.vertices[idx[0]].cast<double>()
                            + its.vertices[idx[1]].cast<double>()
                            + its.vertices[idx[2]].cast<double>()) / 3.0;

        int  agreed       = -1;
        bool contradicted = false;
        for (const Vec3d &d : k_dirs) {
            int coplanar = 0;
            const std::vector<RayMeshHit> hits = caster.all_hits(origin, d.normalized(), &coplanar);
            if (coplanar > 0)
                continue;
            int  crossings = 0;
            bool clean     = true;
            for (const RayMeshHit &hit : hits) {
                if (hit.face < 0 || static_cast<size_t>(hit.face) >= face_comp.size()) {
                    clean = false;
                    break;
                }
                const int owner = face_comp[hit.face];
                if (owner < 0) {
                    clean = false;
                    break;
                }
                // Only closed shells enclose anything, and the component's own
                // faces say nothing about what encloses it.
                if (owner == comp_idx || !comps[owner].closed)
                    continue;
                if (hit_on_edge_or_vertex(hit, k_bary_eps)) {
                    clean = false;
                    break;
                }
                ++crossings;
            }
            if (!clean)
                continue;
            const int parity = crossings & 1;
            if (agreed < 0)
                agreed = parity;
            else if (agreed != parity) {
                contradicted = true;
                break;
            }
        }
        // Closed shells must yield the same parity along every direction, so a
        // disagreement means the shells interpenetrate: stay silent.
        if (contradicted)
            return -1;
        if (agreed >= 0)
            return agreed;
    }
    return -1;
}

static bool detect_inward_orientation(const indexed_triangle_set             &its,
                                      const std::vector<std::vector<size_t>> &adj)
{
    const size_t n = its.indices.size();
    if (n == 0)
        return false;

    std::vector<char>          seen(n, 0);
    std::vector<size_t>        stack;
    std::vector<MeshComponent> comps;

    for (size_t seed = 0; seed < n; ++seed) {
        if (seen[seed])
            continue;

        comps.emplace_back();
        MeshComponent &comp = comps.back();
        comp.closed         = true;
        stack.clear();
        stack.push_back(seed);
        seen[seed] = 1;

        while (!stack.empty()) {
            const size_t f = stack.back();
            stack.pop_back();
            comp.faces.push_back(f);
            if (adj[f].size() < 3)
                comp.closed = false;
            for (size_t nb : adj[f]) {
                if (!seen[nb]) {
                    seen[nb] = 1;
                    stack.push_back(nb);
                }
            }
        }
    }

    // Bounds first: with no containment candidate every shell sits at depth 0
    // and the ray test can be skipped entirely.
    bool nesting_possible = false;
    if (comps.size() > 1) {
        for (MeshComponent &comp : comps)
            component_bbox(its, comp);
        for (size_t i = 0; i < comps.size() && !nesting_possible; ++i)
            for (size_t j = 0; j < comps.size(); ++j)
                if (i != j && comps[j].closed && bbox_may_contain(comps[j], comps[i])) {
                    nesting_possible = true;
                    break;
                }
    }

    for (size_t i = 0; i < comps.size(); ++i) {
        const ComponentOrient orient = eval_component_orient(its, comps[i].faces, comps[i].closed);
        comps[i].inward     = orient.inward;
        comps[i].conclusive = orient.conclusive;
        BOOST_LOG_TRIVIAL(info)
            << "reversed-faces: layer2 comp=" << i
            << " faces=" << comps[i].faces.size()
            << " closed=" << (comps[i].closed ? 1 : 0)
            << " inward=" << (orient.inward ? 1 : 0)
            << " reason=" << orient.reason
            << " metric=" << orient.metric
            << " scale=" << orient.scale
            << " rel=" << orient.rel;
        // Depth 0 everywhere, so an inward shell is reversed and the remaining
        // components need not be evaluated.
        if (!nesting_possible && orient.conclusive && orient.inward)
            return true;
    }

    if (!nesting_possible) {
        BOOST_LOG_TRIVIAL(info) << "reversed-faces: layer2 components=" << comps.size() << " flat no_inward";
        return false;
    }

    // Nested shells: one raycaster over the whole mesh, then let each shell's
    // nesting depth decide which orientation is the correct one.
    std::vector<int> face_comp(n, -1);
    for (size_t i = 0; i < comps.size(); ++i)
        for (size_t fid : comps[i].faces)
            face_comp[fid] = static_cast<int>(i);

    const TriangleRaycaster caster(its);
    bool                    reversed = false;
    for (size_t i = 0; i < comps.size(); ++i) {
        const int  parity        = nesting_parity(its, caster, face_comp, comps, static_cast<int>(i));
        const bool expect_inward = parity == 1;
        const bool mismatch      = parity >= 0 && comps[i].conclusive && comps[i].inward != expect_inward;
        BOOST_LOG_TRIVIAL(info)
            << "reversed-faces: layer2 nesting comp=" << i
            << " parity=" << parity
            << " inward=" << (comps[i].inward ? 1 : 0)
            << " expect_inward=" << (expect_inward ? 1 : 0)
            << " mismatch=" << (mismatch ? 1 : 0);
        if (mismatch) {
            reversed = true;
            break;
        }
    }

    BOOST_LOG_TRIVIAL(info) << "reversed-faces: layer2 components=" << comps.size()
                            << " nested reversed=" << (reversed ? 1 : 0);
    return reversed;
}

// Layer 3: visible backfaces from outside the AABB. Layers 1–2 miss geometric
// self-intersections / folds that still look reversed in the viewport (the
// outermost hit seen from outside is a back-facing triangle). Closed, manifold
// meshes only. Walk every ray so a flagged case dumps front/back counts.
// Per ray: all hits. A closed mesh must produce an even hit count
// (enter/leave pairs); an odd count is discarded as a leak. The whole ray is
// then discarded if the two smallest t differ by less than k_coincident_t, or
// if its outermost hit is a sliver, lands on an edge / vertex, or is grazing.
// Only the outermost surviving hit is tested; one back-facing hit sets the flag.
static bool detect_visible_backfaces(const indexed_triangle_set &its)
{
    if (its.indices.empty() || its.vertices.empty())
        return false;

    // Skip layer 3 on malformed indices; later face lookups index vertices
    // without a bounds check. Face count is not a reason to skip.
    for (const auto &idx : its.indices) {
        if (!face_vertices_valid(its, idx)) {
            BOOST_LOG_TRIVIAL(info) << "reversed-faces: layer3 skip invalid_index";
            return false;
        }
    }

    Eigen::Vector3f bmin_f = its.vertices.front();
    Eigen::Vector3f bmax_f = bmin_f;
    for (const auto &v : its.vertices) {
        bmin_f = bmin_f.cwiseMin(v);
        bmax_f = bmax_f.cwiseMax(v);
    }

    const Vec3d  bmin    = bmin_f.cast<double>();
    const Vec3d  bmax    = bmax_f.cast<double>();
    const Vec3d  center  = 0.5 * (bmin + bmax);
    const Vec3d  extent  = bmax - bmin;
    const double radius  = 0.5 * extent.norm();
    if (radius <= 0.) {
        BOOST_LOG_TRIVIAL(info) << "reversed-faces: layer3 skip zero_radius";
        return false;
    }
    const double max_ext  = extent.maxCoeff();
    const double standoff = radius + std::max(max_ext * 0.05, 1.0);

    BOOST_LOG_TRIVIAL(info)
        << "reversed-faces: layer3 aabb extent=["
        << extent.x() << "," << extent.y() << "," << extent.z() << "]"
        << " faces=" << its.indices.size()
        << " verts=" << its.vertices.size();

    const TriangleRaycaster caster(its);

    std::vector<Vec3d> dirs;
    dirs.reserve(46);
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            for (int z = -1; z <= 1; ++z) {
                if (x == 0 && y == 0 && z == 0)
                    continue;
                dirs.emplace_back(double(x), double(y), double(z));
            }
    constexpr int    extra  = 20;
    constexpr double golden = 2.399963229728653;
    for (int i = 0; i < extra; ++i) {
        const double y   = 1.0 - 2.0 * (i + 0.5) / extra;
        const double r   = std::sqrt(std::max(0.0, 1.0 - y * y));
        const double phi = i * golden;
        dirs.emplace_back(r * std::cos(phi), y, r * std::sin(phi));
    }

    constexpr double graze           = 0.08;
    constexpr double backface        = 1e-4;
    constexpr int    k_log_back_hits = 8;
    // Two crossings closer than this are treated as coincident faces. Relative
    // to the AABB so a 2 mm part and a 300 mm part do not share one absolute
    // threshold; the factor keeps a typical 100 mm part near the former 0.01 mm.
    const double k_coincident_t = radius * 1e-4;
    // nlen = ||(b-a)×(c-a)|| = 2 * area. Drop slivers whose area is tiny
    // relative to the AABB; their normals are not reliable.
    const double min_nlen = 2.0 * radius * radius * 1e-12;

    int n_ray = 0, n_miss = 0, n_odd = 0, n_coincident = 0, n_boundary = 0;
    int n_graze = 0, n_sliver = 0, n_front = 0, n_back = 0, n_coplanar = 0;

    for (Vec3d dir : dirs) {
        const double len = dir.norm();
        if (len == 0.)
            continue;
        dir /= len;
        ++n_ray;
        const Vec3d origin  = center + dir * standoff;
        const Vec3d ray_dir = -dir;

        // Coplanar crossings yield no hit, so they can break the parity check
        // below; count them to explain an elevated odd count in the log.
        int                     ray_coplanar = 0;
        std::vector<RayMeshHit> hits         = caster.all_hits(origin, ray_dir, &ray_coplanar);
        n_coplanar += ray_coplanar;
        hits.erase(std::remove_if(hits.begin(), hits.end(),
                                  [](const RayMeshHit &h) { return h.t < 1e-6; }),
                   hits.end());
        if (hits.empty()) {
            ++n_miss;
            continue;
        }
        // Closed meshes must enter and leave in pairs; an odd hit count is a leak.
        if (hits.size() % 2 == 1) {
            ++n_odd;
            continue;
        }

        if (hits[1].t - hits[0].t < k_coincident_t) {
            ++n_coincident;
            continue;
        }

        const RayMeshHit &hit = hits.front();
        if (hit.face < 0 || static_cast<size_t>(hit.face) >= its.indices.size()) {
            ++n_miss;
            continue;
        }

        const auto &idx = its.indices[hit.face];
        if (!face_vertices_valid(its, idx)) {
            ++n_miss;
            continue;
        }

        const Vec3d a = its.vertices[idx[0]].cast<double>();
        const Vec3d b = its.vertices[idx[1]].cast<double>();
        const Vec3d c = its.vertices[idx[2]].cast<double>();
        Vec3d       N = (b - a).cross(c - a);
        const double nlen = N.norm();
        if (nlen < min_nlen) {
            ++n_sliver;
            continue;
        }
        N /= nlen;

        if (hit_on_edge_or_vertex(hit, k_bary_eps)) {
            ++n_boundary;
            continue;
        }

        const double n_ray_dot = N.dot(ray_dir);
        if (std::abs(n_ray_dot) < graze) {
            ++n_graze;
            continue;
        }

        const Vec3d  hit_pt = origin + ray_dir * hit.t;
        const Vec3d  V      = origin - hit_pt;
        const double ndot   = N.dot(V);

        if (ndot < -backface) {
            ++n_back;
            if (n_back <= k_log_back_hits) {
                BOOST_LOG_TRIVIAL(info)
                    << "reversed-faces: layer3 backface#" << n_back
                    << " face=" << hit.face
                    << " t=" << hit.t
                    << " ndot=" << ndot
                    << " n_dot_ray=" << n_ray_dot
                    << " dir=[" << dir.x() << "," << dir.y() << "," << dir.z() << "]"
                    << " N=[" << N.x() << "," << N.y() << "," << N.z() << "]"
                    << " hit=[" << hit_pt.x() << "," << hit_pt.y() << "," << hit_pt.z() << "]";
            }
        } else {
            ++n_front;
        }
    }

    BOOST_LOG_TRIVIAL(info)
        << "reversed-faces: layer3 summary rays=" << n_ray
        << " miss=" << n_miss
        << " odd=" << n_odd
        << " coplanar=" << n_coplanar
        << " coincident=" << n_coincident
        << " sliver=" << n_sliver
        << " boundary=" << n_boundary
        << " graze=" << n_graze
        << " front=" << n_front
        << " back=" << n_back;

    const bool flagged = n_back > 0;
    if (!flagged)
        BOOST_LOG_TRIVIAL(info) << "reversed-faces: layer3 no visible backface";
    return flagged;
}

static void maybe_detect_visible_backfaces(const indexed_triangle_set &its, MeshDiagnosticStats &result)
{
    if (result.has_reversed_faces) {
        BOOST_LOG_TRIVIAL(info) << "reversed-faces: layer3 skip already_reversed";
        return;
    }
    if (result.open_edges != 0 || result.non_manifold_edges != 0) {
        BOOST_LOG_TRIVIAL(info) << "reversed-faces: layer3 skip open_or_nonmanifold"
                               << " open_edges=" << result.open_edges
                               << " nm_edges=" << result.non_manifold_edges;
        return;
    }
    result.has_reversed_faces = detect_visible_backfaces(its);
}

static void finish_reversed_face_layers(const indexed_triangle_set             &its,
                                        const std::vector<std::vector<size_t>> &adj,
                                        MeshDiagnosticStats                    &result,
                                        size_t                                  same_dir_edges,
                                        const char                             *src)
{
    const bool layer1 = same_dir_edges > 0;
    BOOST_LOG_TRIVIAL(info)
        << "reversed-faces: " << src
        << " layer1 same_dir=" << (layer1 ? 1 : 0)
        << " same_dir_edges=" << same_dir_edges
        << " open_edges=" << result.open_edges
        << " nm_edges=" << result.non_manifold_edges
        << " faces=" << its.indices.size()
        << " verts=" << its.vertices.size();
    const bool layer2 = result.has_reversed_faces ? false : detect_inward_orientation(its, adj);
    if (layer2)
        result.has_reversed_faces = true;
    maybe_detect_visible_backfaces(its, result);
    const char *decided = !result.has_reversed_faces ? "none"
                        : layer1                     ? "1"
                        : layer2                     ? "2"
                                                     : "3";
    BOOST_LOG_TRIVIAL(info)
        << "reversed-faces: " << src
        << " has_reversed_faces=" << (result.has_reversed_faces ? 1 : 0)
        << " decided_layer=" << decided
        << " layer1=" << (layer1 ? 1 : 0)
        << " layer2=" << (layer2 ? 1 : 0);
    if (result.has_reversed_faces) {
        BOOST_LOG_TRIVIAL(info)
            << "reversed-faces: FLAG src=" << src
            << " decided_layer=" << decided
            << " faces=" << its.indices.size()
            << " verts=" << its.vertices.size()
            << " same_dir_edges=" << same_dir_edges
            << " open_edges=" << result.open_edges
            << " nm_edges=" << result.non_manifold_edges;
    }
}

} // anonymous namespace

MeshDiagnosticStats its_mesh_diagnostics(const indexed_triangle_set &its)
{
    MeshDiagnosticStats result;
    const size_t     num_vertices = its.vertices.size();
    const size_t     num_faces    = its.indices.size();

    if (num_faces == 0)
        return result;

    // --- Pass 1: build per-vertex face fans and flat edge refs --------------
    std::vector<VertexFan> vertex_fans(num_vertices);
    std::vector<EdgeRef>   edge_refs;
    edge_refs.reserve(num_faces * 3);

    for (size_t fid = 0; fid < num_faces; ++fid) {
        const auto &face = its.indices[fid];

        // Skip degenerate faces (two or more identical vertex indices).
        if (face[0] == face[1] || face[1] == face[2] || face[2] == face[0])
            continue;

        size_t vertices[3] = {
            static_cast<size_t>(face[0]),
            static_cast<size_t>(face[1]),
            static_cast<size_t>(face[2])
        };
        size_t fan_indices[3] = { size_t(-1), size_t(-1), size_t(-1) };

        for (int i = 0; i < 3; ++i) {
            const size_t vid = vertices[i];
            if (vid >= num_vertices)
                continue;

            fan_indices[i] = vertex_fans[vid].faces.size();
            vertex_fans[vid].faces.push_back(fid);
        }

        for (int i = 0; i < 3; ++i) {
            const int    j  = (i + 1) % 3;
            const size_t va = vertices[i];
            const size_t vb = vertices[j];

            if (va >= num_vertices || vb >= num_vertices)
                continue;

            if (va < vb)
                edge_refs.push_back({ va, vb, fan_indices[i], fan_indices[j], fid, true });
            else
                edge_refs.push_back({ vb, va, fan_indices[j], fan_indices[i], fid, false });
        }
    }

    // Initialize per-vertex union-find storage. Non-degenerate faces insert
    // each incident face only once per vertex, so no sort/unique pass is needed.
    for (auto &fan : vertex_fans) {
        fan.parent.resize(fan.faces.size());
        std::iota(fan.parent.begin(), fan.parent.end(), 0);
    }

    // --- Edge classification (each undirected edge counted at most once) -----
    // Also mark vertices incident on non-manifold edges so that the vertex
    // fan-connectivity test below can skip them (same strategy as VCGlib).
    // Two-face edge groups connect those two face fans at both edge endpoints.
    std::vector<bool>              on_nm_edge(num_vertices, false);
    std::vector<std::vector<size_t>> adj(num_faces);
    size_t                         same_dir_edges = 0;

    std::sort(edge_refs.begin(), edge_refs.end(), [](const EdgeRef &a, const EdgeRef &b) {
        return a.v0 < b.v0 || (a.v0 == b.v0 && a.v1 < b.v1);
    });

    for (size_t i = 0; i < edge_refs.size();) {
        size_t j = i + 1;
        while (j < edge_refs.size() && edge_refs[j].v0 == edge_refs[i].v0 && edge_refs[j].v1 == edge_refs[i].v1)
            ++j;

        const size_t edge_face_count = j - i;
        size_t plus = 0;
        for (size_t k = i; k < j; ++k)
            if (edge_refs[k].plus)
                ++plus;
        const size_t minus = edge_face_count - plus;
        if (plus >= 2 || minus >= 2) {
            result.has_reversed_faces = true;
            ++same_dir_edges;
            if (same_dir_edges <= 4) {
                BOOST_LOG_TRIVIAL(info)
                    << "reversed-faces: layer1 sample#" << same_dir_edges
                    << " v=[" << edge_refs[i].v0 << "," << edge_refs[i].v1 << "]"
                    << " face_count=" << edge_face_count
                    << " plus=" << plus << " minus=" << minus
                    << " face0=" << edge_refs[i].face;
            }
        }

        if (edge_face_count == 1) {
            ++result.open_edges;
        } else if (edge_face_count == 2) {
            fan_union(vertex_fans[edge_refs[i].v0], edge_refs[i].v0_fan_idx, edge_refs[i + 1].v0_fan_idx);
            fan_union(vertex_fans[edge_refs[i].v1], edge_refs[i].v1_fan_idx, edge_refs[i + 1].v1_fan_idx);
            if (plus == 1 && minus == 1) {
                adj[edge_refs[i].face].push_back(edge_refs[i + 1].face);
                adj[edge_refs[i + 1].face].push_back(edge_refs[i].face);
            }
        } else {
            ++result.non_manifold_edges;
            on_nm_edge[edge_refs[i].v0] = true;
            on_nm_edge[edge_refs[i].v1] = true;
        }

        i = j;
    }

    finish_reversed_face_layers(its, adj, result, same_dir_edges, "mesh");

    // --- Pass 2: non-manifold vertex detection ------------------------------
    // A vertex is non-manifold if its incident faces form more than one
    // component when connected through regular two-face edges.
    // Vertices on non-manifold edges are excluded: an edge with >2 faces
    // cannot define a reliable two-face fan traversal, and these vertices are
    // already accounted for by non_manifold_edges.
    std::vector<size_t> roots;

    for (size_t vid = 0; vid < num_vertices; ++vid) {
        if (on_nm_edge[vid])
            continue;

        auto &fan = vertex_fans[vid];
        if (fan.faces.size() <= 1)
            continue;

        roots.clear();
        roots.reserve(fan.parent.size());
        for (size_t i = 0; i < fan.parent.size(); ++i)
            roots.push_back(fan_root(fan.parent, i));

        std::sort(roots.begin(), roots.end());
        if (std::unique(roots.begin(), roots.end()) != roots.begin() + 1)
            ++result.non_manifold_vertices;
    }

    return result;
}

MeshDiagnosticStats its_edge_diagnostics(const indexed_triangle_set &its)
{
    MeshDiagnosticStats result;
    const size_t num_vertices = its.vertices.size();
    const size_t num_faces    = its.indices.size();

    if (num_faces == 0)
        return result;

    std::vector<DirectedEdge> edges;
    edges.reserve(num_faces * 3);

    for (size_t fid = 0; fid < num_faces; ++fid) {
        const auto &face = its.indices[fid];

        if (face[0] == face[1] || face[1] == face[2] || face[2] == face[0])
            continue;

        size_t v[3] = {
            static_cast<size_t>(face[0]),
            static_cast<size_t>(face[1]),
            static_cast<size_t>(face[2])
        };

        for (int i = 0; i < 3; ++i) {
            size_t va = v[i], vb = v[(i + 1) % 3];
            if (va >= num_vertices || vb >= num_vertices)
                continue;
            const bool plus = va < vb;
            if (!plus)
                std::swap(va, vb);
            edges.push_back({ va, vb, fid, plus });
        }
    }

    std::sort(edges.begin(), edges.end(), [](const DirectedEdge &a, const DirectedEdge &b) {
        return a.v0 < b.v0 || (a.v0 == b.v0 && a.v1 < b.v1);
    });

    std::vector<std::vector<size_t>> adj(num_faces);
    size_t same_dir_edges = 0;

    for (size_t i = 0; i < edges.size();) {
        size_t j = i + 1;
        while (j < edges.size() && edges[j].v0 == edges[i].v0 && edges[j].v1 == edges[i].v1)
            ++j;

        const size_t count = j - i;
        size_t plus = 0;
        for (size_t k = i; k < j; ++k)
            if (edges[k].plus)
                ++plus;
        const size_t minus = count - plus;
        if (plus >= 2 || minus >= 2) {
            result.has_reversed_faces = true;
            ++same_dir_edges;
            if (same_dir_edges <= 4) {
                BOOST_LOG_TRIVIAL(info)
                    << "reversed-faces: layer1 sample#" << same_dir_edges
                    << " v=[" << edges[i].v0 << "," << edges[i].v1 << "]"
                    << " face_count=" << count
                    << " plus=" << plus << " minus=" << minus
                    << " face0=" << edges[i].face;
            }
        }

        if (count == 1)
            ++result.open_edges;
        else if (count == 2) {
            if (plus == 1 && minus == 1) {
                adj[edges[i].face].push_back(edges[i + 1].face);
                adj[edges[i + 1].face].push_back(edges[i].face);
            }
        } else if (count > 2)
            ++result.non_manifold_edges;

        i = j;
    }

    finish_reversed_face_layers(its, adj, result, same_dir_edges, "edge");

    return result;
}


} // namespace Slic3r
