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

} // namespace Slic3r
