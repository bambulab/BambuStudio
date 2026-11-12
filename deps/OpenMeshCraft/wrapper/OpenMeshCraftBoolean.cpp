#include "OpenMeshCraftBoolean.h"

#include "OpenMeshCraft/Boolean/MeshBoolean.h"
#include "OpenMeshCraft/Geometry/ExactIndirectPredicatesApproxConstructions.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <map>
#include <string>
#include <vector>

namespace {

using OMCBoolean   = OMC::MeshBoolean<OMC::EIAC, OMC::TriSoupTraits>;
using OMCPoints    = OMC::TriSoupTraits::Points;
using OMCTriangle  = OMC::TriSoupTraits::Triangle;
using OMCTriangles = OMC::TriSoupTraits::Triangles;
using Collinear    = OMC::EIAC::CollinearPoints3;

void set_error(const std::string &message, char *buffer, size_t capacity)
{
    if (buffer == nullptr || capacity == 0)
        return;

    const size_t length = std::min(message.size(), capacity - 1);
    std::memcpy(buffer, message.data(), length);
    buffer[length] = '\0';
}

bool validate_mesh(const BambuOMCMesh *mesh, std::string &error)
{
    if (mesh == nullptr) {
        error = "OpenMeshCraft input mesh is null.";
        return false;
    }
    if ((mesh->point_count != 0 && mesh->points == nullptr) ||
        (mesh->triangle_count != 0 && mesh->triangles == nullptr)) {
        error = "OpenMeshCraft input mesh data is null.";
        return false;
    }
    return true;
}

// One connected component of an operand. Each becomes its own OpenMeshCraft input:
// OpenMeshCraft never intersects an input with itself, so touching or overlapping
// shells of one operand (e.g. raised text standing on a base plate, or two parts of
// one object) must be separate inputs to be resolved.
struct Component
{
    OMCPoints    points;
    OMCTriangles triangles;
    size_t       group = 0; // index of the operand it belongs to
    int          sign  = 1; // -1 when the shell encloses a negative volume (a cavity)
};

// OpenMeshCraft drops zero-area triangles. When the third vertex of such a triangle lies
// inside its long edge, that leaves the neighbour across the long edge facing a chain of
// two short edges: a crack that ends up in the result as open edges. Re-triangulate
// instead: split the neighbour at the middle vertex and drop the zero-area triangle. The
// surface and the vertices stay exactly as given. Collinearity uses OpenMeshCraft's own
// exact predicate, so both sides agree on which triangles have zero area.
void resolve_zero_area_triangles(Component &c)
{
    auto collinear = [&c](const OMCTriangle &t) {
        return Collinear()(c.points[t[0]].as_vec().data(), c.points[t[1]].as_vec().data(),
                           c.points[t[2]].as_vec().data());
    };
    auto same = [&c](size_t a, size_t b) {
        return c.points[a].x() == c.points[b].x() && c.points[a].y() == c.points[b].y() &&
               c.points[a].z() == c.points[b].z();
    };
    // p strictly between a and b, all three collinear
    auto between = [&c, &same](size_t p, size_t a, size_t b) {
        if (same(p, a) || same(p, b))
            return false;
        for (int k = 0; k < 3; ++k)
            if (c.points[p][k] < std::min(c.points[a][k], c.points[b][k]) ||
                c.points[p][k] > std::max(c.points[a][k], c.points[b][k]))
                return false;
        return true;
    };

    std::vector<char> zero_area(c.triangles.size());
    bool              any = false;
    for (size_t t = 0; t < c.triangles.size(); ++t)
        any |= (zero_area[t] = collinear(c.triangles[t])) != 0;
    if (!any)
        return;

    std::vector<char> removed(c.triangles.size(), 0);
    // directed edge (a, b) -> triangle; a closed manifold has each directed edge once
    std::map<std::pair<size_t, size_t>, size_t> edge;
    auto link = [&](size_t t, bool add) {
        for (int k = 0; k < 3; ++k) {
            const std::pair<size_t, size_t> e{c.triangles[t][k], c.triangles[t][(k + 1) % 3]};
            if (add)
                edge[e] = t;
            else
                edge.erase(e);
        }
    };
    for (size_t t = 0; t < c.triangles.size(); ++t)
        link(t, true);

    // A split can leave the neighbour zero-area in turn (a fan of collinear vertices), so
    // repeat until nothing changes.
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t t = 0; t < c.triangles.size(); ++t) {
            if (removed[t] || !zero_area[t])
                continue;
            const OMCTriangle tri = c.triangles[t];
            if (tri[0] == tri[1] || tri[1] == tri[2] || tri[2] == tri[0]) {
                // repeated vertex: its two other edges are each other's twins
                link(t, false);
                removed[t] = 1;
                changed    = true;
                continue;
            }
            for (int k = 0; k < 3; ++k) {
                const size_t a = tri[k], b = tri[(k + 1) % 3], m = tri[(k + 2) % 3];
                if (!between(m, a, b))
                    continue;
                const auto it = edge.find({b, a});
                if (it == edge.end() || it->second == t)
                    break;
                // neighbour (b, a, d) becomes (b, m, d) + (m, a, d); (a, b, m) is dropped
                const size_t      n   = it->second;
                const OMCTriangle nb  = c.triangles[n];
                const size_t      pos = nb[0] == b ? 0 : nb[1] == b ? 1 : 2;
                const size_t      d   = nb[(pos + 2) % 3];
                link(t, false);
                link(n, false);
                removed[t]     = 1;
                c.triangles[n] = OMCTriangle(b, m, d);
                c.triangles.emplace_back(m, a, d);
                zero_area[n] = collinear(c.triangles[n]);
                zero_area.push_back(collinear(c.triangles.back()));
                removed.push_back(0);
                link(n, true);
                link(c.triangles.size() - 1, true);
                changed = true;
                break;
            }
        }
    }

    OMCTriangles kept;
    kept.reserve(c.triangles.size());
    for (size_t t = 0; t < c.triangles.size(); ++t)
        if (!removed[t])
            kept.push_back(c.triangles[t]);
    c.triangles = std::move(kept);
}

// Splits an operand mesh into connected components. Vertices with identical coordinates
// are merged first, so a shell whose triangles do not share vertex indices stays whole.
// A component enclosing a negative volume is passed on flipped, with sign -1: the input
// is used as given, the cavity is subtracted.
void split_components(const BambuOMCMesh &mesh, size_t group, std::vector<Component> &out)
{
    const BambuOMCPoint *pts = mesh.points;
    std::vector<size_t>  order(mesh.point_count);
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    auto less = [pts](size_t a, size_t b) {
        if (pts[a].x != pts[b].x)
            return pts[a].x < pts[b].x;
        if (pts[a].y != pts[b].y)
            return pts[a].y < pts[b].y;
        return pts[a].z < pts[b].z;
    };
    std::sort(order.begin(), order.end(), less);
    std::vector<size_t> unique_id(mesh.point_count);
    size_t              num_unique = 0;
    for (size_t i = 0; i < order.size(); ++i) {
        if (i > 0 && less(order[i - 1], order[i]))
            ++num_unique;
        unique_id[order[i]] = num_unique;
    }
    if (!order.empty())
        ++num_unique;

    std::vector<size_t> parent(num_unique);
    for (size_t i = 0; i < parent.size(); ++i)
        parent[i] = i;
    auto find = [&parent](size_t x) {
        while (parent[x] != x)
            x = parent[x] = parent[parent[x]];
        return x;
    };
    for (size_t t = 0; t < mesh.triangle_count; ++t) {
        const size_t *v = mesh.triangles[t].vertices;
        const size_t  r = find(unique_id[v[0]]);
        parent[find(unique_id[v[1]])] = r;
        parent[find(unique_id[v[2]])] = find(r);
    }

    std::vector<size_t> component_of_root(num_unique, size_t(-1));
    std::vector<size_t> local_index(num_unique, size_t(-1));
    const size_t        first = out.size();
    for (size_t t = 0; t < mesh.triangle_count; ++t) {
        const size_t *v    = mesh.triangles[t].vertices;
        const size_t  root = find(unique_id[v[0]]);
        if (component_of_root[root] == size_t(-1)) {
            component_of_root[root] = out.size();
            out.emplace_back();
            out.back().group = group;
        }
        Component &c = out[component_of_root[root]];
        size_t     tri[3];
        for (int k = 0; k < 3; ++k) {
            const size_t u = unique_id[v[k]];
            if (local_index[u] == size_t(-1)) {
                local_index[u] = c.points.size();
                c.points.emplace_back(pts[v[k]].x, pts[v[k]].y, pts[v[k]].z);
            }
            tri[k] = local_index[u];
        }
        c.triangles.emplace_back(tri[0], tri[1], tri[2]);
    }

    for (size_t i = first; i < out.size(); ++i) {
        Component &c = out[i];
        resolve_zero_area_triangles(c);
        // signed volume, relative to the first vertex to limit cancellation
        const auto &o   = c.points.front();
        double      vol = 0;
        for (const auto &t : c.triangles) {
            const auto  &a = c.points[t[0]], &b = c.points[t[1]], &d = c.points[t[2]];
            const double ax = a.x() - o.x(), ay = a.y() - o.y(), az = a.z() - o.z();
            const double bx = b.x() - o.x(), by = b.y() - o.y(), bz = b.z() - o.z();
            const double dx = d.x() - o.x(), dy = d.y() - o.y(), dz = d.z() - o.z();
            vol += ax * (by * dz - bz * dy) - ay * (bx * dz - bz * dx) + az * (bx * dy - by * dx);
        }
        if (vol < 0) {
            c.sign = -1;
            for (auto &t : c.triangles)
                std::swap(t[1], t[2]);
        }
    }
    // a component made only of zero-area triangles encloses nothing
    out.erase(std::remove_if(out.begin() + std::ptrdiff_t(first), out.end(),
                             [](const Component &c) { return c.triangles.empty(); }),
              out.end());
}

// A point is inside an operand when the signed count of its components containing the
// point is positive (cavities count -1), so nested islands come out right.
bool in_result(const std::vector<int> &winding, BambuOMCOperation operation)
{
    switch (operation) {
    case BAMBU_OMC_UNION:
        return std::any_of(winding.begin(), winding.end(), [](int w) { return w > 0; });
    case BAMBU_OMC_INTERSECTION:
        return std::all_of(winding.begin(), winding.end(), [](int w) { return w > 0; });
    default: // BAMBU_OMC_SUBTRACTION
        return winding.front() > 0 &&
               std::none_of(winding.begin() + 1, winding.end(), [](int w) { return w > 0; });
    }
}

bool run_boolean(const BambuOMCOperand *operands, size_t operand_count, BambuOMCOperation operation,
                 BambuOMCResultCallback result_callback, void *user_data, char *error_message,
                 size_t error_message_capacity)
{
    if (operands == nullptr || operand_count == 0) {
        set_error("OpenMeshCraft boolean has no operands.", error_message, error_message_capacity);
        return false;
    }
    for (size_t g = 0; g < operand_count; ++g) {
        if (operands[g].mesh_count != 0 && operands[g].meshes == nullptr) {
            set_error("OpenMeshCraft operand data is null.", error_message, error_message_capacity);
            return false;
        }
        for (size_t m = 0; m < operands[g].mesh_count; ++m) {
            std::string err;
            if (!validate_mesh(&operands[g].meshes[m], err)) {
                set_error(err, error_message, error_message_capacity);
                return false;
            }
        }
    }
    if (result_callback == nullptr) {
        set_error("OpenMeshCraft result callback is null.", error_message, error_message_capacity);
        return false;
    }
    if (operation != BAMBU_OMC_UNION && operation != BAMBU_OMC_INTERSECTION &&
        operation != BAMBU_OMC_SUBTRACTION) {
        set_error("Unsupported OpenMeshCraft boolean operation.", error_message, error_message_capacity);
        return false;
    }

    std::vector<Component> components;
    for (size_t g = 0; g < operand_count; ++g)
        for (size_t m = 0; m < operands[g].mesh_count; ++m)
            split_components(operands[g].meshes[m], g, components);

    OMCPoints    result_points;
    OMCTriangles result_triangles;
    if (!components.empty()) {
        OMCPoints    unused_points;
        OMCTriangles unused_triangles;
        OMCBoolean   boolean(/*verbose*/ false);
        for (const Component &c : components)
            boolean.addTriMeshAsInput(c.points, c.triangles);
        boolean.setTriMeshAsOutput(unused_points, unused_triangles);
        boolean.computeLabels();

        OMCPoints               arr_points;
        OMCTriangles            arr_triangles;
        std::vector<OMC::Label> surface, inside, opposite;
        boolean.exportLabeledArrangement(arr_points, arr_triangles, surface, inside, opposite);

        // Keep a triangle when the result differs on its two sides. Front is the side its
        // normal points to. A component the triangle lies on has its interior behind its
        // own copy of the triangle; any other component contains both sides or neither.
        std::vector<int> front(operand_count), back(operand_count);
        for (size_t t = 0; t < arr_triangles.size(); ++t) {
            std::fill(front.begin(), front.end(), 0);
            std::fill(back.begin(), back.end(), 0);
            surface[t].forEach([&](size_t c) {
                (opposite[t][c] ? front : back)[components[c].group] += components[c].sign;
            });
            inside[t].forEach([&](size_t c) {
                if (!surface[t][c]) {
                    front[components[c].group] += components[c].sign;
                    back[components[c].group] += components[c].sign;
                }
            });
            const bool in_front = in_result(front, operation);
            const bool in_back  = in_result(back, operation);
            if (in_front == in_back)
                continue;
            auto tri = arr_triangles[t];
            if (in_front) // the result lies in front: turn the normal outward
                std::swap(tri[1], tri[2]);
            result_triangles.push_back(tri);
        }

        // compact the vertices in use
        std::vector<size_t> new_index(arr_points.size(), size_t(-1));
        for (auto &tri : result_triangles)
            for (int k = 0; k < 3; ++k) {
                size_t &ni = new_index[tri[k]];
                if (ni == size_t(-1)) {
                    ni = result_points.size();
                    result_points.push_back(arr_points[tri[k]]);
                }
                tri[k] = ni;
            }
    }

    // Explicit field copy: equal sizeof is not enough across MSVC/Clang/GCC
    // (layout / padding / field order are not guaranteed).
    std::vector<BambuOMCPoint> out_points;
    out_points.reserve(result_points.size());
    for (const auto &p : result_points)
        out_points.push_back(BambuOMCPoint{p.x(), p.y(), p.z()});

    std::vector<BambuOMCTriangle> out_triangles;
    out_triangles.reserve(result_triangles.size());
    for (const auto &t : result_triangles)
        out_triangles.push_back(BambuOMCTriangle{{t[0], t[1], t[2]}});

    result_callback(out_points.data(), out_points.size(), out_triangles.data(), out_triangles.size(), user_data);
    return true;
}

} // namespace

extern "C" bool BAMBU_OMC_CALL bambu_omc_boolean_operands(
    const BambuOMCOperand    *operands,
    size_t                    operand_count,
    BambuOMCOperation         operation,
    BambuOMCResultCallback    result_callback,
    void                     *user_data,
    char                     *error_message,
    size_t                    error_message_capacity)
{
    try {
        return run_boolean(operands, operand_count, operation, result_callback, user_data, error_message,
                           error_message_capacity);
    } catch (const std::exception &error) {
        set_error(error.what(), error_message, error_message_capacity);
        return false;
    } catch (...) {
        set_error("OpenMeshCraft failed with an unknown exception.", error_message, error_message_capacity);
        return false;
    }
}

extern "C" bool BAMBU_OMC_CALL bambu_omc_boolean(
    const BambuOMCMesh       *first,
    const BambuOMCMesh       *second,
    BambuOMCOperation         operation,
    BambuOMCResultCallback    result_callback,
    void                     *user_data,
    char                     *error_message,
    size_t                    error_message_capacity)
{
    // Validate here so a null mesh is reported before it is wrapped in an operand.
    for (const BambuOMCMesh *mesh : {first, second}) {
        std::string err;
        if (!validate_mesh(mesh, err)) {
            set_error(err, error_message, error_message_capacity);
            return false;
        }
    }
    const BambuOMCOperand operands[2] = {{first, 1}, {second, 1}};
    return bambu_omc_boolean_operands(operands, 2, operation, result_callback, user_data, error_message,
                                      error_message_capacity);
}
