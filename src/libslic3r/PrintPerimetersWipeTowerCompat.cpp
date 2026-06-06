#include "Print.hpp"

#include <stdexcept>

namespace Slic3r {

namespace {

[[noreturn]] void perimeter_only_wipe_tower_unreachable(const char *symbol)
{
    throw std::logic_error(std::string("Perimeter-only wipe-tower compat path reached: ") + symbol);
}

} // namespace

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
    perimeter_only_wipe_tower_unreachable("Print::_make_wipe_tower");
}

} // namespace Slic3r
