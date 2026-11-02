#include <GL/glew.h> // must precede any gl.h

#include <algorithm>
#include <array>
#include <set>
#include <utility>
#include <vector>

#include <wx/dcclient.h>
#include <wx/glcanvas.h>
#include <wx/sizer.h>

// The heavy engine headers have a fragile include order (GLCanvas3D.hpp uses GLVolumeCollection, and
// Model.hpp pulls SLA/Hollowing.hpp which uses indexed_triangle_set — both by value). Include them in
// this proven order first, then our own light header last, so the chain parses against a clean prologue.
// clang-format off
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "PartPlate.hpp"
#include "GLCanvas3D.hpp"
#include "OpenGLManager.hpp"
#include "Camera.hpp"
#include "GLShader.hpp"
#include "3DScene.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
// clang-format on

#include "FilamentBitmapUtils.hpp"
#include "Widgets/StateColor.hpp"
#include "ModelPreview.hpp"

namespace Slic3r::GUI {

namespace {

// A scene over the plate's live GLVolumes, coloured per extruder — reuses the plate's uploaded VBOs.
class PlateScene : public PreviewScene
{
public:
    PlateScene(const GLVolumeCollection &volumes, ModelObjectPtrs &objects,
               std::vector<std::array<float, 4>> extruder_colors, std::set<std::pair<int, int>> filter)
        : m_volumes(volumes), m_objects(objects), m_extruder_colors(std::move(extruder_colors)), m_filter(std::move(filter))
    {}

    BoundingBoxf3 bounds() const override
    {
        BoundingBoxf3 box;
        for (const GLVolume *v : m_volumes.volumes)
            if (included(v)) box.merge(v->transformed_bounding_box());
        return box;
    }

    void render(const Camera &camera) override
    {
        // gouraud_light, not thumbnail: same lighting/uniforms without the thumbnail shader's discard of
        // fragments below z=0 (which flickers along an object's z=0 base as a dashed edge).
        const std::shared_ptr<GLShaderProgram> &shader = wxGetApp().get_shader("gouraud_light");
        const auto                              ogl    = wxGetApp().get_opengl_manager();
        if (!shader || !ogl) return;

        ogl->bind_vao();
        ogl->bind_shader(shader);
        const Transform3d view = camera.get_view_matrix();
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        shader->set_uniform("emission_factor", 0.1f);
        for (GLVolume *v : m_volumes.volumes) {
            if (!included(v)) continue;
            const Transform3d vm = view * v->world_matrix();
            shader->set_uniform("view_model_matrix", vm);
            shader->set_uniform("normal_matrix", (Matrix3d) vm.matrix().block(0, 0, 3, 3).inverse().transpose());
            // A plain (non-mmu) volume ignores extruder_colors and draws with whatever uniform_color is
            // seeded, so seed it from the extruder colour for this volume's extruder — this is the only
            // hook a recoloured (mapped) preview has on single-colour objects. simple_render still
            // recolours mmu-painted regions from the same vector. Fall back to the volume's own colour.
            int ext = v->extruder_id; // 1-based
            if (m_extruder_colors.empty() || ext <= 0 || ext > (int) m_extruder_colors.size()) ext = 0;
            shader->set_uniform("uniform_color", ext > 0 ? m_extruder_colors[ext - 1] : v->color);
            v->simple_render(shader, m_objects, m_extruder_colors);
        }
        ogl->unbind_shader();
        ogl->unbind_vao();
    }

private:
    /**
     * \brief True for a printable model volume in this scene's scope: active, not the wipe tower, and on
     *        the filtered plate when a filter is set.
     *
     * \param v  the volume to test; null is excluded
     */
    bool included(const GLVolume *v) const
    {
        return v && v->is_active && !v->is_wipe_tower &&
               (m_filter.empty() || m_filter.count({v->object_idx(), v->instance_idx()}) > 0);
    }

    const GLVolumeCollection         &m_volumes;
    ModelObjectPtrs                  &m_objects;
    std::vector<std::array<float, 4>> m_extruder_colors;
    std::set<std::pair<int, int>>     m_filter; // empty = every plate
};

/**
 * \brief Re-derive mixed filaments from their components' colours in colors, so recoloured components show
 *        through. Physical entries are left untouched.
 */
void reblend_mixed_colors(std::vector<std::array<float, 4>> &colors)
{
    const DynamicPrintConfig &cfg      = wxGetApp().preset_bundle->project_config;
    const auto               *is_mixed = cfg.option<ConfigOptionBools>("filament_is_mixed");
    if (!is_mixed || std::none_of(is_mixed->values.begin(), is_mixed->values.end(), [](unsigned char m) { return m; }))
        return;

    const auto to_byte = [](float v) { return static_cast<unsigned char>(std::clamp(v, 0.f, 1.f) * 255.f + .5f); };
    std::vector<wxColour> palette;
    palette.reserve(colors.size());
    for (const auto &c : colors)
        palette.emplace_back(to_byte(c[0]), to_byte(c[1]), to_byte(c[2]), to_byte(c[3]));
    recompute_mixed_slot_colors(palette, cfg);

    // Write back only mixed slots: the 8-bit round trip must not perturb physical colours.
    for (size_t i = 0; i < colors.size() && i < is_mixed->values.size(); ++i)
        if (is_mixed->values[i] && palette[i].IsOk())
            colors[i] = {palette[i].Red() / 255.f, palette[i].Green() / 255.f, palette[i].Blue() / 255.f, palette[i].Alpha() / 255.f};
}

/**
 * \brief Move the camera so the scene point under from_px ends up under to_px, i.e. the scene follows the cursor.
 *
 * Works for perspective and orthographic cameras alike. Points are framebuffer pixels, y down.
 * Caution: needs the viewport and projection of the last paint; it does nothing before the first one.
 */
void pan_camera(Camera &camera, const Vec2d &from_px, const Vec2d &to_px)
{
    const std::array<int, 4> &vp = camera.get_viewport();
    if (vp[2] <= 0 || vp[3] <= 0) return;

    const Matrix4d view_proj = camera.get_projection_matrix().matrix() * camera.get_view_matrix().matrix();
    const Matrix4d inv       = view_proj.inverse();
    const Vec3d    target    = camera.get_target();
    const Vec4d    clip      = view_proj * target.homogeneous();
    const double   ndc_z     = clip.z() / clip.w(); // pan in the plane through the orbit target

    const auto to_world = [&](const Vec2d &px) {
        const Vec4d ndc(2. * (px.x() - vp[0]) / vp[2] - 1., 1. - 2. * (px.y() - vp[1]) / vp[3], ndc_z, 1.);
        const Vec4d w = inv * ndc;
        return Vec3d(w.head<3>() / w.w());
    };
    camera.set_target(target + to_world(from_px) - to_world(to_px));
}

} // namespace

// -------------------------------------------------------------------------------------------------
// ModelPreview — the canvas: owns the camera, the full per-frame GL state, and the orbit/pan interaction.
// -------------------------------------------------------------------------------------------------
ModelPreview::ModelPreview(wxWindow *parent) : wxWindow(parent, wxID_ANY), m_camera(std::make_unique<Camera>())
{
    // The viewport clears to this widget's own background, so it blends into its container in either
    // theme. A themed neutral by default; a caller sets its own to match a specific card surface.
    SetBackgroundColour(StateColor::darkModeColorFor(ThemeColor::Grey200));

    // One context can serve many canvases only if their pixel formats match, so build this canvas with
    // the same MSAA the editor used, then reuse the app's single context (its VBOs are visible here).
    const auto      ogl  = wxGetApp().get_opengl_manager();
    const EMSAAType msaa = ogl->is_fxaa_enabled() ? EMSAAType::Disabled : ogl->get_msaa_type();
    m_canvas             = OpenGLManager::create_wxglcanvas(*this, msaa);
    m_ctx                = ogl->init_glcontext(*m_canvas);

    auto *s = new wxBoxSizer(wxVERTICAL);
    s->Add(m_canvas, 1, wxEXPAND);
    SetSizer(s);

    m_canvas->Bind(wxEVT_PAINT, [this](wxPaintEvent &) { on_paint(); });
    m_canvas->Bind(wxEVT_SIZE, [this](wxSizeEvent &) { m_canvas->Refresh(); });
    m_canvas->Bind(wxEVT_LEFT_DOWN, &ModelPreview::on_mouse, this);
    m_canvas->Bind(wxEVT_LEFT_UP, &ModelPreview::on_mouse, this);
    m_canvas->Bind(wxEVT_RIGHT_DOWN, &ModelPreview::on_mouse, this);
    m_canvas->Bind(wxEVT_RIGHT_UP, &ModelPreview::on_mouse, this);
    m_canvas->Bind(wxEVT_MIDDLE_DOWN, &ModelPreview::on_mouse, this);
    m_canvas->Bind(wxEVT_MIDDLE_UP, &ModelPreview::on_mouse, this);
    m_canvas->Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &) { m_drag = Drag::None; });
    m_canvas->Bind(wxEVT_MOTION, &ModelPreview::on_mouse, this);
    m_canvas->Bind(wxEVT_MOUSEWHEEL, &ModelPreview::on_wheel, this);
    // Without this, wxOSX sends no pinch event, and a trackpad pinch reaches us only as stray wheel events.
    m_canvas->Bind(wxEVT_GESTURE_ZOOM, &ModelPreview::on_zoom_gesture, this);
    m_canvas->EnableTouchEvents(wxTOUCH_ZOOM_GESTURE);
}

ModelPreview::~ModelPreview() = default; // m_ctx is the app's shared context — not owned

void ModelPreview::set_scene(std::shared_ptr<PreviewScene> scene, bool reframe)
{
    m_scene = std::move(scene);
    // A colour-only swap keeps the current camera; a first scene still frames (m_framed starts false).
    if (reframe) m_framed = false;
    m_canvas->Refresh();
}

Camera &ModelPreview::camera() { return *m_camera; }

void ModelPreview::set_camera_changed(std::function<void(const Camera &)> cb) { m_camera_changed = std::move(cb); }

void ModelPreview::sync_camera(const Camera &src)
{
    // Whole-pose copy: keeps m_view_rotation consistent with m_view_matrix, so a later drag on this card
    // continues smoothly instead of jumping. Viewport/projection are re-derived on the next paint.
    *m_camera = src;
    if (m_canvas) m_canvas->Refresh();
}

void ModelPreview::on_paint()
{
    if (!m_canvas || !m_canvas->IsShownOnScreen()) return;
    wxPaintDC dc(m_canvas);
    m_canvas->SetCurrent(*m_ctx);

    // Clear any error already queued in the borrowed context so the glsafe checks flag only our calls.
    for (int i = 0; i < 64 && ::glGetError() != GL_NO_ERROR; ++i) {}

    // The shared context may still have an offscreen FBO bound by a prior pass (e.g. plate-thumbnail
    // rendering), which would swallow this frame; bind the window framebuffer so the draw reaches the canvas.
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));

    // GL works in framebuffer pixels; client size is in points, which differ on HiDPI (Retina) displays.
    const wxSize sz = m_canvas->GetClientSize() * m_canvas->GetContentScaleFactor();
    glsafe(::glViewport(0, 0, sz.x, sz.y));
    const wxColour bg = GetBackgroundColour(); // themed viewport background, tracks light/dark
    glsafe(::glClearColor(bg.Red() / 255.f, bg.Green() / 255.f, bg.Blue() / 255.f, 1.f));
    glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glEnable(GL_CULL_FACE));

    if (m_scene) {
        const BoundingBoxf3 box = m_scene->bounds();
        if (box.defined) {
            if (!m_framed) {
                m_camera->set_type(Camera::EType::Perspective);
                m_camera->set_scene_box(box);
                m_camera->apply_viewport(0, 0, sz.x, sz.y);
                m_camera->select_view("iso");
                m_camera->zoom_to_box(box);
                m_framed = true;
            }
            m_camera->apply_viewport(0, 0, sz.x, sz.y);
            m_camera->apply_projection(box);
            m_scene->render(*m_camera);
        }
    }

    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glDisable(GL_DEPTH_TEST));
    m_canvas->SwapBuffers();
}

void ModelPreview::on_mouse(wxMouseEvent &e)
{
    // Left orbits; right and middle pan, as in the main 3D view. The first pressed button owns the drag.
    if (e.ButtonDown() && m_drag == Drag::None) {
        m_drag = e.LeftDown() ? Drag::Orbit : Drag::Pan;
        m_last = e.GetPosition();
        if (!m_canvas->HasCapture()) m_canvas->CaptureMouse();
    } else if (e.ButtonUp() && m_drag != Drag::None) {
        if ((m_drag == Drag::Orbit) != e.LeftUp()) return; // another button's release; keep dragging
        m_drag = Drag::None;
        if (m_canvas->HasCapture()) m_canvas->ReleaseMouse();
    } else if (e.Dragging() && m_drag != Drag::None) {
        const wxPoint p = e.GetPosition();
        if (m_drag == Drag::Orbit) {
            m_camera->rotate_on_sphere((p.x - m_last.x) * 0.01, (p.y - m_last.y) * 0.01, true);
        } else {
            // Mouse positions are in points; the camera viewport is in framebuffer pixels (HiDPI).
            const double scale = m_canvas->GetContentScaleFactor();
            pan_camera(*m_camera, Vec2d(m_last.x, m_last.y) * scale, Vec2d(p.x, p.y) * scale);
        }
        m_last = p;
        if (m_camera_changed) m_camera_changed(*m_camera);
        m_canvas->Refresh();
    }
}

void ModelPreview::on_wheel(wxMouseEvent &e)
{
    // A trackpad sends zero-rotation wheel events (resting fingers, scroll phase changes); they must not zoom.
    if (e.GetWheelRotation() == 0 || e.GetWheelDelta() == 0) return;
    double delta = double(e.GetWheelRotation()) / double(e.GetWheelDelta());
    if (wxGetApp().app_config->get_bool("reverse_mouse_wheel_zoom")) delta = -delta;
    m_camera->set_zoom(m_camera->calc_zoom_from_delta(delta));
    if (m_camera_changed) m_camera_changed(*m_camera);
    m_canvas->Refresh();
}

void ModelPreview::on_zoom_gesture(wxZoomGestureEvent &e)
{
    // The zoom factor is relative to the gesture start, not to the previous event.
    if (e.IsGestureStart()) m_pinch_start_zoom = m_camera->get_zoom();
    double factor = e.GetZoomFactor();
    if (wxGetApp().app_config->get_bool("reverse_mouse_wheel_zoom")) factor = 1.0 / std::max(factor, 1e-3);
    m_camera->set_zoom(m_pinch_start_zoom * factor);
    if (m_camera_changed) m_camera_changed(*m_camera);
    m_canvas->Refresh();
}

std::shared_ptr<PreviewScene> make_current_plate_scene(int plate_idx, const std::map<int, std::array<float, 4>> &extruder_color_overrides)
{
    Plater *plater = wxGetApp().plater();
    if (!plater) return nullptr;
    GLCanvas3D *c3d = plater->get_view3D_canvas3D();
    if (!c3d) return nullptr;

    std::set<std::pair<int, int>> filter; // empty = every plate
    if (plate_idx >= 0) {
        PartPlateList &ppl = plater->get_partplate_list();
        if (plate_idx < ppl.get_plate_count())
            if (PartPlate *pp = ppl.get_plate(plate_idx)) filter = pp->get_obj_and_inst_set();
    }

    // colours[i] is project filament i's colour (the extruder_id the volumes carry); recolour the mapped
    // ones in place so the scene draws the after-mapping result through the same per-extruder path, then
    // re-blend mixed filaments so they follow their recoloured components.
    std::vector<std::array<float, 4>> colors = plater->get_extruders_colors();
    for (const auto &[extruder, rgba] : extruder_color_overrides)
        if (extruder >= 0 && extruder < (int) colors.size()) colors[extruder] = rgba;
    reblend_mixed_colors(colors);

    return std::make_shared<PlateScene>(c3d->get_volumes(), plater->model().objects, std::move(colors), std::move(filter));
}

} // namespace Slic3r::GUI
