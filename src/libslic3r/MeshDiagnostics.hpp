#ifndef slic3r_MeshDiagnostics_hpp_
#define slic3r_MeshDiagnostics_hpp_

#include <admesh/stl.h>

namespace Slic3r {

struct MeshDiagnosticStats {
    size_t non_manifold_edges    = 0;
    size_t non_manifold_vertices = 0;
    size_t open_edges            = 0;
    bool   has_reversed_faces    = false;
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
//   Reversed faces:     existence only (no face count). Detected in three layers:
//                       1) same-direction half-edges on one undirected edge;
//                       2) if layer 1 misses, an orientation test per
//                          face-connected component (shell / patch, same unit as
//                          its_number_of_patches). Closed components use signed
//                          volume; open components use an area-weighted centroid
//                          test. Values whose magnitude is below a relative
//                          epsilon are treated as inconclusive (near-planar
//                          sheets) and do not set the flag. Which orientation is
//                          correct depends on nesting depth: even depth faces
//                          outward, odd depth bounds a cavity and faces inward.
//                          Shells whose bounds cannot contain one another are all
//                          at depth 0, so any inward shell sets the flag; once
//                          containment is possible, depth parity comes from ray
//                          crossings against the other closed shells and only a
//                          shell disagreeing with its depth sets the flag. A
//                          depth that no ray could resolve unambiguously is left
//                          to layer 3;
//                       3) if layers 1-2 miss and the mesh is closed with no
//                          non-manifold edges, rays from outside the AABB using
//                          all hits. A closed mesh must yield an even hit count;
//                          an odd count is discarded (parity leak). Rays whose
//                          two smallest t values fall within a threshold
//                          relative to the AABB are discarded as coincident, as
//                          are rays whose outermost hit is a sliver, lands on an
//                          edge/vertex, or is grazing. A back-facing outermost
//                          hit is a visible reversed face (self-intersection /
//                          fold that topology misses). Layer 3 walks every ray
//                          for logging; one such backface still sets the flag.
//
// Each defect is counted exactly once regardless of how many anomalies
// overlap on it.
MeshDiagnosticStats its_mesh_diagnostics(const indexed_triangle_set &its);

// Lightweight edge-only diagnostics: counts non-manifold edges (face count > 2)
// and open/boundary edges (face count == 1), and runs the same reversed-face
// existence test as its_mesh_diagnostics. Skips non-manifold vertex detection.
// non_manifold_vertices in the returned stats is always 0.
MeshDiagnosticStats its_edge_diagnostics(const indexed_triangle_set &its);

} // namespace Slic3r

#endif // slic3r_MeshDiagnostics_hpp_
