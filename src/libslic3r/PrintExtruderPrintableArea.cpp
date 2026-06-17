#include "Print.hpp"
#include "ClipperUtils.hpp"

namespace Slic3r {

std::vector<double> Print::get_extruder_printable_height() const
{
    return m_config.extruder_printable_height.values;
}

std::vector<Polygons> Print::get_extruder_printable_polygons() const
{
    std::vector<Polygons>           extruder_printable_polys;
    std::vector<std::vector<Vec2d>> extruder_printable_areas = m_config.extruder_printable_area.values;
    for (const auto &e_printable_area : extruder_printable_areas) {
        Polygons ploys = {Polygon::new_scale(e_printable_area)};
        extruder_printable_polys.emplace_back(ploys);
    }
    return std::move(extruder_printable_polys);
}

std::vector<Polygons> Print::get_extruder_unprintable_polygons() const
{
    std::vector<Vec2d>              printable_area           = m_config.printable_area.values;
    Polygon                         printable_poly           = Polygon::new_scale(printable_area);
    std::vector<std::vector<Vec2d>> extruder_printable_areas = m_config.extruder_printable_area.values;
    std::vector<Polygons>           extruder_unprintable_polys;
    for (const auto &e_printable_area : extruder_printable_areas) {
        Polygons ploys = diff(printable_poly, Polygon::new_scale(e_printable_area));
        extruder_unprintable_polys.emplace_back(ploys);
    }
    return std::move(extruder_unprintable_polys);
}

Polygons Print::get_extruder_shared_printable_polygon() const
{
    if (m_config.nozzle_diameter.size() < 2) return {Polygon::new_scale(m_config.printable_area.values)};
    std::vector<std::vector<Vec2d>> extruder_printable_areas = m_config.extruder_printable_area.values;
    Polygons shared_printable_polys = {Polygon::new_scale(extruder_printable_areas.front())};
    for (int i = 1; i < extruder_printable_areas.size();i++) {
        Polygons polys = {Polygon::new_scale(extruder_printable_areas[i])};
        shared_printable_polys = intersection(shared_printable_polys, polys);
    }
    return shared_printable_polys;
}

} // namespace Slic3r
