#include "OpenMeshCraftBoolean.h"

#include "OpenMeshCraft/Boolean/MeshBoolean.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace {

using OMCBoolean   = OMC::MeshBoolean<OMC::EIAC, OMC::TriSoupTraits>;
using OMCPoints    = OMC::TriSoupTraits::Points;
using OMCTriangles = OMC::TriSoupTraits::Triangles;

void set_error(const std::string &message, char *buffer, size_t capacity)
{
    if (buffer == nullptr || capacity == 0)
        return;

    const size_t length = std::min(message.size(), capacity - 1);
    std::memcpy(buffer, message.data(), length);
    buffer[length] = '\0';
}

bool validate_mesh(const BambuOMCMesh *mesh, std::string &error)
{
    if (mesh == nullptr) {
        error = "OpenMeshCraft input mesh is null.";
        return false;
    }
    if ((mesh->point_count != 0 && mesh->points == nullptr) ||
        (mesh->triangle_count != 0 && mesh->triangles == nullptr)) {
        error = "OpenMeshCraft input mesh data is null.";
        return false;
    }
    return true;
}

void copy_mesh(const BambuOMCMesh &input, OMCPoints &points, OMCTriangles &triangles)
{
    points.reserve(input.point_count);
    triangles.reserve(input.triangle_count);

    for (size_t index = 0; index < input.point_count; ++index) {
        const BambuOMCPoint &point = input.points[index];
        points.emplace_back(point.x, point.y, point.z);
    }

    for (size_t index = 0; index < input.triangle_count; ++index) {
        const BambuOMCTriangle &triangle = input.triangles[index];
        triangles.emplace_back(triangle.vertices[0], triangle.vertices[1], triangle.vertices[2]);
    }
}

} // namespace

extern "C" bool BAMBU_OMC_CALL bambu_omc_boolean(
    const BambuOMCMesh       *first,
    const BambuOMCMesh       *second,
    BambuOMCOperation         operation,
    BambuOMCResultCallback    result_callback,
    void                     *user_data,
    char                     *error_message,
    size_t                    error_message_capacity)
{
    try {
        // Validate separately so a failing first mesh does not skip the second
        // (|| short-circuits) and so each error message stays accurate.
        std::string err1;
        std::string err2;
        if (!validate_mesh(first, err1)) {
            set_error(err1, error_message, error_message_capacity);
            return false;
        }
        if (!validate_mesh(second, err2)) {
            set_error(err2, error_message, error_message_capacity);
            return false;
        }
        if (result_callback == nullptr) {
            set_error("OpenMeshCraft result callback is null.", error_message, error_message_capacity);
            return false;
        }

        OMCPoints first_points, second_points, result_points;
        OMCTriangles first_triangles, second_triangles, result_triangles;
        copy_mesh(*first, first_points, first_triangles);
        copy_mesh(*second, second_points, second_triangles);

        OMCBoolean boolean(/*verbose*/ false);
        boolean.addTriMeshAsInput(first_points, first_triangles);
        boolean.addTriMeshAsInput(second_points, second_triangles);
        boolean.setTriMeshAsOutput(result_points, result_triangles);
        boolean.computeLabels();

        switch (operation) {
        case BAMBU_OMC_UNION:
            boolean.Union();
            break;
        case BAMBU_OMC_INTERSECTION:
            boolean.Intersection();
            break;
        case BAMBU_OMC_SUBTRACTION:
            boolean.Subtraction();
            break;
        default:
            set_error("Unsupported OpenMeshCraft boolean operation.", error_message, error_message_capacity);
            return false;
        }

        // Explicit field copy: equal sizeof is not enough across MSVC/Clang/GCC
        // (layout / padding / field order are not guaranteed).
        std::vector<BambuOMCPoint> out_points;
        out_points.reserve(result_points.size());
        for (const auto &p : result_points)
            out_points.push_back(BambuOMCPoint{p.x(), p.y(), p.z()});

        std::vector<BambuOMCTriangle> out_triangles;
        out_triangles.reserve(result_triangles.size());
        for (const auto &t : result_triangles)
            out_triangles.push_back(BambuOMCTriangle{{t[0], t[1], t[2]}});

        result_callback(
            out_points.data(),
            out_points.size(),
            out_triangles.data(),
            out_triangles.size(),
            user_data);
        return true;
    } catch (const std::exception &error) {
        set_error(error.what(), error_message, error_message_capacity);
        return false;
    } catch (...) {
        set_error("OpenMeshCraft failed with an unknown exception.", error_message, error_message_capacity);
        return false;
    }
}

