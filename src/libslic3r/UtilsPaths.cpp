#include "Utils.hpp"

#include <boost/filesystem.hpp>

namespace Slic3r {

static std::string g_var_dir_core;
static std::string g_resources_dir_core;
static std::string g_temporary_dir_core;

void set_var_dir(const std::string &dir)
{
    g_var_dir_core = dir;
}

const std::string &var_dir()
{
    return g_var_dir_core;
}

std::string var(const std::string &file_name)
{
    boost::system::error_code ec;
    if (boost::filesystem::exists(file_name, ec))
        return file_name;
    return (boost::filesystem::path(g_var_dir_core) / file_name).make_preferred().string();
}

void set_resources_dir(const std::string &dir)
{
    g_resources_dir_core = dir;
}

const std::string &resources_dir()
{
    return g_resources_dir_core;
}

void set_temporary_dir(const std::string &dir)
{
    g_temporary_dir_core = dir;
}

const std::string &temporary_dir()
{
    return g_temporary_dir_core;
}

} // namespace Slic3r
