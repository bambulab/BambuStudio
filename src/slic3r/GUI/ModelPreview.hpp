#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>

#include <wx/window.h>

class wxGLCanvas;
class wxGLContext;

namespace Slic3r {
class BoundingBoxf3; // returned by value from PreviewScene::bounds(); complete type only in the .cpp
}

namespace Slic3r::GUI {

struct Camera;

/**
 * \brief Geometry + colours the preview draws — the format-independent data layer.
 *
 * Each concrete scene owns its shader and delegates to an existing engine renderer for its data type;
 * a new data format is a new subclass in the .cpp plus a factory, never a new renderer. Concrete scenes
 * (plate, raw mesh) stay implementation-private; callers hold this interface. \ref bounds frames the camera.
 */
class PreviewScene
{
public:
    virtual ~PreviewScene()                       = default;
    virtual void          render(const Camera &camera) = 0;
    virtual BoundingBoxf3 bounds() const               = 0;
};

/**
 * \brief A lightweight interactive 3D viewer — the Sync-AMS preview cards, and future previews.
 *
 * Owns a \ref Camera (mouse orbit / wheel and pinch zoom) and draws a pluggable \ref PreviewScene. Reuses the
 * app's single GL context on its own matched-format canvas, so it sees already-uploaded geometry
 * read-only; it owns its full per-frame GL state and never mutates shared resources. Sharing the
 * camera-changed callback between two instances gives a linked-camera comparison.
 */
class ModelPreview : public wxWindow
{
public:
    explicit ModelPreview(wxWindow *parent);
    ~ModelPreview() override;

    /**
     * \brief Swap the drawn scene.
     *
     * \param scene    the scene to draw; null draws only the background
     * \param reframe  re-frame the camera to the new scene's bounds on the next paint; pass false for a
     *                 colour-only swap (same geometry) to keep the viewer's current orientation
     */
    void    set_scene(std::shared_ptr<PreviewScene> scene, bool reframe = true);
    Camera &camera();

    /**
     * \brief Set the callback invoked after each orbit/zoom, e.g. to mirror the camera onto a second preview.
     *
     * \param cb  receives this preview's camera after the change
     */
    void    set_camera_changed(std::function<void(const Camera &)> cb);

    /**
     * \brief Adopt another preview's camera pose (orbit + zoom) and repaint.
     *
     * Point two previews' camera-changed callbacks at each other's sync_camera for a linked before/after
     * comparison. It never fires the camera-changed callback, so linked previews cannot loop.
     *
     * \param src  the camera to copy
     */
    void    sync_camera(const Camera &src);

private:
    enum class Drag { None, Orbit, Pan };

    void on_paint();
    void on_mouse(wxMouseEvent &e);
    void on_wheel(wxMouseEvent &e);
    void on_zoom_gesture(wxZoomGestureEvent &e);

    wxGLCanvas                         *m_canvas{nullptr};
    wxGLContext                        *m_ctx{nullptr}; // the app's shared context — not owned
    std::unique_ptr<Camera>             m_camera;
    std::shared_ptr<PreviewScene>       m_scene;
    std::function<void(const Camera &)> m_camera_changed;
    bool                                m_framed{false};
    Drag                                m_drag{Drag::None};
    wxPoint                             m_last;
    double                              m_pinch_start_zoom{1.0}; // camera zoom when the current pinch began
};

/**
 * \brief A scene of the editor's plates, volumes coloured per extruder, or null when none is available.
 *
 * \param plate_idx                 plate to show; < 0 shows every plate
 * \param extruder_color_overrides  0-based project filament id -> RGBA (0..1) drawn instead of the
 *                                  project colour, e.g. a mapped source's destination slot colour.
 *                                  Extruders left out keep their project colour. Mixed filaments always
 *                                  follow their components' colours, so overriding a mixed id has no effect.
 */
std::shared_ptr<PreviewScene> make_current_plate_scene(int plate_idx = -1,
                                                       const std::map<int, std::array<float, 4>> &extruder_color_overrides = {});

} // namespace Slic3r::GUI
