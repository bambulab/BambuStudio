#include "Polygon.hpp"

namespace Slic3r {

Lines Polygon::lines() const
{
    return to_lines(*this);
}

} // namespace Slic3r
