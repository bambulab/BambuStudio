#ifndef slic3r_ColorCutProvenance_hpp_
#define slic3r_ColorCutProvenance_hpp_

#include <array>
#include <vector>

namespace Slic3r {
namespace ColorCut {

enum class ProvenanceTriangleKind : int {
    Unknown = 0,
    Inherited,
    Split,
    Cap
};

enum class ProvenanceSide : int {
    Unknown = 0,
    Upper,
    Lower
};

struct SourceTriangleRef {
    int volume_id{-1};
    int triangle_index{-1};
};

struct SplitVertexSource {
    int source_triangle_index{-1};
    std::array<float, 3> barycentric_weights{{0.0f, 0.0f, 0.0f}};
};

struct OutputTriangleProvenance {
    ProvenanceTriangleKind             kind{ProvenanceTriangleKind::Unknown};
    ProvenanceSide                     side{ProvenanceSide::Unknown};
    std::vector<SourceTriangleRef>     source_triangles;
    std::vector<SplitVertexSource>     split_vertices;
};

struct VolumeCutProvenance {
    std::vector<OutputTriangleProvenance> triangles;
    std::vector<SourceTriangleRef>        cap_adjacent_sources;
};

} // namespace ColorCut
} // namespace Slic3r

#endif
