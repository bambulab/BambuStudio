#ifndef slic3r_OverviewUtils_hpp_
#define slic3r_OverviewUtils_hpp_

#include "libslic3r/Point.hpp"

class wxEvtHandler;

namespace Slic3r {

class Model;
class ModelObject;
class ModelVolume;

namespace GUI {

class Plater;
class GLCanvas3D;

// Shared helpers for Overview / assembly-view actions that edit assemble poses.
// Prefer assemble_model when populated, then mirror onto prepare model / GLVolumes.
class OverviewUtils
{
public:
    static Model &assembly_edit_primary_model(Plater &plater);
    static void   set_model_assemble_instance_offsets(Model &model, const Vec3d &offset);
    static void   sync_assemble_instance_offset_to_prepare(const ModelObject *assemble_obj, int inst_idx, Model &prepare_model);
    static void   sync_assemble_volume_offset_to_prepare(const ModelVolume *assemble_vol, Model &prepare_model);
    static void   sync_all_assemble_instance_offsets_to_prepare(Model &assemble_model, Model &prepare_model);
    static void   sync_canvas_glvolume_instance_offsets_from_model(GLCanvas3D *canvas, const Model &model);

    // NotificationManager action callbacks (signature matches push_notification).
    static bool move_isolated_volumes_closer(wxEvtHandler *);
    static bool reset_assembly_to_origin(wxEvtHandler *);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_OverviewUtils_hpp_
