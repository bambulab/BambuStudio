#include "OverviewUtils.hpp"

#include "libslic3r/Model.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include "../GLCanvas3D.hpp"
#include "../GUI_App.hpp"
#include "../Plater.hpp"
#include "../PartPlate.hpp"

#include <wx/event.h>

namespace Slic3r {
namespace GUI {

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

    for (const auto &iv : GLCanvas3D::s_isolated_volumes) {
        if (iv.obj_idx < 0 || iv.obj_idx >= (int) primary_model.objects.size())
            continue;

        const int    inst_idx = iv.instance_idx;
        ModelObject *obj      = primary_model.objects[iv.obj_idx];
        if (obj == nullptr || inst_idx < 0 || inst_idx >= (int) obj->instances.size())
            continue;
        ModelInstance *inst = obj->instances[inst_idx];
        if (inst == nullptr)
            continue;

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

        if (delta.squaredNorm() < 1e-3)
            continue;

        Geometry::Transformation new_trafo = inst->get_assemble_transformation();
        new_trafo.set_offset(new_trafo.get_offset() + delta);
        inst->set_assemble_transformation(new_trafo);

        if (&primary_model == &assemble_model)
            sync_assemble_instance_offset_to_prepare(obj, inst_idx, prepare_model);
    }

    GLCanvas3D *canvas = plater->get_current_canvas3D();
    sync_canvas_glvolume_instance_offsets_from_model(canvas, primary_model);
    if (canvas && canvas->get_canvas_type() == GLCanvas3D::ECanvasType::CanvasAssembleView) {
        Selection &sel = canvas->get_selection();
        sel.clear();
        for (const auto &iv : GLCanvas3D::s_isolated_volumes) {
            if (iv.obj_idx >= 0 && iv.obj_idx < (int) primary_model.objects.size())
                sel.add_object((unsigned int) iv.obj_idx, false);
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
