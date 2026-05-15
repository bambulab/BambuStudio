#ifndef slic3r_MeshDiagnostics_hpp_
#define slic3r_MeshDiagnostics_hpp_

#include <admesh/stl.h>

namespace Slic3r {

struct MeshDiagnosticStats {
    size_t non_manifold_edges    = 0;
    size_t non_manifold_vertices = 0;
    size_t open_edges            = 0;
};

// Detect topological defects on an indexed triangle set.
//
// Reported defects:
//   Open edge:          an undirected edge referenced by exactly 1 face
//                       (a.k.a. boundary / border edge).
//   Non-manifold edge:  an undirected edge shared by more than 2 faces.
//   Non-manifold vertex: a vertex whose incident faces do not form a single
//                        connected fan when traversed through shared edges
//                        (e.g. butterfly / bowtie vertex).
//
// Each defect is counted exactly once regardless of how many anomalies
// overlap on it.
MeshDiagnosticStats its_mesh_diagnostics(const indexed_triangle_set &its);

// Lightweight edge-only diagnostics: counts non-manifold edges (face count > 2)
// and open/boundary edges (face count == 1). Skips non-manifold vertex detection.
// non_manifold_vertices in the returned stats is always 0.
MeshDiagnosticStats its_edge_diagnostics(const indexed_triangle_set &its);

} // namespace Slic3r

#endif // slic3r_MeshDiagnostics_hpp_
