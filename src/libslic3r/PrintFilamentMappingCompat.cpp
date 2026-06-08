#include "Print.hpp"

#include <stdexcept>
#include <string>

namespace Slic3r {

namespace {

[[noreturn]] void filament_mapping_only_unreachable(const char *symbol)
{
    throw std::logic_error(std::string("Filament-mapping-only compat path reached: ") + symbol);
}

} // namespace

void Print::process_perimeters(std::unordered_map<std::string, long long> *slice_time, bool use_cache)
{
    (void) slice_time;
    (void) use_cache;
    filament_mapping_only_unreachable("Print::process_perimeters");
}

void Print::process(std::unordered_map<std::string, long long> *slice_time, bool use_cache)
{
    (void) slice_time;
    (void) use_cache;
    filament_mapping_only_unreachable("Print::process");
}

StringObjectException Print::validate(
    StringObjectException *warning,
    Polygons *collison_polygons,
    std::vector<std::pair<Polygon, float>> *height_polygons) const
{
    (void) warning;
    (void) collison_polygons;
    (void) height_polygons;
    filament_mapping_only_unreachable("Print::validate");
}

} // namespace Slic3r
