#ifndef slic3r_BallEraserSession_hpp_
#define slic3r_BallEraserSession_hpp_

#include "libslic3r/libslic3r.h"
#include "libslic3r/Color.hpp"
#include "libslic3r/Point.hpp"

#include <vector>

namespace Slic3r {
namespace BallEraser {

enum class PrimitiveType : unsigned char
{
    Sphere,
    Cube
};

enum class OutputMode : unsigned char
{
    GroupedVolumes,
    SeparateObjects
};

enum class HoverTarget : unsigned char
{
    EmptyScene,
    EditedObject,
    Cursor
};

enum class DragOwnership : unsigned char
{
    None,
    BallEraser,
    PassThrough
};

struct Dimensions
{
    double x{10.0};
    double y{10.0};
    double z{10.0};
};

struct Stroke
{
    enum class GestureType : unsigned char
    {
        Discrete,
        Drag
    };

    PrimitiveType primitive{PrimitiveType::Sphere};
    GestureType gesture{GestureType::Discrete};

    // Canonical BallEraser coordinate space:
    // All pending strokes are authored and stored in the selected instance's
    // local editing space. The selected source mesh must be transformed into
    // this same space for preview and final apply. This keeps stroke replay,
    // boolean subtraction, and later appearance transfer aligned.
    Dimensions dimensions{};
    Vec3d rotation_degrees{Vec3d::Zero()};
    size_t sequence_index{0};
    std::vector<Vec3d> placement_centers_instance_local;

    bool empty() const { return placement_centers_instance_local.empty(); }
    size_t placement_count() const { return placement_centers_instance_local.size(); }
    const Vec3d &first_center_instance_local() const { return placement_centers_instance_local.front(); }
};

struct Session
{
    int object_index{-1};
    int instance_index{-1};
    bool active{false};
    bool preview_scene_isolated{false};
    bool preview_monocolor_enabled{false};
    bool preview_dirty{true};
    PrimitiveType primitive{PrimitiveType::Sphere};
    OutputMode output_mode{OutputMode::GroupedVolumes};
    HoverTarget hover_target{HoverTarget::EmptyScene};
    DragOwnership drag_ownership{DragOwnership::None};
    bool exit_confirmation_pending{false};
    bool pointer_down{false};
    bool pointer_dragged{false};
    bool stroke_capture_active{false};
    bool single_cut_surface_color{false};
    ColorRGBA cut_surface_color{0.90f, 0.20f, 0.17f, 1.0f};
    Vec3d hover_center_instance_local{Vec3d::Zero()};
    Vec3d active_center_instance_local{Vec3d::Zero()};
    Vec3d drag_start_center_instance_local{Vec3d::Zero()};
    Vec3d drag_last_sample_center_instance_local{Vec3d::Zero()};
    double drag_sample_spacing{2.0};
    std::vector<Vec3d> in_progress_stroke_centers_instance_local;
    std::vector<Stroke> pending_strokes;

    void reset()
    {
        object_index = -1;
        instance_index = -1;
        active = false;
        preview_scene_isolated = false;
        preview_monocolor_enabled = false;
        preview_dirty = true;
        primitive = PrimitiveType::Sphere;
        output_mode = OutputMode::GroupedVolumes;
        hover_target = HoverTarget::EmptyScene;
        drag_ownership = DragOwnership::None;
        exit_confirmation_pending = false;
        pointer_down = false;
        pointer_dragged = false;
        stroke_capture_active = false;
        single_cut_surface_color = false;
        cut_surface_color = ColorRGBA(0.90f, 0.20f, 0.17f, 1.0f);
        hover_center_instance_local = Vec3d::Zero();
        active_center_instance_local = Vec3d::Zero();
        drag_start_center_instance_local = Vec3d::Zero();
        drag_last_sample_center_instance_local = Vec3d::Zero();
        drag_sample_spacing = 2.0;
        in_progress_stroke_centers_instance_local.clear();
        pending_strokes.clear();
    }

    bool has_target() const
    {
        return object_index >= 0 && instance_index >= 0;
    }
};

} // namespace BallEraser
} // namespace Slic3r

#endif // slic3r_BallEraserSession_hpp_
