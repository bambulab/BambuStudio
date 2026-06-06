#include "Model.hpp"

namespace Slic3r {

Model &Model::assign_copy(const Model &rhs)
{
    this->copy_id(rhs);
    this->clear_materials();
    this->materials = rhs.materials;
    for (std::pair<const t_model_material_id, ModelMaterial *> &material : this->materials) {
        material.second = new ModelMaterial(*material.second);
        material.second->set_model(this);
    }

    this->clear_objects();
    this->objects.reserve(rhs.objects.size());
    for (const ModelObject *model_object : rhs.objects) {
        auto copy = ModelObject::new_copy(*model_object);
        copy->set_model(this);
        this->objects.emplace_back(copy);
    }

    this->plates_custom_gcodes = rhs.plates_custom_gcodes;
    this->curr_plate_index     = rhs.curr_plate_index;
    if (rhs.calib_pa_pattern)
        this->calib_pa_pattern = std::make_unique<CalibPressureAdvancePattern>(*rhs.calib_pa_pattern);
    else
        this->calib_pa_pattern.reset();

    this->design_info        = rhs.design_info;
    this->model_info         = rhs.model_info;
    this->design_id          = rhs.design_id;
    this->stl_design_id      = rhs.stl_design_id;
    this->stl_design_country = rhs.stl_design_country;
    this->makerlab_region    = rhs.makerlab_region;
    this->makerlab_name      = rhs.makerlab_name;
    this->makerlab_id        = rhs.makerlab_id;
    this->profile_info       = rhs.profile_info;
    this->mk_name            = rhs.mk_name;
    this->mk_version         = rhs.mk_version;
    this->md_name            = rhs.md_name;
    this->md_value           = rhs.md_value;
    this->texture_mesh       = rhs.texture_mesh;

    return *this;
}

Model &Model::assign_copy(Model &&rhs)
{
    this->copy_id(rhs);

    this->clear_materials();
    this->materials = std::move(rhs.materials);
    for (std::pair<const t_model_material_id, ModelMaterial *> &material : this->materials)
        material.second->set_model(this);
    rhs.materials.clear();

    this->clear_objects();
    this->objects = std::move(rhs.objects);
    for (ModelObject *model_object : this->objects)
        model_object->set_model(this);
    rhs.objects.clear();

    this->plates_custom_gcodes = std::move(rhs.plates_custom_gcodes);
    this->curr_plate_index     = rhs.curr_plate_index;
    this->calib_pa_pattern     = std::move(rhs.calib_pa_pattern);
    this->design_id            = rhs.design_id;
    this->stl_design_id        = rhs.stl_design_id;
    this->stl_design_country   = rhs.stl_design_country;
    this->makerlab_region      = rhs.makerlab_region;
    this->makerlab_name        = rhs.makerlab_name;
    this->makerlab_id          = rhs.makerlab_id;
    this->mk_name              = rhs.mk_name;
    this->mk_version           = rhs.mk_version;
    this->md_name              = rhs.md_name;
    this->md_value             = rhs.md_value;
    this->texture_mesh         = std::move(rhs.texture_mesh);
    this->backup_path          = std::move(rhs.backup_path);
    this->object_backup_id_map = std::move(rhs.object_backup_id_map);
    this->next_object_backup_id = rhs.next_object_backup_id;
    this->design_info          = rhs.design_info;
    rhs.design_info.reset();
    this->model_info           = rhs.model_info;
    rhs.model_info.reset();
    this->profile_info         = rhs.profile_info;
    rhs.profile_info.reset();

    return *this;
}

ModelObject &ModelObject::assign_copy(const ModelObject &rhs)
{
    assert(this->id().invalid() || this->id() == rhs.id());
    assert(this->config.id().invalid() || this->config.id() == rhs.config.id());
    this->copy_id(rhs);

    this->name                         = rhs.name;
    this->module_name                  = rhs.module_name;
    this->input_file                   = rhs.input_file;
    this->config                       = rhs.config;
    this->sla_support_points           = rhs.sla_support_points;
    this->sla_points_status            = rhs.sla_points_status;
    this->sla_drain_holes              = rhs.sla_drain_holes;
    this->brim_points                  = rhs.brim_points;
    this->layer_config_ranges          = rhs.layer_config_ranges;
    this->layer_height_profile         = rhs.layer_height_profile;
    this->printable                    = rhs.printable;
    this->origin_translation           = rhs.origin_translation;
    this->cut_id.copy(rhs.cut_id);
    this->m_bounding_box               = rhs.m_bounding_box;
    this->m_bounding_box_valid         = rhs.m_bounding_box_valid;
    this->m_raw_bounding_box           = rhs.m_raw_bounding_box;
    this->m_raw_bounding_box_valid     = rhs.m_raw_bounding_box_valid;
    this->m_raw_mesh_bounding_box      = rhs.m_raw_mesh_bounding_box;
    this->m_raw_mesh_bounding_box_valid = rhs.m_raw_mesh_bounding_box_valid;

    this->clear_volumes();
    this->volumes.reserve(rhs.volumes.size());
    for (ModelVolume *model_volume : rhs.volumes) {
        this->volumes.emplace_back(new ModelVolume(*model_volume));
        this->volumes.back()->set_model_object(this);
    }

    this->clear_instances();
    this->instances.reserve(rhs.instances.size());
    for (const ModelInstance *model_instance : rhs.instances) {
        this->instances.emplace_back(new ModelInstance(*model_instance));
        this->instances.back()->set_model_object(this);
    }

    return *this;
}

ModelObject &ModelObject::assign_copy(ModelObject &&rhs)
{
    assert(this->id().invalid());
    this->copy_id(rhs);

    this->name                         = std::move(rhs.name);
    this->module_name                  = std::move(rhs.module_name);
    this->input_file                   = std::move(rhs.input_file);
    this->config                       = std::move(rhs.config);
    this->sla_support_points           = std::move(rhs.sla_support_points);
    this->sla_points_status            = std::move(rhs.sla_points_status);
    this->sla_drain_holes              = std::move(rhs.sla_drain_holes);
    this->brim_points                  = std::move(rhs.brim_points);
    this->layer_config_ranges          = std::move(rhs.layer_config_ranges);
    this->layer_height_profile         = std::move(rhs.layer_height_profile);
    this->printable                    = std::move(rhs.printable);
    this->origin_translation           = std::move(rhs.origin_translation);
    this->m_bounding_box               = std::move(rhs.m_bounding_box);
    this->m_bounding_box_valid         = std::move(rhs.m_bounding_box_valid);
    this->m_raw_bounding_box           = rhs.m_raw_bounding_box;
    this->m_raw_bounding_box_valid     = rhs.m_raw_bounding_box_valid;
    this->m_raw_mesh_bounding_box      = rhs.m_raw_mesh_bounding_box;
    this->m_raw_mesh_bounding_box_valid = rhs.m_raw_mesh_bounding_box_valid;

    this->clear_volumes();
    this->volumes = std::move(rhs.volumes);
    rhs.volumes.clear();
    for (ModelVolume *model_volume : this->volumes)
        model_volume->set_model_object(this);

    this->clear_instances();
    this->instances = std::move(rhs.instances);
    rhs.instances.clear();
    for (ModelInstance *model_instance : this->instances)
        model_instance->set_model_object(this);

    return *this;
}

} // namespace Slic3r
