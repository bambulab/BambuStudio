#include "Print.hpp"

namespace Slic3r {

void Print::process(std::unordered_map<std::string, long long> *slice_time, bool use_cache)
{
    this->process_perimeters(slice_time, use_cache);
}

} // namespace Slic3r
