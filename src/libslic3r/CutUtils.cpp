#include "CutUtils.hpp"
#include "Geometry.hpp"
#include "libslic3r.h"
#include "Model.hpp"
#include "PaintReproject.hpp"
#include "TriangleMeshSlicer.hpp"
#include "TriangleSelector.hpp"
#include "ObjectID.hpp"
#include <boost/log/trivial.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Slic3r {

using namespace Geometry;

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

// Already extracted for i18n via GLGizmoAdvancedCut.cpp; CutUtils is not in list.txt,
// so pass the English marker string only (no L() here).
static const char *const CutStageCutting = "Cutting model object";

// Returns false when the paint reprojection was canceled; the caller has to unwind.
static bool add_cut_volume(TriangleMesh &     mesh,
                           ModelObject *      object,
                           const ModelVolume *src_volume,
                           const Transform3d &cut_matrix,
                           const std::string &suffix = {},
                           ModelVolumeType    type   = ModelVolumeType::MODEL_PART,
                           const std::vector<int>* src_faces = nullptr,
                           const Transform3d* destination_to_source = nullptr,
                           const CutProgressRange &progress = {})
{
    if (mesh.empty())
        return true;

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

    assert(src_faces == nullptr || destination_to_source != nullptr);
    if (src_faces && src_volume && destination_to_source) {
        // add_volume() recenters the mesh (center_geometry_after_creation) by
        // -mesh_offset, but destination_to_source was computed for the
        // un-centered mesh. Compose the recenter shift so a centered vertex
        // v_c maps back to its true source position: source = dts * (v_c + shift).
        // Without this both cut halves collapse onto the same centered source
        // band and inherit identical paint.
        const Transform3d dts_centered =
            (*destination_to_source) * Eigen::Translation3d(vol->source.mesh_offset);
        if (!reproject_paint(*src_volume, *vol, *src_faces, dts_centered,
                             [&progress](int percent, const char *message) { progress.report(percent, message); },
                             [&progress]() { return progress.canceled(); }))
            return false;
    }
    return true;
}

// Returns false when the cut was canceled; the caller has to unwind.
static bool process_volume_cut(ModelVolume *            volume,
                               const Transform3d &      instance_matrix,
                               const Transform3d &      cut_matrix,
                               ModelObjectCutAttributes attributes,
                               TriangleMesh &           upper_mesh,
                               TriangleMesh &           lower_mesh,
                               std::vector<int>*        upper_src_faces = nullptr,
                               std::vector<int>*        lower_src_faces = nullptr,
                               Transform3d*             destination_to_source = nullptr,
                               const CutProgressRange & progress = {})
{
    const auto volume_matrix = volume->get_matrix();

    const Transformation cut_transformation = Transformation(cut_matrix);

    const Transform3d invert_cut_matrix = cut_transformation.get_rotation_matrix().inverse() * translation_transform(-1 * cut_transformation.get_offset());

    // Transform the mesh by the combined transformation matrix.
    // Flip the triangles in case the composite transformation is left handed.
    const Transform3d source_to_cut = invert_cut_matrix * instance_matrix * volume_matrix;
    if (destination_to_source)
        *destination_to_source = (cut_matrix * source_to_cut).inverse();

    TriangleMesh mesh(volume->mesh());
    mesh.transform(source_to_cut, true);

    indexed_triangle_set upper_its, lower_its;
    // Slicing dominates this stage; leave the tail of the range to building the two meshes.
    // Always refresh the stage label: a prior volume / nested groove cut may have left a
    // "Restoring ..." message in the shared progress callback, and a null message would stick.
    progress.report(0., CutStageCutting);
    const CutProgressRange slice_progress = progress.sub(0., 90.);
    cut_mesh(mesh.its, 0.0f, &upper_its, &lower_its, true, upper_src_faces, lower_src_faces,
             [&slice_progress](int percent) { slice_progress.report(percent, CutStageCutting); },
             [&slice_progress]() { return slice_progress.canceled(); });
    if (progress.canceled())
        return false;
    if (attributes.has(ModelObjectCutAttribute::KeepUpper))
        upper_mesh = TriangleMesh(upper_its);
    if (attributes.has(ModelObjectCutAttribute::KeepLower))
        lower_mesh = TriangleMesh(lower_its);
    progress.report(100., CutStageCutting);
    return true;
}

// Returns false when the cut was canceled; the caller has to unwind.
static bool process_connector_cut(ModelVolume *               volume,
                                  const Transform3d &         instance_matrix,
                                  const Transform3d &         cut_matrix,
                                  ModelObjectCutAttributes    attributes,
                                  ModelObject *               upper,
                                  ModelObject *               lower,
                                  std::vector<ModelObject *> &dowels,
                                  const CutProgressRange &    progress = {})
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
        // A dowel carries no paint, so the whole slice goes to the geometry.
        if (!process_volume_cut(volume, Transform3d::Identity(), cut_matrix, attributes, upper_mesh, lower_mesh,
                                nullptr, nullptr, nullptr, progress))
            return false;

        // add small Z offset to better preview
        upper_mesh.translate((-0.05 * Vec3d::UnitZ()).cast<float>());
        lower_mesh.translate((0.05 * Vec3d::UnitZ()).cast<float>());

        // Add cut parts to the related objects. No paint is carried here, so neither call can
        // report a cancellation.
        add_cut_volume(upper_mesh, upper, volume, cut_matrix, "_A", volume->type());
        add_cut_volume(lower_mesh, lower, volume, cut_matrix, "_B", volume->type());
    }
    progress.report(100.);
    return true;
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

// Returns false when the cut or the paint reprojection was canceled; the caller has to unwind.
static bool process_solid_part_cut(ModelVolume *            volume,
                                   const Transform3d &      instance_matrix,
                                   const Transform3d &      cut_matrix,
                                   ModelObjectCutAttributes attributes,
                                   ModelObject *            upper,
                                   ModelObject *            lower,
                                   const CutProgressRange & progress = {})
{
    // Perform cut
    TriangleMesh upper_mesh, lower_mesh;
    std::vector<int> upper_src, lower_src;
    Transform3d destination_to_source;
    // The geometry owns the first 70% of this volume's slice, restoring its paint the last 30%,
    // shared by the two halves whenever both are produced.
    if (!process_volume_cut(volume, instance_matrix, cut_matrix, attributes, upper_mesh, lower_mesh,
                            &upper_src, &lower_src, &destination_to_source, progress.sub(0., 70.)))
        return false;

    const CutProgressRange paint       = progress.sub(70., 100.);
    const bool             both_halves = !upper_mesh.empty() && !lower_mesh.empty();
    const CutProgressRange upper_paint = both_halves ? paint.sub(0., 50.) : paint;
    const CutProgressRange lower_paint = both_halves ? paint.sub(50., 100.) : paint;

    // Add required cut parts to the objects

    if (attributes.has(ModelObjectCutAttribute::CutToParts)) {
        if (!add_cut_volume(upper_mesh, upper, volume, cut_matrix, "_A", ModelVolumeType::MODEL_PART, &upper_src, &destination_to_source, upper_paint))
            return false;
        if (!lower_mesh.empty()) {
            if (!add_cut_volume(lower_mesh, upper, volume, cut_matrix, "_B", ModelVolumeType::MODEL_PART, &lower_src, &destination_to_source, lower_paint))
                return false;
            upper->volumes.back()->cut_info.is_from_upper = false;
        }
        progress.report(100.);
        return true;
    }

    if (attributes.has(ModelObjectCutAttribute::KeepUpper)) {
        if (!add_cut_volume(upper_mesh, upper, volume, cut_matrix, {}, ModelVolumeType::MODEL_PART, &upper_src, &destination_to_source, upper_paint))
            return false;
    }

    if (attributes.has(ModelObjectCutAttribute::KeepLower) && !lower_mesh.empty()) {
        if (!add_cut_volume(lower_mesh, lower, volume, cut_matrix, {}, ModelVolumeType::MODEL_PART, &lower_src, &destination_to_source, lower_paint))
            return false;
    }
    progress.report(100.);
    return true;
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

void Cut::set_progress(CutProgressCallback progress, CutCancelCallback cancel)
{
    m_progress = CutProgressRange(std::move(progress), std::move(cancel));
}

const ModelObjectPtrs &Cut::abandon(ModelObject *upper, ModelObject *lower, const std::vector<ModelObject *> &dowels)
{
    m_canceled = true;
    // Same ownership transfer post_process() uses for a half that is not kept.
    if (upper)
        m_model.objects.push_back(upper);
    if (lower)
        m_model.objects.push_back(lower);
    for (ModelObject *dowel : dowels)
        m_model.objects.push_back(dowel);
    m_model.clear_objects();
    return m_model.objects;
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

    // Slices are weighted by face count rather than handed out evenly: a modifier volume costs
    // nothing while a dense solid part dominates the run, and an even split makes the bar crawl
    // through the expensive volume and then jump. The floor of one keeps empty volumes from
    // collapsing to a zero-width slice.
    m_progress.report(0., CutStageCutting);
    const size_t volume_count = mo->volumes.size();
    std::vector<double> volume_weight_prefix(volume_count + 1, 0.);
    for (size_t volume_idx = 0; volume_idx < volume_count; ++ volume_idx)
        volume_weight_prefix[volume_idx + 1] =
            volume_weight_prefix[volume_idx] +
            std::max<double>(1., double(mo->volumes[volume_idx]->mesh().its.indices.size()));
    const double volume_weight_total = std::max(1., volume_weight_prefix.back());
    for (size_t volume_idx = 0; volume_idx < volume_count; ++ volume_idx) {
        if (m_progress.canceled())
            return this->abandon(upper, lower, dowels);
        ModelVolume *volume = mo->volumes[volume_idx];
        const CutProgressRange volume_progress = m_progress.sub(volume_weight_prefix[volume_idx] * 100. / volume_weight_total,
                                                                volume_weight_prefix[volume_idx + 1] * 100. / volume_weight_total);
        if (!volume->is_model_part()) {
            if (volume->cut_info.is_processed){
                process_modifier_cut(volume, instance_matrix, inverse_cut_matrix, m_attributes, upper, lower);
            }
            else{
                if (!process_connector_cut(volume, instance_matrix, m_cut_matrix, m_attributes, upper, lower, dowels, volume_progress))
                    return this->abandon(upper, lower, dowels);
            }
        } else if (!volume->mesh().empty()) {
            if (!process_solid_part_cut(volume, instance_matrix, m_cut_matrix, m_attributes, upper, lower, volume_progress))
                return this->abandon(upper, lower, dowels);
        }
        volume_progress.report(100.);
    }
    if (m_progress.canceled())
        return this->abandon(upper, lower, dowels);

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

// Puts the pieces a cut left behind back together, one volume per source part. Pieces are
// grouped by ModelVolume::CutInfo::source_part_idx when source_volumes is provided (groove
// and contour). Without source_volumes there is nothing to group by, so every piece goes
// into a single group, independent of any source_part_idx the incoming pieces happen to carry.
// source_volumes, when given, is the part list of the object that was cut and provides the
// config, material and name for the merged volumes. Without it the merged volume keeps an
// empty config, so its part falls back to the object level filament.
static void merge_solid_parts_inside_object(ModelObjectPtrs &objects, const ModelVolumePtrs *source_volumes = nullptr)
{
    // A source object with a single part keeps naming its merged volume after the object, as
    // it always has. With several parts each merged volume takes the name of the part it came
    // from, and that must not depend on how many parts survived in this particular half.
    const size_t source_parts_cnt = source_volumes == nullptr ? 0 :
        size_t(std::count_if(source_volumes->begin(), source_volumes->end(),
                             [](const ModelVolume *mv) { return mv->is_model_part() && !mv->is_cut_connector(); }));

    for (ModelObject *mo : objects) {
        // Group the pieces to merge, in order of first appearance so that the merged volumes
        // keep the order of the parts they came from.
        std::vector<int>             group_source_idxs;
        std::vector<ModelVolumePtrs> groups;
        for (ModelVolume *mv : mo->volumes) {
            if (!mv->is_model_part() || mv->is_cut_connector())
                continue;
            // Without source_volumes there is nothing to group by, so everything goes into a
            // single group, whatever source_part_idx the incoming pieces happen to carry.
            const int  group_key = source_volumes != nullptr ? mv->cut_info.source_part_idx : -1;
            const auto it        = std::find(group_source_idxs.begin(), group_source_idxs.end(), group_key);
            if (it == group_source_idxs.end()) {
                group_source_idxs.emplace_back(group_key);
                groups.emplace_back(ModelVolumePtrs{mv});
            } else
                groups[it - group_source_idxs.begin()].emplace_back(mv);
        }
        if (groups.empty())
            continue;

        const size_t volumes_cnt_before_merge = mo->volumes.size();
        bool         merged_any               = false;

        for (size_t group_idx = 0; group_idx < groups.size(); ++group_idx) {
            TriangleMesh             mesh;
            std::vector<std::string> merged_supported, merged_seam, merged_mmu, merged_fuzzy;
            // Merge all SolidPart of this group but not Connectors
            for (const ModelVolume *mv : groups[group_idx]) {
                const size_t face_count = mv->mesh().its.indices.size();
                for (size_t face_idx = 0; face_idx < face_count; ++face_idx) {
                    merged_supported.emplace_back(mv->supported_facets.get_triangle_as_string(int(face_idx)));
                    merged_seam.emplace_back(mv->seam_facets.get_triangle_as_string(int(face_idx)));
                    merged_mmu.emplace_back(mv->mmu_segmentation_facets.get_triangle_as_string(int(face_idx)));
                    merged_fuzzy.emplace_back(mv->fuzzy_skin_facets.get_triangle_as_string(int(face_idx)));
                }
                TriangleMesh m = mv->mesh();
                m.transform(mv->get_matrix());
                mesh.merge(m);
            }
            if (mesh.empty())
                continue;

            ModelVolume *new_volume = mo->add_volume(mesh);

            // Carry the identity of the source part over, the same way the planar cut does in
            // add_cut_volume(). The per part config holds the filament and the region
            // overrides, so dropping it here would recolor the part.
            const int          source_idx = group_source_idxs[group_idx];
            const ModelVolume *src_volume = (source_volumes != nullptr && source_idx >= 0 && size_t(source_idx) < source_volumes->size()) ?
                                                (*source_volumes)[source_idx] :
                                                nullptr;
            if (src_volume != nullptr) {
                // Don't copy the config's ID.
                new_volume->config.assign_config(src_volume->config);
                if (const ModelMaterial *material = src_volume->material(); material != nullptr)
                    new_volume->set_material(src_volume->material_id(), *material);
            }
            new_volume->name = (source_parts_cnt <= 1 || src_volume == nullptr) ? mo->name : src_volume->name;

            assert(merged_supported.size() == mesh.its.indices.size());
            const size_t annotation_face_count = std::min(merged_supported.size(), mesh.its.indices.size());
            for (size_t face_idx = 0; face_idx < annotation_face_count; ++face_idx) {
                if (!merged_supported[face_idx].empty())
                    new_volume->supported_facets.set_triangle_from_string(int(face_idx), merged_supported[face_idx]);
                if (!merged_seam[face_idx].empty())
                    new_volume->seam_facets.set_triangle_from_string(int(face_idx), merged_seam[face_idx]);
                if (!merged_mmu[face_idx].empty())
                    new_volume->mmu_segmentation_facets.set_triangle_from_string(int(face_idx), merged_mmu[face_idx]);
                if (!merged_fuzzy[face_idx].empty())
                    new_volume->fuzzy_skin_facets.set_triangle_from_string(int(face_idx), merged_fuzzy[face_idx]);
            }
            merged_any = true;
        }

        if (!merged_any)
            continue;

        // Delete all merged SolidPart but not Connectors. The merged volumes were appended
        // past volumes_cnt_before_merge, so walking the leading range backwards leaves them
        // untouched.
        for (int i = int(volumes_cnt_before_merge) - 1; i >= 0; --i) {
            const ModelVolume *mv = mo->volumes[i];
            if (mv->is_model_part() && !mv->is_cut_connector()) mo->delete_volume(i);
        }
        // Ensuring that volumes start with solid parts for proper slicing
        mo->sort_volumes(true);
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

    m_progress.report(0., CutStageCutting);

    const size_t cut_parts_cnt = parts.size();
    bool         has_modifiers = false;

    // Same provenance stamp as perform_with_groove: parts[i] maps to cut_mo->volumes[i], and
    // add_volume() copies cut_info so merge_solid_parts_inside_object() can merge within each
    // source part instead of collapsing every solid part on a side into one volume.
    for (size_t i = 0; i < cut_parts_cnt; ++i)
        cut_mo->volumes[i]->cut_info.source_part_idx = int(i);

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

    // The parts themselves were already cut by the caller; what is left here is distributing them
    // and merging. Only the connector branch does real cutting, so it owns the bulk of the range.
    m_progress.report(10., CutStageCutting);

    ModelVolumePtrs &volumes = cut_mo->volumes;
    if (volumes.size() == cut_parts_cnt) {
        // Means that object is cut without connectors

        // Just add Upper and Lower objects to cut_object_ptrs
        post_process(upper, lower, cut_object_ptrs);
        // Merge within each source part; cut_mo still owns the source volumes here.
        merge_solid_parts_inside_object(cut_object_ptrs, &cut_mo->volumes);
        m_progress.report(90., CutStageCutting);

        finalize(cut_object_ptrs);
    } else if (volumes.size() > cut_parts_cnt) {
        // Means that object is cut with connectors

        // Take ownership of the distributed source parts so merge can still look them up
        // after they are removed from cut_mo (connectors stay behind for perform_with_plane).
        ModelVolumePtrs source_parts(volumes.begin(), volumes.begin() + cut_parts_cnt);
        volumes.erase(volumes.begin(), volumes.begin() + cut_parts_cnt);

        // Perform cut just to get connectors
        Cut                    cut(cut_mo, m_instance, m_cut_matrix, m_attributes);
        // Leave the tail of the range to the merge below, which is not free on dense parts.
        cut.set_progress(m_progress.sub(10., 80.));
        const ModelObjectPtrs &cut_connectors_obj = cut.perform_with_plane();
        if (cut.was_canceled()) {
            for (ModelVolume *v : source_parts)
                delete v;
            return this->abandon(upper, lower);
        }
        assert(dowels_count > 0 ? cut_connectors_obj.size() >= 3 : cut_connectors_obj.size() == 2);

        // Connectors from upper object
        for (const ModelVolume *volume : cut_connectors_obj[0]->volumes) upper->add_volume(*volume, volume->type());

        // Connectors from lower object
        for (const ModelVolume *volume : cut_connectors_obj[1]->volumes) lower->add_volume(*volume, volume->type());

        // Add Upper and Lower objects to cut_object_ptrs
        post_process(upper, lower, cut_object_ptrs);

        merge_solid_parts_inside_object(cut_object_ptrs, &source_parts);
        for (ModelVolume *v : source_parts)
            delete v;
        m_progress.report(90., CutStageCutting);

        finalize(cut_object_ptrs);
        // Add Dowel-connectors as separate objects to cut_object_ptrs
        if (cut_connectors_obj.size() >= 3)
            for (size_t id = 2; id < cut_connectors_obj.size(); id++)
                m_model.add_object(*cut_connectors_obj[id]);
    }
    m_progress.report(100.);
    return m_model.objects;
}

const ModelObjectPtrs &Cut::perform_with_groove(const Groove &groove, const Transform3d &rotation_m, bool keep_as_parts /* = false*/)
{
    ModelObject *cut_mo = m_model.objects.front();

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

    // A groove is carved by applying several cut planes in a row, so every part ends up
    // split into a handful of pieces. Tag each part with its index here (tmp_object is a
    // clone of cut_mo, so the indices match) and let cut_info travel along with the
    // pieces, so merge_solid_parts_inside_object() can put the pieces of one part back
    // together instead of collapsing all parts into a single volume.
    for (size_t i = 0; i < tmp_object->volumes.size(); ++i)
        tmp_object->volumes[i]->cut_info.source_part_idx = int(i);

    auto add_volumes_from_cut = [](ModelObject *object, const ModelObjectCutAttribute attribute, const Model &tmp_model_for_cut) {
        const auto &volumes = tmp_model_for_cut.objects.front()->volumes;
        for (const ModelVolume *volume : volumes)
            if (volume->is_model_part()) {
                if ((attribute == ModelObjectCutAttribute::KeepUpper && volume->is_from_upper()) ||
                    (attribute != ModelObjectCutAttribute::KeepUpper && !volume->is_from_upper())) {
                    ModelVolume *new_vol = object->add_volume(*volume);
                    new_vol->reset_from_upper();
                }
            }
    };

    // A groove is carved by seven plane cuts in a row; hand each an equal slice of the range.
    // static so the lambda below can read it without capturing it.
    int                    groove_cut_index = 0;
    static constexpr int   GrooveCutCount   = 7;
    auto groove_step = [this, &groove_cut_index]() {
        const double from = double(groove_cut_index) * 100. / double(GrooveCutCount);
        const double to   = double(groove_cut_index + 1) * 100. / double(GrooveCutCount);
        ++ groove_cut_index;
        return m_progress.sub(from, to);
    };

    auto cut = [this, add_volumes_from_cut](ModelObject *object, const Transform3d &cut_matrix, const ModelObjectCutAttribute add_volumes_attribute,
                                           Model &tmp_model_for_cut, const CutProgressRange &progress) {
        Cut cut(object, m_instance, cut_matrix);
        cut.set_progress(progress);

        tmp_model_for_cut = Model();
        const ModelObjectPtrs &cut_result = cut.perform_with_plane();
        if (cut.was_canceled())
            return false;
        tmp_model_for_cut.add_object(*cut_result.front());
        assert(!tmp_model_for_cut.objects.empty());

        object->clear_volumes();
        add_volumes_from_cut(object, add_volumes_attribute, tmp_model_for_cut);
        reset_instance_transformation(object, m_instance);
        return true;
    };

    // cut by upper plane

    const Transform3d cut_matrix_upper = translation_transform(rotation_m * (groove_half_depth * Vec3d::UnitZ())) * m_cut_matrix;
    {
        if (!cut(tmp_object, cut_matrix_upper, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut, groove_step()))
            return this->abandon(upper, lower);
        add_volumes_from_cut(upper, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut);
    }

    // cut by lower plane

    const Transform3d cut_matrix_lower = translation_transform(rotation_m * (-groove_half_depth * Vec3d::UnitZ())) * m_cut_matrix;
    {
        if (!cut(tmp_object, cut_matrix_lower, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut, groove_step()))
            return this->abandon(upper, lower);
        add_volumes_from_cut(lower, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut);
    }

    // cut middle part with 2 angles and add parts to related upper/lower objects

    const double h_side_shift = 0.5 * double(groove.width + groove.depth / tan(groove.flaps_angle));

    // cut by angle1 plane
    {
        const Transform3d cut_matrix_angle1 = translation_transform(rotation_m * (-h_side_shift * Vec3d::UnitX())) * m_cut_matrix *
                                              rotation_transform(Vec3d(0, -groove.flaps_angle, -groove.angle));

        if (!cut(tmp_object, cut_matrix_angle1, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut, groove_step()))
            return this->abandon(upper, lower);
        add_volumes_from_cut(lower, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut);
    }

    // cut by angle2 plane
    {
        const Transform3d cut_matrix_angle2 = translation_transform(rotation_m * (h_side_shift * Vec3d::UnitX())) * m_cut_matrix *
                                              rotation_transform(Vec3d(0, groove.flaps_angle, groove.angle));

        if (!cut(tmp_object, cut_matrix_angle2, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut, groove_step()))
            return this->abandon(upper, lower);
        add_volumes_from_cut(lower, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut);
    }

    // apply tolerance to the middle part
    {
        const double h_groove_shift_tolerance = groove_half_depth - (double) groove.depth_tolerance;

        const Transform3d cut_matrix_lower_tolerance = translation_transform(rotation_m * (-h_groove_shift_tolerance * Vec3d::UnitZ())) * m_cut_matrix;
        if (!cut(tmp_object, cut_matrix_lower_tolerance, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut, groove_step()))
            return this->abandon(upper, lower);

        const double h_side_shift_tolerance = h_side_shift - 0.5 * double(groove.width_tolerance);

        const Transform3d cut_matrix_angle1_tolerance = translation_transform(rotation_m * (-h_side_shift_tolerance * Vec3d::UnitX())) * m_cut_matrix *
                                                        rotation_transform(Vec3d(0, -groove.flaps_angle, -groove.angle));
        if (!cut(tmp_object, cut_matrix_angle1_tolerance, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut, groove_step()))
            return this->abandon(upper, lower);

        const Transform3d cut_matrix_angle2_tolerance = translation_transform(rotation_m * (h_side_shift_tolerance * Vec3d::UnitX())) * m_cut_matrix *
                                                        rotation_transform(Vec3d(0, groove.flaps_angle, groove.angle));
        if (!cut(tmp_object, cut_matrix_angle2_tolerance, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut, groove_step()))
            return this->abandon(upper, lower);
    }
    // Adding or removing a cut above without updating GrooveCutCount would silently skew the
    // whole range, so tie the constant to the number of steps actually taken.
    assert(groove_cut_index == GrooveCutCount);

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

        // Now merge the pieces of every model part back together. cut_mo is still alive here,
        // finalize() below is what drops it.
        merge_solid_parts_inside_object(cut_object_ptrs, &cut_mo->volumes);
    }

    finalize(cut_object_ptrs);

    m_progress.report(100.);
    return m_model.objects;
}

} // namespace Slic3r

