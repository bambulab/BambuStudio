#include "Utils.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace Slic3r {

std::string format_diameter_to_str(double diameter, int precision)
{
    double candidates[] = {0.2, 0.4, 0.6, 0.8};
    double best = *std::min_element(std::begin(candidates), std::end(candidates), [diameter](double a, double b) { return std::abs(a - diameter) < std::abs(b - diameter); });
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << best;
    return oss.str();
}

} // namespace Slic3r
