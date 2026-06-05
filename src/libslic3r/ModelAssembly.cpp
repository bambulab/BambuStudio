#include "Model.hpp"

namespace Slic3r {

void delete_object_mesh(ModelObject &) {}

void save_object_mesh(ModelObject &) {}

Lines Polygon::lines() const
{
    return to_lines(*this);
}

void Model::assign_new_unique_ids_recursive()
{
    this->set_new_unique_id();
    for (std::pair<const t_model_material_id, ModelMaterial *> &material : this->materials)
        material.second->assign_new_unique_ids_recursive();
    for (ModelObject *model_object : this->objects)
        model_object->assign_new_unique_ids_recursive();
}

Model::~Model()
{
    this->clear_objects();
    this->clear_materials();
}

void Model::clear_objects()
{
    for (ModelObject *object : this->objects)
        delete object;
    this->objects.clear();
    object_backup_id_map.clear();
}

void Model::clear_materials()
{
    for (auto &material : this->materials)
        delete material.second;
    this->materials.clear();
}

ModelObject *Model::add_object()
{
    this->objects.emplace_back(new ModelObject(this));
    return this->objects.back();
}

ModelObject::~ModelObject()
{
    this->clear_volumes();
    this->clear_instances();
}

void ModelObject::assign_new_unique_ids_recursive()
{
    this->set_new_unique_id();
    for (ModelVolume *model_volume : this->volumes)
        model_volume->assign_new_unique_ids_recursive();
    for (ModelInstance *model_instance : this->instances)
        model_instance->assign_new_unique_ids_recursive();
    this->layer_height_profile.set_new_unique_id();
}

ModelVolume *ModelObject::add_volume(const TriangleMesh &mesh, bool modify_to_center_geometry)
{
    ModelVolume *volume = new ModelVolume(this, mesh);
    this->volumes.push_back(volume);
    if (modify_to_center_geometry) {
        volume->center_geometry_after_creation();
        this->invalidate_bounding_box();
    }
    Slic3r::save_object_mesh(*this);
    return volume;
}

void ModelObject::clear_volumes()
{
    for (ModelVolume *volume : this->volumes)
        delete volume;
    this->volumes.clear();
    this->invalidate_bounding_box();
}

ModelInstance *ModelObject::add_instance()
{
    ModelInstance *instance = new ModelInstance(this);
    this->instances.push_back(instance);
    this->invalidate_bounding_box();
    if (this->instances.size() == 1)
        Slic3r::save_object_mesh(*this);
    return instance;
}

void ModelObject::clear_instances()
{
    for (ModelInstance *instance : this->instances)
        delete instance;
    this->instances.clear();
    this->invalidate_bounding_box();
}

void ModelVolume::center_geometry_after_creation(bool update_source_offset)
{
    const BoundingBoxf3 bbox  = this->mesh().bounding_box();
    Vec3d              shift = Vec3d::Zero();
    if (bbox.defined)
        shift = (bbox.min + bbox.max) * 0.5;
    if (!shift.isApprox(Vec3d::Zero())) {
        if (m_mesh) {
            const_cast<TriangleMesh *>(m_mesh.get())->translate(-(float)shift(0), -(float)shift(1), -(float)shift(2));
            const_cast<TriangleMesh *>(m_mesh.get())->set_init_shift(shift);
        }
        if (m_convex_hull)
            const_cast<TriangleMesh *>(m_convex_hull.get())->translate(-(float)shift(0), -(float)shift(1), -(float)shift(2));
        translate(shift);
    }

    if (update_source_offset)
        source.mesh_offset = shift;
}

void ModelVolume::translate(const Vec3d &displacement)
{
    set_offset(get_offset() + displacement);
}

void ModelVolume::calculate_convex_hull()
{
    m_convex_hull.reset();
    invalidate_convex_hull_2d();
}

void ModelVolume::assign_new_unique_ids_recursive()
{
    ObjectBase::set_new_unique_id();
    config.set_new_unique_id();
    supported_facets.set_new_unique_id();
    fuzzy_skin_facets.set_new_unique_id();
    seam_facets.set_new_unique_id();
    mmu_segmentation_facets.set_new_unique_id();
}

} // namespace Slic3r
