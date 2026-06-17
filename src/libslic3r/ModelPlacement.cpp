#include "Model.hpp"

namespace Slic3r {

void ModelObject::ensure_on_bed(bool allow_negative_z)
{
    double z_offset = 0.0;

    if (allow_negative_z) {
        if (parts_count() == 1) {
            const double min_z = get_min_z();
            const double max_z = get_max_z();
            if (min_z >= SINKING_Z_THRESHOLD || max_z < 0.0)
                z_offset = -min_z;
        } else {
            const double max_z = get_max_z();
            if (max_z < SINKING_MIN_Z_THRESHOLD)
                z_offset = SINKING_MIN_Z_THRESHOLD - max_z;
        }
    } else {
        z_offset = -get_min_z();
    }

    if (z_offset != 0.0)
        translate_instances(z_offset * Vec3d::UnitZ());
}

void ModelObject::translate_instances(const Vec3d &vector)
{
    for (size_t i = 0; i < instances.size(); ++i)
        translate_instance(i, vector);
}

void ModelObject::translate_instance(size_t instance_idx, const Vec3d &vector)
{
    assert(instance_idx < instances.size());
    ModelInstance *instance = instances[instance_idx];
    instance->set_offset(instance->get_offset() + vector);
    invalidate_bounding_box();
}

size_t ModelObject::parts_count() const
{
    size_t num = 0;
    for (const ModelVolume *volume : this->volumes)
        if (volume->is_model_part())
            ++num;
    return num;
}

double ModelObject::get_min_z() const
{
    if (instances.empty())
        return 0.0;

    double min_z = DBL_MAX;
    for (size_t i = 0; i < instances.size(); ++i)
        min_z = std::min(min_z, get_instance_min_z(i));
    return min_z;
}

double ModelObject::get_max_z() const
{
    if (instances.empty())
        return 0.0;

    double max_z = -DBL_MAX;
    for (size_t i = 0; i < instances.size(); ++i)
        max_z = std::max(max_z, get_instance_max_z(i));
    return max_z;
}

double ModelObject::get_instance_min_z(size_t instance_idx) const
{
    double min_z = DBL_MAX;

    const ModelInstance *instance        = instances[instance_idx];
    const Transform3d   &instance_matrix = instance->get_transformation().get_matrix(true);

    for (const ModelVolume *volume : volumes) {
        if (!volume->is_model_part())
            continue;

        const Transform3d   volume_matrix = instance_matrix * volume->get_transformation().get_matrix();
        const TriangleMesh &hull          = volume->get_convex_hull();

        if (hull.its.indices.empty()) {
            const TriangleMesh &mesh = volume->mesh();
            for (const stl_triangle_vertex_indices &facet : mesh.its.indices)
                for (int i = 0; i < 3; ++i)
                    min_z = std::min(min_z, (volume_matrix * mesh.its.vertices[facet[i]].cast<double>()).z());
        } else {
            for (const stl_triangle_vertex_indices &facet : hull.its.indices)
                for (int i = 0; i < 3; ++i)
                    min_z = std::min(min_z, (volume_matrix * hull.its.vertices[facet[i]].cast<double>()).z());
        }
    }

    if (min_z == DBL_MAX)
        min_z = 0.0;
    return min_z + instance->get_offset(Z);
}

double ModelObject::get_instance_max_z(size_t instance_idx) const
{
    double max_z = -DBL_MAX;

    const ModelInstance *instance        = instances[instance_idx];
    const Transform3d   &instance_matrix = instance->get_transformation().get_matrix(true);

    for (const ModelVolume *volume : volumes) {
        if (!volume->is_model_part())
            continue;

        const Transform3d   volume_matrix = instance_matrix * volume->get_transformation().get_matrix();
        const TriangleMesh &hull          = volume->get_convex_hull();
        for (const stl_triangle_vertex_indices &facet : hull.its.indices)
            for (int i = 0; i < 3; ++i)
                max_z = std::max(max_z, (volume_matrix * hull.its.vertices[facet[i]].cast<double>()).z());
    }

    return max_z + instance->get_offset(Z);
}

} // namespace Slic3r
