#ifndef slic3r_MeshDiagnostics_hpp_
#define slic3r_MeshDiagnostics_hpp_

#include <admesh/stl.h>

#include <vector>

namespace Slic3r {

struct MeshDiagnosticStats {
    size_t non_manifold_edges    = 0;
    size_t non_manifold_vertices = 0;
    size_t open_edges            = 0;
    size_t same_direction_edges  = 0;
    bool   has_reversed_faces    = false;
};

// Count open edges (undirected face count == 1), non-manifold edges
// (undirected face count > 2), and same-direction half-edges (one
// undirected edge with at least two half-edges in the same winding).
// Does not inspect vertices, does not run ray tests, and leaves
// has_reversed_faces false. If neighbors is non-null, it is resized to
// the face count and filled with opposite-winding partners (-1 if none).
MeshDiagnosticStats its_edge_diagnostics(const indexed_triangle_set &its, std::vector<Vec3i> *neighbors = nullptr);

// Set has_reversed_faces from already-computed edge stats:
//   non-manifold edges or same-direction edges -> true, no rays;
//   open edges          -> leave the flag unchanged (not watertight);
//   otherwise (watertight) -> rays from outside the AABB (TriangleRaycaster
//                             all-hits). A closed mesh must yield an even hit
//                             count; odd counts are discarded. Rays whose two
//                             smallest t values are coincident relative to the
//                             AABB, or whose outermost hit is a sliver, lands
//                             on an edge/vertex, or is grazing, are discarded.
//                             A back-facing outermost hit sets the flag.
void its_detect_reversed_faces(const indexed_triangle_set &its, MeshDiagnosticStats &stats);

// Import-path diagnostics: its_edge_diagnostics followed by
// its_detect_reversed_faces. Skips non-manifold vertex detection;
// non_manifold_vertices in the returned stats is always 0.
// Optional neighbors is forwarded to its_edge_diagnostics.
MeshDiagnosticStats its_quick_diagnostics(const indexed_triangle_set &its, std::vector<Vec3i> *neighbors = nullptr);

// Full diagnostics: open / non-manifold edges, non-manifold vertices, and the
// same reversed-face test as its_detect_reversed_faces.
//
// Reported defects:
//   Open edge:           an undirected edge referenced by exactly 1 face
//                        (a.k.a. boundary / border edge).
//   Non-manifold edge:   an undirected edge shared by more than 2 faces.
//   Non-manifold vertex: a vertex whose incident faces do not form a single
//                        connected fan when traversed through shared edges
//                        (e.g. butterfly / bowtie vertex).
//   Reversed faces:      existence only (no face count). Non-manifold edges
//                        or same-direction half-edges set the flag without
//                        rays. Open meshes skip the ray test. Watertight
//                        meshes use the all-hits ray test described on
//                        its_detect_reversed_faces.
//
// Each defect is counted exactly once regardless of how many anomalies
// overlap on it.
MeshDiagnosticStats its_mesh_diagnostics(const indexed_triangle_set &its);

} // namespace Slic3r

#endif // slic3r_MeshDiagnostics_hpp_
