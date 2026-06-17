#include "Model.hpp"

#include "Exception.hpp"

namespace Slic3r {

const BoundingBoxf3 &ModelObject::bounding_box() const
{
    if (!m_bounding_box_valid) {
        m_bounding_box_valid = true;
        BoundingBoxf3 raw_bbox = this->raw_mesh_bounding_box();
        m_bounding_box.reset();
        for (const ModelInstance *i : this->instances)
            m_bounding_box.merge(i->transform_bounding_box(raw_bbox));
    }
    return m_bounding_box;
}

const BoundingBoxf3 &ModelObject::bounding_box_in_assembly_view() const
{
    m_bounding_box_in_assembly_view.reset();
    BoundingBoxf3 raw_bbox = this->raw_mesh_bounding_box();
    for (const ModelInstance *i : this->instances)
        m_bounding_box_in_assembly_view.merge(i->transform_bounding_box_in_assembly_view(raw_bbox));
    return m_bounding_box_in_assembly_view;
}

indexed_triangle_set ModelObject::raw_indexed_triangle_set() const
{
    size_t num_vertices = 0;
    size_t num_faces    = 0;
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part()) {
            num_vertices += v->mesh().its.vertices.size();
            num_faces += v->mesh().its.indices.size();
        }

    indexed_triangle_set out;
    out.vertices.reserve(num_vertices);
    out.indices.reserve(num_faces);
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part()) {
            size_t i = out.vertices.size();
            size_t j = out.indices.size();
            append(out.vertices, v->mesh().its.vertices);
            append(out.indices, v->mesh().its.indices);
            auto m = v->get_matrix();
            for (; i < out.vertices.size(); ++i)
                out.vertices[i] = (m * out.vertices[i].cast<double>()).cast<float>().eval();
            if (v->is_left_handed()) {
                for (; j < out.indices.size(); ++j)
                    std::swap(out.indices[j][0], out.indices[j][1]);
            }
        }
    return out;
}

const BoundingBoxf3 &ModelObject::raw_bounding_box() const
{
    if (!m_raw_bounding_box_valid) {
        m_raw_bounding_box_valid = true;
        m_raw_bounding_box.reset();
        if (this->instances.empty())
            throw Slic3r::InvalidArgument("Can't call raw_bounding_box() with no instances");

        const Transform3d &inst_matrix = this->instances.front()->get_transformation().get_matrix(true);
        for (const ModelVolume *v : this->volumes)
            if (v->is_model_part())
                m_raw_bounding_box.merge(v->mesh().transformed_bounding_box(inst_matrix * v->get_matrix()));
    }
    return m_raw_bounding_box;
}

ModelMaterial *ModelVolume::material() const
{
    return this->object->get_model()->get_material(m_material_id);
}

} // namespace Slic3r
