#ifndef slic3r_ColorCutGeometryBackend_hpp_
#define slic3r_ColorCutGeometryBackend_hpp_

#include "ColorCutProvenance.hpp"
#include "ColorCutTypes.hpp"

#include "libslic3r/TriangleMesh.hpp"

#include <string>
#include <optional>
#include <vector>

namespace Slic3r {

class ModelObject;
class ModelVolume;

namespace ColorCut {

struct GeometryCutInput {
    const ModelObject *      source_object{nullptr};
    const ModelVolume *      source_volume{nullptr};
    int                      object_index{-1};
    int                      instance_index{-1};
    Transform3d              cut_matrix{Transform3d::Identity()};
    ModelObjectCutAttributes attributes{};
    ColorCutProgressCB       progress_cb;
    ColorCutCancelCB         cancel_cb;
};

struct GeometryCutOutputVolume {
    TriangleMesh         mesh;
    ProvenanceSide       side{ProvenanceSide::Unknown};
    VolumeCutProvenance  provenance;
};

struct GeometryCutOutput {
    bool                         success{false};
    ColorCutCapabilities         capabilities;
    std::vector<GeometryCutOutputVolume> volumes;
    ModelObjectPtrs              new_objects;
    std::vector<ColorCutWarning> warnings;
};

class IColorCutGeometryBackend
{
public:
    virtual ~IColorCutGeometryBackend() = default;

    virtual GeometryCutOutput cut(const GeometryCutInput &input) = 0;
    virtual ColorCutCapabilities capabilities() const = 0;
    virtual const char *backend_name() const = 0;
};

} // namespace ColorCut
} // namespace Slic3r

#endif
