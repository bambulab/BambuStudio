#include "ColorCutLegacyPlaneBackend.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

#include <array>

namespace Slic3r {
namespace ColorCut {

static std::array<Vec3d, 4> make_plane_points(const Transform3d &cut_matrix)
{
    const std::array<Vec3d, 4> local_plane = {
        Vec3d(-1.0, -1.0, 0.0),
        Vec3d( 1.0, -1.0, 0.0),
        Vec3d( 1.0,  1.0, 0.0),
        Vec3d(-1.0,  1.0, 0.0)
    };

    std::array<Vec3d, 4> plane_points{};
    for (size_t index = 0; index < local_plane.size(); ++index)
        plane_points[index] = cut_matrix * local_plane[index];
    return plane_points;
}

static VolumeCutProvenance make_volume_provenance(
    const indexed_triangle_set &mesh,
    int source_volume_id,
    ProvenanceSide side)
{
    VolumeCutProvenance provenance;
    provenance.triangles.reserve(mesh.indices.size());
    for (size_t triangle_index = 0; triangle_index < mesh.indices.size(); ++triangle_index) {
        OutputTriangleProvenance triangle;
        triangle.side = side;
        triangle.kind = ProvenanceTriangleKind::Unknown;
        triangle.source_triangles.push_back({source_volume_id, -1});
        provenance.triangles.emplace_back(std::move(triangle));
    }
    return provenance;
}

GeometryCutOutput ColorCutLegacyPlaneBackend::cut(const GeometryCutInput &input)
{
    GeometryCutOutput output;
    if (input.source_object == nullptr || input.source_volume == nullptr) {
        output.warnings.push_back({ColorCutWarningCode::UnsupportedAppearanceData, "ColorCut legacy plane backend requires a single model-part source volume."});
        return output;
    }

    auto attributes = input.attributes;

    std::array<Vec3d, 4> plane_points = make_plane_points(input.cut_matrix);
    std::array<Vec3d, 4> provenance_plane_points = plane_points;
    const Vec3d instance_offset = input.source_object->instances[static_cast<size_t>(input.instance_index)]->get_offset();
    for (Vec3d &point : provenance_plane_points)
        point -= instance_offset;

    const auto volume_matrix = input.source_volume->get_matrix();
    const Transform3d invert_cut_matrix = const_cast<ModelObject *>(input.source_object)->calculate_cut_plane_inverse_matrix(provenance_plane_points);
    const Transform3d instance_matrix = input.source_object->instances[static_cast<size_t>(input.instance_index)]->get_transformation().get_matrix_no_offset();

    TriangleMesh mesh(input.source_volume->mesh());
    mesh.transform(invert_cut_matrix * instance_matrix * volume_matrix, true);

    indexed_triangle_set upper_its, lower_its;
    cut_mesh(mesh.its, 0.0f, &upper_its, &lower_its, true);

    auto *source_object = const_cast<ModelObject *>(input.source_object);
    output.new_objects = source_object->cut(static_cast<size_t>(input.instance_index), plane_points, attributes);
    output.success = !output.new_objects.empty();

    const int source_volume_id = input.source_volume->id().id;
    if (attributes.has(ModelObjectCutAttribute::KeepUpper)) {
        GeometryCutOutputVolume volume;
        volume.mesh = TriangleMesh(upper_its);
        volume.side = ProvenanceSide::Upper;
        volume.provenance = make_volume_provenance(upper_its, source_volume_id, ProvenanceSide::Upper);
        output.volumes.emplace_back(std::move(volume));
    }
    if (attributes.has(ModelObjectCutAttribute::KeepLower)) {
        GeometryCutOutputVolume volume;
        volume.mesh = TriangleMesh(lower_its);
        volume.side = ProvenanceSide::Lower;
        volume.provenance = make_volume_provenance(lower_its, source_volume_id, ProvenanceSide::Lower);
        output.volumes.emplace_back(std::move(volume));
    }

    return output;
}

ColorCutCapabilities ColorCutLegacyPlaneBackend::capabilities() const
{
    ColorCutCapabilities capabilities;
    capabilities.supports_cap_surface_provenance = false;
    return capabilities;
}

const char *ColorCutLegacyPlaneBackend::backend_name() const
{
    return "legacy-plane";
}

} // namespace ColorCut
} // namespace Slic3r
