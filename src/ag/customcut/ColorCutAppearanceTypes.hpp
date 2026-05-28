#ifndef slic3r_ColorCutAppearanceTypes_hpp_
#define slic3r_ColorCutAppearanceTypes_hpp_

#include <array>
#include <cassert>
#include <optional>
#include <string>
#include <vector>

#include "libslic3r/Color.hpp"

namespace Slic3r {
namespace ColorCut {

enum class TriangleAppearanceKind : int {
    None = 0,
    MaterialOnly,
    MMUSegmentation,
    TriangleColor,
    TextureMapped
};

struct TriangleAppearanceRecord {
    TriangleAppearanceKind    kind{TriangleAppearanceKind::None};
    int                       source_triangle_index{-1};
    std::string               payload;
    int                       triangle_color_pid{-1};
    std::array<int, 3>        color_indices{{-1, -1, -1}};
};

struct TextureBindingSnapshot {
    std::string                                    texture_name;
    std::array<std::array<float, 2>, 3>            uvs{{{{0.0f, 0.0f}}, {{0.0f, 0.0f}}, {{0.0f, 0.0f}}}};
    bool                                           valid{false};
};

struct CutSurfaceAppearancePolicy {
    bool use_dominant_adjacent{true};
    bool warn_on_fallback{true};
};

struct VolumeAppearanceSnapshot {
    int                                  volume_id{-1};
    std::string                          material_id;
    int                                  default_extruder{-1};
    std::vector<TriangleAppearanceRecord> triangle_records;
    std::optional<TextureBindingSnapshot> texture_binding;
};

struct ObjectAppearanceSnapshot {
    int                                   object_id{-1};
    std::vector<VolumeAppearanceSnapshot>  volumes;
    CutSurfaceAppearancePolicy             cut_surface_policy;
};

} // namespace ColorCut
} // namespace Slic3r

#endif
