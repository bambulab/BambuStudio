#ifndef slic3r_WarpCutTypes_hpp_
#define slic3r_WarpCutTypes_hpp_

#include "libslic3r/Geometry.hpp"

#include <cstddef>
#include <vector>

namespace Slic3r {
namespace WarpCut {

struct SurfaceDefinition {
    size_t             rows{0};
    size_t             cols{0};
    Vec3d              center{Vec3d::Zero()};
    Vec3d              axis_u{Vec3d::UnitX()};
    Vec3d              axis_v{Vec3d::UnitY()};
    Vec3d              normal{Vec3d::UnitZ()};
    Vec2d              half_extents{1.0, 1.0};
    std::vector<float> offsets;

    bool is_valid() const { return rows >= 2 && cols >= 2 && offsets.size() == rows * cols; }
};

struct Frame {
    Vec3d center{ Vec3d::Zero() };
    Vec3d axis_u{ Vec3d::UnitX() };
    Vec3d axis_v{ Vec3d::UnitY() };
    Vec3d normal{ Vec3d::UnitZ() };
    Vec2d half_extents{ 50.0, 50.0 };
};

struct ControlGrid {
    size_t rows{ 10 };
    size_t cols{ 10 };
    std::vector<float> offsets;

    size_t size() const { return rows * cols; }
};

} // namespace WarpCut
} // namespace Slic3r

#endif // slic3r_WarpCutTypes_hpp_
