#include "CutUtils.hpp"
#include "ag/customcut/ColorCutIntegrationBridge.hpp"
#include "ag/customcut/ColorCutRepositoryBridge.hpp"
#include "Geometry.hpp"
#include "libslic3r.h"
#include "Model.hpp"
#include "TriangleMeshSlicer.hpp"
#include "TriangleSelector.hpp"
#include "ObjectID.hpp"
#include <boost/log/trivial.hpp>

namespace Slic3r {

using namespace Geometry;
namespace CCIBridge = ColorCut::IntegrationBridge;

static void apply_tolerance(ModelVolume *vol)
{
    ModelVolume::CutInfo &cut_info = vol->cut_info;

    assert(cut_info.is_connector);
    if (!cut_info.is_processed) return;

    Vec3d sf = vol->get_scaling_factor();

    // make a "hole" wider
    sf[X] += double(cut_info.radius_tolerance);
    sf[Y] += double(cut_info.radius_tolerance);

    // make a "hole" dipper
    sf[Z] += double(cut_info.height_tolerance);

    vol->set_scaling_factor(sf);

    // correct offset in respect to the new depth
    Vec3d rot_norm = rotation_transform(vol->get_rotation()) * Vec3d::UnitZ();
    if (rot_norm.norm() != 0.0) rot_norm.normalize();

    double z_offset = 0.5 * static_cast<double>(cut_info.height_tolerance);
    if (cut_info.connector_type == CutConnectorType::Plug || cut_info.connector_type == CutConnectorType::Snap) z_offset -= 0.05; // add small Z offset to better preview

    vol->set_offset(vol->get_offset() + rot_norm * z_offset);
}

static void add_cut_volume(TriangleMesh &     mesh,
                           ModelObject *      object,
                           const ModelVolume *src_volume,
                           const Transform3d &cut_matrix,
                           const std::string &suffix = {},
                           ModelVolumeType    type   = ModelVolumeType::MODEL_PART,
                           std::optional<bool> is_from_upper = std::nullopt)
{
    if (mesh.empty())
        return;

    mesh.transform(cut_matrix);
    ModelVolume *vol = object->add_volume(mesh);
    vol->set_type(type);

    vol->name = src_volume->name + suffix;
    // Don't copy the config's ID.
    vol->config.assign_config(src_volume->config);
    assert(vol->config.id().valid());
    assert(vol->config.id() != src_volume->config.id());
    vol->set_material(src_volume->material_id(), *src_volume->material());
    vol->cut_info = src_volume->cut_info;
    if (is_from_upper.has_value())
        vol->cut_info.is_from_upper = *is_from_upper;

}

static void process_volume_cut(ModelVolume *            volume,
                               const Transform3d &      instance_matrix,
                               const Transform3d &      cut_matrix,
                               ModelObjectCutAttributes attributes,
                               TriangleMesh &           upper_mesh,
                               TriangleMesh &           lower_mesh,
                               CutMeshProvenance *      provenance = nullptr)
{
    const auto volume_matrix = volume->get_matrix();

    const Transformation cut_transformation = Transformation(cut_matrix);

    const Transform3d invert_cut_matrix = cut_transformation.get_rotation_matrix().inverse() * translation_transform(-1 * cut_transformation.get_offset());

    // Transform the mesh by the combined transformation matrix.
    // Flip the triangles in case the composite transformation is left handed.
    TriangleMesh mesh(volume->mesh());
    mesh.transform(invert_cut_matrix * instance_matrix * volume_matrix, true);

    indexed_triangle_set upper_its, lower_its;
    cut_mesh(mesh.its, 0.0f, &upper_its, &lower_its, true, provenance);
    if (attributes.has(ModelObjectCutAttribute::KeepUpper))
        upper_mesh = TriangleMesh(upper_its);
    if (attributes.has(ModelObjectCutAttribute::KeepLower))
        lower_mesh = TriangleMesh(lower_its);
}

static bool is_temp_cut_surface_triangle(const ModelVolume &volume, size_t triangle_index)
{
    return !volume.exterior_facets.get_triangle_as_string(static_cast<int>(triangle_index)).empty();
}

static void propagate_temp_cut_surface_facets(
    const std::vector<CutMeshFacetProvenance> &facet_provenance,
    const ModelVolume &source_volume,
    ModelVolume &target_volume)
{
    target_volume.exterior_facets.reset();
    target_volume.exterior_facets.reserve(static_cast<int>(facet_provenance.size()));

    for (size_t triangle_index = 0; triangle_index < facet_provenance.size(); ++triangle_index) {
        const CutMeshFacetProvenance &provenance = facet_provenance[triangle_index];
        bool mark_triangle = provenance.is_cap;

        if (!mark_triangle && provenance.source_facet >= 0)
            mark_triangle = is_temp_cut_surface_triangle(source_volume, static_cast<size_t>(provenance.source_facet));

        if (mark_triangle)
            target_volume.exterior_facets.set_triangle_from_string(static_cast<int>(triangle_index), "1");
    }

    target_volume.exterior_facets.shrink_to_fit();
}

static void merge_temp_cut_surface_data(const std::vector<const ModelVolume *> &source_volumes, ModelVolume &target_volume)
{
    target_volume.exterior_facets.reset();
    target_volume.exterior_facets.reserve(static_cast<int>(target_volume.mesh().its.indices.size()));

    int target_triangle_index = 0;
    for (const ModelVolume *volume : source_volumes) {
        if (volume == nullptr)
            continue;

        for (size_t triangle_index = 0; triangle_index < volume->mesh().its.indices.size(); ++triangle_index, ++target_triangle_index) {
            const std::string marker = volume->exterior_facets.get_triangle_as_string(static_cast<int>(triangle_index));
            if (!marker.empty())
                target_volume.exterior_facets.set_triangle_from_string(target_triangle_index, marker);
        }
    }

    target_volume.exterior_facets.shrink_to_fit();
}

static void process_connector_cut(ModelVolume *               volume,
                                  const Transform3d &         instance_matrix,
                                  const Transform3d &         cut_matrix,
                                  ModelObjectCutAttributes    attributes,
                                  ModelObject *               upper,
                                  ModelObject *               lower,
                                  std::vector<ModelObject *> &dowels)
{
    assert(volume->cut_info.is_connector);
    volume->cut_info.set_processed();

    const auto volume_matrix = volume->get_matrix();

    // ! Don't apply instance transformation for the conntectors.
    // This transformation is already there
    if (volume->cut_info.connector_type != CutConnectorType::Dowel) {
        if (attributes.has(ModelObjectCutAttribute::KeepUpper)) {
            ModelVolume *vol = nullptr;
            
            // --- OUR TRACER BULLET FOR THE THREAD ---
            if (volume->cut_info.connector_type == CutConnectorType::Thread) {
                TriangleMesh mesh = TriangleMesh(its_make_thread(1.0, 1.0, 0.2f, PI / 180.0));

                vol = upper->add_volume(std::move(mesh));
                vol->set_transformation(volume->get_transformation());
                vol->set_type(ModelVolumeType::NEGATIVE_VOLUME);
                
                vol->cut_info = volume->cut_info;
                vol->name     = volume->name;
            }

            // --- THE ORIGINAL SNAP LOGIC ---
            else if (volume->cut_info.connector_type == CutConnectorType::Snap) {
                TriangleMesh mesh = TriangleMesh(its_make_cylinder(1.0, 1.0, PI / 180.));

                vol = upper->add_volume(std::move(mesh));
                vol->set_transformation(volume->get_transformation());
                vol->set_type(ModelVolumeType::NEGATIVE_VOLUME);

                vol->cut_info = volume->cut_info;
                vol->name     = volume->name;
            } 
            else {
                vol = upper->add_volume(*volume);
            }

            vol->set_transformation(volume_matrix);
            apply_tolerance(vol);
        }
        if (attributes.has(ModelObjectCutAttribute::KeepLower)) {
            ModelVolume *vol = lower->add_volume(*volume);
            vol->set_transformation(volume_matrix);
            // for lower part change type of connector from NEGATIVE_VOLUME to MODEL_PART if this connector is a plug
            vol->set_type(ModelVolumeType::MODEL_PART);
        }
    } else {
        if (attributes.has(ModelObjectCutAttribute::CreateDowels)) {
            ModelObject *dowel{nullptr};
            // Clone the object to duplicate instances, materials etc.
            volume->get_object()->clone_for_cut(&dowel);

            // add one more solid part same as connector if this connector is a dowel
            ModelVolume *vol = dowel->add_volume(*volume);
            vol->set_type(ModelVolumeType::MODEL_PART);

            // But discard rotation and Z-offset for this volume
            vol->set_rotation(Vec3d::Zero());
            vol->set_offset(Z, 0.0);

            dowels.push_back(dowel);
        }

        // Cut the dowel
        apply_tolerance(volume);

        // Perform cut
        TriangleMesh upper_mesh, lower_mesh;
        process_volume_cut(volume, Transform3d::Identity(), cut_matrix, attributes, upper_mesh, lower_mesh);

        // add small Z offset to better preview
        upper_mesh.translate((-0.05 * Vec3d::UnitZ()).cast<float>());
        lower_mesh.translate((0.05 * Vec3d::UnitZ()).cast<float>());

        // Add cut parts to the related objects
        add_cut_volume(upper_mesh, upper, volume, cut_matrix, "_A", volume->type());
        add_cut_volume(lower_mesh, lower, volume, cut_matrix, "_B", volume->type());
    }
}

static void process_modifier_cut(ModelVolume *volume, const Transform3d &instance_matrix, const Transform3d &inverse_cut_matrix, ModelObjectCutAttributes attributes, ModelObject *upper, ModelObject *lower)
{
    const auto volume_matrix = instance_matrix * volume->get_matrix();

    // Modifiers are not cut, but we still need to add the instance transformation
    // to the modifier volume transformation to preserve their shape properly.
    volume->set_transformation(Transformation(volume_matrix));

    if (attributes.has(ModelObjectCutAttribute::CutToParts)) {
        upper->add_volume(*volume);
        return;
    }

    // Some logic for the negative volumes/connectors. Add only needed modifiers
    auto bb                = volume->mesh().transformed_bounding_box(inverse_cut_matrix * volume_matrix);
    bool is_crossed_by_cut = bb.min[Z] <= 0 && bb.max[Z] >= 0;
    if (attributes.has(ModelObjectCutAttribute::KeepUpper) && (bb.min[Z] >= 0 || is_crossed_by_cut))
        upper->add_volume(*volume);
    if (attributes.has(ModelObjectCutAttribute::KeepLower) && (bb.max[Z] <= 0 || is_crossed_by_cut))
        lower->add_volume(*volume);
}

static void process_solid_part_cut(
    ModelVolume *volume, const Transform3d &instance_matrix, const Transform3d &cut_matrix, ModelObjectCutAttributes attributes, ModelObject *upper, ModelObject *lower)
{
    // Perform cut
    TriangleMesh upper_mesh, lower_mesh;
    CutMeshProvenance provenance;
    process_volume_cut(volume, instance_matrix, cut_matrix, attributes, upper_mesh, lower_mesh, &provenance);

    // Add required cut parts to the objects

    if (attributes.has(ModelObjectCutAttribute::CutToParts)) {
        add_cut_volume(upper_mesh, upper, volume, cut_matrix, "_A", ModelVolumeType::MODEL_PART, true);
        if (!upper->volumes.empty()) {
            propagate_temp_cut_surface_facets(provenance.upper_facets, *volume, *upper->volumes.back());
            CCIBridge::reapply_using_cut_provenance(*volume, *upper->volumes.back(), provenance.upper_facets);
        }
        if (!lower_mesh.empty()) {
            add_cut_volume(lower_mesh, upper, volume, cut_matrix, "_B", ModelVolumeType::MODEL_PART, false);
            propagate_temp_cut_surface_facets(provenance.lower_facets, *volume, *upper->volumes.back());
            CCIBridge::reapply_using_cut_provenance(*volume, *upper->volumes.back(), provenance.lower_facets);
        }
        return;
    }

    if (attributes.has(ModelObjectCutAttribute::KeepUpper)) {
        add_cut_volume(upper_mesh, upper, volume, cut_matrix, {}, ModelVolumeType::MODEL_PART, true);
        if (!upper->volumes.empty()) {
            propagate_temp_cut_surface_facets(provenance.upper_facets, *volume, *upper->volumes.back());
            CCIBridge::reapply_using_cut_provenance(*volume, *upper->volumes.back(), provenance.upper_facets);
        }
    }

    if (attributes.has(ModelObjectCutAttribute::KeepLower) && !lower_mesh.empty()) {
        add_cut_volume(lower_mesh, lower, volume, cut_matrix, {}, ModelVolumeType::MODEL_PART, false);
        if (!lower->volumes.empty()) {
            propagate_temp_cut_surface_facets(provenance.lower_facets, *volume, *lower->volumes.back());
            CCIBridge::reapply_using_cut_provenance(*volume, *lower->volumes.back(), provenance.lower_facets);
        }
    }
}

static void reset_instance_transformation(ModelObject *      object,
                                          size_t             src_instance_idx,
                                          const Transform3d &cut_matrix     = Transform3d::Identity(),
                                          bool               place_on_cut   = false,
                                          bool               flip           = false,
                                          bool               is_set_offset  = false,
                                          bool               offset_pos_dir = true,
                                          bool               set_displace   = false,
                                          Vec3d              local_displace = Vec3d::Zero())
{
    // Reset instance transformation except offset and Z-rotation

    for (size_t i = 0; i < object->instances.size(); ++i) {
        auto &       obj_instance = object->instances[i];
        const double rot_z        = obj_instance->get_rotation().z();

        Transformation inst_trafo = Transformation(obj_instance->get_transformation().get_matrix_no_scaling_factor());
        // add respect to mirroring
        if (obj_instance->is_left_handed())
            inst_trafo = inst_trafo * Transformation(scale_transform(Vec3d(-1, 1, 1)));

        obj_instance->set_transformation(inst_trafo);

        if (object->volumes.size() > 0) {
            if (is_set_offset) {
                BoundingBoxf3 curBox;
                for (size_t i = 0; i < object->volumes.size(); i++) { curBox.merge(object->volumes[i]->mesh().bounding_box()); }
                float offset_x = curBox.size().x() * (offset_pos_dir ? 1.1 : 0);
                Vec3d displace(offset_x, 0, 0);
                displace = rotation_transform(obj_instance->get_rotation()) * displace;
                obj_instance->set_offset(obj_instance->get_offset() + displace);
            } else if (set_displace) {
                Vec3d displace = rotation_transform(obj_instance->get_rotation()) * local_displace;
                obj_instance->set_offset(obj_instance->get_offset() + displace);
            }
        }

        Vec3d rotation = Vec3d::Zero();
        if (!flip && !place_on_cut) {
            if (i != src_instance_idx)
                rotation[Z] = rot_z;
        } else {
            Transform3d rotation_matrix = Transform3d::Identity();
            if (flip)
                rotation_matrix = rotation_transform(PI * Vec3d::UnitX());

            if (place_on_cut)
                rotation_matrix = rotation_matrix * Transformation(cut_matrix).get_rotation_matrix().inverse();

            if (i != src_instance_idx)
                rotation_matrix = rotation_transform(rot_z * Vec3d::UnitZ()) * rotation_matrix;

            rotation = Transformation(rotation_matrix).get_rotation();
        }

        obj_instance->set_rotation(rotation);
    }
}

Cut::Cut(const ModelObject *      object,
         int                      instance,
         const Transform3d &      cut_matrix,
         ModelObjectCutAttributes attributes /*= ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower | ModelObjectCutAttribute::CutToParts*/)
    : m_instance(instance), m_cut_matrix(cut_matrix), m_attributes(attributes)
{
    m_model = Model();
    if (object) m_model.add_object(*object);
}

void Cut::post_process(ModelObject *object, bool is_upper, ModelObjectPtrs &cut_object_ptrs, bool keep, bool place_on_cut, bool flip,  bool discard_half_cut)
{
    if (!object) return;

    if (keep && !object->volumes.empty()) {
        reset_instance_transformation(object, m_instance, m_cut_matrix, place_on_cut, flip, set_offset_for_two_part, discard_half_cut ? false :is_upper);
        cut_object_ptrs.push_back(object);
    } else
        m_model.objects.push_back(object); // will be deleted in m_model.clear_objects();
}

void Cut::post_process(ModelObject *upper, ModelObject *lower, ModelObjectPtrs &cut_object_ptrs)
{
    bool discard_half_cut = false;
    if (!(upper && lower)) {
        discard_half_cut = true;
    }
    post_process(upper,true, cut_object_ptrs, m_attributes.has(ModelObjectCutAttribute::KeepUpper), m_attributes.has(ModelObjectCutAttribute::PlaceOnCutUpper),
                 m_attributes.has(ModelObjectCutAttribute::FlipUpper), discard_half_cut);

    post_process(lower, false, cut_object_ptrs, m_attributes.has(ModelObjectCutAttribute::KeepLower), m_attributes.has(ModelObjectCutAttribute::PlaceOnCutLower),
                 m_attributes.has(ModelObjectCutAttribute::PlaceOnCutLower) || m_attributes.has(ModelObjectCutAttribute::FlipLower), discard_half_cut);
}

void Cut::finalize(const ModelObjectPtrs &objects)
{
    // clear model from temporarry objects
    m_model.clear_objects();

    // add to model result objects
    m_model.objects = objects;
}

const ModelObjectPtrs &Cut::perform_with_plane()
{
    if (!m_attributes.has(ModelObjectCutAttribute::KeepUpper) && !m_attributes.has(ModelObjectCutAttribute::KeepLower)) {
        m_model.clear_objects();
        return m_model.objects;
    }

    ModelObject *mo = m_model.objects.front();

    BOOST_LOG_TRIVIAL(trace) << "ModelObject::cut - start";

    // Clone the object to duplicate instances, materials etc.
    ModelObject *upper{nullptr};
    if (m_attributes.has(ModelObjectCutAttribute::KeepUpper))
        mo->clone_for_cut(&upper);

    ModelObject *lower{nullptr};
    if (m_attributes.has(ModelObjectCutAttribute::KeepLower) && !m_attributes.has(ModelObjectCutAttribute::CutToParts))
        mo->clone_for_cut(&lower);

    if (upper && lower &&!m_attributes.has(ModelObjectCutAttribute::CutToParts)) {
        upper->name = upper->name + "_A";
        lower->name = lower->name + "_B";
    }
    std::vector<ModelObject *> dowels;

    // Because transformations are going to be applied to meshes directly,
    // we reset transformation of all instances and volumes,
    // except for translation and Z-rotation on instances, which are preserved
    // in the transformation matrix and not applied to the mesh transform.

    const auto           instance_matrix    = mo->instances[m_instance]->get_transformation().get_matrix_no_offset();
    const Transformation cut_transformation = Transformation(m_cut_matrix);
    const Transform3d    inverse_cut_matrix = cut_transformation.get_rotation_matrix().inverse() * translation_transform(-1. * cut_transformation.get_offset());

    for (ModelVolume *volume : mo->volumes) {
        // NOTE: reset_extra_facets() must run AFTER process_*_cut so that
        // process_solid_part_cut can capture the source volume's MMU/seam/etc.
        // appearance snapshot before it is wiped. Resetting first caused
        // ColorCut appearance preservation to fail on tilted/vertical planar
        // cuts: the snapshot came back empty, cut-time reapply became a no-op,
        // and the GUI fallback then reapplied colors after place_on_cut had
        // already reoriented the cut parts, producing horizontally-projected
        // colors on a vertical cut.
        if (!volume->is_model_part()) {
            if (volume->cut_info.is_processed){
                process_modifier_cut(volume, instance_matrix, inverse_cut_matrix, m_attributes, upper, lower);
            }
            else{
                process_connector_cut(volume, instance_matrix, m_cut_matrix, m_attributes, upper, lower, dowels);
            }
        } else if (!volume->mesh().empty()) {
            process_solid_part_cut(volume, instance_matrix, m_cut_matrix, m_attributes, upper, lower);
        }

        volume->reset_extra_facets();
    }

    // Post-process cut parts
    if (m_attributes.has(ModelObjectCutAttribute::CutToParts) && upper->volumes.empty()) {
        m_model = Model();
        m_model.objects.push_back(upper);
        return m_model.objects;
    }

    ModelObjectPtrs cut_object_ptrs;

    if (m_attributes.has(ModelObjectCutAttribute::CutToParts) && !upper->volumes.empty()) {
        reset_instance_transformation(upper, m_instance, m_cut_matrix);
        cut_object_ptrs.push_back(upper);
    } else {
        // Delete all modifiers which are not intersecting with solid parts bounding box
        auto delete_extra_modifiers = [this](ModelObject *mo) {
            if (!mo) return;
            const BoundingBoxf3 obj_bb      = mo->instance_bounding_box(m_instance);
            const Transform3d   inst_matrix = mo->instances[m_instance]->get_transformation().get_matrix();

            for (int i = int(mo->volumes.size()) - 1; i >= 0; --i)
                if (const ModelVolume *vol = mo->volumes[i]; !vol->is_model_part() && !vol->is_cut_connector()) {
                    auto bb = vol->mesh().transformed_bounding_box(inst_matrix * vol->get_matrix());
                    if (!obj_bb.intersects(bb))
                        mo->delete_volume(i);
                }
        };

        post_process(upper, lower, cut_object_ptrs);
        delete_extra_modifiers(upper);
        delete_extra_modifiers(lower);

        if (m_attributes.has(ModelObjectCutAttribute::CreateDowels) && !dowels.empty()) {
            auto  object_box            = mo->bounding_box();//in world
            auto  origin_box_size       = object_box.size();
            Vec3d local_dowels_displace = Vec3d(0, -origin_box_size.y() * 0.7, 0);
            for (auto dowel : dowels) {
                reset_instance_transformation(dowel, m_instance, Transform3d::Identity(), false, false,false,false,true, local_dowels_displace);
                local_dowels_displace += dowel->full_raw_mesh_bounding_box().size().cwiseProduct(Vec3d(1.5, 0.0, 0.0));
                dowel->name += "-Dowel-" + dowel->volumes[0]->name;
                for (auto &volume : dowel->volumes) {
                    volume->set_offset(Vec3d::Zero());
                }
                cut_object_ptrs.push_back(dowel);
            }
        }
    }

    BOOST_LOG_TRIVIAL(trace) << "ModelObject::cut - end";

    finalize(cut_object_ptrs);

    return m_model.objects;
}

static void distribute_modifiers_from_object(ModelObject *from_obj, const int instance_idx, ModelObject *to_obj1, ModelObject *to_obj2)
{
    auto              obj1_bb     = to_obj1 ? to_obj1->instance_bounding_box(instance_idx) : BoundingBoxf3();
    auto              obj2_bb     = to_obj2 ? to_obj2->instance_bounding_box(instance_idx) : BoundingBoxf3();
    const Transform3d inst_matrix = from_obj->instances[instance_idx]->get_transformation().get_matrix();

    for (ModelVolume *vol : from_obj->volumes)
        if (!vol->is_model_part()) {
            // Don't add modifiers which are processed connectors
            if (vol->cut_info.is_connector && !vol->cut_info.is_processed)
                continue;

            // Modifiers are not cut, but we still need to add the instance transformation
            // to the modifier volume transformation to preserve their shape properly.
            const auto modifier_trafo = Transformation(from_obj->instances[instance_idx]->get_transformation().get_matrix_no_offset() * vol->get_matrix());

            auto bb = vol->mesh().transformed_bounding_box(inst_matrix * vol->get_matrix());
            // Don't add modifiers which are not intersecting with solid parts
            if (obj1_bb.intersects(bb))
                to_obj1->add_volume(*vol)->set_transformation(modifier_trafo);
            if (obj2_bb.intersects(bb))
                to_obj2->add_volume(*vol)->set_transformation(modifier_trafo);
        }
}

static void merge_solid_parts_inside_object(ModelObjectPtrs &objects)
{
    for (ModelObject *mo : objects) {
        TriangleMesh mesh;
        std::vector<const ModelVolume *> source_volumes;
        bool all_sources_from_lower = true;
        // Merge all SolidPart but not Connectors
        for (const ModelVolume *mv : mo->volumes) {
            if (mv->is_model_part() && !mv->is_cut_connector()) {
                source_volumes.push_back(mv);
                all_sources_from_lower &= !mv->is_from_upper();
                TriangleMesh m = mv->mesh();
                m.transform(mv->get_matrix());
                mesh.merge(m);
            }
        }
        if (!mesh.empty()) {
            ModelVolume *new_volume = mo->add_volume(mesh);
            new_volume->name        = mo->name;
            if (!source_volumes.empty())
                new_volume->cut_info.is_from_upper = !all_sources_from_lower;
            CCIBridge::merge_volume_appearance_data(source_volumes, *new_volume);
            merge_temp_cut_surface_data(source_volumes, *new_volume);
            // Delete all merged SolidPart but not Connectors
            for (int i = int(mo->volumes.size()) - 2; i >= 0; --i) {
                const ModelVolume *mv = mo->volumes[i];
                if (mv->is_model_part() && !mv->is_cut_connector()) mo->delete_volume(i);
            }
            // Ensuring that volumes start with solid parts for proper slicing
            mo->sort_volumes(true);
        }
    }
}

const ModelObjectPtrs &Cut::perform_by_contour(std::vector<Part> parts, int dowels_count)
{
    ModelObject *cut_mo = m_model.objects.front();

    // Clone the object to duplicate instances, materials etc.
    ModelObject *upper{nullptr};
    if (m_attributes.has(ModelObjectCutAttribute::KeepUpper))
        cut_mo->clone_for_cut(&upper);
    ModelObject *lower{nullptr};
    if (m_attributes.has(ModelObjectCutAttribute::KeepLower))
        cut_mo->clone_for_cut(&lower);

    if (upper && lower) {
        upper->name = upper->name + "_A";
        lower->name = lower->name + "_B";
    }

    const size_t cut_parts_cnt = parts.size();
    bool         has_modifiers = false;

    // Distribute SolidParts to the Upper/Lower object
    for (size_t id = 0; id < cut_parts_cnt; ++id) {
        if (parts[id].is_modifier)
            has_modifiers = true; // modifiers will be added later to the related parts
        else if (ModelObject *obj = (parts[id].selected ? upper : lower))
            obj->add_volume(*(cut_mo->volumes[id]));
    }

    if (has_modifiers) {
        // Distribute Modifiers to the Upper/Lower object
        distribute_modifiers_from_object(cut_mo, m_instance, upper, lower);
    }

    ModelObjectPtrs cut_object_ptrs;

    ModelVolumePtrs &volumes = cut_mo->volumes;
    if (volumes.size() == cut_parts_cnt) {
        // Means that object is cut without connectors

        // Just add Upper and Lower objects to cut_object_ptrs
        post_process(upper, lower, cut_object_ptrs);
        // Now merge all model parts together:
        merge_solid_parts_inside_object(cut_object_ptrs);

        finalize(cut_object_ptrs);
    } else if (volumes.size() > cut_parts_cnt) {
        // Means that object is cut with connectors

        // All volumes are distributed to Upper / Lower object,
        // So we don’t need them anymore
        for (size_t id = 0; id < cut_parts_cnt; id++) delete *(volumes.begin() + id);
        volumes.erase(volumes.begin(), volumes.begin() + cut_parts_cnt);

        // Perform cut just to get connectors
        Cut                    cut(cut_mo, m_instance, m_cut_matrix, m_attributes);
        const ModelObjectPtrs &cut_connectors_obj = cut.perform_with_plane();
        assert(dowels_count > 0 ? cut_connectors_obj.size() >= 3 : cut_connectors_obj.size() == 2);

        // Connectors from upper object
        for (const ModelVolume *volume : cut_connectors_obj[0]->volumes) upper->add_volume(*volume, volume->type());

        // Connectors from lower object
        for (const ModelVolume *volume : cut_connectors_obj[1]->volumes) lower->add_volume(*volume, volume->type());

        // Add Upper and Lower objects to cut_object_ptrs
        post_process(upper, lower, cut_object_ptrs);

        // Now merge all model parts together:
        merge_solid_parts_inside_object(cut_object_ptrs);

        finalize(cut_object_ptrs);
        // Add Dowel-connectors as separate objects to cut_object_ptrs
        if (cut_connectors_obj.size() >= 3)
            for (size_t id = 2; id < cut_connectors_obj.size(); id++)
                m_model.add_object(*cut_connectors_obj[id]);
    }
    return m_model.objects;
}

const ModelObjectPtrs &Cut::perform_with_groove(const Groove &groove, const Transform3d &rotation_m, bool keep_as_parts /* = false*/)
{
    ModelObject *cut_mo = m_model.objects.front();

    struct GrooveAppearanceSource
    {
        ColorCut::VolumeAppearanceSnapshot snapshot;
        TriangleMesh                       transformed_mesh;
        Vec3d                              centroid{Vec3d::Zero()};
    };

    // Clone the object to duplicate instances, materials etc.
    ModelObject *upper{nullptr};
    cut_mo->clone_for_cut(&upper);
    ModelObject *lower{nullptr};
    cut_mo->clone_for_cut(&lower);

    if (upper && lower) {
        upper->name = upper->name + "_A";
        lower->name = lower->name + "_B";
    }
    const double groove_half_depth = 0.5 * double(groove.depth);

    Model tmp_model_for_cut = Model();

    Model tmp_model = Model();
    tmp_model.add_object(*cut_mo);
    ModelObject *tmp_object = tmp_model.objects.front();

    std::vector<GrooveAppearanceSource> groove_sources;
    groove_sources.reserve(cut_mo->volumes.size());
    for (const ModelVolume *volume : cut_mo->volumes) {
        if (volume == nullptr || !volume->is_model_part() || volume->is_cut_connector() || volume->mesh().empty())
            continue;

        const std::optional<ColorCut::VolumeAppearanceSnapshot> snapshot = CCIBridge::capture_volume_appearance_snapshot(*volume);
        if (!snapshot.has_value())
            continue;

        GrooveAppearanceSource source;
        source.snapshot = *snapshot;
        source.transformed_mesh = CCIBridge::build_transformed_volume_mesh(*volume, m_instance);
        source.centroid = source.transformed_mesh.bounding_box().center().cast<double>();
        groove_sources.emplace_back(std::move(source));
    }

    auto reapply_original_groove_appearance = [this, &groove_sources](ModelObject *object) {
        if (object == nullptr || groove_sources.empty())
            return;

        for (ModelVolume *volume : object->volumes) {
            if (volume == nullptr || !volume->is_model_part() || volume->is_cut_connector() || volume->mesh().empty())
                continue;

            const GrooveAppearanceSource *best_source = nullptr;
            if (groove_sources.size() == 1) {
                best_source = &groove_sources.front();
            } else {
                const TriangleMesh transformed_target_mesh = CCIBridge::build_transformed_volume_mesh(*volume, m_instance < static_cast<int>(object->instances.size()) ? m_instance : 0);
                const Vec3d target_centroid = transformed_target_mesh.bounding_box().center().cast<double>();
                double best_distance = std::numeric_limits<double>::max();
                for (const GrooveAppearanceSource &source : groove_sources) {
                    const double distance = (source.centroid - target_centroid).squaredNorm();
                    if (distance < best_distance) {
                        best_distance = distance;
                        best_source = &source;
                    }
                }
            }

            if (best_source != nullptr)
                CCIBridge::reapply_after_mesh_repair(best_source->snapshot, best_source->transformed_mesh, *volume);
        }
    };

    auto add_volumes_from_cut = [](ModelObject *object, const ModelObjectCutAttribute attribute, const Model &tmp_model_for_cut) {
        const auto &volumes = tmp_model_for_cut.objects.front()->volumes;
        for (const ModelVolume *volume : volumes)
            if (volume->is_model_part()) {
                if ((attribute == ModelObjectCutAttribute::KeepUpper && volume->is_from_upper()) ||
                    (attribute != ModelObjectCutAttribute::KeepUpper && !volume->is_from_upper())) {
                    ModelVolume *new_vol = object->add_volume(*volume);
                    new_vol->cut_info.is_from_upper = volume->is_from_upper();
                }
            }
    };

    auto cut = [this, add_volumes_from_cut, &reapply_original_groove_appearance](ModelObject *object, const Transform3d &cut_matrix, const ModelObjectCutAttribute add_volumes_attribute, Model &tmp_model_for_cut) {
        Cut cut(object, m_instance, cut_matrix);

        tmp_model_for_cut = Model();
        tmp_model_for_cut.add_object(*cut.perform_with_plane().front());
        assert(!tmp_model_for_cut.objects.empty());

        reapply_original_groove_appearance(tmp_model_for_cut.objects.front());

        object->clear_volumes();
        add_volumes_from_cut(object, add_volumes_attribute, tmp_model_for_cut);
        reset_instance_transformation(object, m_instance);
    };

    // cut by upper plane

    const Transform3d cut_matrix_upper = translation_transform(rotation_m * (groove_half_depth * Vec3d::UnitZ())) * m_cut_matrix;
    {
        cut(tmp_object, cut_matrix_upper, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut);
        add_volumes_from_cut(upper, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut);
    }

    // cut by lower plane

    const Transform3d cut_matrix_lower = translation_transform(rotation_m * (-groove_half_depth * Vec3d::UnitZ())) * m_cut_matrix;
    {
        cut(tmp_object, cut_matrix_lower, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut);
        add_volumes_from_cut(lower, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut);
    }

    // cut middle part with 2 angles and add parts to related upper/lower objects

    const double h_side_shift = 0.5 * double(groove.width + groove.depth / tan(groove.flaps_angle));

    // cut by angle1 plane
    {
        const Transform3d cut_matrix_angle1 = translation_transform(rotation_m * (-h_side_shift * Vec3d::UnitX())) * m_cut_matrix *
                                              rotation_transform(Vec3d(0, -groove.flaps_angle, -groove.angle));

        cut(tmp_object, cut_matrix_angle1, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut);
        add_volumes_from_cut(lower, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut);
    }

    // cut by angle2 plane
    {
        const Transform3d cut_matrix_angle2 = translation_transform(rotation_m * (h_side_shift * Vec3d::UnitX())) * m_cut_matrix *
                                              rotation_transform(Vec3d(0, groove.flaps_angle, groove.angle));

        cut(tmp_object, cut_matrix_angle2, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut);
        add_volumes_from_cut(lower, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut);
    }

    // apply tolerance to the middle part
    {
        const double h_groove_shift_tolerance = groove_half_depth - (double) groove.depth_tolerance;

        const Transform3d cut_matrix_lower_tolerance = translation_transform(rotation_m * (-h_groove_shift_tolerance * Vec3d::UnitZ())) * m_cut_matrix;
        cut(tmp_object, cut_matrix_lower_tolerance, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut);

        const double h_side_shift_tolerance = h_side_shift - 0.5 * double(groove.width_tolerance);

        const Transform3d cut_matrix_angle1_tolerance = translation_transform(rotation_m * (-h_side_shift_tolerance * Vec3d::UnitX())) * m_cut_matrix *
                                                        rotation_transform(Vec3d(0, -groove.flaps_angle, -groove.angle));
        cut(tmp_object, cut_matrix_angle1_tolerance, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut);

        const Transform3d cut_matrix_angle2_tolerance = translation_transform(rotation_m * (h_side_shift_tolerance * Vec3d::UnitX())) * m_cut_matrix *
                                                        rotation_transform(Vec3d(0, groove.flaps_angle, groove.angle));
        cut(tmp_object, cut_matrix_angle2_tolerance, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut);
    }

    // this part can be added to the upper object now
    add_volumes_from_cut(upper, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut);

    ModelObjectPtrs cut_object_ptrs;

    if (keep_as_parts) {
        // add volumes from lower object to the upper, but mark them as a lower
        const auto &volumes = lower->volumes;
        for (const ModelVolume *volume : volumes) {
            ModelVolume *new_vol            = upper->add_volume(*volume);
            new_vol->cut_info.is_from_upper = false;
        }

        // add modifiers
        for (const ModelVolume *volume : cut_mo->volumes)
            if (!volume->is_model_part()) {
                // Modifiers are not cut, but we still need to add the instance transformation
                // to the modifier volume transformation to preserve their shape properly.
                const auto modifier_trafo = Transformation(cut_mo->instances[m_instance]->get_transformation().get_matrix_no_offset() * volume->get_matrix());
                upper->add_volume(*volume)->set_transformation(modifier_trafo);
            }

        cut_object_ptrs.push_back(upper);

        // add lower object to the cut_object_ptrs just to correct delete it from the Model destructor and avoid memory leaks
        cut_object_ptrs.push_back(lower);
    } else {
        reset_instance_transformation(upper, m_instance, m_cut_matrix);
        reset_instance_transformation(lower, m_instance, m_cut_matrix);

        // Add modifiers if object has any
        // Note: make it after all transformations are reset for upper/lower object
        for (const ModelVolume *volume : cut_mo->volumes)
            if (!volume->is_model_part()) {
                distribute_modifiers_from_object(cut_mo, m_instance, upper, lower);
                break;
            }

        assert(!upper->volumes.empty() && !lower->volumes.empty());

        // Add Upper and Lower parts to cut_object_ptrs

        post_process(upper, lower, cut_object_ptrs);

        // Now merge all model parts together:
        merge_solid_parts_inside_object(cut_object_ptrs);
    }

    finalize(cut_object_ptrs);

    return m_model.objects;
}

} // namespace Slic3r

