#include "WarpCutSurfaceClassifier.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace WarpCut {

namespace {

float sample_offset(const SurfaceDefinition &surface, double u, double v)
{
    const double clamped_u = std::clamp(u, 0.0, 1.0);
    const double clamped_v = std::clamp(v, 0.0, 1.0);
    const double grid_x = clamped_u * double(surface.cols - 1);
    const double grid_y = clamped_v * double(surface.rows - 1);
    const size_t x0 = size_t(std::floor(grid_x));
    const size_t y0 = size_t(std::floor(grid_y));
    const size_t x1 = std::min(x0 + 1, surface.cols - 1);
    const size_t y1 = std::min(y0 + 1, surface.rows - 1);
    const double tx = grid_x - double(x0);
    const double ty = grid_y - double(y0);

    const auto at = [&surface](size_t row, size_t col) {
        return surface.offsets[row * surface.cols + col];
    };

    const double top = double(at(y0, x0)) * (1.0 - tx) + double(at(y0, x1)) * tx;
    const double bottom = double(at(y1, x0)) * (1.0 - tx) + double(at(y1, x1)) * tx;
    return float(top * (1.0 - ty) + bottom * ty);
}

Vec3d surface_position(const SurfaceDefinition &surface, double u, double v, const Vec3d &instance_offset)
{
    const double u_local = (u - 0.5) * 2.0 * surface.half_extents.x();
    const double v_local = (v - 0.5) * 2.0 * surface.half_extents.y();
    return surface.center - instance_offset
        + surface.axis_u * u_local
        + surface.axis_v * v_local
        + surface.normal * double(sample_offset(surface, u, v));
}

Vec3d surface_normal(const SurfaceDefinition &surface, double u, double v, const Vec3d &instance_offset)
{
    const double du = surface.cols > 1 ? 1.0 / double(surface.cols - 1) : 0.01;
    const double dv = surface.rows > 1 ? 1.0 / double(surface.rows - 1) : 0.01;
    const double u0 = std::clamp(u - du, 0.0, 1.0);
    const double u1 = std::clamp(u + du, 0.0, 1.0);
    const double v0 = std::clamp(v - dv, 0.0, 1.0);
    const double v1 = std::clamp(v + dv, 0.0, 1.0);
    const Vec3d tangent_u = surface_position(surface, u1, v, instance_offset) - surface_position(surface, u0, v, instance_offset);
    const Vec3d tangent_v = surface_position(surface, u, v1, instance_offset) - surface_position(surface, u, v0, instance_offset);
    Vec3d normal = tangent_u.cross(tangent_v);
    if (normal.squaredNorm() <= 1e-12)
        normal = surface.normal;
    const double norm = normal.norm();
    return norm > 1e-12 ? (normal / norm) : surface.normal;
}

} // namespace

bool triangle_on_cut_surface(
    const SurfaceDefinition &surface,
    const Vec3d &instance_offset,
    const Vec3d &centroid,
    const Vec3d &normal)
{
    if (!surface.is_valid())
        return false;

    const Vec3d origin = surface.center - instance_offset;
    const Vec3d relative = centroid - origin;
    const double u_den = 2.0 * surface.half_extents.x();
    const double v_den = 2.0 * surface.half_extents.y();
    if (u_den <= 1e-9 || v_den <= 1e-9)
        return false;

    const double u = 0.5 + relative.dot(surface.axis_u) / u_den;
    const double v = 0.5 + relative.dot(surface.axis_v) / v_den;
    const double uv_tolerance = 0.02;
    if (u < -uv_tolerance || u > 1.0 + uv_tolerance || v < -uv_tolerance || v > 1.0 + uv_tolerance)
        return false;

    const double clamped_u = std::clamp(u, 0.0, 1.0);
    const double clamped_v = std::clamp(v, 0.0, 1.0);
    const Vec3d surface_point = surface_position(surface, clamped_u, clamped_v, instance_offset);
    const Vec3d surface_normal_value = surface_normal(surface, clamped_u, clamped_v, instance_offset);

    const double plane_distance = std::abs((centroid - surface_point).dot(surface_normal_value));
    const double radial_distance = (centroid - surface_point).norm();
    const double normal_alignment = std::abs(normal.dot(surface_normal_value));

    return plane_distance <= 0.08 && radial_distance <= 0.20 && normal_alignment >= 0.80;
}

ColorCut::CutSurfaceClassifier make_surface_classifier(const SurfaceDefinition &surface, const Vec3d &instance_offset)
{
    return [surface, instance_offset](const Vec3d &centroid, const Vec3d &normal) {
        return triangle_on_cut_surface(surface, instance_offset, centroid, normal);
    };
}

} // namespace WarpCut
} // namespace Slic3r
