#ifndef slic3r_FacetPicker_hpp_
#define slic3r_FacetPicker_hpp_

#include "slic3r/GUI/GLModel.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {
class ModelVolume;

namespace GUI {
class CommonGizmosDataPool;
class Selection;
struct Camera; // declared as a struct in Camera.hpp; matching it avoids MSVC ABI linker errors

// Picks a single triangular facet of the selected object under the mouse and draws it
// highlighted.
//
// It deliberately knows nothing about what a picked facet is *for* - the caller decides
// that - so it can serve any gizmo that needs "which triangle is under the cursor".
// GLGizmoAdvancedCut uses it to place the cut plane; GLGizmoFlatten does the same job
// inline today and could be moved onto this later.
class FacetPicker
{
public:
    struct Hit
    {
        int                facet        = -1;
        const ModelVolume *volume       = nullptr;
        Transform3d        trafo        = Transform3d::Identity(); // volume -> world
        Vec3d              world_pos    = Vec3d::Zero();
        Vec3d              world_normal = Vec3d::Zero();

        bool valid() const { return facet >= 0 && volume != nullptr; }
    };

    bool is_active() const { return m_active; }
    void set_active(bool active);

    // Drops the cached hit and the highlight geometry.
    void reset();

    // Recomputes the cached hit from the mouse position. Returns true when the ray hits a
    // model part; on a miss the cache is left invalid.
    bool update(const Vec2d &mouse_position, const CommonGizmosDataPool *pool, const Selection &selection, const Camera &camera);

    // Draws the cached facet. No-op when there is no valid hit.
    void render(const Camera &camera);

    const Hit &hit() const { return m_hit; }

    // World-space unit normal of a facet whose mesh-local normal is mesh_normal, for a
    // volume placed by trafo. Returns zero for a degenerate facet.
    //
    // Uses the inverse-transpose of the linear part, not the linear part itself: on a
    // non-uniformly scaled instance the latter yields a normal that is visibly not
    // perpendicular to the facet.
    static Vec3d world_normal(const Transform3d &trafo, const Vec3f &mesh_normal);

private:
    bool    m_active{false};
    Hit     m_hit;
    int     m_rendered_facet{-1}; // so the highlight is rebuilt only when the facet changes
    GLModel m_highlight;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_FacetPicker_hpp_
