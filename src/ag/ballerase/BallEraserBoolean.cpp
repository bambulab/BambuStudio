#include "BallEraserBoolean.hpp"

#include "mcut/mcut.h"

#include <algorithm>
#include <map>

namespace Slic3r {
namespace BallEraser {

namespace {

struct McutDispatchMesh
{
    std::vector<uint32_t> face_sizes;
    std::vector<uint32_t> face_indices;
    std::vector<double>   vertex_coords;
};

McutDispatchMesh triangle_mesh_to_mcut_dispatch_mesh(const TriangleMesh &mesh)
{
    McutDispatchMesh result;

    result.vertex_coords.reserve(mesh.its.vertices.size() * 3);
    for (const Vec3f &vertex : mesh.its.vertices) {
        result.vertex_coords.push_back(static_cast<double>(vertex.x()));
        result.vertex_coords.push_back(static_cast<double>(vertex.y()));
        result.vertex_coords.push_back(static_cast<double>(vertex.z()));
    }

    result.face_indices.reserve(mesh.its.indices.size() * 3);
    result.face_sizes.reserve(mesh.its.indices.size());
    for (const Vec3i &face : mesh.its.indices) {
        result.face_indices.push_back(static_cast<uint32_t>(face[0]));
        result.face_indices.push_back(static_cast<uint32_t>(face[1]));
        result.face_indices.push_back(static_cast<uint32_t>(face[2]));
        result.face_sizes.push_back(3);
    }

    return result;
}

FaceOriginTriangleMesh build_face_origin_triangle_mesh(
    const std::vector<double> &vertices,
    const std::vector<uint32_t> &face_indices,
    const std::vector<uint32_t> &face_origin,
    size_t source_face_count)
{
    FaceOriginTriangleMesh result;

    const uint32_t vertex_count = static_cast<uint32_t>(vertices.size() / 3);
    const uint32_t face_count = static_cast<uint32_t>(face_indices.size() / 3);

    std::vector<Vec3f> mesh_vertices(vertex_count);
    for (uint32_t index = 0; index < vertex_count; ++index) {
        const uint64_t offset = static_cast<uint64_t>(index) * 3;
        mesh_vertices[index][0] = static_cast<float>(vertices[offset + 0]);
        mesh_vertices[index][1] = static_cast<float>(vertices[offset + 1]);
        mesh_vertices[index][2] = static_cast<float>(vertices[offset + 2]);
    }

    std::vector<Vec3i> mesh_faces(face_count);
    result.cut_face_mask.assign(face_count, 0);
    for (uint32_t face_index = 0; face_index < face_count; ++face_index) {
        const uint64_t offset = static_cast<uint64_t>(face_index) * 3;
        mesh_faces[face_index][0] = static_cast<int>(face_indices[offset + 0]);
        mesh_faces[face_index][1] = static_cast<int>(face_indices[offset + 1]);
        mesh_faces[face_index][2] = static_cast<int>(face_indices[offset + 2]);
        if (face_index < face_origin.size() && face_origin[face_index] >= source_face_count)
            result.cut_face_mask[face_index] = 1;
    }

    result.mesh = TriangleMesh(mesh_vertices, mesh_faces);
    return result;
}

bool get_connected_component_data(
    McContext context,
    McConnectedComponent component,
    McConnectedComponentData query,
    std::vector<double> &buffer)
{
    McSize byte_count = 0;
    McResult err = mcGetConnectedComponentData(context, component, query, 0, nullptr, &byte_count);
    if (err != MC_NO_ERROR)
        return false;

    buffer.assign(static_cast<size_t>(byte_count / sizeof(double)), 0.0);
    if (buffer.empty())
        return true;

    err = mcGetConnectedComponentData(context, component, query, byte_count, buffer.data(), nullptr);
    return err == MC_NO_ERROR;
}

bool get_connected_component_data(
    McContext context,
    McConnectedComponent component,
    McConnectedComponentData query,
    std::vector<uint32_t> &buffer)
{
    McSize byte_count = 0;
    McResult err = mcGetConnectedComponentData(context, component, query, 0, nullptr, &byte_count);
    if (err != MC_NO_ERROR)
        return false;

    buffer.assign(static_cast<size_t>(byte_count / sizeof(uint32_t)), 0);
    if (buffer.empty())
        return true;

    err = mcGetConnectedComponentData(context, component, query, byte_count, buffer.data(), nullptr);
    return err == MC_NO_ERROR;
}

template <typename T>
bool get_connected_component_scalar(
    McContext context,
    McConnectedComponent component,
    McConnectedComponentData query,
    T &value)
{
    return mcGetConnectedComponentData(context, component, query, sizeof(T), &value, nullptr) == MC_NO_ERROR;
}

} // namespace

bool make_boolean_with_face_origin(
    const TriangleMesh &src_mesh,
    const TriangleMesh &cut_mesh,
    std::vector<FaceOriginTriangleMesh> &dst_mesh,
    const std::string &boolean_opts,
    const MeshBoolean::mcut::BooleanCancelCB &cancel_cb,
    const MeshBoolean::mcut::BooleanProgressCB &progress_cb)
{
    dst_mesh.clear();

    const std::map<std::string, McFlags> boolean_opts_map = {
        {"A_NOT_B", MC_DISPATCH_FILTER_FRAGMENT_SEALING_INSIDE | MC_DISPATCH_FILTER_FRAGMENT_LOCATION_ABOVE},
        {"B_NOT_A", MC_DISPATCH_FILTER_FRAGMENT_SEALING_OUTSIDE | MC_DISPATCH_FILTER_FRAGMENT_LOCATION_BELOW},
        {"UNION", MC_DISPATCH_FILTER_FRAGMENT_SEALING_OUTSIDE | MC_DISPATCH_FILTER_FRAGMENT_LOCATION_ABOVE},
        {"INTERSECTION", MC_DISPATCH_FILTER_FRAGMENT_SEALING_INSIDE | MC_DISPATCH_FILTER_FRAGMENT_LOCATION_BELOW},
    };

    const auto boolean_it = boolean_opts_map.find(boolean_opts);
    if (boolean_it == boolean_opts_map.end())
        return false;

    const McutDispatchMesh src_mcut = triangle_mesh_to_mcut_dispatch_mesh(src_mesh);
    const McutDispatchMesh cut_mcut = triangle_mesh_to_mcut_dispatch_mesh(cut_mesh);

    McContext context = MC_NULL_HANDLE;
    if (mcCreateContext(&context, 0) != MC_NO_ERROR)
        return false;

    auto release_context = [&context]() {
        if (context != MC_NULL_HANDLE) {
            mcReleaseConnectedComponents(context, 0, nullptr);
            mcReleaseContext(context);
            context = MC_NULL_HANDLE;
        }
    };

    const McResult dispatch_err = mcDispatch(
        context,
        MC_DISPATCH_VERTEX_ARRAY_DOUBLE |
            MC_DISPATCH_ENFORCE_GENERAL_POSITION |
            MC_DISPATCH_INCLUDE_FACE_MAP |
            boolean_it->second,
        reinterpret_cast<const void *>(src_mcut.vertex_coords.data()),
        reinterpret_cast<const uint32_t *>(src_mcut.face_indices.data()),
        src_mcut.face_sizes.data(),
        static_cast<uint32_t>(src_mcut.vertex_coords.size() / 3),
        static_cast<uint32_t>(src_mcut.face_sizes.size()),
        reinterpret_cast<const void *>(cut_mcut.vertex_coords.data()),
        cut_mcut.face_indices.data(),
        cut_mcut.face_sizes.data(),
        static_cast<uint32_t>(cut_mcut.vertex_coords.size() / 3),
        static_cast<uint32_t>(cut_mcut.face_sizes.size()));
    if (progress_cb)
        progress_cb(10.0f);
    if (dispatch_err != MC_NO_ERROR) {
        release_context();
        return false;
    }

    uint32_t component_count = 0;
    McResult err = mcGetConnectedComponents(context, MC_CONNECTED_COMPONENT_TYPE_FRAGMENT, 0, nullptr, &component_count);
    if (progress_cb)
        progress_cb(20.0f);
    if (err != MC_NO_ERROR || component_count == 0) {
        release_context();
        return false;
    }

    std::vector<McConnectedComponent> components(component_count, MC_NULL_HANDLE);
    err = mcGetConnectedComponents(context, MC_CONNECTED_COMPONENT_TYPE_FRAGMENT, component_count, components.data(), nullptr);
    if (progress_cb)
        progress_cb(30.0f);
    if (err != MC_NO_ERROR) {
        release_context();
        return false;
    }

    dst_mesh.reserve(component_count);
    const size_t source_face_count = src_mesh.its.indices.size();
    for (uint32_t component_index = 0; component_index < component_count; ++component_index) {
        if (cancel_cb && cancel_cb()) {
            dst_mesh.clear();
            release_context();
            return false;
        }

        std::vector<double> vertices;
        std::vector<uint32_t> face_indices;
        std::vector<uint32_t> face_origin;
        McPatchLocation patch_location = static_cast<McPatchLocation>(0);
        McFragmentLocation fragment_location = static_cast<McFragmentLocation>(0);

        const McConnectedComponent component = components[component_index];
        const bool have_vertices = get_connected_component_data(context, component, MC_CONNECTED_COMPONENT_DATA_VERTEX_DOUBLE, vertices);
        const bool have_faces = get_connected_component_data(context, component, MC_CONNECTED_COMPONENT_DATA_FACE_TRIANGULATION, face_indices);
        const bool have_face_origin = get_connected_component_data(context, component, MC_CONNECTED_COMPONENT_DATA_FACE_TRIANGULATION_MAP, face_origin);
        const bool have_patch = get_connected_component_scalar(context, component, MC_CONNECTED_COMPONENT_DATA_PATCH_LOCATION, patch_location);
        const bool have_fragment = get_connected_component_scalar(context, component, MC_CONNECTED_COMPONENT_DATA_FRAGMENT_LOCATION, fragment_location);
        if (!have_vertices || !have_faces || !have_face_origin || !have_patch || !have_fragment) {
            dst_mesh.clear();
            release_context();
            return false;
        }

        const bool reverse_winding = (fragment_location == MC_FRAGMENT_LOCATION_BELOW) && (patch_location == MC_PATCH_LOCATION_OUTSIDE);
        if (reverse_winding) {
            for (size_t offset = 0; offset + 2 < face_indices.size(); offset += 3)
                std::swap(face_indices[offset + 0], face_indices[offset + 2]);
        }

        dst_mesh.emplace_back(build_face_origin_triangle_mesh(vertices, face_indices, face_origin, source_face_count));

        if (progress_cb)
            progress_cb(30.0f + 60.0f * float(component_index + 1) / float(component_count));
    }

    release_context();
    if (progress_cb)
        progress_cb(100.0f);
    return true;
}

} // namespace BallEraser
} // namespace Slic3r
