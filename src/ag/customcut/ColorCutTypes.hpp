#ifndef slic3r_ColorCutTypes_hpp_
#define slic3r_ColorCutTypes_hpp_

#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {
namespace ColorCut {

enum class ColorCutMode : int {
    Auto = 0,
    LegacyPlane,
    Mcut
};

enum class ColorAssignmentPolicy : int {
    DominantAdjacent = 0,
    VolumeFallback
};

enum class ColorCutWarningCode : int {
    None = 0,
    UnsupportedAppearanceData,
    FallbackToLegacyBackend,
    FallbackColorAssignment,
    PartialTexturePreservation,
    Unhandled
};

struct ColorCutWarning {
    ColorCutWarningCode code{ColorCutWarningCode::None};
    std::string         message;
};

struct ColorCutCapabilities {
    bool supports_textured_transfer{false};
    bool supports_arbitrary_mesh_cut{false};
    bool supports_cap_surface_provenance{false};
};

using CutSurfaceClassifier = std::function<bool(const Vec3d &centroid, const Vec3d &normal)>;

struct SurfacePlaneDefinition {
    Vec3d point{Vec3d::Zero()};
    Vec3d normal{Vec3d::UnitZ()};

    bool is_valid() const { return normal.squaredNorm() > 1e-12; }
};

// Progress callback: receives overall fraction [0..1] and a phase description.
using ColorCutProgressCB = std::function<void(float fraction, const char *phase)>;
// Cancel check: returns true if the user requested cancellation.
using ColorCutCancelCB   = std::function<bool()>;

struct ColorCutRequest {
    const ModelObject *         object{nullptr};
    int                         object_index{-1};
    int                         instance_index{-1};
    Transform3d                 cut_matrix{Transform3d::Identity()};
    ModelObjectCutAttributes    attributes{};
    ColorCutMode                mode{ColorCutMode::Auto};
    ColorAssignmentPolicy       assignment_policy{ColorAssignmentPolicy::DominantAdjacent};
    bool                        enable_warnings{true};
    bool                        uniform_cap_color{false};
    ColorCutProgressCB          progress_cb;
    ColorCutCancelCB            cancel_cb;
};

struct ColorCutVolumeResult {
    ModelVolume *volume{nullptr};
};

struct ColorCutObjectResult {
    ModelObject *                    object{nullptr};
    std::vector<ColorCutVolumeResult> volumes;
};

struct ColorCutResult {
    bool                        handled{false};
    ModelObjectPtrs             new_objects;
    std::vector<ColorCutWarning> warnings;
};

} // namespace ColorCut
} // namespace Slic3r

#endif
