#ifndef slic3r_AvoidCrossingPerimeters_hpp_
#define slic3r_AvoidCrossingPerimeters_hpp_

#include "../libslic3r.h"
#include "../ExPolygon.hpp"
#include "../EdgeGrid.hpp"

#include <unordered_map>

namespace Slic3r {

// Forward declarations.
class GCode;
class Layer;
class Point;
class PrintObject;

class AvoidCrossingPerimeters
{
public:
    // Routing around the objects vs. inside a single object.
    void        use_external_mp(bool use = true) { m_use_external_mp = use; };
    bool        used_external_mp() { return m_use_external_mp; }
    void        use_external_mp_once()  { m_use_external_mp_once = true; }
    bool        used_external_mp_once() { return m_use_external_mp_once; }
    void        disable_once()          { m_disabled_once = true; }
    bool        disabled_once() const   { return m_disabled_once; }
    void        reset_once_modifiers()  { m_use_external_mp_once = false; m_disabled_once = false; }

    void        init_layer(const Layer &layer);

    Polyline    travel_to(const GCode& gcodegen, const Point& point)
    {
        bool could_be_wipe_disabled;
        return this->travel_to(gcodegen, point, &could_be_wipe_disabled);
    }

    Polyline    travel_to(const GCode& gcodegen, const Point& point, bool* could_be_wipe_disabled);

    struct Boundary {
        // Collection of boundaries used for detection of crossing perimeters for travels
        Polygons                        boundaries;
        // Bounding box of boundaries
        BoundingBoxf                    bbox;
        // Precomputed distances of all points in boundaries
        std::vector<std::vector<float>> boundaries_params;
        // Used for detection of intersection between line and any polygon from boundaries
        EdgeGrid::Grid                  grid;

        void clear()
        {
            boundaries.clear();
            boundaries_params.clear();
        }
    };

    // Per-object, per-print-height cache of get_boundary_external()'s expensive local-coordinate
    // geometry (each object's resampled islands run through the variable-width inner-offset pipeline).
    // That geometry depends only on (object, print height, is_support_layer, include_supports_in_boundary),
    // never on a PrintInstance's world-space shift, so it can be reused across the many
    // get_boundary_external() rebuilds that happen within a single print height.
    // Invalidation key is the print height (print_z): GCode::process_layer resets gcodegen.layer() to
    // each object's own Layer while iterating the objects at one print height, so a Layer* key would
    // thrash across objects; every object's Layer at the same height shares the same print_z.
    struct ObjectBoundaryCache {
        std::unordered_map<const PrintObject *, ExPolygons> per_object;
        coordf_t                                            built_for_print_z { -1. };
    };

private:
    bool           m_use_external_mp { false };
    // just for the next travel move
    bool           m_use_external_mp_once { false };
    // this flag disables reduce_crossing_wall just for the next travel move
    // we enable it by default for the first travel move in print
    bool           m_disabled_once { true };

    // Lslices offseted by half an external perimeter width. Used for detection if line or polyline is inside of any polygon.
    ExPolygons               m_lslices_offset;
    std::vector<BoundingBox> m_lslices_offset_bboxes;
    // Used for detection of line or polyline is inside of any polygon.
    EdgeGrid::Grid m_grid_lslice;
    // Store all needed data for travels inside object
    Boundary m_internal;
    // Store all needed data for travels outside object
    Boundary m_external;
    // Per-object local-coordinate boundary geometry cache for the current layer (see ObjectBoundaryCache).
    ObjectBoundaryCache m_object_boundary_cache;
};

} // namespace Slic3r

#endif // slic3r_AvoidCrossingPerimeters_hpp_
