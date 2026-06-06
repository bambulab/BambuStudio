#include "Utils.hpp"

#include <boost/filesystem.hpp>

namespace Slic3r {

static std::string g_data_dir_core;

void set_data_dir(const std::string &dir)
{
    g_data_dir_core = dir;
}

const std::string &data_dir()
{
    return g_data_dir_core;
}

std::string custom_shapes_dir()
{
    return (boost::filesystem::path(g_data_dir_core) / "shapes").string();
}

} // namespace Slic3r
