#include "ClipperUtils.hpp"
#include "EdgeGrid.hpp"
#include "SVG.hpp"

#include <set>

namespace Slic3r {

void export_intersections_to_svg(const std::string &filename, const Polygons &polygons)
{
    std::vector<std::pair<EdgeGrid::Grid::ContourEdge, EdgeGrid::Grid::ContourEdge>> intersections = intersecting_edges(polygons);
    BoundingBox bbox = get_extents(polygons);
    SVG svg(filename.c_str(), bbox);
    svg.draw(union_ex(polygons), "gray", 0.25f);
    svg.draw_outline(polygons, "black");
    std::set<const EdgeGrid::Contour*> intersecting_contours;
    for (const std::pair<EdgeGrid::Grid::ContourEdge, EdgeGrid::Grid::ContourEdge> &ie : intersections) {
        intersecting_contours.insert(ie.first.first);
        intersecting_contours.insert(ie.second.first);
    }
    coord_t line_width = coord_t(scale_(0.01));
    for (const EdgeGrid::Contour *ic : intersecting_contours) {
        if (ic->open())
            svg.draw(Polyline(Points(ic->begin(), ic->end())), "green");
        else {
            Polygon polygon(Points(ic->begin(), ic->end()));
            svg.draw_outline(polygon, "green");
            svg.draw_outline(polygon, "black", line_width);
        }
    }
    for (const std::pair<EdgeGrid::Grid::ContourEdge, EdgeGrid::Grid::ContourEdge> &intersecting_edges : intersections) {
        auto edge = [](const EdgeGrid::Grid::ContourEdge &e) {
            return Line(e.first->segment_start(e.second), e.first->segment_end(e.second));
        };
        svg.draw(edge(intersecting_edges.first), "red", line_width);
        svg.draw(edge(intersecting_edges.second), "red", line_width);
    }
    svg.Close();
}

} // namespace Slic3r
