#include "Fill/FillAdaptive.hpp"
#include "Fill/FillBase.hpp"
#include "Fill/FillLightning.hpp"
#include "Fill/FillRectilinear.hpp"
#include "Print.hpp"
#include "Utils.hpp"

#include <stdexcept>
#include <string>

namespace Slic3r {

namespace {

[[noreturn]] void apply_validate_support_unreachable(const char *symbol)
{
    throw std::logic_error(std::string("Apply/validate support compat path reached: ") + symbol);
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

int Print::export_cached_data(const std::string& /*directory*/, int& obj_cnt_exported, bool /*with_space*/)
{
    obj_cnt_exported = 0;
    return CLI_EXPORT_CACHE_WRITE_FAILED;
}

int Print::load_cached_data(const std::string& /*directory*/)
{
    return CLI_IMPORT_CACHE_LOAD_FAILED;
}

std::string Print::output_filename(const std::string &filename_base) const
{
    return filename_base;
}

bool Print::has_wipe_tower() const
{
    return false;
}

const WipeTowerData& Print::wipe_tower_data(size_t filaments_cnt) const
{
    (void) filaments_cnt;
    return m_wipe_tower_data;
}

bool Print::enable_timelapse_print() const
{
    return false;
}

void Print::_make_wipe_tower()
{
    apply_validate_support_unreachable("Print::_make_wipe_tower");
}

Fill *Fill::new_from_type(const InfillPattern type)
{
    (void) type;
    apply_validate_support_unreachable("Fill::new_from_type(InfillPattern)");
}

void Fill::connect_infill(Polylines &&infill_ordered, const ExPolygon &boundary, Polylines &polylines_out, const double spacing, const FillParams &params)
{
    (void) infill_ordered;
    (void) boundary;
    (void) polylines_out;
    (void) spacing;
    (void) params;
    apply_validate_support_unreachable("Fill::connect_infill");
}

void multiline_fill(Polylines &polylines, const FillParams &params, float spacing)
{
    (void) polylines;
    (void) params;
    (void) spacing;
    apply_validate_support_unreachable("multiline_fill");
}

Points sample_grid_pattern(const ExPolygon &expolygon, coord_t spacing, const BoundingBox &global_bounding_box)
{
    (void) expolygon;
    (void) spacing;
    (void) global_bounding_box;
    apply_validate_support_unreachable("sample_grid_pattern(ExPolygon)");
}

Points sample_grid_pattern(const ExPolygons &expolygons, coord_t spacing, const BoundingBox &global_bounding_box)
{
    (void) expolygons;
    (void) spacing;
    (void) global_bounding_box;
    apply_validate_support_unreachable("sample_grid_pattern(ExPolygons)");
}

Points sample_grid_pattern(const Polygons &polygons, coord_t spacing, const BoundingBox &global_bounding_box)
{
    (void) polygons;
    (void) spacing;
    (void) global_bounding_box;
    apply_validate_support_unreachable("sample_grid_pattern(Polygons)");
}

} // namespace Slic3r
