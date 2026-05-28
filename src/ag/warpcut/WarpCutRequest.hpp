#ifndef slic3r_WarpCutRequest_hpp_
#define slic3r_WarpCutRequest_hpp_

#include "WarpCutTypes.hpp"

#include "libslic3r/Model.hpp"

#include <functional>

namespace Slic3r {
namespace WarpCut {

using ProgressCB = std::function<void(float fraction, const char *phase)>;
using CancelCB   = std::function<bool()>;

struct Request {
    const ModelObject *      object{nullptr};
    int                      object_index{-1};
    int                      instance_index{-1};
    Transform3d              cut_matrix{Transform3d::Identity()};
    ModelObjectCutAttributes attributes{};
    bool                     enable_warnings{true};
    bool                     uniform_cap_color{false};
    SurfaceDefinition        surface;
    ProgressCB               progress_cb;
    CancelCB                 cancel_cb;
};

} // namespace WarpCut
} // namespace Slic3r

#endif // slic3r_WarpCutRequest_hpp_
