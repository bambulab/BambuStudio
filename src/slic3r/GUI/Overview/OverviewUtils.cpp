#include "OverviewUtils.hpp"

#include "libslic3r/Model.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include "../GLCanvas3D.hpp"
#include "../GUI_App.hpp"
#include "../Plater.hpp"
#include "../PartPlate.hpp"

#include <wx/event.h>

#include <boost/log/trivial.hpp>
#include <boost/format.hpp>

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace GUI {

namespace {

struct AssembleTargetRef
{
    ModelObject   *object{nullptr};
    ModelInstance *instance{nullptr};
    ModelVolume   *volume{nullptr};
    int            object_idx{-1};
    int            instance_idx{-1};
    const char    *how{"none"};
};

bool volume_matches_guid(const ModelVolume *mv, const std::string &guid)
{
    if (mv == nullptr || guid.empty())
        return false;
    return mv->assembly_src_guid() == guid || mv->part_guid() == guid;
}

// Resolve the ModelObject / ModelInstance / ModelVolume that owns the assemble pose of an isolated
// volume. IsolatedVolumeInfo::obj_idx comes from a 3D-view GLVolume and therefore addresses the
// prepare model, while `model` is the independent assembly model whose object list may have a
// different size and order (rebuilt from assembly_model.json). Only the recorded ObjectIDs / part
// GUID are stable across the two; the index is kept as a last-resort fallback for old cache entries.
AssembleTargetRef resolve_assemble_target(Model &model, const GLCanvas3D::IsolatedVolumeInfo &iv)
{
    AssembleTargetRef ref;

    if (iv.target.object_id != 0) {
        for (int oi = 0; oi < (int) model.objects.size(); ++oi) {
            ModelObject *mo = model.objects[oi];
            if (mo != nullptr && mo->id().id == iv.target.object_id) {
                ref.object     = mo;
                ref.object_idx = oi;
                ref.how        = "object_id";
                break;
            }
        }
    }

    if (ref.object == nullptr && !iv.target.part_guid.empty()) {
        for (int oi = 0; oi < (int) model.objects.size() && ref.object == nullptr; ++oi) {
            ModelObject *mo = model.objects[oi];
            if (mo == nullptr)
                continue;
            for (ModelVolume *mv : mo->volumes) {
                if (mv != nullptr && mv->is_model_part() && volume_matches_guid(mv, iv.target.part_guid)) {
                    ref.object     = mo;
                    ref.object_idx = oi;
                    ref.volume     = mv;
                    ref.how        = "part_guid";
                    break;
                }
            }
        }
    }

    if (ref.object == nullptr && iv.obj_idx >= 0 && iv.obj_idx < (int) model.objects.size()) {
        ref.object     = model.objects[iv.obj_idx];
        ref.object_idx = iv.obj_idx;
        ref.how        = "obj_idx";
    }

    if (ref.object == nullptr)
        return ref;

    if (ref.volume == nullptr && !iv.target.part_guid.empty()) {
        for (ModelVolume *mv : ref.object->volumes) {
            if (mv != nullptr && mv->is_model_part() && volume_matches_guid(mv, iv.target.part_guid)) {
                ref.volume = mv;
                break;
            }
        }
    }

    if (iv.target.instance_id != 0) {
        for (int ii = 0; ii < (int) ref.object->instances.size(); ++ii) {
            ModelInstance *mi = ref.object->instances[ii];
            if (mi != nullptr && mi->id().id == iv.target.instance_id) {
                ref.instance     = mi;
                ref.instance_idx = ii;
                break;
            }
        }
    }
    if (ref.instance == nullptr && iv.instance_idx >= 0 && iv.instance_idx < (int) ref.object->instances.size()) {
        ref.instance     = ref.object->instances[iv.instance_idx];
        ref.instance_idx = iv.instance_idx;
    }
    if (ref.instance == nullptr && !ref.object->instances.empty()) {
        ref.instance     = ref.object->instances.front();
        ref.instance_idx = 0;
    }
    return ref;
}

int count_model_parts(const ModelObject *mo)
{
    if (mo == nullptr)
        return 0;
    int n = 0;
    for (const ModelVolume *mv : mo->volumes)
        if (mv != nullptr && mv->is_model_part())
            ++n;
    return n;
}

} // namespace

Model &OverviewUtils::assembly_edit_primary_model(Plater &plater)
{
    Model &assemble_model = plater.assemble_model();
    // Prefer assemble_model when populated: assembly-view indices and poses live there.
    return !assemble_model.objects.empty() ? assemble_model : plater.model();
}

void OverviewUtils::set_model_assemble_instance_offsets(Model &model, const Vec3d &offset)
{
    for (ModelObject *obj : model.objects) {
        if (obj == nullptr)
            continue;
        for (ModelInstance *inst : obj->instances) {
            if (inst == nullptr)
                continue;
            Geometry::Transformation trafo = inst->get_assemble_transformation();
            trafo.set_offset(offset);
            inst->set_assemble_transformation(trafo);
        }
    }
}

void OverviewUtils::sync_assemble_instance_offset_to_prepare(const ModelObject *assemble_obj, int inst_idx, Model &prepare_model)
{
    if (assemble_obj == nullptr || assemble_obj->instances.empty())
        return;
    ModelInstance *src_inst = nullptr;
    if (inst_idx >= 0 && inst_idx < (int) assemble_obj->instances.size())
        src_inst = assemble_obj->instances[inst_idx];
    else
        src_inst = assemble_obj->instances.front();
    if (src_inst == nullptr)
        return;
    const Vec3d src_offset = src_inst->get_assemble_transformation().get_offset();

    for (ModelVolume *av : assemble_obj->volumes) {
        if (av == nullptr || !av->is_model_part())
            continue;
        const std::string &guid = !av->assembly_src_guid().empty() ?
            av->assembly_src_guid() : av->part_guid();
        if (guid.empty())
            continue;
        for (ModelObject *po : prepare_model.objects) {
            if (po == nullptr)
                continue;
            bool match = false;
            for (ModelVolume *pv : po->volumes) {
                if (pv != nullptr && pv->is_model_part() && pv->part_guid() == guid) {
                    match = true;
                    break;
                }
            }
            if (!match)
                continue;
            ModelInstance *pi = nullptr;
            if (inst_idx >= 0 && inst_idx < (int) po->instances.size())
                pi = po->instances[inst_idx];
            else if (!po->instances.empty())
                pi = po->instances.front();
            if (pi == nullptr)
                return;
            Geometry::Transformation t = pi->get_assemble_transformation();
            t.set_offset(src_offset);
            pi->set_assemble_transformation(t);
            return;
        }
    }
}

void OverviewUtils::sync_assemble_volume_offset_to_prepare(const ModelVolume *assemble_vol, Model &prepare_model)
{
    if (assemble_vol == nullptr)
        return;
    const std::string &guid = !assemble_vol->assembly_src_guid().empty() ?
        assemble_vol->assembly_src_guid() : assemble_vol->part_guid();
    if (guid.empty())
        return;
    const Vec3d src_offset = assemble_vol->get_assemble_transformation().get_offset();
    for (ModelObject *po : prepare_model.objects) {
        if (po == nullptr)
            continue;
        for (ModelVolume *pv : po->volumes) {
            if (pv == nullptr || !pv->is_model_part() || !volume_matches_guid(pv, guid))
                continue;
            Geometry::Transformation t = pv->get_assemble_transformation();
            t.set_offset(src_offset);
            pv->set_assemble_transformation(t);
            return;
        }
    }
}

void OverviewUtils::sync_all_assemble_instance_offsets_to_prepare(Model &assemble_model, Model &prepare_model)
{
    for (ModelObject *ao : assemble_model.objects) {
        if (ao == nullptr)
            continue;
        for (int i = 0; i < (int) ao->instances.size(); ++i)
            sync_assemble_instance_offset_to_prepare(ao, i, prepare_model);
    }
}

void OverviewUtils::sync_canvas_glvolume_instance_offsets_from_model(GLCanvas3D *canvas, const Model &model)
{
    if (canvas == nullptr)
        return;
    // Only the assembly canvas stores the assemble pose in GLVolume::instance_transformation and indexes
    // the same model. On the 3D view the instance offset is the bed pose and object_idx addresses the
    // prepare model, so writing assemble offsets there would teleport objects on the plate.
    if (canvas->get_canvas_type() != GLCanvas3D::ECanvasType::CanvasAssembleView)
        return;
    for (GLVolume *gv : canvas->get_volumes().volumes) {
        if (gv == nullptr)
            continue;
        const int oi = gv->object_idx();
        const int ii = gv->instance_idx();
        if (oi < 0 || oi >= (int) model.objects.size())
            continue;
        const ModelObject *obj = model.objects[oi];
        if (obj == nullptr || ii < 0 || ii >= (int) obj->instances.size() || obj->instances[ii] == nullptr)
            continue;
        gv->set_instance_offset(obj->instances[ii]->get_assemble_transformation().get_offset());
        const int vi = gv->volume_idx();
        if (vi >= 0 && vi < (int) obj->volumes.size() && obj->volumes[vi] != nullptr)
            gv->set_volume_offset(obj->volumes[vi]->get_assemble_transformation().get_offset());
    }
}

bool OverviewUtils::reset_assembly_to_origin(wxEvtHandler *)
{
    auto *plater = wxGetApp().plater();
    if (!plater)
        return false;

    Model &assemble_model = plater->assemble_model();
    Model &prepare_model  = plater->model();
    Model &primary_model  = assembly_edit_primary_model(*plater);

    plater->take_snapshot("reset all volumes to assembly origin", UndoRedo::SnapshotType::GizmoAction);

    // Write assemble_model first when available, then mirror onto prepare.
    set_model_assemble_instance_offsets(primary_model, Vec3d::Zero());
    if (&primary_model == &assemble_model)
        sync_all_assemble_instance_offsets_to_prepare(assemble_model, prepare_model);

    GLCanvas3D *canvas = plater->get_current_canvas3D();
    sync_canvas_glvolume_instance_offsets_from_model(canvas, primary_model);
    if (canvas && canvas->get_canvas_type() == GLCanvas3D::ECanvasType::CanvasAssembleView)
        canvas->zoom_to_fit();

    GLCanvas3D::s_bvh_primary_bounds.reset();
    GLCanvas3D::s_far_from_origin_notification_shown = false;
    plater->get_partplate_list().reset_thumbnail_assembly_view_data();
    plater->update();
    return false;
}

bool OverviewUtils::move_isolated_volumes_closer(wxEvtHandler *)
{
    auto *plater = wxGetApp().plater();
    if (!plater)
        return false;

    Model &assemble_model = plater->assemble_model();
    Model &prepare_model  = plater->model();
    Model &primary_model  = assembly_edit_primary_model(*plater);

    const double target_dist = 30.0;

    plater->take_snapshot("Move isolated volumes", UndoRedo::SnapshotType::GizmoAction);

    std::vector<int> moved_object_idxs;

    for (const auto &iv : GLCanvas3D::s_isolated_volumes) {
        const AssembleTargetRef ref = resolve_assemble_target(primary_model, iv);
        if (ref.object == nullptr || ref.instance == nullptr) {
            BOOST_LOG_TRIVIAL(warning) << boost::format("move_isolated_volumes_closer: unresolved target name=%1% glvol_obj_idx=%2% target_obj_id=%3% guid=%4%")
                                              % iv.name % iv.obj_idx % iv.target.object_id % iv.target.part_guid;
            continue;
        }
        const int      inst_idx = ref.instance_idx;
        ModelObject   *obj      = ref.object;
        ModelInstance *inst     = ref.instance;

        const Vec3d world_center = iv.world_box_assembly.center();
        const Vec3d obj_half     = iv.world_box_assembly.size() * 0.5;

        Vec3d delta = Vec3d::Zero();
        for (int axis = 0; axis < 3; ++axis) {
            const double obj_min = world_center(axis) - obj_half(axis);
            const double obj_max = world_center(axis) + obj_half(axis);
            const double pri_min = GLCanvas3D::s_bvh_primary_bounds.min(axis);
            const double pri_max = GLCanvas3D::s_bvh_primary_bounds.max(axis);

            if (obj_max < pri_min - target_dist) {
                delta(axis) = (pri_min - target_dist - obj_half(axis)) - world_center(axis);
            } else if (obj_min > pri_max + target_dist) {
                delta(axis) = (pri_max + target_dist + obj_half(axis)) - world_center(axis);
            }
        }

        BOOST_LOG_TRIVIAL(info) << boost::format("move_isolated_volumes_closer: name=%1% resolved_by=%2% obj_idx=%3%(glvol %4%) inst_idx=%5% parts=%6% "
                                                 "box=[%7%,%8%,%9%]..[%10%,%11%,%12%] primary=[%13%,%14%,%15%]..[%16%,%17%,%18%] delta=(%19%,%20%,%21%)")
                                       % iv.name % ref.how % ref.object_idx % iv.obj_idx % inst_idx % count_model_parts(obj)
                                       % iv.world_box_assembly.min.x() % iv.world_box_assembly.min.y() % iv.world_box_assembly.min.z()
                                       % iv.world_box_assembly.max.x() % iv.world_box_assembly.max.y() % iv.world_box_assembly.max.z()
                                       % GLCanvas3D::s_bvh_primary_bounds.min.x() % GLCanvas3D::s_bvh_primary_bounds.min.y() % GLCanvas3D::s_bvh_primary_bounds.min.z()
                                       % GLCanvas3D::s_bvh_primary_bounds.max.x() % GLCanvas3D::s_bvh_primary_bounds.max.y() % GLCanvas3D::s_bvh_primary_bounds.max.z()
                                       % delta.x() % delta.y() % delta.z();

        if (delta.squaredNorm() < 1e-3)
            continue;

        // The world pose of an isolated part is instance_assemble * volume_assemble * mesh. Shifting the
        // instance offset shifts that product by delta, but only when the instance carries a single part:
        // an assembly object that groups several parts as volumes shares one instance, so moving it would
        // drag the whole group and never bring the isolated part closer. In that case push delta into the
        // volume's own assemble offset instead, expressed in the instance's local frame.
        const bool move_volume = ref.volume != nullptr && count_model_parts(obj) > 1;
        bool       moved       = false;
        if (move_volume) {
            const Matrix3d inst_linear = inst->get_assemble_transformation().get_matrix().linear();
            const double   det         = inst_linear.determinant();
            if (std::abs(det) > 1e-9) {
                const Vec3d              local_delta = inst_linear.inverse() * delta;
                Geometry::Transformation vol_trafo   = ref.volume->get_assemble_transformation();
                vol_trafo.set_offset(vol_trafo.get_offset() + local_delta);
                ref.volume->set_assemble_transformation(vol_trafo);
                moved = true;
                BOOST_LOG_TRIVIAL(info) << boost::format("move_isolated_volumes_closer: moved volume '%1%' by local delta (%2%,%3%,%4%)")
                                               % ref.volume->name % local_delta.x() % local_delta.y() % local_delta.z();
                if (&primary_model == &assemble_model)
                    sync_assemble_volume_offset_to_prepare(ref.volume, prepare_model);
            } else {
                BOOST_LOG_TRIVIAL(warning) << "move_isolated_volumes_closer: singular instance matrix, falling back to instance move";
            }
        }
        if (!moved) {
            Geometry::Transformation new_trafo = inst->get_assemble_transformation();
            new_trafo.set_offset(new_trafo.get_offset() + delta);
            inst->set_assemble_transformation(new_trafo);

            if (&primary_model == &assemble_model)
                sync_assemble_instance_offset_to_prepare(obj, inst_idx, prepare_model);
        }

        if (ref.object_idx >= 0 && std::find(moved_object_idxs.begin(), moved_object_idxs.end(), ref.object_idx) == moved_object_idxs.end())
            moved_object_idxs.push_back(ref.object_idx);
    }

    GLCanvas3D *canvas = plater->get_current_canvas3D();
    sync_canvas_glvolume_instance_offsets_from_model(canvas, primary_model);
    if (canvas && canvas->get_canvas_type() == GLCanvas3D::ECanvasType::CanvasAssembleView) {
        Selection &sel = canvas->get_selection();
        sel.clear();
        for (int oi : moved_object_idxs) {
            if (oi >= 0 && oi < (int) primary_model.objects.size())
                sel.add_object((unsigned int) oi, false);
        }
    }

    GLCanvas3D::s_isolated_volumes.clear();
    GLCanvas3D::s_isolated_notification_shown   = false;
    GLCanvas3D::s_intersects_notification_shown = false;
    plater->get_partplate_list().reset_thumbnail_assembly_view_data();
    plater->update();
    return false;
}

} // namespace GUI
} // namespace Slic3r
