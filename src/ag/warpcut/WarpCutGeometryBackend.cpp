#include "WarpCutGeometryBackend.hpp"

#include "ag/customcut/ColorCutLegacyPlaneBackend.hpp"

#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <future>
#include <iomanip>
#include <mutex>
#include <numeric>
#include <sstream>

namespace Slic3r {
namespace WarpCut {

using ColorCut::ColorCutCapabilities;
using ColorCut::ColorCutWarningCode;
using ColorCut::GeometryCutOutput;
using ColorCut::GeometryCutOutputVolume;
using ColorCut::ProvenanceSide;

namespace {

static void append_curve_log(const std::string &message)
{
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    std::ofstream out("curve.log", std::ios::app);
    if (!out)
        return;
    out << message << std::endl;
}

static std::string format_vec3(const Vec3d &value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(4)
           << "(" << value.x() << ", " << value.y() << ", " << value.z() << ")";
    return stream.str();
}

static std::string format_bbox(const BoundingBoxf3 &bbox)
{
    if (!bbox.defined)
        return "<undefined>";

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(4)
           << "min(" << bbox.min.x() << ", " << bbox.min.y() << ", " << bbox.min.z() << ")"
           << " max(" << bbox.max.x() << ", " << bbox.max.y() << ", " << bbox.max.z() << ")";
    return stream.str();
}

static void log_object_state(const char *stage, const ModelObject *object)
{
    std::ostringstream stream;
    stream << "[WarpCut] " << stage;
    if (object == nullptr) {
        stream << " object=<null>";
        append_curve_log(stream.str());
        return;
    }

    stream << " object='" << object->name << "'"
           << " volumes=" << object->volumes.size()
           << " instances=" << object->instances.size()
           << " origin_translation=" << format_vec3(object->origin_translation)
           << " raw_bbox=" << format_bbox(object->raw_mesh_bounding_box())
           << " full_raw_bbox=" << format_bbox(object->full_raw_mesh_bounding_box());
    append_curve_log(stream.str());

    for (size_t instance_index = 0; instance_index < object->instances.size(); ++instance_index) {
        const ModelInstance *instance = object->instances[instance_index];
        std::ostringstream instance_stream;
        instance_stream << "[WarpCut]   instance[" << instance_index << "]"
                        << " offset=" << format_vec3(instance->get_offset())
                        << " rotation=" << format_vec3(instance->get_rotation())
                        << " scaling=" << format_vec3(instance->get_scaling_factor())
                        << " mirror=" << format_vec3(instance->get_mirror())
                        << " assemble_offset=" << format_vec3(instance->get_assemble_transformation().get_offset());
        append_curve_log(instance_stream.str());
    }

    for (size_t volume_index = 0; volume_index < object->volumes.size(); ++volume_index) {
        const ModelVolume *volume = object->volumes[volume_index];
        std::ostringstream volume_stream;
        volume_stream << "[WarpCut]   volume[" << volume_index << "]"
                      << " name='" << volume->name << "'"
                      << " type=" << int(volume->type())
                      << " from_upper=" << volume->is_from_upper()
                      << " offset=" << format_vec3(volume->get_offset())
                      << " rotation=" << format_vec3(volume->get_rotation())
                      << " mesh_bbox=" << format_bbox(volume->mesh().bounding_box());
        append_curve_log(volume_stream.str());
    }
}

static float sample_offset(const SurfaceDefinition &surface, double u, double v)
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

static Vec3d surface_position(const SurfaceDefinition &surface, double u, double v, const Vec3d &instance_offset)
{
    const double u_local = (u - 0.5) * 2.0 * surface.half_extents.x();
    const double v_local = (v - 0.5) * 2.0 * surface.half_extents.y();
    return surface.center - instance_offset
        + surface.axis_u * u_local
        + surface.axis_v * v_local
        + surface.normal * double(sample_offset(surface, u, v));
}

static void add_oriented_triangle(indexed_triangle_set &mesh, int a, int b, int c, const Vec3d &volume_center)
{
    const Vec3d pa = mesh.vertices[size_t(a)].cast<double>();
    const Vec3d pb = mesh.vertices[size_t(b)].cast<double>();
    const Vec3d pc = mesh.vertices[size_t(c)].cast<double>();
    Vec3d normal = (pb - pa).cross(pc - pa);
    const Vec3d face_center = (pa + pb + pc) / 3.0;
    if (normal.dot(face_center - volume_center) < 0.0)
        std::swap(b, c);
    mesh.indices.emplace_back(a, b, c);
}

static indexed_triangle_set build_extruded_volume(const SurfaceDefinition &surface, const Vec3d &instance_offset, double extrude_sign, double extrude_distance, size_t resolution)
{
    indexed_triangle_set mesh;
    const size_t sample_resolution = std::max<size_t>(resolution, 8);
    mesh.vertices.reserve(sample_resolution * sample_resolution * 2);
    mesh.indices.reserve((sample_resolution - 1) * (sample_resolution - 1) * 4 + (sample_resolution - 1) * 8);

    const Vec3d extrusion = surface.normal.normalized() * (extrude_sign * extrude_distance);
    const Vec3d volume_center = surface.center - instance_offset + extrusion * 0.5;

    auto grid_index = [sample_resolution](size_t row, size_t col, bool cap) {
        return int((cap ? sample_resolution * sample_resolution : 0) + row * sample_resolution + col);
    };

    for (size_t row = 0; row < sample_resolution; ++row) {
        for (size_t col = 0; col < sample_resolution; ++col) {
            const double u = sample_resolution > 1 ? double(col) / double(sample_resolution - 1) : 0.5;
            const double v = sample_resolution > 1 ? double(row) / double(sample_resolution - 1) : 0.5;
            const Vec3f base = surface_position(surface, u, v, instance_offset).cast<float>();
            mesh.vertices.emplace_back(base);
        }
    }
    for (size_t row = 0; row < sample_resolution; ++row) {
        for (size_t col = 0; col < sample_resolution; ++col) {
            const double u = sample_resolution > 1 ? double(col) / double(sample_resolution - 1) : 0.5;
            const double v = sample_resolution > 1 ? double(row) / double(sample_resolution - 1) : 0.5;
            const Vec3f cap = (surface_position(surface, u, v, instance_offset) + extrusion).cast<float>();
            mesh.vertices.emplace_back(cap);
        }
    }

    for (size_t row = 0; row + 1 < sample_resolution; ++row) {
        for (size_t col = 0; col + 1 < sample_resolution; ++col) {
            const int b00 = grid_index(row, col, false);
            const int b01 = grid_index(row, col + 1, false);
            const int b10 = grid_index(row + 1, col, false);
            const int b11 = grid_index(row + 1, col + 1, false);
            const int c00 = grid_index(row, col, true);
            const int c01 = grid_index(row, col + 1, true);
            const int c10 = grid_index(row + 1, col, true);
            const int c11 = grid_index(row + 1, col + 1, true);

            add_oriented_triangle(mesh, b00, b10, b01, volume_center);
            add_oriented_triangle(mesh, b01, b10, b11, volume_center);
            add_oriented_triangle(mesh, c00, c01, c10, volume_center);
            add_oriented_triangle(mesh, c01, c11, c10, volume_center);
        }
    }

    for (size_t col = 0; col + 1 < sample_resolution; ++col) {
        const int b0 = grid_index(0, col, false);
        const int b1 = grid_index(0, col + 1, false);
        const int c0 = grid_index(0, col, true);
        const int c1 = grid_index(0, col + 1, true);
        add_oriented_triangle(mesh, b0, b1, c1, volume_center);
        add_oriented_triangle(mesh, b0, c1, c0, volume_center);
    }
    for (size_t row = 0; row + 1 < sample_resolution; ++row) {
        const int b0 = grid_index(row, sample_resolution - 1, false);
        const int b1 = grid_index(row + 1, sample_resolution - 1, false);
        const int c0 = grid_index(row, sample_resolution - 1, true);
        const int c1 = grid_index(row + 1, sample_resolution - 1, true);
        add_oriented_triangle(mesh, b0, b1, c1, volume_center);
        add_oriented_triangle(mesh, b0, c1, c0, volume_center);
    }
    for (size_t col = 0; col + 1 < sample_resolution; ++col) {
        const size_t rev_col = sample_resolution - 1 - col;
        const int b0 = grid_index(sample_resolution - 1, rev_col, false);
        const int b1 = grid_index(sample_resolution - 1, rev_col - 1, false);
        const int c0 = grid_index(sample_resolution - 1, rev_col, true);
        const int c1 = grid_index(sample_resolution - 1, rev_col - 1, true);
        add_oriented_triangle(mesh, b0, b1, c1, volume_center);
        add_oriented_triangle(mesh, b0, c1, c0, volume_center);
    }
    for (size_t row = 0; row + 1 < sample_resolution; ++row) {
        const size_t rev_row = sample_resolution - 1 - row;
        const int b0 = grid_index(rev_row, 0, false);
        const int b1 = grid_index(rev_row - 1, 0, false);
        const int c0 = grid_index(rev_row, 0, true);
        const int c1 = grid_index(rev_row - 1, 0, true);
        add_oriented_triangle(mesh, b0, b1, c1, volume_center);
        add_oriented_triangle(mesh, b0, c1, c0, volume_center);
    }

    return mesh;
}

static TriangleMesh merge_meshes(std::vector<TriangleMesh> meshes)
{
    TriangleMesh merged;
    for (TriangleMesh &mesh : meshes) {
        if (!mesh.empty())
            merged.merge(mesh);
    }
    return merged;
}

static std::vector<ModelVolume *> collect_output_model_parts(ModelObject *object)
{
    std::vector<ModelVolume *> volumes;
    if (object == nullptr)
        return volumes;
    for (ModelVolume *volume : object->volumes)
        if (volume->is_model_part() && !volume->is_cut_connector())
            volumes.emplace_back(volume);
    return volumes;
}

} // namespace

static Transform3d make_proxy_cut_matrix(const SurfaceDefinition &surface)
{
    const double average_offset = surface.offsets.empty()
        ? 0.0
        : std::accumulate(surface.offsets.begin(), surface.offsets.end(), 0.0) / double(surface.offsets.size());

    Transform3d cut_matrix = Transform3d::Identity();
    cut_matrix.matrix().block<3, 1>(0, 0) = surface.axis_u.normalized() * surface.half_extents.x();
    cut_matrix.matrix().block<3, 1>(0, 1) = surface.axis_v.normalized() * surface.half_extents.y();
    cut_matrix.matrix().block<3, 1>(0, 2) = surface.normal.normalized();
    cut_matrix.translation() = surface.center + surface.normal.normalized() * average_offset;
    return cut_matrix;
}

GeometryCutOutput WarpCutGeometryBackend::cut(const GeometryInput &input) const
{
    GeometryCutOutput output;
    output.capabilities = capabilities();
    auto attributes = input.attributes;

    append_curve_log("[WarpCut] cut begin");

    if (!input.surface.is_valid()) {
        output.warnings.push_back({ColorCutWarningCode::Unhandled, "WarpCut backend requires a valid warped surface definition."});
        return output;
    }

    if (input.source_object == nullptr || input.source_volume == nullptr || input.instance_index < 0 || size_t(input.instance_index) >= input.source_object->instances.size()) {
        output.warnings.push_back({ColorCutWarningCode::Unhandled, "WarpCut backend requires a single valid source volume and instance."});
        return output;
    }

    const SurfaceDefinition &surface = input.surface;
    const ModelInstance *instance = input.source_object->instances[size_t(input.instance_index)];
    const auto volume_matrix = input.source_volume->get_matrix();
    const auto instance_matrix = Geometry::assemble_transform(
        Vec3d::Zero(),
        instance->get_rotation(),
        instance->get_scaling_factor(),
        instance->get_mirror());

    TriangleMesh source_mesh(input.source_volume->mesh());
    source_mesh.transform(instance_matrix * volume_matrix, true);

    {
        std::ostringstream stream;
        stream << "[WarpCut] source_mesh bbox=" << format_bbox(source_mesh.bounding_box())
               << " instance_offset=" << format_vec3(instance->get_offset())
               << " source_volume_offset=" << format_vec3(input.source_volume->get_offset())
               << " surface_center=" << format_vec3(surface.center)
               << " surface_normal=" << format_vec3(surface.normal);
        append_curve_log(stream.str());
    }

    const BoundingBoxf3 bbox = source_mesh.bounding_box();
    const double extrude_distance = std::max(20.0, bbox.radius() * 3.0);
    const size_t dense_resolution = std::max<size_t>(std::max(surface.rows, surface.cols) * 8, size_t(48));

    if (input.progress_cb)
        input.progress_cb(0.05f, "Building cutter volumes");
    if (input.cancel_cb && input.cancel_cb())
        return output;

    TriangleMesh upper_cutter(build_extruded_volume(surface, instance->get_offset(), 1.0, extrude_distance, dense_resolution));
    TriangleMesh lower_cutter(build_extruded_volume(surface, instance->get_offset(), -1.0, extrude_distance, dense_resolution));

    if (input.progress_cb)
        input.progress_cb(0.10f, "Computing boolean intersections");
    if (input.cancel_cb && input.cancel_cb())
        return output;

    const bool do_upper = attributes.has(ModelObjectCutAttribute::KeepUpper);
    const bool do_lower = attributes.has(ModelObjectCutAttribute::KeepLower);
    std::vector<TriangleMesh> upper_results;
    std::vector<TriangleMesh> lower_results;

    auto mcut_cancel = [&input]() -> bool {
        return input.cancel_cb && input.cancel_cb();
    };

    std::atomic<float> upper_pct{0.0f};
    std::atomic<float> lower_pct{0.0f};

    auto report_combined = [&input, &upper_pct, &lower_pct, &do_upper, &do_lower](const char *label) {
        if (!input.progress_cb)
            return;

        float slower = 0.0f;
        if (do_upper && do_lower)
            slower = std::min(upper_pct.load(std::memory_order_relaxed), lower_pct.load(std::memory_order_relaxed));
        else if (do_upper)
            slower = upper_pct.load(std::memory_order_relaxed);
        else
            slower = lower_pct.load(std::memory_order_relaxed);
        const float frac = 0.10f + (slower / 100.0f) * 0.45f;
        input.progress_cb(frac, label);
    };

    auto upper_progress = [&upper_pct, &report_combined](float p) {
        upper_pct.store(p, std::memory_order_relaxed);
        report_combined("Computing mesh intersections");
    };
    auto lower_progress = [&lower_pct, &report_combined](float p) {
        lower_pct.store(p, std::memory_order_relaxed);
        report_combined("Computing mesh intersections");
    };

    if (do_upper && do_lower) {
        auto upper_future = std::async(std::launch::async, [&]() {
            MeshBoolean::mcut::make_boolean(source_mesh, upper_cutter, upper_results, "INTERSECTION", mcut_cancel, upper_progress);
        });
        MeshBoolean::mcut::make_boolean(source_mesh, lower_cutter, lower_results, "INTERSECTION", mcut_cancel, lower_progress);
        upper_future.get();
    } else {
        if (do_upper)
            MeshBoolean::mcut::make_boolean(source_mesh, upper_cutter, upper_results, "INTERSECTION", mcut_cancel, upper_progress);
        if (do_lower)
            MeshBoolean::mcut::make_boolean(source_mesh, lower_cutter, lower_results, "INTERSECTION", mcut_cancel, lower_progress);
    }

    if (input.cancel_cb && input.cancel_cb())
        return output;
    if (input.progress_cb)
        input.progress_cb(0.50f, "Merging mesh fragments");

    TriangleMesh upper_mesh = merge_meshes(std::move(upper_results));
    TriangleMesh lower_mesh = merge_meshes(std::move(lower_results));

    if (attributes.has(ModelObjectCutAttribute::KeepUpper) && upper_mesh.empty())
        output.warnings.push_back({ColorCutWarningCode::Unhandled, "WarpCut could not produce an upper mesh from the warped surface."});
    if (attributes.has(ModelObjectCutAttribute::KeepLower) && lower_mesh.empty())
        output.warnings.push_back({ColorCutWarningCode::Unhandled, "WarpCut could not produce a lower mesh from the warped surface."});

    if (!upper_mesh.empty()) {
        GeometryCutOutputVolume volume;
        volume.mesh = upper_mesh;
        volume.side = ProvenanceSide::Upper;
        output.volumes.emplace_back(std::move(volume));
    }
    if (!lower_mesh.empty()) {
        GeometryCutOutputVolume volume;
        volume.mesh = lower_mesh;
        volume.side = ProvenanceSide::Lower;
        output.volumes.emplace_back(std::move(volume));
    }

    if (input.progress_cb)
        input.progress_cb(0.55f, "Building proxy geometry");
    if (input.cancel_cb && input.cancel_cb())
        return output;

    ColorCut::GeometryCutInput proxy_input;
    proxy_input.source_object = input.source_object;
    proxy_input.source_volume = input.source_volume;
    proxy_input.object_index = input.object_index;
    proxy_input.instance_index = input.instance_index;
    proxy_input.attributes = input.attributes;
    proxy_input.cut_matrix = make_proxy_cut_matrix(surface);
    proxy_input.progress_cb = nullptr;
    proxy_input.cancel_cb = input.cancel_cb;
    ColorCut::ColorCutLegacyPlaneBackend legacy_backend;
    GeometryCutOutput proxy_output = legacy_backend.cut(proxy_input);

    if (!proxy_output.new_objects.empty()) {
        for (ModelObject *object : proxy_output.new_objects) {
            log_object_state("proxy_output_before_swap", object);
            const std::vector<ModelVolume *> volumes = collect_output_model_parts(object);
            if (volumes.empty())
                continue;

            const bool is_upper = volumes.front()->is_from_upper();
            const TriangleMesh &replacement = is_upper ? upper_mesh : lower_mesh;
            if (replacement.empty())
                continue;

            for (ModelVolume *volume : volumes) {
                volume->set_mesh(replacement);
                volume->calculate_convex_hull();
            }
            log_object_state("proxy_output_after_swap", object);
            object->invalidate_bounding_box();
        }
    }

    output.new_objects = std::move(proxy_output.new_objects);
    output.success = !output.new_objects.empty() && (!upper_mesh.empty() || !lower_mesh.empty());
    if (input.progress_cb)
        input.progress_cb(0.65f, "Boolean geometry complete");
    {
        std::ostringstream stream;
        stream << "[WarpCut] cut end success=" << output.success
               << " new_objects=" << output.new_objects.size()
               << " upper_empty=" << upper_mesh.empty()
               << " lower_empty=" << lower_mesh.empty();
        append_curve_log(stream.str());
    }
    return output;
}

ColorCutCapabilities WarpCutGeometryBackend::capabilities() const
{
    ColorCutCapabilities capabilities;
    capabilities.supports_textured_transfer = false;
    capabilities.supports_arbitrary_mesh_cut = true;
    capabilities.supports_cap_surface_provenance = false;
    return capabilities;
}

const char *WarpCutGeometryBackend::backend_name() const
{
    return "warp-surface";
}

} // namespace WarpCut
} // namespace Slic3r
