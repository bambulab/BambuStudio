#include "Fill/FillAdaptive.hpp"
#include "Fill/FillBase.hpp"
#include "Fill/FillLightning.hpp"
#include "Fill/FillRectilinear.hpp"

#include <stdexcept>

namespace Slic3r {

namespace {

[[noreturn]] void perimeter_only_unreachable(const char *symbol)
{
    throw std::logic_error(std::string("Perimeter-only compat path reached: ") + symbol);
}

} // namespace

namespace FillAdaptive {

void OctreeDeleter::operator()(Octree *p)
{
    (void) p;
}

} // namespace FillAdaptive

namespace FillLightning {

void GeneratorDeleter::operator()(Generator *p)
{
    (void) p;
}

} // namespace FillLightning

Fill *Fill::new_from_type(const InfillPattern type)
{
    (void) type;
    perimeter_only_unreachable("Fill::new_from_type(InfillPattern)");
}

void Fill::connect_infill(Polylines &&infill_ordered, const ExPolygon &boundary, Polylines &polylines_out, const double spacing, const FillParams &params)
{
    (void) infill_ordered;
    (void) boundary;
    (void) polylines_out;
    (void) spacing;
    (void) params;
    perimeter_only_unreachable("Fill::connect_infill");
}

void multiline_fill(Polylines &polylines, const FillParams &params, float spacing)
{
    (void) polylines;
    (void) params;
    (void) spacing;
    perimeter_only_unreachable("multiline_fill");
}

Points sample_grid_pattern(const ExPolygon &expolygon, coord_t spacing, const BoundingBox &global_bounding_box)
{
    (void) expolygon;
    (void) spacing;
    (void) global_bounding_box;
    perimeter_only_unreachable("sample_grid_pattern(ExPolygon)");
}

Points sample_grid_pattern(const ExPolygons &expolygons, coord_t spacing, const BoundingBox &global_bounding_box)
{
    (void) expolygons;
    (void) spacing;
    (void) global_bounding_box;
    perimeter_only_unreachable("sample_grid_pattern(ExPolygons)");
}

Points sample_grid_pattern(const Polygons &polygons, coord_t spacing, const BoundingBox &global_bounding_box)
{
    (void) polygons;
    (void) spacing;
    (void) global_bounding_box;
    perimeter_only_unreachable("sample_grid_pattern(Polygons)");
}

} // namespace Slic3r
