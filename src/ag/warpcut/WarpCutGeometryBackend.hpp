#ifndef slic3r_WarpCutGeometryBackend_hpp_
#define slic3r_WarpCutGeometryBackend_hpp_

#include "WarpCutRequest.hpp"

#include "ag/customcut/ColorCutGeometryBackend.hpp"

namespace Slic3r {
namespace WarpCut {

struct GeometryInput {
    const ModelObject *      source_object{nullptr};
    const ModelVolume *      source_volume{nullptr};
    int                      object_index{-1};
    int                      instance_index{-1};
    Transform3d              cut_matrix{Transform3d::Identity()};
    ModelObjectCutAttributes attributes{};
    SurfaceDefinition        surface;
    ProgressCB               progress_cb;
    CancelCB                 cancel_cb;
};

class WarpCutGeometryBackend
{
public:
    ColorCut::GeometryCutOutput cut(const GeometryInput &input) const;
    ColorCut::ColorCutCapabilities capabilities() const;
    const char *backend_name() const;
};

} // namespace WarpCut
} // namespace Slic3r

#endif // slic3r_WarpCutGeometryBackend_hpp_
