#include "Model.hpp"

#include <cmath>

namespace Slic3r {

void ModelVolume::set_transformation(const Geometry::Transformation &transformation)
{
    m_transformation = transformation;
}

void ModelVolume::set_transformation(const Transform3d &trafo)
{
    m_transformation.set_from_transform(trafo);
}

const Transform3d &ModelVolume::get_matrix(bool dont_translate, bool dont_rotate, bool dont_scale, bool dont_mirror) const
{
    return m_transformation.get_matrix(dont_translate, dont_rotate, dont_scale, dont_mirror);
}

void ModelInstance::set_transformation(const Geometry::Transformation &transformation)
{
    m_transformation = transformation;
    m_assemble_scalling_factor_dirty = true;
}

const Geometry::Transformation &ModelInstance::get_assemble_transformation() const
{
    if (m_assemble_scalling_factor_dirty) {
        m_assemble_transformation.set_scaling_factor(m_transformation.get_scaling_factor());
        m_assemble_scalling_factor_dirty = false;
    }
    return m_assemble_transformation;
}

void ModelInstance::set_assemble_transformation(const Geometry::Transformation &transformation)
{
    m_assemble_initialized    = true;
    m_assemble_transformation = transformation;
}

void ModelInstance::set_assemble_from_transform(const Transform3d &transform)
{
    m_assemble_initialized = true;
    m_assemble_transformation.set_from_transform(transform);
}

void ModelInstance::set_assemble_offset(const Vec3d &offset)
{
    m_assemble_initialized = true;
    m_assemble_transformation.set_offset(offset);
}

void ModelInstance::set_scaling_factor(const Vec3d &scaling_factor)
{
    m_transformation.set_scaling_factor(scaling_factor);
    m_assemble_scalling_factor_dirty = true;
}

void ModelInstance::set_scaling_factor(Axis axis, double scaling_factor)
{
    m_transformation.set_scaling_factor(axis, scaling_factor);
    m_assemble_scalling_factor_dirty = true;
}

void ModelInstance::transform_mesh(TriangleMesh *mesh, bool dont_translate) const
{
    mesh->transform(get_matrix(dont_translate));
}

void ModelInstance::rotate(Matrix3d rotation_matrix)
{
    auto new_rotation_mat = Transform3d(rotation_matrix) * m_transformation.get_rotation_matrix();
    m_transformation.set_rotation_matrix(new_rotation_mat);
}

BoundingBoxf3 ModelInstance::transform_mesh_bounding_box(const TriangleMesh &mesh, bool dont_translate) const
{
    TriangleMesh copy(mesh);
    copy.transform(get_matrix(true, false, true, true));
    BoundingBoxf3 bbox = copy.bounding_box();

    if (! empty(bbox)) {
        for (unsigned int i = 0; i < 3; ++i) {
            if (std::abs(get_scaling_factor((Axis) i) - 1.0) > EPSILON) {
                bbox.min(i) *= get_scaling_factor((Axis) i);
                bbox.max(i) *= get_scaling_factor((Axis) i);
            }
        }

        if (! dont_translate) {
            bbox.min += get_offset();
            bbox.max += get_offset();
        }
    }
    return bbox;
}

BoundingBoxf3 ModelInstance::transform_bounding_box(const BoundingBoxf3 &bbox, bool dont_translate) const
{
    return bbox.transformed(get_matrix(dont_translate));
}

BoundingBoxf3 ModelInstance::transform_bounding_box_in_assembly_view(const BoundingBoxf3 &bbox, bool dont_translate) const
{
    return bbox.transformed(get_assemble_transformation().get_matrix());
}

Vec3d ModelInstance::transform_vector(const Vec3d &v, bool dont_translate) const
{
    return get_matrix(dont_translate) * v;
}

void ModelInstance::transform_polygon(Polygon *polygon) const
{
    polygon->rotate(get_rotation(Z));
    polygon->scale(get_scaling_factor(X), get_scaling_factor(Y));
}

const Transform3d &ModelInstance::get_matrix(bool dont_translate, bool dont_rotate, bool dont_scale, bool dont_mirror) const
{
    return m_transformation.get_matrix(dont_translate, dont_rotate, dont_scale, dont_mirror);
}

} // namespace Slic3r
