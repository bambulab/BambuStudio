#include "Utils.hpp"

namespace Slic3r {

bool is_gcode_file(const std::string &path)
{
    return boost::iends_with(path, ".gcode");
}

bool is_json_file(const std::string &path)
{
    return boost::iends_with(path, ".json");
}

std::string header_slic3r_generated()
{
    return std::string(SLIC3R_APP_NAME " " SLIC3R_VERSION);
}

} // namespace Slic3r
