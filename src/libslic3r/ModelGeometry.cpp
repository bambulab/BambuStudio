#include "Model.hpp"

#include "libslic3r/Geometry/ConvexHull.hpp"

#include <algorithm>
#include <float.h>

namespace Slic3r {

bool ModelObject::is_mm_painted() const
{
    return std::any_of(this->volumes.cbegin(), this->volumes.cend(), [](const ModelVolume *mv) { return mv->is_mm_painted(); });
}

bool ModelObject::is_fuzzy_skin_painted() const
{
    return std::any_of(this->volumes.cbegin(), this->volumes.cend(), [](const ModelVolume *mv) { return mv->is_fuzzy_skin_facets_painted(); });
}

BoundingBoxf3 ModelObject::instance_bounding_box(size_t instance_idx, bool dont_translate) const
{
    BoundingBoxf3      bb;
    const Transform3d &inst_matrix = this->instances[instance_idx]->get_transformation().get_matrix(dont_translate);
    for (ModelVolume *v : this->volumes) {
        if (v->is_model_part())
            bb.merge(v->mesh().transformed_bounding_box(inst_matrix * v->get_matrix()));
    }
    return bb;
}

BoundingBoxf3 ModelObject::instance_bounding_box(const ModelInstance &instance, bool dont_translate) const
{
    BoundingBoxf3 bbox;
    const auto   &inst_mat = instance.get_transformation().get_matrix(dont_translate);
    for (auto vol : this->volumes) {
        if (vol->is_model_part())
            bbox.merge(vol->mesh().transformed_bounding_box(inst_mat * vol->get_matrix()));
    }
    return bbox;
}

BoundingBoxf3 ModelObject::instance_convex_hull_bounding_box(size_t instance_idx, bool dont_translate) const
{
    return instance_convex_hull_bounding_box(this->instances[instance_idx], dont_translate);
}

BoundingBoxf3 ModelObject::instance_convex_hull_bounding_box(const ModelInstance *instance, bool dont_translate) const
{
    BoundingBoxf3      bb;
    const Transform3d &inst_matrix = instance->get_transformation().get_matrix(dont_translate);
    for (ModelVolume *v : this->volumes) {
        if (v->is_model_part())
            bb.merge(v->get_convex_hull().transformed_bounding_box(inst_matrix * v->get_matrix()));
    }
    return bb;
}

Polygon ModelObject::convex_hull_2d(const Transform3d &trafo_instance) const
{
    Points pts;
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part()) {
            const Polygon &volume_hull = v->get_convex_hull_2d(trafo_instance);
            pts.insert(pts.end(), volume_hull.points.begin(), volume_hull.points.end());
        }

    return Geometry::convex_hull(std::move(pts));
}

void ModelVolume::calculate_convex_hull_2d(const Geometry::Transformation &transformation) const
{
    const indexed_triangle_set &its = m_convex_hull->its;
    if (its.vertices.empty())
        return;

    Points                   pts;
    Geometry::Transformation new_trans(transformation);
    new_trans.reset_offset();
    Transform3d new_matrix = new_trans.get_matrix();

    pts.reserve(its.vertices.size());
    for (size_t i = 0; i < its.vertices.size(); ++i) {
        Vec3d p = new_matrix * its.vertices[i].cast<double>();
        pts.emplace_back(coord_t(scale_(p.x())), coord_t(scale_(p.y())));
    }
    m_cached_2d_polygon = Slic3r::Geometry::convex_hull(pts);

    m_convex_hull_2d = m_cached_2d_polygon;
    m_convex_hull_2d.translate(scale_(transformation.get_offset(X)), scale_(transformation.get_offset(Y)));
}

const Polygon &ModelVolume::get_convex_hull_2d(const Transform3d &trafo_instance) const
{
    Transform3d new_matrix = trafo_instance * m_transformation.get_matrix();

    auto need_recompute = [](Geometry::Transformation &old_transform, Geometry::Transformation &new_transform) -> bool {
        const Vec3d &old_rotation = old_transform.get_rotation();
        const Vec3d &new_rotation = new_transform.get_rotation();
        const Vec3d &old_mirror   = old_transform.get_mirror();
        const Vec3d &new_mirror   = new_transform.get_mirror();
        const Vec3d &old_scaling  = old_transform.get_scaling_factor();
        const Vec3d &new_scaling  = new_transform.get_scaling_factor();

        return (old_scaling != new_scaling) || (old_rotation != new_rotation) || (old_mirror != new_mirror);
    };

    if ((new_matrix.matrix() != m_cached_trans_matrix.matrix()) || ! m_convex_hull_2d.is_valid()) {
        Geometry::Transformation new_trans(new_matrix), old_trans(m_cached_trans_matrix);

        if (need_recompute(old_trans, new_trans) || ! m_convex_hull_2d.is_valid())
            calculate_convex_hull_2d(new_trans);
        else {
            m_convex_hull_2d = m_cached_2d_polygon;
            m_convex_hull_2d.translate(scale_(new_trans.get_offset(X)), scale_(new_trans.get_offset(Y)));
        }
        m_cached_trans_matrix = new_matrix;
    }

    return m_convex_hull_2d;
}

Polygon ModelInstance::convex_hull_2d()
{
    const Transform3d &trafo_instance = get_matrix(false);
    convex_hull = get_object()->convex_hull_2d(trafo_instance);
    return convex_hull;
}

void ModelInstance::invalidate_convex_hull_2d()
{
    convex_hull.clear();
}

} // namespace Slic3r
