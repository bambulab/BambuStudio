#include "BoundingBox.hpp"
#include "Surface.hpp"

namespace Slic3r {

BoundingBox get_extents(const Surface &surface)
{
    return get_extents(surface.expolygon.contour);
}

BoundingBox get_extents(const Surfaces &surfaces)
{
    BoundingBox bbox;
    if (! surfaces.empty()) {
        bbox = get_extents(surfaces.front());
        for (size_t i = 1; i < surfaces.size(); ++ i)
            bbox.merge(get_extents(surfaces[i]));
    }
    return bbox;
}

BoundingBox get_extents(const SurfacesPtr &surfaces)
{
    BoundingBox bbox;
    if (! surfaces.empty()) {
        bbox = get_extents(*surfaces.front());
        for (size_t i = 1; i < surfaces.size(); ++ i)
            bbox.merge(get_extents(*surfaces[i]));
    }
    return bbox;
}

const char* surface_type_to_color_name(const SurfaceType surface_type)
{
    switch (surface_type) {
        case stTop:             return "rgb(255,0,0)"; // "red";
        case stBottom:          return "rgb(0,255,0)"; // "green";
        case stBottomBridge:    return "rgb(0,0,255)"; // "blue";
        case stInternal:        return "rgb(255,255,128)"; // yellow 
        case stFloatingVerticalShell:
        case stInternalSolid:   return "rgb(255,0,255)"; // magenta
        case stInternalBridge:  return "rgb(0,255,255)";
        case stInternalVoid:    return "rgb(128,128,128)";
        case stPerimeter:       return "rgb(128,0,0)"; // maroon
        default:                return "rgb(64,64,64)";
    };
}

}
