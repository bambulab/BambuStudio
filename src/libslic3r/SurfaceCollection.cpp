#include "SurfaceCollection.hpp"
#include "BoundingBox.hpp"
#include "SVG.hpp"

#include <map>

namespace Slic3r {

void SurfaceCollection::simplify(double tolerance)
{
    Surfaces ss;
    for (Surfaces::const_iterator it_s = this->surfaces.begin(); it_s != this->surfaces.end(); ++it_s) {
        ExPolygons expp;
        it_s->expolygon.simplify(tolerance, &expp);
        for (ExPolygons::const_iterator it_e = expp.begin(); it_e != expp.end(); ++it_e) {
            Surface s = *it_s;
            s.expolygon = *it_e;
            ss.push_back(s);
        }
    }
    this->surfaces = ss;
}

/* group surfaces by common properties */
void SurfaceCollection::group(std::vector<SurfacesPtr> *retval)
{
    for (Surfaces::iterator it = this->surfaces.begin(); it != this->surfaces.end(); ++it) {
        // find a group with the same properties
        SurfacesPtr* group = NULL;
        for (std::vector<SurfacesPtr>::iterator git = retval->begin(); git != retval->end(); ++git)
            if (! git->empty() && surfaces_could_merge(*git->front(), *it)) {
                group = &*git;
                break;
            }
        // if no group with these properties exists, add one
        if (group == NULL) {
            retval->resize(retval->size() + 1);
            group = &retval->back();
        }
        // append surface to group
        group->push_back(&*it);
    }
}

SurfacesPtr SurfaceCollection::filter_by_type(const SurfaceType type)
{
    SurfacesPtr ss;
    for (Surfaces::iterator surface = this->surfaces.begin(); surface != this->surfaces.end(); ++surface) {
        if (surface->surface_type == type) ss.push_back(&*surface);
    }
    return ss;
}

SurfacesPtr SurfaceCollection::filter_by_types(const SurfaceType *types, int ntypes)
{
    SurfacesPtr ss;
    for (Surfaces::iterator surface = this->surfaces.begin(); surface != this->surfaces.end(); ++surface) {
        for (int i = 0; i < ntypes; ++ i) {
            if (surface->surface_type == types[i]) {
                ss.push_back(&*surface);
                break;
            }
        }
    }
    return ss;
}

void SurfaceCollection::filter_by_type(SurfaceType type, Polygons* polygons)
{
    for (const Surface &surface : this->surfaces)
        if (surface.surface_type == type)
            polygons_append(*polygons, to_polygons(surface.expolygon));
}

void SurfaceCollection::keep_type(const SurfaceType type)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++ i) {
        if (surfaces[i].surface_type == type) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++ j;
        }
    }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
}

void SurfaceCollection::keep_types(const SurfaceType *types, int ntypes)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++ i) {
        bool keep = false;
        for (int k = 0; k < ntypes; ++ k) {
            if (surfaces[i].surface_type == types[k]) {
                keep = true;
                break;
            }
        }
        if (keep) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++ j;
        }
    }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
}

void SurfaceCollection::remove_type(const SurfaceType type)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++ i) {
        if (surfaces[i].surface_type != type) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++ j;
        }
    }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
}

void SurfaceCollection::remove_types(const SurfaceType *types, int ntypes)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++ i) {
        bool remove = false;
        for (int k = 0; k < ntypes; ++ k) {
            if (surfaces[i].surface_type == types[k]) {
                remove = true;
                break;
            }
        }
        if (! remove) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++ j;
        }
    }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
}

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

}
