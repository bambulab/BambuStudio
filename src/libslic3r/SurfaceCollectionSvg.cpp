#include "SurfaceCollection.hpp"

#include "BoundingBox.hpp"
#include "SVG.hpp"

#include <algorithm>
#include <cstdio>

namespace Slic3r {

void SurfaceCollection::export_to_svg(const char *path, bool show_labels)
{
    BoundingBox bbox;
    for (Surfaces::const_iterator surface = this->surfaces.begin(); surface != this->surfaces.end(); ++surface)
        bbox.merge(get_extents(surface->expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (Surfaces::const_iterator surface = this->surfaces.begin(); surface != this->surfaces.end(); ++surface) {
        svg.draw(surface->expolygon, surface_type_to_color_name(surface->surface_type), transparency);
        if (show_labels) {
            int idx = int(surface - this->surfaces.begin());
            char label[64];
            sprintf(label, "%d", idx);
            svg.draw_text(surface->expolygon.contour.points.front(), label, "black");
        }
    }
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

} // namespace Slic3r
