#ifndef slic3r_BallEraserBoolean_hpp_
#define slic3r_BallEraserBoolean_hpp_

#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <string>
#include <vector>

namespace Slic3r {
namespace BallEraser {

struct FaceOriginTriangleMesh
{
    TriangleMesh         mesh;
    std::vector<uint8_t> cut_face_mask;
};

bool make_boolean_with_face_origin(
    const TriangleMesh &src_mesh,
    const TriangleMesh &cut_mesh,
    std::vector<FaceOriginTriangleMesh> &dst_mesh,
    const std::string &boolean_opts,
    const MeshBoolean::mcut::BooleanCancelCB &cancel_cb = nullptr,
    const MeshBoolean::mcut::BooleanProgressCB &progress_cb = nullptr);

} // namespace BallEraser
} // namespace Slic3r

#endif // slic3r_BallEraserBoolean_hpp_
