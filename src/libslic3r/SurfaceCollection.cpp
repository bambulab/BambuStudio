#include "SurfaceCollection.hpp"
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
    for (Surface &surface : this->surfaces)
        if (surface.surface_type == type)
            ss.push_back(&surface);
    return ss;
}

SurfacesPtr SurfaceCollection::filter_by_types(std::initializer_list<SurfaceType> types)
{
    SurfacesPtr ss;
    for (Surface& surface : this->surfaces)
        if (std::find(types.begin(), types.end(), surface.surface_type) != types.end())
            ss.push_back(&surface);
    return ss;
}

void SurfaceCollection::filter_by_type(SurfaceType type, Polygons* polygons)
{
    for (const Surface &surface : this->surfaces)
        if (surface.surface_type == type)
            polygons_append(*polygons, to_polygons(surface.expolygon));
}

void SurfaceCollection::keep_type(SurfaceType type, ExPolygons &exps)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++ i) {
        if (surfaces[i].surface_type == type) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++ j;
        }else{
            exps.push_back(surfaces[i].expolygon);
        }
    }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
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

void SurfaceCollection::keep_types(std::initializer_list<SurfaceType> types)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++ i)
        if (std::find(types.begin(), types.end(), surfaces[i].surface_type) != types.end()) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++ j;
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

void SurfaceCollection::remove_types(std::initializer_list<SurfaceType> types)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++ i)
        if (std::find(types.begin(), types.end(), surfaces[i].surface_type) == types.end()) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++ j;
        }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
}

}
